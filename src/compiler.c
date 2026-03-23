#include "runtime.h"
#include "compiler.h"

/* ---- Builtin name-to-ID mapping ---- */

typedef enum {
    AGO_BUILTIN_PRINT,
    AGO_BUILTIN_LEN,
    AGO_BUILTIN_TYPE,
    AGO_BUILTIN_STR,
    AGO_BUILTIN_INT,
    AGO_BUILTIN_FLOAT,
    AGO_BUILTIN_PUSH,
    AGO_BUILTIN_MAP,
    AGO_BUILTIN_FILTER,
    AGO_BUILTIN_ABS,
    AGO_BUILTIN_MIN,
    AGO_BUILTIN_MAX,
    AGO_BUILTIN_READ_FILE,
    AGO_BUILTIN_WRITE_FILE,
    AGO_BUILTIN_FILE_EXISTS,
    AGO_BUILTIN_COUNT,
    AGO_BUILTIN_NONE = -1,
} AgoBuiltinId;

static AgoBuiltinId resolve_builtin(const char *name, int len) {
    if (ago_str_eq(name, len, "print", 5)) return AGO_BUILTIN_PRINT;
    if (ago_str_eq(name, len, "len", 3)) return AGO_BUILTIN_LEN;
    if (ago_str_eq(name, len, "type", 4)) return AGO_BUILTIN_TYPE;
    if (ago_str_eq(name, len, "str", 3)) return AGO_BUILTIN_STR;
    if (ago_str_eq(name, len, "int", 3)) return AGO_BUILTIN_INT;
    if (ago_str_eq(name, len, "float", 5)) return AGO_BUILTIN_FLOAT;
    if (ago_str_eq(name, len, "push", 4)) return AGO_BUILTIN_PUSH;
    if (ago_str_eq(name, len, "map", 3)) return AGO_BUILTIN_MAP;
    if (ago_str_eq(name, len, "filter", 6)) return AGO_BUILTIN_FILTER;
    if (ago_str_eq(name, len, "abs", 3)) return AGO_BUILTIN_ABS;
    if (ago_str_eq(name, len, "min", 3)) return AGO_BUILTIN_MIN;
    if (ago_str_eq(name, len, "max", 3)) return AGO_BUILTIN_MAX;
    if (ago_str_eq(name, len, "read_file", 9)) return AGO_BUILTIN_READ_FILE;
    if (ago_str_eq(name, len, "write_file", 10)) return AGO_BUILTIN_WRITE_FILE;
    if (ago_str_eq(name, len, "file_exists", 11)) return AGO_BUILTIN_FILE_EXISTS;
    if (ago_str_eq(name, len, "ok", 2)) return AGO_BUILTIN_NONE;
    if (ago_str_eq(name, len, "err", 3)) return AGO_BUILTIN_NONE;
    return AGO_BUILTIN_NONE;
}

/* ---- Compiler state ---- */

typedef struct {
    AgoChunk *chunk;
    AgoCtx *ctx;
    AgoArena *arena;
    AgoGc *gc;
    int scope_depth;        /* for tracking block-local var counts */
    int block_var_count;    /* vars defined in current block scope */
} Compiler;

static void compile_expr(Compiler *c, AgoNode *node);
static void compile_stmt(Compiler *c, AgoNode *node);

/* ---- Helpers ---- */

static void emit(Compiler *c, uint8_t byte) {
    ago_chunk_write(c->chunk, byte);
}

static void emit_u16(Compiler *c, uint16_t val) {
    ago_chunk_write_u16(c->chunk, val);
}

static int add_const(Compiler *c, AgoVal val) {
    return ago_chunk_add_const(c->chunk, val);
}

static int emit_const(Compiler *c, AgoVal val) {
    int idx = add_const(c, val);
    emit(c, AGO_OP_CONST);
    emit_u16(c, (uint16_t)idx);
    return idx;
}

static int add_string_const(Compiler *c, const char *s, int len) {
    AgoVal v;
    v.kind = VAL_STRING;
    v.as.string.data = s;
    v.as.string.length = len;
    return add_const(c, v);
}

static void emit_line(Compiler *c, int line) {
    emit(c, AGO_OP_LINE);
    emit_u16(c, (uint16_t)line);
}

/* ---- Expression compilation ---- */

static void compile_expr(Compiler *c, AgoNode *node) {
    if (!node || ago_error_occurred(c->ctx)) return;

    switch (node->kind) {
    case AGO_NODE_INT_LIT:
        emit_const(c, (AgoVal){VAL_INT, {.integer = node->as.int_lit.value}});
        break;

    case AGO_NODE_FLOAT_LIT:
        emit_const(c, (AgoVal){VAL_FLOAT, {.floating = node->as.float_lit.value}});
        break;

    case AGO_NODE_STRING_LIT:
        emit_const(c, (AgoVal){VAL_STRING, {.string = {node->as.string_lit.value,
                                                        node->as.string_lit.length}}});
        break;

    case AGO_NODE_BOOL_LIT:
        emit(c, node->as.bool_lit.value ? AGO_OP_TRUE : AGO_OP_FALSE);
        break;

    case AGO_NODE_IDENT: {
        int idx = add_string_const(c, node->as.ident.name, node->as.ident.length);
        emit(c, AGO_OP_GET_VAR);
        emit_u16(c, (uint16_t)idx);
        break;
    }

    case AGO_NODE_UNARY:
        compile_expr(c, node->as.unary.operand);
        if (node->as.unary.op == AGO_TOKEN_MINUS) emit(c, AGO_OP_NEGATE);
        else if (node->as.unary.op == AGO_TOKEN_NOT) emit(c, AGO_OP_NOT);
        break;

    case AGO_NODE_BINARY: {
        AgoTokenKind op = node->as.binary.op;

        /* Field access: right side is a name, not an expression */
        if (op == AGO_TOKEN_DOT) {
            compile_expr(c, node->as.binary.left);
            AgoNode *field = node->as.binary.right;
            int idx = add_string_const(c, field->as.ident.name, field->as.ident.length);
            emit(c, AGO_OP_GET_FIELD);
            emit_u16(c, (uint16_t)idx);
            break;
        }

        /* Short-circuit && */
        if (op == AGO_TOKEN_AND) {
            compile_expr(c, node->as.binary.left);
            int jump = ago_chunk_emit_jump(c->chunk, AGO_OP_JUMP_IF_FALSE);
            emit(c, AGO_OP_POP);
            compile_expr(c, node->as.binary.right);
            ago_chunk_patch_jump(c->chunk, jump);
            break;
        }

        /* Short-circuit || */
        if (op == AGO_TOKEN_OR) {
            compile_expr(c, node->as.binary.left);
            int jump = ago_chunk_emit_jump(c->chunk, AGO_OP_JUMP_IF_TRUE);
            emit(c, AGO_OP_POP);
            compile_expr(c, node->as.binary.right);
            ago_chunk_patch_jump(c->chunk, jump);
            break;
        }

        compile_expr(c, node->as.binary.left);
        compile_expr(c, node->as.binary.right);

        switch (op) {
        case AGO_TOKEN_PLUS:    emit(c, AGO_OP_ADD); break;
        case AGO_TOKEN_MINUS:   emit(c, AGO_OP_SUB); break;
        case AGO_TOKEN_STAR:    emit(c, AGO_OP_MUL); break;
        case AGO_TOKEN_SLASH:   emit(c, AGO_OP_DIV); break;
        case AGO_TOKEN_PERCENT: emit(c, AGO_OP_MOD); break;
        case AGO_TOKEN_EQ:      emit(c, AGO_OP_EQ); break;
        case AGO_TOKEN_NEQ:     emit(c, AGO_OP_NEQ); break;
        case AGO_TOKEN_LT:      emit(c, AGO_OP_LT); break;
        case AGO_TOKEN_GT:      emit(c, AGO_OP_GT); break;
        case AGO_TOKEN_LE:      emit(c, AGO_OP_LE); break;
        case AGO_TOKEN_GE:      emit(c, AGO_OP_GE); break;
        default:
            ago_error_set(c->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, node->line, node->column),
                          "unsupported binary operator in compiler");
            break;
        }
        break;
    }

    case AGO_NODE_CALL: {
        /* Check if callee is a builtin */
        if (node->as.call.callee->kind == AGO_NODE_IDENT) {
            const char *name = node->as.call.callee->as.ident.name;
            int len = node->as.call.callee->as.ident.length;
            AgoBuiltinId bid = resolve_builtin(name, len);
            if (bid != AGO_BUILTIN_NONE) {
                /* Compile arguments */
                for (int i = 0; i < node->as.call.arg_count; i++) {
                    compile_expr(c, node->as.call.args[i]);
                }
                emit(c, AGO_OP_CALL_BUILTIN);
                emit_u16(c, (uint16_t)bid);
                emit(c, (uint8_t)node->as.call.arg_count);
                break;
            }
        }
        /* User function call: compile callee, then args */
        compile_expr(c, node->as.call.callee);
        for (int i = 0; i < node->as.call.arg_count; i++) {
            compile_expr(c, node->as.call.args[i]);
        }
        emit(c, AGO_OP_CALL);
        emit(c, (uint8_t)node->as.call.arg_count);
        break;
    }

    case AGO_NODE_ARRAY_LIT:
        for (int i = 0; i < node->as.array_lit.count; i++) {
            compile_expr(c, node->as.array_lit.elements[i]);
        }
        emit(c, AGO_OP_ARRAY);
        emit_u16(c, (uint16_t)node->as.array_lit.count);
        break;

    case AGO_NODE_INDEX:
        compile_expr(c, node->as.index_expr.object);
        compile_expr(c, node->as.index_expr.index);
        emit(c, AGO_OP_INDEX);
        break;

    case AGO_NODE_STRUCT_LIT: {
        /* Push field name indices and values interleaved */
        for (int i = 0; i < node->as.struct_lit.field_count; i++) {
            /* Push field name as constant (for struct construction) */
            int name_idx = add_string_const(c, node->as.struct_lit.field_names[i],
                                            node->as.struct_lit.field_name_lengths[i]);
            emit(c, AGO_OP_CONST);
            emit_u16(c, (uint16_t)name_idx);
            compile_expr(c, node->as.struct_lit.field_values[i]);
        }
        int type_idx = add_string_const(c, node->as.struct_lit.name,
                                        node->as.struct_lit.name_length);
        emit(c, AGO_OP_STRUCT);
        emit_u16(c, (uint16_t)type_idx);
        emit(c, (uint8_t)node->as.struct_lit.field_count);
        break;
    }

    case AGO_NODE_LAMBDA: {
        /* Sub-compile the lambda body into a new chunk */
        Compiler sub;
        sub.ctx = c->ctx;
        sub.arena = c->arena;
        sub.gc = c->gc;
        sub.chunk = ago_chunk_new();
        sub.scope_depth = 0;
        sub.block_var_count = 0;
        if (!sub.chunk) return;

        /* Compile function body */
        if (node->as.fn_decl.body) {
            compile_stmt(&sub, node->as.fn_decl.body);
        }
        /* Implicit return nil */
        emit(&sub, AGO_OP_RETURN_NIL);

        /* Create function value with the chunk */
        AgoFnVal *fn = ago_gc_alloc(c->gc, sizeof(AgoFnVal), fn_cleanup);
        if (!fn) { ago_chunk_free(sub.chunk); return; }
        fn->decl = node;
        fn->chunk = sub.chunk;
        fn->arity = node->as.fn_decl.param_count;
        fn->captured_count = 0;
        fn->captured_names = NULL;
        fn->captured_name_lengths = NULL;
        fn->captured_values = NULL;
        fn->captured_immutable = NULL;

        AgoVal fn_val;
        fn_val.kind = VAL_FN;
        fn_val.as.fn = fn;
        int idx = add_const(c, fn_val);
        emit(c, AGO_OP_CLOSURE);
        emit_u16(c, (uint16_t)idx);
        break;
    }

    case AGO_NODE_RESULT_OK:
        compile_expr(c, node->as.result_val.value);
        emit(c, AGO_OP_RESULT_OK);
        break;

    case AGO_NODE_RESULT_ERR:
        compile_expr(c, node->as.result_val.value);
        emit(c, AGO_OP_RESULT_ERR);
        break;

    case AGO_NODE_MATCH_EXPR: {
        compile_expr(c, node->as.match_expr.subject);
        /* OP_MATCH ok_name_idx err_name_idx ok_offset err_offset */
        int ok_name = add_string_const(c, node->as.match_expr.ok_name,
                                       node->as.match_expr.ok_name_length);
        int err_name = add_string_const(c, node->as.match_expr.err_name,
                                        node->as.match_expr.err_name_length);
        emit(c, AGO_OP_MATCH);
        emit_u16(c, (uint16_t)ok_name);
        emit_u16(c, (uint16_t)err_name);
        int ok_offset_pos = c->chunk->code_count;
        emit_u16(c, 0); /* placeholder for ok branch offset */
        int err_offset_pos = c->chunk->code_count;
        emit_u16(c, 0); /* placeholder for err branch offset */

        /* Ok branch */
        int ok_start = c->chunk->code_count;
        compile_expr(c, node->as.match_expr.ok_body);
        int ok_end_jump = ago_chunk_emit_jump(c->chunk, AGO_OP_JUMP);

        /* Err branch */
        int err_start = c->chunk->code_count;
        compile_expr(c, node->as.match_expr.err_body);

        /* Patch offsets: relative to after the OP_MATCH operands */
        int base = err_offset_pos + 2;
        c->chunk->code[ok_offset_pos] = (uint8_t)((ok_start - base) & 0xff);
        c->chunk->code[ok_offset_pos + 1] = (uint8_t)((ok_start - base) >> 8);
        c->chunk->code[err_offset_pos] = (uint8_t)((err_start - base) & 0xff);
        c->chunk->code[err_offset_pos + 1] = (uint8_t)((err_start - base) >> 8);
        ago_chunk_patch_jump(c->chunk, ok_end_jump);
        break;
    }

    default:
        ago_error_set(c->ctx, AGO_ERR_RUNTIME,
                      ago_loc(NULL, node->line, node->column),
                      "unsupported expression in compiler");
        break;
    }
}

/* ---- Statement compilation ---- */

static void compile_stmt(Compiler *c, AgoNode *node) {
    if (!node || ago_error_occurred(c->ctx)) return;

    emit_line(c, node->line);

    switch (node->kind) {
    case AGO_NODE_EXPR_STMT:
        compile_expr(c, node->as.expr_stmt.expr);
        emit(c, AGO_OP_POP);
        break;

    case AGO_NODE_LET_STMT:
    case AGO_NODE_VAR_STMT: {
        if (node->as.var_decl.initializer) {
            compile_expr(c, node->as.var_decl.initializer);
        } else {
            emit(c, AGO_OP_NIL);
        }
        int idx = add_string_const(c, node->as.var_decl.name,
                                   node->as.var_decl.name_length);
        emit(c, node->kind == AGO_NODE_LET_STMT ? AGO_OP_DEFINE_LET : AGO_OP_DEFINE_VAR);
        emit_u16(c, (uint16_t)idx);
        c->block_var_count++;
        break;
    }

    case AGO_NODE_ASSIGN_STMT: {
        compile_expr(c, node->as.assign_stmt.value);
        int idx = add_string_const(c, node->as.assign_stmt.name,
                                   node->as.assign_stmt.name_length);
        emit(c, AGO_OP_SET_VAR);
        emit_u16(c, (uint16_t)idx);
        break;
    }

    case AGO_NODE_RETURN_STMT:
        if (node->as.return_stmt.value) {
            compile_expr(c, node->as.return_stmt.value);
            emit(c, AGO_OP_RETURN);
        } else {
            emit(c, AGO_OP_RETURN_NIL);
        }
        break;

    case AGO_NODE_IF_STMT: {
        compile_expr(c, node->as.if_stmt.condition);
        int false_jump = ago_chunk_emit_jump(c->chunk, AGO_OP_JUMP_IF_FALSE);
        compile_stmt(c, node->as.if_stmt.then_block);
        if (node->as.if_stmt.else_block) {
            int end_jump = ago_chunk_emit_jump(c->chunk, AGO_OP_JUMP);
            ago_chunk_patch_jump(c->chunk, false_jump);
            compile_stmt(c, node->as.if_stmt.else_block);
            ago_chunk_patch_jump(c->chunk, end_jump);
        } else {
            ago_chunk_patch_jump(c->chunk, false_jump);
        }
        break;
    }

    case AGO_NODE_WHILE_STMT: {
        int loop_start = c->chunk->code_count;
        compile_expr(c, node->as.while_stmt.condition);
        int exit_jump = ago_chunk_emit_jump(c->chunk, AGO_OP_JUMP_IF_FALSE);
        compile_stmt(c, node->as.while_stmt.body);
        ago_chunk_emit_loop(c->chunk, loop_start);
        ago_chunk_patch_jump(c->chunk, exit_jump);
        break;
    }

    case AGO_NODE_FOR_STMT: {
        /* Compile iterable */
        compile_expr(c, node->as.for_stmt.iterable);
        emit(c, AGO_OP_ITER_SETUP);

        int loop_start = c->chunk->code_count;
        int exit_jump_pos = c->chunk->code_count;
        emit(c, AGO_OP_ITER_NEXT);
        emit_u16(c, 0); /* placeholder for end offset */

        /* Define loop variable from TOS (element pushed by ITER_NEXT) */
        int var_idx = add_string_const(c, node->as.for_stmt.var_name,
                                       node->as.for_stmt.var_name_length);
        emit(c, AGO_OP_DEFINE_VAR);
        emit_u16(c, (uint16_t)var_idx);

        /* Compile body */
        compile_stmt(c, node->as.for_stmt.body);

        /* Pop loop variable */
        emit(c, AGO_OP_POP_SCOPE);
        emit(c, 1);

        ago_chunk_emit_loop(c->chunk, loop_start);

        /* Patch ITER_NEXT exit jump */
        ago_chunk_patch_jump(c->chunk, exit_jump_pos + 1);

        emit(c, AGO_OP_ITER_CLEANUP);
        break;
    }

    case AGO_NODE_BLOCK: {
        int saved_var_count = c->block_var_count;
        c->block_var_count = 0;
        for (int i = 0; i < node->as.block.stmt_count; i++) {
            compile_stmt(c, node->as.block.stmts[i]);
            if (ago_error_occurred(c->ctx)) return;
        }
        /* Pop block-local variables */
        if (c->block_var_count > 0) {
            emit(c, AGO_OP_POP_SCOPE);
            emit(c, (uint8_t)c->block_var_count);
        }
        c->block_var_count = saved_var_count;
        break;
    }

    case AGO_NODE_FN_DECL: {
        /* Sub-compile the function body */
        Compiler sub;
        sub.ctx = c->ctx;
        sub.arena = c->arena;
        sub.gc = c->gc;
        sub.chunk = ago_chunk_new();
        sub.scope_depth = 0;
        sub.block_var_count = 0;
        if (!sub.chunk) return;

        if (node->as.fn_decl.body) {
            compile_stmt(&sub, node->as.fn_decl.body);
        }
        emit(&sub, AGO_OP_RETURN_NIL);

        AgoFnVal *fn = ago_gc_alloc(c->gc, sizeof(AgoFnVal), fn_cleanup);
        if (!fn) { ago_chunk_free(sub.chunk); return; }
        fn->decl = node;
        fn->chunk = sub.chunk;
        fn->arity = node->as.fn_decl.param_count;
        fn->captured_count = 0;
        fn->captured_names = NULL;
        fn->captured_name_lengths = NULL;
        fn->captured_values = NULL;
        fn->captured_immutable = NULL;

        AgoVal fn_val;
        fn_val.kind = VAL_FN;
        fn_val.as.fn = fn;
        int fn_idx = add_const(c, fn_val);
        emit(c, AGO_OP_CLOSURE);
        emit_u16(c, (uint16_t)fn_idx);

        int name_idx = add_string_const(c, node->as.fn_decl.name,
                                        node->as.fn_decl.name_length);
        emit(c, AGO_OP_DEFINE_LET);
        emit_u16(c, (uint16_t)name_idx);
        c->block_var_count++;
        break;
    }

    case AGO_NODE_STRUCT_DECL:
        /* No-op at runtime */
        break;

    case AGO_NODE_IMPORT: {
        int idx = add_string_const(c, node->as.import_stmt.path,
                                   node->as.import_stmt.path_length);
        emit(c, AGO_OP_IMPORT);
        emit_u16(c, (uint16_t)idx);
        break;
    }

    default:
        ago_error_set(c->ctx, AGO_ERR_RUNTIME,
                      ago_loc(NULL, node->line, node->column),
                      "unsupported statement in compiler");
        break;
    }
}

/* ---- Public API ---- */

AgoChunk *ago_compile(AgoNode *program, AgoCtx *ctx, AgoArena *arena, AgoGc *gc) {
    if (!program || program->kind != AGO_NODE_PROGRAM) return NULL;

    AgoChunk *chunk = ago_chunk_new();
    if (!chunk) return NULL;

    Compiler c;
    c.chunk = chunk;
    c.ctx = ctx;
    c.arena = arena;
    c.gc = gc;
    c.scope_depth = 0;
    c.block_var_count = 0;

    for (int i = 0; i < program->as.program.decl_count; i++) {
        compile_stmt(&c, program->as.program.decls[i]);
        if (ago_error_occurred(ctx)) {
            ago_chunk_free(chunk);
            return NULL;
        }
    }

    /* End with implicit return nil for top-level */
    emit(&c, AGO_OP_RETURN_NIL);

    return chunk;
}
