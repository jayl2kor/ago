#include "runtime.h"
#include "compiler.h"
#include "builtins_core.h"

/* ---- Builtin name-to-ID mapping ---- */

static AglBuiltinId resolve_builtin(const char *name, int len) {
    /* ok/err are keywords, not builtins */
    if (agl_str_eq(name, len, "ok", 2)) return AGL_BUILTIN_NONE;
    if (agl_str_eq(name, len, "err", 3)) return AGL_BUILTIN_NONE;
    return agl_builtin_resolve(name, len);
}

/* ---- Loop context for break/continue ---- */

#define MAX_LOOP_DEPTH 16
#define MAX_BREAK_PATCHES 32

typedef struct {
    int continue_target;            /* IP to jump back to for continue */
    int break_patches[MAX_BREAK_PATCHES]; /* offsets to patch for break */
    int break_count;
    bool is_for;                    /* true if for-in loop (needs extra cleanup) */
    int scope_var_count;            /* vars defined inside loop body to pop before break/continue */
} LoopCtx;

/* ---- Compiler state ---- */

typedef struct {
    AglChunk *chunk;
    AglCtx *ctx;
    AglArena *arena;
    AglGc *gc;
    int scope_depth;        /* for tracking block-local var counts */
    int block_var_count;    /* vars defined in current block scope */
    LoopCtx loop_stack[MAX_LOOP_DEPTH];
    int loop_depth;
    bool is_top_level;      /* true for the main compilation context */
} Compiler;

static void compile_expr(Compiler *c, AglNode *node);
static void compile_stmt(Compiler *c, AglNode *node);

/* ---- Helpers ---- */

static void emit(Compiler *c, uint8_t byte) {
    agl_chunk_write(c->chunk, byte);
}

static void emit_u16(Compiler *c, uint16_t val) {
    agl_chunk_write_u16(c->chunk, val);
}

static int add_const(Compiler *c, AglVal val) {
    return agl_chunk_add_const(c->chunk, val);
}

static int emit_const(Compiler *c, AglVal val) {
    int idx = add_const(c, val);
    emit(c, AGL_OP_CONST);
    emit_u16(c, (uint16_t)idx);
    return idx;
}

static int add_string_const(Compiler *c, const char *s, int len) {
    AglVal v;
    v.kind = VAL_STRING;
    v.as.string.data = s;
    v.as.string.length = len;
    return add_const(c, v);
}

static void emit_line(Compiler *c, int line) {
    emit(c, AGL_OP_LINE);
    emit_u16(c, (uint16_t)line);
}

/* ---- Expression compilation ---- */

static void compile_expr(Compiler *c, AglNode *node) {
    if (!node || agl_error_occurred(c->ctx)) return;

    switch (node->kind) {
    case AGL_NODE_INT_LIT:
        emit_const(c, (AglVal){VAL_INT, {.integer = node->as.int_lit.value}});
        break;

    case AGL_NODE_FLOAT_LIT:
        emit_const(c, (AglVal){VAL_FLOAT, {.floating = node->as.float_lit.value}});
        break;

    case AGL_NODE_STRING_LIT:
        emit_const(c, (AglVal){VAL_STRING, {.string = {node->as.string_lit.value,
                                                        node->as.string_lit.length}}});
        break;

    case AGL_NODE_BOOL_LIT:
        emit(c, node->as.bool_lit.value ? AGL_OP_TRUE : AGL_OP_FALSE);
        break;

    case AGL_NODE_IDENT: {
        int idx = add_string_const(c, node->as.ident.name, node->as.ident.length);
        emit(c, AGL_OP_GET_VAR);
        emit_u16(c, (uint16_t)idx);
        break;
    }

    case AGL_NODE_UNARY:
        compile_expr(c, node->as.unary.operand);
        if (node->as.unary.op == AGL_TOKEN_MINUS) emit(c, AGL_OP_NEGATE);
        else if (node->as.unary.op == AGL_TOKEN_NOT) emit(c, AGL_OP_NOT);
        break;

    case AGL_NODE_BINARY: {
        AglTokenKind op = node->as.binary.op;

        /* Field access: right side is a name, not an expression */
        if (op == AGL_TOKEN_DOT) {
            compile_expr(c, node->as.binary.left);
            AglNode *field = node->as.binary.right;
            int idx = add_string_const(c, field->as.ident.name, field->as.ident.length);
            emit(c, AGL_OP_GET_FIELD);
            emit_u16(c, (uint16_t)idx);
            break;
        }

        /* Short-circuit && */
        if (op == AGL_TOKEN_AND) {
            compile_expr(c, node->as.binary.left);
            int jump = agl_chunk_emit_jump(c->chunk, AGL_OP_JUMP_IF_FALSE);
            emit(c, AGL_OP_POP);
            compile_expr(c, node->as.binary.right);
            agl_chunk_patch_jump(c->chunk, jump);
            break;
        }

        /* Short-circuit || */
        if (op == AGL_TOKEN_OR) {
            compile_expr(c, node->as.binary.left);
            int jump = agl_chunk_emit_jump(c->chunk, AGL_OP_JUMP_IF_TRUE);
            emit(c, AGL_OP_POP);
            compile_expr(c, node->as.binary.right);
            agl_chunk_patch_jump(c->chunk, jump);
            break;
        }

        compile_expr(c, node->as.binary.left);
        compile_expr(c, node->as.binary.right);

        switch (op) {
        case AGL_TOKEN_PLUS:    emit(c, AGL_OP_ADD); break;
        case AGL_TOKEN_MINUS:   emit(c, AGL_OP_SUB); break;
        case AGL_TOKEN_STAR:    emit(c, AGL_OP_MUL); break;
        case AGL_TOKEN_SLASH:   emit(c, AGL_OP_DIV); break;
        case AGL_TOKEN_PERCENT: emit(c, AGL_OP_MOD); break;
        case AGL_TOKEN_EQ:      emit(c, AGL_OP_EQ); break;
        case AGL_TOKEN_NEQ:     emit(c, AGL_OP_NEQ); break;
        case AGL_TOKEN_LT:      emit(c, AGL_OP_LT); break;
        case AGL_TOKEN_GT:      emit(c, AGL_OP_GT); break;
        case AGL_TOKEN_LE:      emit(c, AGL_OP_LE); break;
        case AGL_TOKEN_GE:      emit(c, AGL_OP_GE); break;
        default:
            agl_error_set(c->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, node->line, node->column),
                          "unsupported binary operator in compiler");
            break;
        }
        break;
    }

    case AGL_NODE_CALL: {
        /* Check if callee is a builtin */
        if (node->as.call.callee->kind == AGL_NODE_IDENT) {
            const char *name = node->as.call.callee->as.ident.name;
            int len = node->as.call.callee->as.ident.length;
            AglBuiltinId bid = resolve_builtin(name, len);
            if (bid != AGL_BUILTIN_NONE) {
                /* Compile arguments */
                for (int i = 0; i < node->as.call.arg_count; i++) {
                    compile_expr(c, node->as.call.args[i]);
                }
                emit(c, AGL_OP_CALL_BUILTIN);
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
        emit(c, AGL_OP_CALL);
        emit(c, (uint8_t)node->as.call.arg_count);
        break;
    }

    case AGL_NODE_ARRAY_LIT:
        for (int i = 0; i < node->as.array_lit.count; i++) {
            compile_expr(c, node->as.array_lit.elements[i]);
        }
        emit(c, AGL_OP_ARRAY);
        emit_u16(c, (uint16_t)node->as.array_lit.count);
        break;

    case AGL_NODE_MAP_LIT:
        for (int i = 0; i < node->as.map_lit.count; i++) {
            /* Push key as string constant */
            int key_idx = add_string_const(c, node->as.map_lit.keys[i],
                                           node->as.map_lit.key_lengths[i]);
            emit(c, AGL_OP_CONST);
            emit_u16(c, (uint16_t)key_idx);
            /* Compile value expression */
            compile_expr(c, node->as.map_lit.values[i]);
        }
        emit(c, AGL_OP_MAP);
        emit_u16(c, (uint16_t)node->as.map_lit.count);
        break;

    case AGL_NODE_INDEX:
        compile_expr(c, node->as.index_expr.object);
        compile_expr(c, node->as.index_expr.index);
        emit(c, AGL_OP_INDEX);
        break;

    case AGL_NODE_STRUCT_LIT: {
        /* Push field name indices and values interleaved */
        for (int i = 0; i < node->as.struct_lit.field_count; i++) {
            /* Push field name as constant (for struct construction) */
            int name_idx = add_string_const(c, node->as.struct_lit.field_names[i],
                                            node->as.struct_lit.field_name_lengths[i]);
            emit(c, AGL_OP_CONST);
            emit_u16(c, (uint16_t)name_idx);
            compile_expr(c, node->as.struct_lit.field_values[i]);
        }
        int type_idx = add_string_const(c, node->as.struct_lit.name,
                                        node->as.struct_lit.name_length);
        emit(c, AGL_OP_STRUCT);
        emit_u16(c, (uint16_t)type_idx);
        emit(c, (uint8_t)node->as.struct_lit.field_count);
        break;
    }

    case AGL_NODE_LAMBDA: {
        /* Sub-compile the lambda body into a new chunk */
        Compiler sub;
        sub.ctx = c->ctx;
        sub.arena = c->arena;
        sub.gc = c->gc;
        sub.chunk = agl_chunk_new();
        sub.scope_depth = 0;
        sub.block_var_count = 0;
        sub.loop_depth = 0;
        sub.is_top_level = false;
        if (!sub.chunk) return;

        /* Compile function body */
        if (node->as.fn_decl.body) {
            compile_stmt(&sub, node->as.fn_decl.body);
        }
        /* Implicit return nil */
        emit(&sub, AGL_OP_RETURN_NIL);

        /* Create function value with the chunk */
        AglFnVal *fn = agl_gc_alloc(c->gc, sizeof(AglFnVal), fn_cleanup);
        if (!fn) { agl_chunk_free(sub.chunk); return; }
        fn->decl = node;
        fn->chunk = sub.chunk;
        fn->arity = node->as.fn_decl.param_count;
        fn->captured_count = 0;
        fn->captured_names = NULL;
        fn->captured_name_lengths = NULL;
        fn->captured_values = NULL;
        fn->captured_immutable = NULL;

        AglVal fn_val;
        fn_val.kind = VAL_FN;
        fn_val.as.fn = fn;
        int idx = add_const(c, fn_val);
        emit(c, AGL_OP_CLOSURE);
        emit_u16(c, (uint16_t)idx);
        break;
    }

    case AGL_NODE_RESULT_OK:
        compile_expr(c, node->as.result_val.value);
        emit(c, AGL_OP_RESULT_OK);
        break;

    case AGL_NODE_RESULT_ERR:
        compile_expr(c, node->as.result_val.value);
        emit(c, AGL_OP_RESULT_ERR);
        break;

    case AGL_NODE_MATCH_EXPR: {
        compile_expr(c, node->as.match_expr.subject);
        /* OP_MATCH ok_name_idx err_name_idx ok_offset err_offset */
        int ok_name = add_string_const(c, node->as.match_expr.ok_name,
                                       node->as.match_expr.ok_name_length);
        int err_name = add_string_const(c, node->as.match_expr.err_name,
                                        node->as.match_expr.err_name_length);
        emit(c, AGL_OP_MATCH);
        emit_u16(c, (uint16_t)ok_name);
        emit_u16(c, (uint16_t)err_name);
        int ok_offset_pos = c->chunk->code_count;
        emit_u16(c, 0); /* placeholder for ok branch offset */
        int err_offset_pos = c->chunk->code_count;
        emit_u16(c, 0); /* placeholder for err branch offset */

        /* Ok branch */
        int ok_start = c->chunk->code_count;
        compile_expr(c, node->as.match_expr.ok_body);
        int ok_end_jump = agl_chunk_emit_jump(c->chunk, AGL_OP_JUMP);

        /* Err branch */
        int err_start = c->chunk->code_count;
        compile_expr(c, node->as.match_expr.err_body);

        /* Patch offsets: relative to after the OP_MATCH operands */
        int base = err_offset_pos + 2;
        c->chunk->code[ok_offset_pos] = (uint8_t)((ok_start - base) & 0xff);
        c->chunk->code[ok_offset_pos + 1] = (uint8_t)((ok_start - base) >> 8);
        c->chunk->code[err_offset_pos] = (uint8_t)((err_start - base) & 0xff);
        c->chunk->code[err_offset_pos + 1] = (uint8_t)((err_start - base) >> 8);
        agl_chunk_patch_jump(c->chunk, ok_end_jump);
        break;
    }

    default:
        agl_error_set(c->ctx, AGL_ERR_RUNTIME,
                      agl_loc(NULL, node->line, node->column),
                      "unsupported expression in compiler");
        break;
    }
}

/* ---- Statement compilation ---- */

static void compile_stmt(Compiler *c, AglNode *node) {
    if (!node || agl_error_occurred(c->ctx)) return;

    emit_line(c, node->line);

    switch (node->kind) {
    case AGL_NODE_EXPR_STMT:
        compile_expr(c, node->as.expr_stmt.expr);
        emit(c, AGL_OP_POP);
        break;

    case AGL_NODE_LET_STMT:
    case AGL_NODE_VAR_STMT: {
        if (node->as.var_decl.initializer) {
            compile_expr(c, node->as.var_decl.initializer);
        } else {
            emit(c, AGL_OP_NIL);
        }
        int idx = add_string_const(c, node->as.var_decl.name,
                                   node->as.var_decl.name_length);
        emit(c, node->kind == AGL_NODE_LET_STMT ? AGL_OP_DEFINE_LET : AGL_OP_DEFINE_VAR);
        emit_u16(c, (uint16_t)idx);
        c->block_var_count++;
        break;
    }

    case AGL_NODE_ASSIGN_STMT: {
        compile_expr(c, node->as.assign_stmt.value);
        int idx = add_string_const(c, node->as.assign_stmt.name,
                                   node->as.assign_stmt.name_length);
        emit(c, AGL_OP_SET_VAR);
        emit_u16(c, (uint16_t)idx);
        break;
    }

    case AGL_NODE_RETURN_STMT:
        if (node->as.return_stmt.value) {
            compile_expr(c, node->as.return_stmt.value);
            emit(c, AGL_OP_RETURN);
        } else {
            emit(c, AGL_OP_RETURN_NIL);
        }
        break;

    case AGL_NODE_IF_STMT: {
        compile_expr(c, node->as.if_stmt.condition);
        int false_jump = agl_chunk_emit_jump(c->chunk, AGL_OP_JUMP_IF_FALSE);
        compile_stmt(c, node->as.if_stmt.then_block);
        if (node->as.if_stmt.else_block) {
            int end_jump = agl_chunk_emit_jump(c->chunk, AGL_OP_JUMP);
            agl_chunk_patch_jump(c->chunk, false_jump);
            compile_stmt(c, node->as.if_stmt.else_block);
            agl_chunk_patch_jump(c->chunk, end_jump);
        } else {
            agl_chunk_patch_jump(c->chunk, false_jump);
        }
        break;
    }

    case AGL_NODE_WHILE_STMT: {
        int loop_start = c->chunk->code_count;
        compile_expr(c, node->as.while_stmt.condition);
        int exit_jump = agl_chunk_emit_jump(c->chunk, AGL_OP_JUMP_IF_FALSE);

        /* Push loop context */
        if (c->loop_depth >= MAX_LOOP_DEPTH) {
            agl_error_set(c->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, node->line, node->column),
                          "too many nested loops (max %d)", MAX_LOOP_DEPTH);
            break;
        }
        LoopCtx *lc = &c->loop_stack[c->loop_depth++];
        lc->continue_target = loop_start;
        lc->break_count = 0;
        lc->is_for = false;
        lc->scope_var_count = 0;

        compile_stmt(c, node->as.while_stmt.body);

        c->loop_depth--;

        agl_chunk_emit_loop(c->chunk, loop_start);
        agl_chunk_patch_jump(c->chunk, exit_jump);

        /* Patch all break jumps to here (past the loop) */
        for (int i = 0; i < lc->break_count; i++) {
            agl_chunk_patch_jump(c->chunk, lc->break_patches[i]);
        }
        break;
    }

    case AGL_NODE_FOR_STMT: {
        /* Compile iterable */
        compile_expr(c, node->as.for_stmt.iterable);
        emit(c, AGL_OP_ITER_SETUP);

        int loop_start = c->chunk->code_count;
        int exit_jump_pos = c->chunk->code_count;
        emit(c, AGL_OP_ITER_NEXT);
        emit_u16(c, 0); /* placeholder for end offset */

        /* Define loop variable from TOS (element pushed by ITER_NEXT) */
        int var_idx = add_string_const(c, node->as.for_stmt.var_name,
                                       node->as.for_stmt.var_name_length);
        emit(c, AGL_OP_DEFINE_VAR);
        emit_u16(c, (uint16_t)var_idx);

        /* Push loop context */
        if (c->loop_depth >= MAX_LOOP_DEPTH) {
            agl_error_set(c->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, node->line, node->column),
                          "too many nested loops (max %d)", MAX_LOOP_DEPTH);
            break;
        }
        LoopCtx *lc = &c->loop_stack[c->loop_depth++];
        lc->continue_target = loop_start;
        lc->break_count = 0;
        lc->is_for = true;
        lc->scope_var_count = 0;

        /* Compile body */
        compile_stmt(c, node->as.for_stmt.body);

        c->loop_depth--;

        /* Pop loop variable */
        emit(c, AGL_OP_POP_SCOPE);
        emit(c, 1);

        agl_chunk_emit_loop(c->chunk, loop_start);

        /* Patch ITER_NEXT exit jump */
        agl_chunk_patch_jump(c->chunk, exit_jump_pos + 1);

        /* Patch all break jumps to here (before ITER_CLEANUP) */
        for (int i = 0; i < lc->break_count; i++) {
            agl_chunk_patch_jump(c->chunk, lc->break_patches[i]);
        }

        emit(c, AGL_OP_ITER_CLEANUP);
        break;
    }

    case AGL_NODE_BREAK_STMT: {
        if (c->loop_depth == 0) {
            agl_error_set(c->ctx, AGL_ERR_SYNTAX,
                          agl_loc(NULL, node->line, node->column),
                          "'break' outside of loop");
            break;
        }
        LoopCtx *lc = &c->loop_stack[c->loop_depth - 1];
        /* Pop block-local variables defined inside the loop body */
        if (c->block_var_count > 0) {
            emit(c, AGL_OP_POP_SCOPE);
            emit(c, (uint8_t)c->block_var_count);
        }
        if (lc->is_for) {
            /* Pop the loop variable (1) defined by the for-in */
            emit(c, AGL_OP_POP_SCOPE);
            emit(c, 1);
        }
        /* Emit forward jump to be patched at loop end */
        int patch = agl_chunk_emit_jump(c->chunk, AGL_OP_JUMP);
        if (lc->break_count < MAX_BREAK_PATCHES) {
            lc->break_patches[lc->break_count++] = patch;
        }
        break;
    }

    case AGL_NODE_CONTINUE_STMT: {
        if (c->loop_depth == 0) {
            agl_error_set(c->ctx, AGL_ERR_SYNTAX,
                          agl_loc(NULL, node->line, node->column),
                          "'continue' outside of loop");
            break;
        }
        LoopCtx *lc = &c->loop_stack[c->loop_depth - 1];
        /* Pop block-local variables defined inside the loop body */
        if (c->block_var_count > 0) {
            emit(c, AGL_OP_POP_SCOPE);
            emit(c, (uint8_t)c->block_var_count);
        }
        if (lc->is_for) {
            /* Pop the loop variable (1) defined by the for-in */
            emit(c, AGL_OP_POP_SCOPE);
            emit(c, 1);
        }
        /* Jump back to loop start */
        agl_chunk_emit_loop(c->chunk, lc->continue_target);
        break;
    }

    case AGL_NODE_BLOCK: {
        int saved_var_count = c->block_var_count;
        c->block_var_count = 0;
        for (int i = 0; i < node->as.block.stmt_count; i++) {
            compile_stmt(c, node->as.block.stmts[i]);
            if (agl_error_occurred(c->ctx)) return;
        }
        /* Pop block-local variables */
        if (c->block_var_count > 0) {
            emit(c, AGL_OP_POP_SCOPE);
            emit(c, (uint8_t)c->block_var_count);
        }
        c->block_var_count = saved_var_count;
        break;
    }

    case AGL_NODE_FN_DECL: {
        /* Sub-compile the function body */
        Compiler sub;
        sub.ctx = c->ctx;
        sub.arena = c->arena;
        sub.gc = c->gc;
        sub.chunk = agl_chunk_new();
        sub.scope_depth = 0;
        sub.block_var_count = 0;
        sub.loop_depth = 0;
        sub.is_top_level = false;
        if (!sub.chunk) return;

        if (node->as.fn_decl.body) {
            compile_stmt(&sub, node->as.fn_decl.body);
        }
        emit(&sub, AGL_OP_RETURN_NIL);

        AglFnVal *fn = agl_gc_alloc(c->gc, sizeof(AglFnVal), fn_cleanup);
        if (!fn) { agl_chunk_free(sub.chunk); return; }
        fn->decl = node;
        fn->chunk = sub.chunk;
        fn->arity = node->as.fn_decl.param_count;
        fn->captured_count = 0;
        fn->captured_names = NULL;
        fn->captured_name_lengths = NULL;
        fn->captured_values = NULL;
        fn->captured_immutable = NULL;

        AglVal fn_val;
        fn_val.kind = VAL_FN;
        fn_val.as.fn = fn;
        int fn_idx = add_const(c, fn_val);
        /* Top-level functions use CONST (no env capture) so that
         * forward references and mutual recursion work: all top-level
         * fns are defined before any bodies execute, and at call time
         * they see the current env which contains all definitions. */
        if (c->is_top_level) {
            emit(c, AGL_OP_CONST);
        } else {
            emit(c, AGL_OP_CLOSURE);
        }
        emit_u16(c, (uint16_t)fn_idx);

        int name_idx = add_string_const(c, node->as.fn_decl.name,
                                        node->as.fn_decl.name_length);
        emit(c, AGL_OP_DEFINE_LET);
        emit_u16(c, (uint16_t)name_idx);
        c->block_var_count++;
        break;
    }

    case AGL_NODE_STRUCT_DECL:
        /* No-op at runtime */
        break;

    case AGL_NODE_IMPORT: {
        int idx = add_string_const(c, node->as.import_stmt.path,
                                   node->as.import_stmt.path_length);
        emit(c, AGL_OP_IMPORT);
        emit_u16(c, (uint16_t)idx);
        break;
    }

    default:
        agl_error_set(c->ctx, AGL_ERR_RUNTIME,
                      agl_loc(NULL, node->line, node->column),
                      "unsupported statement in compiler");
        break;
    }
}

/* ---- Public API ---- */

AglChunk *agl_compile(AglNode *program, AglCtx *ctx, AglArena *arena, AglGc *gc) {
    if (!program || program->kind != AGL_NODE_PROGRAM) return NULL;

    AglChunk *chunk = agl_chunk_new();
    if (!chunk) return NULL;

    Compiler c;
    c.chunk = chunk;
    c.ctx = ctx;
    c.arena = arena;
    c.gc = gc;
    c.scope_depth = 0;
    c.block_var_count = 0;
    c.loop_depth = 0;
    c.is_top_level = true;

    for (int i = 0; i < program->as.program.decl_count; i++) {
        compile_stmt(&c, program->as.program.decls[i]);
        if (agl_error_occurred(ctx)) {
            agl_chunk_free(chunk);
            return NULL;
        }
    }

    /* End with implicit return nil for top-level */
    emit(&c, AGL_OP_RETURN_NIL);

    return chunk;
}
