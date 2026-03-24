#include "runtime.h"
#include "json.h"
#include "http.h"
#include "process.h"
#include "time_funcs.h"

/* ---- Built-in function dispatch ---- */

bool try_builtin_call(AglInterp *interp, const char *name, int name_len,
                      AglNode *call_node, AglVal *out) {
    int argc = call_node->as.call.arg_count;
    int line = call_node->line;
    int col  = call_node->column;

    /* print(args...) */
    if (agl_str_eq(name, name_len, "print", 5)) {
        for (int i = 0; i < argc; i++) {
            AglVal arg = eval_expr(interp, call_node->as.call.args[i]);
            if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
            builtin_print(arg);
        }
        *out = val_nil();
        return true;
    }

    /* len(x) */
    if (agl_str_eq(name, name_len, "len", 3)) {
        if (argc != 1) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, line, col), "len() takes exactly 1 argument");
            *out = val_nil(); return true;
        }
        AglVal arg = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (arg.kind == VAL_ARRAY) { *out = val_int(arg.as.array->count); return true; }
        if (arg.kind == VAL_STRING) {
            int slen; str_content(arg, &slen);
            *out = val_int(slen); return true;
        }
        if (arg.kind == VAL_MAP) { *out = val_int(arg.as.map->count); return true; }
        agl_error_set(interp->ctx, AGL_ERR_TYPE,
                      agl_loc(NULL, line, col), "len() requires an array, string, or map");
        *out = val_nil(); return true;
    }

    /* type(val) -> string */
    if (agl_str_eq(name, name_len, "type", 4)) {
        if (argc != 1) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, line, col), "type() takes exactly 1 argument");
            *out = val_nil(); return true;
        }
        AglVal arg = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        const char *tname;
        switch (arg.kind) {
        case VAL_INT:    tname = "int"; break;
        case VAL_FLOAT:  tname = "float"; break;
        case VAL_BOOL:   tname = "bool"; break;
        case VAL_STRING: tname = "string"; break;
        case VAL_FN:     tname = "fn"; break;
        case VAL_ARRAY:  tname = "array"; break;
        case VAL_STRUCT: tname = "struct"; break;
        case VAL_RESULT: tname = "result"; break;
        case VAL_MAP:    tname = "map"; break;
        case VAL_NIL:    tname = "nil"; break;
        default:         tname = "unknown"; break;
        }
        *out = val_string(tname, (int)strlen(tname));
        return true;
    }

    /* str(val) -> string */
    if (agl_str_eq(name, name_len, "str", 3)) {
        if (argc != 1) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, line, col), "str() takes exactly 1 argument");
            *out = val_nil(); return true;
        }
        AglVal arg = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        char buf[256];
        int n = 0;
        switch (arg.kind) {
        case VAL_INT:    n = snprintf(buf, sizeof(buf), "%lld", (long long)arg.as.integer); break;
        case VAL_FLOAT:  n = snprintf(buf, sizeof(buf), "%g", arg.as.floating); break;
        case VAL_BOOL:   n = snprintf(buf, sizeof(buf), "%s", arg.as.boolean ? "true" : "false"); break;
        case VAL_NIL:    n = snprintf(buf, sizeof(buf), "nil"); break;
        case VAL_FN:     n = snprintf(buf, sizeof(buf), "<fn>"); break;
        case VAL_STRUCT: n = snprintf(buf, sizeof(buf), "<struct %.*s>", arg.as.strct->type_name_length, arg.as.strct->type_name); break;
        case VAL_RESULT: n = snprintf(buf, sizeof(buf), "%s(...)", arg.as.result->is_ok ? "ok" : "err"); break;
        case VAL_ARRAY:  n = snprintf(buf, sizeof(buf), "<array[%d]>", arg.as.array->count); break;
        case VAL_MAP:    n = snprintf(buf, sizeof(buf), "<map[%d]>", arg.as.map->count); break;
        case VAL_STRING: {
            int slen; const char *sd = str_content(arg, &slen);
            char *copy = agl_arena_alloc(interp->arena, (size_t)slen);
            if (copy) memcpy(copy, sd, (size_t)slen);
            *out = val_string(copy ? copy : "", copy ? slen : 0);
            return true;
        }
        }
        if (n < 0) n = 0;
        if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
        char *s = agl_arena_alloc(interp->arena, (size_t)n);
        if (s) memcpy(s, buf, (size_t)n);
        *out = val_string(s ? s : "", s ? n : 0);
        return true;
    }

    /* int(x) -> int */
    if (agl_str_eq(name, name_len, "int", 3)) {
        if (argc != 1) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, line, col), "int() takes exactly 1 argument");
            *out = val_nil(); return true;
        }
        AglVal arg = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (arg.kind == VAL_STRING) {
            int slen; const char *sd = str_content(arg, &slen);
            char tmp[64];
            if (slen >= (int)sizeof(tmp)) slen = (int)sizeof(tmp) - 1;
            memcpy(tmp, sd, (size_t)slen); tmp[slen] = '\0';
            char *end;
            int64_t v = strtoll(tmp, &end, 10);
            if (end == tmp) {
                agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                              agl_loc(NULL, line, col), "int() invalid integer string");
                *out = val_nil(); return true;
            }
            *out = val_int(v); return true;
        }
        if (arg.kind == VAL_FLOAT) { *out = val_int((int64_t)arg.as.floating); return true; }
        if (arg.kind == VAL_INT) { *out = arg; return true; }
        agl_error_set(interp->ctx, AGL_ERR_TYPE,
                      agl_loc(NULL, line, col), "int() cannot convert this type");
        *out = val_nil(); return true;
    }

    /* float(x) -> float */
    if (agl_str_eq(name, name_len, "float", 5)) {
        if (argc != 1) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, line, col), "float() takes exactly 1 argument");
            *out = val_nil(); return true;
        }
        AglVal arg = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (arg.kind == VAL_STRING) {
            int slen; const char *sd = str_content(arg, &slen);
            char tmp[64];
            if (slen >= (int)sizeof(tmp)) slen = (int)sizeof(tmp) - 1;
            memcpy(tmp, sd, (size_t)slen); tmp[slen] = '\0';
            char *end;
            double v = strtod(tmp, &end);
            if (end == tmp) {
                agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                              agl_loc(NULL, line, col), "float() invalid number string");
                *out = val_nil(); return true;
            }
            *out = val_float(v); return true;
        }
        if (arg.kind == VAL_INT) { *out = val_float((double)arg.as.integer); return true; }
        if (arg.kind == VAL_FLOAT) { *out = arg; return true; }
        agl_error_set(interp->ctx, AGL_ERR_TYPE,
                      agl_loc(NULL, line, col), "float() cannot convert this type");
        *out = val_nil(); return true;
    }

    /* push(arr, val) -> new array */
    if (agl_str_eq(name, name_len, "push", 4)) {
        if (argc != 2) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, line, col), "push() takes exactly 2 arguments");
            *out = val_nil(); return true;
        }
        AglVal arr_val = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal elem = eval_expr(interp, call_node->as.call.args[1]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (arr_val.kind != VAL_ARRAY) {
            agl_error_set(interp->ctx, AGL_ERR_TYPE,
                          agl_loc(NULL, line, col), "push() first argument must be an array");
            *out = val_nil(); return true;
        }
        AglArrayVal *old = arr_val.as.array;
        if (old->count >= MAX_ARRAY_SIZE) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, line, col),
                          "array size limit exceeded (max %d)", MAX_ARRAY_SIZE);
            *out = val_nil(); return true;
        }
        int new_count = old->count + 1;
        AglArrayVal *arr = agl_gc_alloc(interp->gc, sizeof(AglArrayVal), array_cleanup);
        if (!arr) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, line, col), "out of memory");
            *out = val_nil(); return true;
        }
        arr->count = new_count;
        arr->elements = malloc(sizeof(AglVal) * (size_t)new_count);
        if (!arr->elements) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, line, col), "out of memory");
            *out = val_nil(); return true;
        }
        memcpy(arr->elements, old->elements, sizeof(AglVal) * (size_t)old->count);
        arr->elements[old->count] = elem;
        AglVal v; v.kind = VAL_ARRAY; v.as.array = arr;
        *out = v; return true;
    }

    /* map(arr, fn) -> new array */
    if (agl_str_eq(name, name_len, "map", 3) && argc == 2) {
        AglVal arr_val = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal fn_val = eval_expr(interp, call_node->as.call.args[1]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (arr_val.kind != VAL_ARRAY || fn_val.kind != VAL_FN) {
            agl_error_set(interp->ctx, AGL_ERR_TYPE,
                          agl_loc(NULL, line, col), "map() requires (array, fn)");
            *out = val_nil(); return true;
        }
        AglArrayVal *src = arr_val.as.array;
        AglArrayVal *dst = agl_gc_alloc(interp->gc, sizeof(AglArrayVal), array_cleanup);
        if (!dst) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, line, col), "out of memory");
            *out = val_nil(); return true;
        }
        dst->count = src->count;
        dst->elements = src->count > 0 ? malloc(sizeof(AglVal) * (size_t)src->count) : NULL;
        for (int i = 0; i < src->count; i++) {
            dst->elements[i] = call_fn_direct(interp, fn_val.as.fn,
                                              &src->elements[i], 1, line, col);
            if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        }
        AglVal v; v.kind = VAL_ARRAY; v.as.array = dst;
        *out = v; return true;
    }

    /* filter(arr, fn) -> new array */
    if (agl_str_eq(name, name_len, "filter", 6) && argc == 2) {
        AglVal arr_val = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal fn_val = eval_expr(interp, call_node->as.call.args[1]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (arr_val.kind != VAL_ARRAY || fn_val.kind != VAL_FN) {
            agl_error_set(interp->ctx, AGL_ERR_TYPE,
                          agl_loc(NULL, line, col), "filter() requires (array, fn)");
            *out = val_nil(); return true;
        }
        AglArrayVal *src = arr_val.as.array;
        AglVal *temp = src->count > 0 ? malloc(sizeof(AglVal) * (size_t)src->count) : NULL;
        int kept = 0;
        for (int i = 0; i < src->count; i++) {
            AglVal pred = call_fn_direct(interp, fn_val.as.fn,
                                         &src->elements[i], 1, line, col);
            if (agl_error_occurred(interp->ctx)) { free(temp); *out = val_nil(); return true; }
            if (is_truthy(pred)) temp[kept++] = src->elements[i];
        }
        AglArrayVal *dst = agl_gc_alloc(interp->gc, sizeof(AglArrayVal), array_cleanup);
        if (!dst) {
            free(temp);
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, line, col), "out of memory");
            *out = val_nil(); return true;
        }
        dst->count = kept;
        dst->elements = kept > 0 ? malloc(sizeof(AglVal) * (size_t)kept) : NULL;
        if (kept > 0) memcpy(dst->elements, temp, sizeof(AglVal) * (size_t)kept);
        free(temp);
        AglVal v; v.kind = VAL_ARRAY; v.as.array = dst;
        *out = v; return true;
    }

    /* abs(n) */
    if (agl_str_eq(name, name_len, "abs", 3)) {
        if (argc != 1) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, line, col), "abs() takes exactly 1 argument");
            *out = val_nil(); return true;
        }
        AglVal arg = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (arg.kind == VAL_INT) {
            *out = val_int(arg.as.integer < 0 ? -arg.as.integer : arg.as.integer);
            return true;
        }
        if (arg.kind == VAL_FLOAT) {
            *out = val_float(arg.as.floating < 0 ? -arg.as.floating : arg.as.floating);
            return true;
        }
        agl_error_set(interp->ctx, AGL_ERR_TYPE,
                      agl_loc(NULL, line, col), "abs() requires a number");
        *out = val_nil(); return true;
    }

    /* min(a, b) */
    if (agl_str_eq(name, name_len, "min", 3)) {
        if (argc != 2) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, line, col), "min() takes exactly 2 arguments");
            *out = val_nil(); return true;
        }
        AglVal a = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal b = eval_expr(interp, call_node->as.call.args[1]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (a.kind == VAL_INT && b.kind == VAL_INT) {
            *out = a.as.integer <= b.as.integer ? a : b; return true;
        }
        if (a.kind == VAL_FLOAT && b.kind == VAL_FLOAT) {
            *out = a.as.floating <= b.as.floating ? a : b; return true;
        }
        agl_error_set(interp->ctx, AGL_ERR_TYPE,
                      agl_loc(NULL, line, col),
                      "min() requires two numbers of the same type");
        *out = val_nil(); return true;
    }

    /* max(a, b) */
    if (agl_str_eq(name, name_len, "max", 3)) {
        if (argc != 2) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, line, col), "max() takes exactly 2 arguments");
            *out = val_nil(); return true;
        }
        AglVal a = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal b = eval_expr(interp, call_node->as.call.args[1]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (a.kind == VAL_INT && b.kind == VAL_INT) {
            *out = a.as.integer >= b.as.integer ? a : b; return true;
        }
        if (a.kind == VAL_FLOAT && b.kind == VAL_FLOAT) {
            *out = a.as.floating >= b.as.floating ? a : b; return true;
        }
        agl_error_set(interp->ctx, AGL_ERR_TYPE,
                      agl_loc(NULL, line, col),
                      "max() requires two numbers of the same type");
        *out = val_nil(); return true;
    }

    /* read_file(path) -> Result<string, string> */
    if (agl_str_eq(name, name_len, "read_file", 9)) {
        if (argc != 1) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, line, col), "read_file() takes exactly 1 argument");
            *out = val_nil(); return true;
        }
        AglVal arg = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (arg.kind != VAL_STRING) {
            agl_error_set(interp->ctx, AGL_ERR_TYPE,
                          agl_loc(NULL, line, col), "read_file() requires a string path");
            *out = val_nil(); return true;
        }
        int slen; const char *sd = str_content(arg, &slen);
        char tmp_path[512];
        if (slen >= (int)sizeof(tmp_path)) slen = (int)sizeof(tmp_path) - 1;
        memcpy(tmp_path, sd, (size_t)slen); tmp_path[slen] = '\0';

        FILE *f = fopen(tmp_path, "rb");
        if (!f) {
            /* Return err("cannot read file") */
            const char *msg = "cannot read file";
            AglResultVal *rv = agl_gc_alloc(interp->gc, sizeof(AglResultVal), NULL);
            if (!rv) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "out of memory"); *out = val_nil(); return true; }
            rv->is_ok = false;
            rv->value = val_string(msg, (int)strlen(msg));
            AglVal v; v.kind = VAL_RESULT; v.as.result = rv;
            *out = v; return true;
        }
        fseek(f, 0, SEEK_END);
        long flen = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (flen < 0 || flen > 10 * 1024 * 1024) {
            fclose(f);
            const char *msg = "file too large";
            AglResultVal *rv = agl_gc_alloc(interp->gc, sizeof(AglResultVal), NULL);
            if (!rv) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "out of memory"); *out = val_nil(); return true; }
            rv->is_ok = false;
            rv->value = val_string(msg, (int)strlen(msg));
            AglVal v; v.kind = VAL_RESULT; v.as.result = rv;
            *out = v; return true;
        }
        char *buf = agl_arena_alloc(interp->arena, (size_t)flen);
        if (!buf) { fclose(f); agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "out of memory"); *out = val_nil(); return true; }
        size_t nread = fread(buf, 1, (size_t)flen, f);
        fclose(f);
        AglResultVal *rv = agl_gc_alloc(interp->gc, sizeof(AglResultVal), NULL);
        if (!rv) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "out of memory"); *out = val_nil(); return true; }
        rv->is_ok = true;
        rv->value = val_string(buf, (int)nread);
        AglVal v; v.kind = VAL_RESULT; v.as.result = rv;
        *out = v; return true;
    }

    /* write_file(path, content) -> Result<bool, string> */
    if (agl_str_eq(name, name_len, "write_file", 10)) {
        if (argc != 2) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, line, col), "write_file() takes exactly 2 arguments");
            *out = val_nil(); return true;
        }
        AglVal path_val = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal content_val = eval_expr(interp, call_node->as.call.args[1]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (path_val.kind != VAL_STRING || content_val.kind != VAL_STRING) {
            agl_error_set(interp->ctx, AGL_ERR_TYPE,
                          agl_loc(NULL, line, col), "write_file() requires (string, string)");
            *out = val_nil(); return true;
        }
        int plen; const char *pd = str_content(path_val, &plen);
        int clen; const char *cd = str_content(content_val, &clen);
        char tmp_path[512];
        if (plen >= (int)sizeof(tmp_path)) plen = (int)sizeof(tmp_path) - 1;
        memcpy(tmp_path, pd, (size_t)plen); tmp_path[plen] = '\0';

        FILE *f = fopen(tmp_path, "wb");
        AglResultVal *rv = agl_gc_alloc(interp->gc, sizeof(AglResultVal), NULL);
        if (!rv) { if (f) fclose(f); agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "out of memory"); *out = val_nil(); return true; }
        if (!f) {
            rv->is_ok = false;
            const char *msg = "cannot write file";
            rv->value = val_string(msg, (int)strlen(msg));
        } else {
            fwrite(cd, 1, (size_t)clen, f);
            fclose(f);
            rv->is_ok = true;
            rv->value = val_bool(true);
        }
        AglVal v; v.kind = VAL_RESULT; v.as.result = rv;
        *out = v; return true;
    }

    /* file_exists(path) -> bool */
    if (agl_str_eq(name, name_len, "file_exists", 11)) {
        if (argc != 1) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, line, col), "file_exists() takes exactly 1 argument");
            *out = val_nil(); return true;
        }
        AglVal arg = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (arg.kind != VAL_STRING) {
            agl_error_set(interp->ctx, AGL_ERR_TYPE,
                          agl_loc(NULL, line, col), "file_exists() requires a string path");
            *out = val_nil(); return true;
        }
        int slen; const char *sd = str_content(arg, &slen);
        char tmp_path[512];
        if (slen >= (int)sizeof(tmp_path)) slen = (int)sizeof(tmp_path) - 1;
        memcpy(tmp_path, sd, (size_t)slen); tmp_path[slen] = '\0';
        FILE *f = fopen(tmp_path, "r");
        if (f) { fclose(f); *out = val_bool(true); }
        else { *out = val_bool(false); }
        return true;
    }

    /* len() map support - add to existing len() above */

    /* map_get(m, key) */
    if (agl_str_eq(name, name_len, "map_get", 7)) {
        if (argc != 2) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "map_get() takes exactly 2 arguments"); *out = val_nil(); return true; }
        AglVal m = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal key = eval_expr(interp, call_node->as.call.args[1]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (m.kind != VAL_MAP) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "map_get() first argument must be a map"); *out = val_nil(); return true; }
        if (key.kind != VAL_STRING) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "map_get() key must be a string"); *out = val_nil(); return true; }
        int klen; const char *kdata = str_content(key, &klen);
        AglMapVal *mp = m.as.map;
        for (int i = 0; i < mp->count; i++) {
            if (mp->key_lengths[i] == klen && memcmp(mp->keys[i], kdata, (size_t)klen) == 0) { *out = mp->values[i]; return true; }
        }
        *out = val_nil(); return true;
    }

    /* map_set(m, key, val) -> new map */
    if (agl_str_eq(name, name_len, "map_set", 7)) {
        if (argc != 3) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "map_set() takes exactly 3 arguments"); *out = val_nil(); return true; }
        AglVal m = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal key = eval_expr(interp, call_node->as.call.args[1]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal val = eval_expr(interp, call_node->as.call.args[2]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (m.kind != VAL_MAP) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "map_set() first argument must be a map"); *out = val_nil(); return true; }
        if (key.kind != VAL_STRING) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "map_set() key must be a string"); *out = val_nil(); return true; }
        AglMapVal *old = m.as.map;
        int klen; const char *kdata = str_content(key, &klen);
        int existing = -1;
        for (int i = 0; i < old->count; i++) {
            if (old->key_lengths[i] == klen && memcmp(old->keys[i], kdata, (size_t)klen) == 0) { existing = i; break; }
        }
        int new_count = existing >= 0 ? old->count : old->count + 1;
        AglMapVal *nm = agl_gc_alloc(interp->gc, sizeof(AglMapVal), map_cleanup);
        if (!nm) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "out of memory"); *out = val_nil(); return true; }
        nm->count = new_count; nm->capacity = new_count;
        nm->keys = malloc(sizeof(char *) * (size_t)new_count);
        nm->key_lengths = malloc(sizeof(int) * (size_t)new_count);
        nm->values = malloc(sizeof(AglVal) * (size_t)new_count);
        for (int i = 0; i < old->count; i++) { nm->keys[i] = old->keys[i]; nm->key_lengths[i] = old->key_lengths[i]; nm->values[i] = old->values[i]; }
        if (existing >= 0) { nm->values[existing] = val; }
        else { nm->keys[old->count] = kdata; nm->key_lengths[old->count] = klen; nm->values[old->count] = val; }
        AglVal v; v.kind = VAL_MAP; v.as.map = nm;
        *out = v; return true;
    }

    /* map_keys(m) -> array */
    if (agl_str_eq(name, name_len, "map_keys", 8)) {
        if (argc != 1) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "map_keys() takes exactly 1 argument"); *out = val_nil(); return true; }
        AglVal m = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (m.kind != VAL_MAP) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "map_keys() requires a map"); *out = val_nil(); return true; }
        AglMapVal *mp = m.as.map;
        AglArrayVal *arr = agl_gc_alloc(interp->gc, sizeof(AglArrayVal), array_cleanup);
        if (!arr) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "out of memory"); *out = val_nil(); return true; }
        arr->count = mp->count;
        arr->elements = mp->count > 0 ? malloc(sizeof(AglVal) * (size_t)mp->count) : NULL;
        for (int i = 0; i < mp->count; i++) arr->elements[i] = val_string(mp->keys[i], mp->key_lengths[i]);
        AglVal v; v.kind = VAL_ARRAY; v.as.array = arr;
        *out = v; return true;
    }

    /* map_has(m, key) -> bool */
    if (agl_str_eq(name, name_len, "map_has", 7)) {
        if (argc != 2) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "map_has() takes exactly 2 arguments"); *out = val_nil(); return true; }
        AglVal m = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal key = eval_expr(interp, call_node->as.call.args[1]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (m.kind != VAL_MAP) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "map_has() first argument must be a map"); *out = val_nil(); return true; }
        if (key.kind != VAL_STRING) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "map_has() key must be a string"); *out = val_nil(); return true; }
        int klen; const char *kdata = str_content(key, &klen);
        AglMapVal *mp = m.as.map;
        for (int i = 0; i < mp->count; i++) {
            if (mp->key_lengths[i] == klen && memcmp(mp->keys[i], kdata, (size_t)klen) == 0) { *out = val_bool(true); return true; }
        }
        *out = val_bool(false); return true;
    }

    /* map_del(m, key) -> new map */
    if (agl_str_eq(name, name_len, "map_del", 7)) {
        if (argc != 2) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "map_del() takes exactly 2 arguments"); *out = val_nil(); return true; }
        AglVal m = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal key = eval_expr(interp, call_node->as.call.args[1]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (m.kind != VAL_MAP) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "map_del() first argument must be a map"); *out = val_nil(); return true; }
        if (key.kind != VAL_STRING) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "map_del() key must be a string"); *out = val_nil(); return true; }
        AglMapVal *old = m.as.map;
        int klen; const char *kdata = str_content(key, &klen);
        AglMapVal *nm = agl_gc_alloc(interp->gc, sizeof(AglMapVal), map_cleanup);
        if (!nm) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "out of memory"); *out = val_nil(); return true; }
        int new_count = 0;
        nm->keys = old->count > 0 ? malloc(sizeof(char *) * (size_t)old->count) : NULL;
        nm->key_lengths = old->count > 0 ? malloc(sizeof(int) * (size_t)old->count) : NULL;
        nm->values = old->count > 0 ? malloc(sizeof(AglVal) * (size_t)old->count) : NULL;
        for (int i = 0; i < old->count; i++) {
            if (old->key_lengths[i] == klen && memcmp(old->keys[i], kdata, (size_t)klen) == 0) continue;
            nm->keys[new_count] = old->keys[i]; nm->key_lengths[new_count] = old->key_lengths[i]; nm->values[new_count] = old->values[i]; new_count++;
        }
        nm->count = new_count; nm->capacity = old->count;
        AglVal v; v.kind = VAL_MAP; v.as.map = nm;
        *out = v; return true;
    }

    /* split(str, sep) -> array */
    if (agl_str_eq(name, name_len, "split", 5)) {
        if (argc != 2) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "split() takes exactly 2 arguments"); *out = val_nil(); return true; }
        AglVal sval = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal sepval = eval_expr(interp, call_node->as.call.args[1]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (sval.kind != VAL_STRING || sepval.kind != VAL_STRING) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "split() requires (string, string)"); *out = val_nil(); return true; }
        int slen, seplen;
        const char *sdata = str_content(sval, &slen);
        const char *sepdata = str_content(sepval, &seplen);
        if (seplen == 0) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col),
                          "split() separator cannot be empty");
            *out = val_nil(); return true;
        }
        AglVal parts[256]; int pcount = 0;
        int start = 0;
        for (int i = 0; i <= slen - seplen && pcount < 255; i++) {
            if (memcmp(sdata + i, sepdata, (size_t)seplen) == 0) {
                int plen = i - start;
                char *p = agl_arena_alloc(interp->arena, (size_t)(plen > 0 ? plen : 1));
                if (p && plen > 0) memcpy(p, sdata + start, (size_t)plen);
                parts[pcount++] = val_string(p ? p : "", plen);
                start = i + seplen; i = start - 1;
            }
        }
        if (pcount < 256) {
            int plen = slen - start;
            char *p = agl_arena_alloc(interp->arena, (size_t)(plen > 0 ? plen : 1));
            if (p && plen > 0) memcpy(p, sdata + start, (size_t)plen);
            parts[pcount++] = val_string(p ? p : "", plen);
        }
        AglArrayVal *arr = agl_gc_alloc(interp->gc, sizeof(AglArrayVal), array_cleanup);
        if (!arr) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "out of memory"); *out = val_nil(); return true; }
        arr->count = pcount; arr->elements = malloc(sizeof(AglVal) * (size_t)pcount);
        memcpy(arr->elements, parts, sizeof(AglVal) * (size_t)pcount);
        AglVal v; v.kind = VAL_ARRAY; v.as.array = arr;
        *out = v; return true;
    }

    /* trim(str) -> string */
    if (agl_str_eq(name, name_len, "trim", 4)) {
        if (argc != 1) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "trim() takes exactly 1 argument"); *out = val_nil(); return true; }
        AglVal sval = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (sval.kind != VAL_STRING) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "trim() requires a string"); *out = val_nil(); return true; }
        int slen; const char *sdata = str_content(sval, &slen);
        int start = 0, end = slen;
        while (start < end && (sdata[start] == ' ' || sdata[start] == '\t' || sdata[start] == '\n' || sdata[start] == '\r')) start++;
        while (end > start && (sdata[end-1] == ' ' || sdata[end-1] == '\t' || sdata[end-1] == '\n' || sdata[end-1] == '\r')) end--;
        int rlen = end - start;
        char *buf = agl_arena_alloc(interp->arena, (size_t)(rlen > 0 ? rlen : 1));
        if (buf && rlen > 0) memcpy(buf, sdata + start, (size_t)rlen);
        *out = val_string(buf ? buf : "", rlen); return true;
    }

    /* contains(str, substr) -> bool */
    if (agl_str_eq(name, name_len, "contains", 8)) {
        if (argc != 2) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "contains() takes exactly 2 arguments"); *out = val_nil(); return true; }
        AglVal sval = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal subval = eval_expr(interp, call_node->as.call.args[1]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (sval.kind != VAL_STRING || subval.kind != VAL_STRING) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "contains() requires (string, string)"); *out = val_nil(); return true; }
        int slen, sublen;
        const char *sdata = str_content(sval, &slen);
        const char *subdata = str_content(subval, &sublen);
        if (sublen > slen) { *out = val_bool(false); return true; }
        if (sublen == 0) { *out = val_bool(true); return true; }
        for (int i = 0; i <= slen - sublen; i++) {
            if (memcmp(sdata + i, subdata, (size_t)sublen) == 0) { *out = val_bool(true); return true; }
        }
        *out = val_bool(false); return true;
    }

    /* replace(str, old, new) -> string */
    if (agl_str_eq(name, name_len, "replace", 7)) {
        if (argc != 3) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "replace() takes exactly 3 arguments"); *out = val_nil(); return true; }
        AglVal sval = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal oldval = eval_expr(interp, call_node->as.call.args[1]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal newval = eval_expr(interp, call_node->as.call.args[2]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (sval.kind != VAL_STRING || oldval.kind != VAL_STRING || newval.kind != VAL_STRING) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "replace() requires (string, string, string)"); *out = val_nil(); return true; }
        int slen, oldlen, newlen;
        const char *sdata = str_content(sval, &slen);
        const char *olddata = str_content(oldval, &oldlen);
        const char *newdata = str_content(newval, &newlen);
        if (oldlen == 0) { char *buf = agl_arena_alloc(interp->arena, (size_t)slen); if (buf) memcpy(buf, sdata, (size_t)slen); *out = val_string(buf ? buf : sdata, slen); return true; }
        int occ = 0;
        for (int i = 0; i <= slen - oldlen; i++) { if (memcmp(sdata + i, olddata, (size_t)oldlen) == 0) { occ++; i += oldlen - 1; } }
        int rlen = slen + occ * (newlen - oldlen);
        char *buf = agl_arena_alloc(interp->arena, (size_t)(rlen > 0 ? rlen : 1));
        if (!buf) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "out of memory"); *out = val_nil(); return true; }
        int wi = 0;
        for (int i = 0; i < slen; ) {
            if (i <= slen - oldlen && memcmp(sdata + i, olddata, (size_t)oldlen) == 0) { memcpy(buf + wi, newdata, (size_t)newlen); wi += newlen; i += oldlen; }
            else { buf[wi++] = sdata[i++]; }
        }
        *out = val_string(buf, rlen); return true;
    }

    /* starts_with(str, prefix) -> bool */
    if (agl_str_eq(name, name_len, "starts_with", 11)) {
        if (argc != 2) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "starts_with() takes exactly 2 arguments"); *out = val_nil(); return true; }
        AglVal sval = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal pval = eval_expr(interp, call_node->as.call.args[1]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (sval.kind != VAL_STRING || pval.kind != VAL_STRING) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "starts_with() requires (string, string)"); *out = val_nil(); return true; }
        int slen, plen;
        const char *sdata = str_content(sval, &slen);
        const char *pdata = str_content(pval, &plen);
        *out = val_bool(slen >= plen && memcmp(sdata, pdata, (size_t)plen) == 0); return true;
    }

    /* ends_with(str, suffix) -> bool */
    if (agl_str_eq(name, name_len, "ends_with", 9)) {
        if (argc != 2) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "ends_with() takes exactly 2 arguments"); *out = val_nil(); return true; }
        AglVal sval = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal pval = eval_expr(interp, call_node->as.call.args[1]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (sval.kind != VAL_STRING || pval.kind != VAL_STRING) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "ends_with() requires (string, string)"); *out = val_nil(); return true; }
        int slen, plen;
        const char *sdata = str_content(sval, &slen);
        const char *pdata = str_content(pval, &plen);
        *out = val_bool(slen >= plen && memcmp(sdata + slen - plen, pdata, (size_t)plen) == 0); return true;
    }

    /* to_upper(str) -> string */
    if (agl_str_eq(name, name_len, "to_upper", 8)) {
        if (argc != 1) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "to_upper() takes exactly 1 argument"); *out = val_nil(); return true; }
        AglVal sval = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (sval.kind != VAL_STRING) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "to_upper() requires a string"); *out = val_nil(); return true; }
        int slen; const char *sdata = str_content(sval, &slen);
        char *buf = agl_arena_alloc(interp->arena, (size_t)(slen > 0 ? slen : 1));
        if (!buf) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "out of memory"); *out = val_nil(); return true; }
        for (int i = 0; i < slen; i++) buf[i] = (char)(sdata[i] >= 'a' && sdata[i] <= 'z' ? sdata[i] - 32 : sdata[i]);
        *out = val_string(buf, slen); return true;
    }

    /* to_lower(str) -> string */
    if (agl_str_eq(name, name_len, "to_lower", 8)) {
        if (argc != 1) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "to_lower() takes exactly 1 argument"); *out = val_nil(); return true; }
        AglVal sval = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (sval.kind != VAL_STRING) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "to_lower() requires a string"); *out = val_nil(); return true; }
        int slen; const char *sdata = str_content(sval, &slen);
        char *buf = agl_arena_alloc(interp->arena, (size_t)(slen > 0 ? slen : 1));
        if (!buf) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "out of memory"); *out = val_nil(); return true; }
        for (int i = 0; i < slen; i++) buf[i] = (char)(sdata[i] >= 'A' && sdata[i] <= 'Z' ? sdata[i] + 32 : sdata[i]);
        *out = val_string(buf, slen); return true;
    }

    /* join(arr, sep) -> string */
    if (agl_str_eq(name, name_len, "join", 4)) {
        if (argc != 2) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "join() takes exactly 2 arguments"); *out = val_nil(); return true; }
        AglVal aval = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal sepval = eval_expr(interp, call_node->as.call.args[1]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (aval.kind != VAL_ARRAY || sepval.kind != VAL_STRING) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "join() requires (array, string)"); *out = val_nil(); return true; }
        AglArrayVal *arr = aval.as.array;
        int seplen; const char *sepdata = str_content(sepval, &seplen);
        int total = 0;
        for (int i = 0; i < arr->count; i++) {
            if (arr->elements[i].kind != VAL_STRING) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "join() array elements must be strings"); *out = val_nil(); return true; }
            int elen; str_content(arr->elements[i], &elen); total += elen;
            if (i > 0) total += seplen;
        }
        char *buf = agl_arena_alloc(interp->arena, (size_t)(total > 0 ? total : 1));
        int wi = 0;
        for (int i = 0; i < arr->count; i++) {
            if (i > 0 && seplen > 0) { memcpy(buf + wi, sepdata, (size_t)seplen); wi += seplen; }
            int elen; const char *edata = str_content(arr->elements[i], &elen);
            if (elen > 0) { memcpy(buf + wi, edata, (size_t)elen); wi += elen; }
        }
        *out = val_string(buf ? buf : "", total); return true;
    }

    /* substr(str, start, len) -> string */
    if (agl_str_eq(name, name_len, "substr", 6)) {
        if (argc != 3) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "substr() takes exactly 3 arguments"); *out = val_nil(); return true; }
        AglVal sval = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal startval = eval_expr(interp, call_node->as.call.args[1]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal lenval = eval_expr(interp, call_node->as.call.args[2]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (sval.kind != VAL_STRING || startval.kind != VAL_INT || lenval.kind != VAL_INT) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "substr() requires (string, int, int)"); *out = val_nil(); return true; }
        int slen; const char *sdata = str_content(sval, &slen);
        int start = (int)startval.as.integer;
        int rlen = (int)lenval.as.integer;
        if (start < 0) start = 0;
        if (start > slen) start = slen;
        if (rlen < 0) rlen = 0;
        if (start + rlen > slen) rlen = slen - start;
        char *buf = agl_arena_alloc(interp->arena, (size_t)(rlen > 0 ? rlen : 1));
        if (buf && rlen > 0) memcpy(buf, sdata + start, (size_t)rlen);
        *out = val_string(buf ? buf : "", rlen); return true;
    }

    /* json_parse(str) -> Result<value, string> */
    if (agl_str_eq(name, name_len, "json_parse", 10)) {
        if (argc != 1) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "json_parse() takes exactly 1 argument"); *out = val_nil(); return true; }
        AglVal arg = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (arg.kind != VAL_STRING) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "json_parse() requires a string"); *out = val_nil(); return true; }
        int slen; const char *sd = str_content(arg, &slen);
        *out = agl_json_parse(sd, slen, interp->arena, interp->gc);
        return true;
    }

    /* json_stringify(val) -> string */
    if (agl_str_eq(name, name_len, "json_stringify", 14)) {
        if (argc != 1) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "json_stringify() takes exactly 1 argument"); *out = val_nil(); return true; }
        AglVal arg = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        int out_len;
        const char *s = agl_json_stringify(arg, &out_len, interp->arena);
        *out = val_string(s, out_len);
        return true;
    }

    /* env(name) -> Result<string, string> */
    if (agl_str_eq(name, name_len, "env", 3)) {
        if (argc != 1) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "env() takes exactly 1 argument"); *out = val_nil(); return true; }
        AglVal arg = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (arg.kind != VAL_STRING) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "env() requires a string"); *out = val_nil(); return true; }
        int slen; const char *sd = str_content(arg, &slen);
        char tmp[256]; if (slen >= (int)sizeof(tmp)) slen = (int)sizeof(tmp) - 1;
        memcpy(tmp, sd, (size_t)slen); tmp[slen] = '\0';
        const char *val = getenv(tmp);
        AglResultVal *rv = agl_gc_alloc(interp->gc, sizeof(AglResultVal), NULL);
        if (!rv) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "out of memory"); *out = val_nil(); return true; }
        if (val) {
            int vlen = (int)strlen(val);
            char *copy = agl_arena_alloc(interp->arena, (size_t)vlen);
            if (copy) memcpy(copy, val, (size_t)vlen);
            rv->is_ok = true;
            rv->value = val_string(copy ? copy : "", copy ? vlen : 0);
        } else {
            rv->is_ok = false;
            rv->value = val_string("not set", 7);
        }
        AglVal v; v.kind = VAL_RESULT; v.as.result = rv;
        *out = v; return true;
    }

    /* env_default(name, fallback) -> string */
    if (agl_str_eq(name, name_len, "env_default", 11)) {
        if (argc != 2) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "env_default() takes exactly 2 arguments"); *out = val_nil(); return true; }
        AglVal name_arg = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal fallback_arg = eval_expr(interp, call_node->as.call.args[1]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (name_arg.kind != VAL_STRING || fallback_arg.kind != VAL_STRING) {
            agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "env_default() requires (string, string)");
            *out = val_nil(); return true;
        }
        int slen; const char *sd = str_content(name_arg, &slen);
        char tmp[256]; if (slen >= (int)sizeof(tmp)) slen = (int)sizeof(tmp) - 1;
        memcpy(tmp, sd, (size_t)slen); tmp[slen] = '\0';
        const char *val = getenv(tmp);
        if (val) {
            int vlen = (int)strlen(val);
            char *copy = agl_arena_alloc(interp->arena, (size_t)vlen);
            if (copy) memcpy(copy, val, (size_t)vlen);
            *out = val_string(copy ? copy : "", copy ? vlen : 0);
        } else {
            int flen; const char *fdata = str_content(fallback_arg, &flen);
            char *copy = agl_arena_alloc(interp->arena, (size_t)flen);
            if (copy) memcpy(copy, fdata, (size_t)flen);
            *out = val_string(copy ? copy : "", copy ? flen : 0);
        }
        return true;
    }

    /* http_get(url, headers) -> Result<map, string> */
    if (agl_str_eq(name, name_len, "http_get", 8)) {
        if (argc != 2) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "http_get() takes exactly 2 arguments"); *out = val_nil(); return true; }
        AglVal url_val = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal hdr_val = eval_expr(interp, call_node->as.call.args[1]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (url_val.kind != VAL_STRING) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "http_get() first argument must be a string URL"); *out = val_nil(); return true; }
        if (hdr_val.kind != VAL_MAP) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "http_get() second argument must be a map of headers"); *out = val_nil(); return true; }
        int slen; const char *sd = str_content(url_val, &slen);
        *out = agl_http_request("GET", sd, slen, hdr_val.as.map, NULL, 0, interp->arena, interp->gc);
        return true;
    }

    /* http_post(url, headers, body) -> Result<map, string> */
    if (agl_str_eq(name, name_len, "http_post", 9)) {
        if (argc != 3) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "http_post() takes exactly 3 arguments"); *out = val_nil(); return true; }
        AglVal url_val = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal hdr_val = eval_expr(interp, call_node->as.call.args[1]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal body_val = eval_expr(interp, call_node->as.call.args[2]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (url_val.kind != VAL_STRING) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "http_post() first argument must be a string URL"); *out = val_nil(); return true; }
        if (hdr_val.kind != VAL_MAP) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "http_post() second argument must be a map of headers"); *out = val_nil(); return true; }
        if (body_val.kind != VAL_STRING) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "http_post() third argument must be a string body"); *out = val_nil(); return true; }
        int ulen; const char *ud = str_content(url_val, &ulen);
        int blen; const char *bd = str_content(body_val, &blen);
        *out = agl_http_request("POST", ud, ulen, hdr_val.as.map, bd, blen, interp->arena, interp->gc);
        return true;
    }

    /* exec(cmd, args) -> Result<map, string> */
    if (agl_str_eq(name, name_len, "exec", 4)) {
        if (argc != 2) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "exec() takes exactly 2 arguments"); *out = val_nil(); return true; }
        AglVal cmd_val = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AglVal args_val = eval_expr(interp, call_node->as.call.args[1]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (cmd_val.kind != VAL_STRING) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "exec() first argument must be a string command"); *out = val_nil(); return true; }
        if (args_val.kind != VAL_ARRAY) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "exec() second argument must be an array of arguments"); *out = val_nil(); return true; }
        int clen; const char *cd = str_content(cmd_val, &clen);
        *out = agl_exec(cd, clen, args_val.as.array, interp->arena, interp->gc);
        return true;
    }

    /* now() -> int (milliseconds since Unix epoch) */
    if (agl_str_eq(name, name_len, "now", 3)) {
        if (argc != 0) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "now() takes no arguments"); *out = val_nil(); return true; }
        *out = val_int(agl_now_ms());
        return true;
    }

    /* sleep(ms) -> nil */
    if (agl_str_eq(name, name_len, "sleep", 5)) {
        if (argc != 1) { agl_error_set(interp->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "sleep() takes exactly 1 argument"); *out = val_nil(); return true; }
        AglVal ms_val = eval_expr(interp, call_node->as.call.args[0]);
        if (agl_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (ms_val.kind != VAL_INT) { agl_error_set(interp->ctx, AGL_ERR_TYPE, agl_loc(NULL, line, col), "sleep() requires an integer (milliseconds)"); *out = val_nil(); return true; }
        agl_sleep_ms(ms_val.as.integer);
        *out = val_nil();
        return true;
    }

    return false;  /* not a builtin */
}
