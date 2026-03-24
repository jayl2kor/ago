#include "builtins_core.h"
#include "json.h"
#include "http.h"
#include "process.h"
#include "time_funcs.h"

/* ---- Name resolution ---- */

AglBuiltinId agl_builtin_resolve(const char *name, int len) {
    if (agl_str_eq(name, len, "print", 5)) return AGL_BUILTIN_PRINT;
    if (agl_str_eq(name, len, "len", 3)) return AGL_BUILTIN_LEN;
    if (agl_str_eq(name, len, "type", 4)) return AGL_BUILTIN_TYPE;
    if (agl_str_eq(name, len, "str", 3)) return AGL_BUILTIN_STR;
    if (agl_str_eq(name, len, "int", 3)) return AGL_BUILTIN_INT;
    if (agl_str_eq(name, len, "float", 5)) return AGL_BUILTIN_FLOAT;
    if (agl_str_eq(name, len, "push", 4)) return AGL_BUILTIN_PUSH;
    if (agl_str_eq(name, len, "map", 3)) return AGL_BUILTIN_MAP;
    if (agl_str_eq(name, len, "filter", 6)) return AGL_BUILTIN_FILTER;
    if (agl_str_eq(name, len, "abs", 3)) return AGL_BUILTIN_ABS;
    if (agl_str_eq(name, len, "min", 3)) return AGL_BUILTIN_MIN;
    if (agl_str_eq(name, len, "max", 3)) return AGL_BUILTIN_MAX;
    if (agl_str_eq(name, len, "read_file", 9)) return AGL_BUILTIN_READ_FILE;
    if (agl_str_eq(name, len, "write_file", 10)) return AGL_BUILTIN_WRITE_FILE;
    if (agl_str_eq(name, len, "file_exists", 11)) return AGL_BUILTIN_FILE_EXISTS;
    if (agl_str_eq(name, len, "map_get", 7)) return AGL_BUILTIN_MAP_GET;
    if (agl_str_eq(name, len, "map_set", 7)) return AGL_BUILTIN_MAP_SET;
    if (agl_str_eq(name, len, "map_keys", 8)) return AGL_BUILTIN_MAP_KEYS;
    if (agl_str_eq(name, len, "map_has", 7)) return AGL_BUILTIN_MAP_HAS;
    if (agl_str_eq(name, len, "map_del", 7)) return AGL_BUILTIN_MAP_DEL;
    if (agl_str_eq(name, len, "split", 5)) return AGL_BUILTIN_SPLIT;
    if (agl_str_eq(name, len, "trim", 4)) return AGL_BUILTIN_TRIM;
    if (agl_str_eq(name, len, "contains", 8)) return AGL_BUILTIN_CONTAINS;
    if (agl_str_eq(name, len, "replace", 7)) return AGL_BUILTIN_REPLACE;
    if (agl_str_eq(name, len, "starts_with", 11)) return AGL_BUILTIN_STARTS_WITH;
    if (agl_str_eq(name, len, "ends_with", 9)) return AGL_BUILTIN_ENDS_WITH;
    if (agl_str_eq(name, len, "to_upper", 8)) return AGL_BUILTIN_TO_UPPER;
    if (agl_str_eq(name, len, "to_lower", 8)) return AGL_BUILTIN_TO_LOWER;
    if (agl_str_eq(name, len, "join", 4)) return AGL_BUILTIN_JOIN;
    if (agl_str_eq(name, len, "substr", 6)) return AGL_BUILTIN_SUBSTR;
    if (agl_str_eq(name, len, "json_parse", 10)) return AGL_BUILTIN_JSON_PARSE;
    if (agl_str_eq(name, len, "json_stringify", 14)) return AGL_BUILTIN_JSON_STRINGIFY;
    if (agl_str_eq(name, len, "env", 3)) return AGL_BUILTIN_ENV;
    if (agl_str_eq(name, len, "env_default", 11)) return AGL_BUILTIN_ENV_DEFAULT;
    if (agl_str_eq(name, len, "http_get", 8)) return AGL_BUILTIN_HTTP_GET;
    if (agl_str_eq(name, len, "http_post", 9)) return AGL_BUILTIN_HTTP_POST;
    if (agl_str_eq(name, len, "exec", 4)) return AGL_BUILTIN_EXEC;
    if (agl_str_eq(name, len, "now", 3)) return AGL_BUILTIN_NOW;
    if (agl_str_eq(name, len, "sleep", 5)) return AGL_BUILTIN_SLEEP;
    return AGL_BUILTIN_NONE;
}

/* ---- Helper macros for concise error reporting ---- */

#define BLOC agl_loc(NULL, bctx->line, bctx->col)
#define BCTX bctx->ctx
#define BARENA bctx->arena
#define BGC bctx->gc

/* ---- Individual builtin implementations ---- */

static AglVal builtin_len(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 1) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "len() takes exactly 1 argument");
        return val_nil();
    }
    if (args[0].kind == VAL_ARRAY) return val_int(args[0].as.array->count);
    if (args[0].kind == VAL_STRING) {
        int slen; str_content(args[0], &slen);
        return val_int(slen);
    }
    if (args[0].kind == VAL_MAP) return val_int(args[0].as.map->count);
    agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                  "len() requires an array, string, or map");
    return val_nil();
}

static AglVal builtin_type(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 1) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "type() takes exactly 1 argument");
        return val_nil();
    }
    const char *tname;
    switch (args[0].kind) {
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
    return val_string(tname, (int)strlen(tname));
}

static AglVal builtin_str(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 1) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "str() takes exactly 1 argument");
        return val_nil();
    }
    AglVal arg = args[0];
    char buf[256];
    int n = 0;
    switch (arg.kind) {
    case VAL_INT:    n = snprintf(buf, sizeof(buf), "%lld", (long long)arg.as.integer); break;
    case VAL_FLOAT:  n = snprintf(buf, sizeof(buf), "%g", arg.as.floating); break;
    case VAL_BOOL:   n = snprintf(buf, sizeof(buf), "%s", arg.as.boolean ? "true" : "false"); break;
    case VAL_NIL:    n = snprintf(buf, sizeof(buf), "nil"); break;
    case VAL_FN:     n = snprintf(buf, sizeof(buf), "<fn>"); break;
    case VAL_STRUCT: n = snprintf(buf, sizeof(buf), "<struct %.*s>",
                         arg.as.strct->type_name_length, arg.as.strct->type_name); break;
    case VAL_RESULT: n = snprintf(buf, sizeof(buf), "%s(...)",
                         arg.as.result->is_ok ? "ok" : "err"); break;
    case VAL_ARRAY:  n = snprintf(buf, sizeof(buf), "<array[%d]>", arg.as.array->count); break;
    case VAL_MAP:    n = snprintf(buf, sizeof(buf), "<map[%d]>", arg.as.map->count); break;
    case VAL_STRING: {
        int slen; const char *sd = str_content(arg, &slen);
        char *copy = agl_arena_alloc(BARENA, (size_t)slen);
        if (copy) memcpy(copy, sd, (size_t)slen);
        return val_string(copy ? copy : "", copy ? slen : 0);
    }
    }
    if (n < 0) n = 0;
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
    char *s = agl_arena_alloc(BARENA, (size_t)n);
    if (s) memcpy(s, buf, (size_t)n);
    return val_string(s ? s : "", s ? n : 0);
}

static AglVal builtin_int_conv(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 1) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "int() takes exactly 1 argument");
        return val_nil();
    }
    if (args[0].kind == VAL_STRING) {
        int slen; const char *sd = str_content(args[0], &slen);
        char tmp[64];
        if (slen >= (int)sizeof(tmp)) slen = (int)sizeof(tmp) - 1;
        memcpy(tmp, sd, (size_t)slen); tmp[slen] = '\0';
        char *end;
        int64_t v = strtoll(tmp, &end, 10);
        if (end == tmp) {
            agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                          "int() invalid integer string");
            return val_nil();
        }
        return val_int(v);
    }
    if (args[0].kind == VAL_FLOAT) return val_int((int64_t)args[0].as.floating);
    if (args[0].kind == VAL_INT) return args[0];
    agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                  "int() cannot convert this type");
    return val_nil();
}

static AglVal builtin_float_conv(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 1) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "float() takes exactly 1 argument");
        return val_nil();
    }
    if (args[0].kind == VAL_STRING) {
        int slen; const char *sd = str_content(args[0], &slen);
        char tmp[64];
        if (slen >= (int)sizeof(tmp)) slen = (int)sizeof(tmp) - 1;
        memcpy(tmp, sd, (size_t)slen); tmp[slen] = '\0';
        char *end;
        double v = strtod(tmp, &end);
        if (end == tmp) {
            agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                          "float() invalid number string");
            return val_nil();
        }
        return val_float(v);
    }
    if (args[0].kind == VAL_INT) return val_float((double)args[0].as.integer);
    if (args[0].kind == VAL_FLOAT) return args[0];
    agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                  "float() cannot convert this type");
    return val_nil();
}

static AglVal builtin_push(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 2) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "push() takes exactly 2 arguments");
        return val_nil();
    }
    if (args[0].kind != VAL_ARRAY) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "push() first argument must be an array");
        return val_nil();
    }
    AglArrayVal *old = args[0].as.array;
    if (old->count >= MAX_ARRAY_SIZE) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "array size limit exceeded (max %d)", MAX_ARRAY_SIZE);
        return val_nil();
    }
    int new_count = old->count + 1;
    AglArrayVal *arr = agl_gc_alloc(BGC, sizeof(AglArrayVal), array_cleanup);
    if (!arr) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC, "out of memory");
        return val_nil();
    }
    arr->count = new_count;
    arr->elements = malloc(sizeof(AglVal) * (size_t)new_count);
    if (!arr->elements) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC, "out of memory");
        return val_nil();
    }
    memcpy(arr->elements, old->elements, sizeof(AglVal) * (size_t)old->count);
    arr->elements[old->count] = args[1];
    AglVal v; v.kind = VAL_ARRAY; v.as.array = arr;
    return v;
}

static AglVal builtin_map_fn(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 2) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "map() requires (array, fn)");
        return val_nil();
    }
    if (args[0].kind != VAL_ARRAY || args[1].kind != VAL_FN) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "map() requires (array, fn)");
        return val_nil();
    }
    AglArrayVal *src = args[0].as.array;
    AglArrayVal *dst = agl_gc_alloc(BGC, sizeof(AglArrayVal), array_cleanup);
    if (!dst) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC, "out of memory");
        return val_nil();
    }
    dst->count = src->count;
    dst->elements = src->count > 0
        ? malloc(sizeof(AglVal) * (size_t)src->count) : NULL;
    for (int i = 0; i < src->count; i++) {
        dst->elements[i] = bctx->call_fn(bctx->caller_data, args[1].as.fn,
                                          &src->elements[i], 1,
                                          bctx->line, bctx->col);
        if (agl_error_occurred(BCTX)) return val_nil();
    }
    AglVal v; v.kind = VAL_ARRAY; v.as.array = dst;
    return v;
}

static AglVal builtin_filter_fn(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 2) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "filter() requires (array, fn)");
        return val_nil();
    }
    if (args[0].kind != VAL_ARRAY || args[1].kind != VAL_FN) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "filter() requires (array, fn)");
        return val_nil();
    }
    AglArrayVal *src = args[0].as.array;
    AglVal *temp = src->count > 0
        ? malloc(sizeof(AglVal) * (size_t)src->count) : NULL;
    int kept = 0;
    for (int i = 0; i < src->count; i++) {
        AglVal pred = bctx->call_fn(bctx->caller_data, args[1].as.fn,
                                     &src->elements[i], 1,
                                     bctx->line, bctx->col);
        if (agl_error_occurred(BCTX)) { free(temp); return val_nil(); }
        if (is_truthy(pred)) temp[kept++] = src->elements[i];
    }
    AglArrayVal *dst = agl_gc_alloc(BGC, sizeof(AglArrayVal), array_cleanup);
    if (!dst) {
        free(temp);
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC, "out of memory");
        return val_nil();
    }
    dst->count = kept;
    dst->elements = kept > 0
        ? malloc(sizeof(AglVal) * (size_t)kept) : NULL;
    if (kept > 0) memcpy(dst->elements, temp, sizeof(AglVal) * (size_t)kept);
    free(temp);
    AglVal v; v.kind = VAL_ARRAY; v.as.array = dst;
    return v;
}

static AglVal builtin_abs(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 1) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "abs() takes exactly 1 argument");
        return val_nil();
    }
    if (args[0].kind == VAL_INT)
        return val_int(args[0].as.integer < 0
                       ? -args[0].as.integer : args[0].as.integer);
    if (args[0].kind == VAL_FLOAT)
        return val_float(args[0].as.floating < 0
                         ? -args[0].as.floating : args[0].as.floating);
    agl_error_set(BCTX, AGL_ERR_TYPE, BLOC, "abs() requires a number");
    return val_nil();
}

static AglVal builtin_min(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 2) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "min() takes exactly 2 arguments");
        return val_nil();
    }
    if (args[0].kind == VAL_INT && args[1].kind == VAL_INT)
        return args[0].as.integer <= args[1].as.integer ? args[0] : args[1];
    if (args[0].kind == VAL_FLOAT && args[1].kind == VAL_FLOAT)
        return args[0].as.floating <= args[1].as.floating ? args[0] : args[1];
    agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                  "min() requires two numbers of the same type");
    return val_nil();
}

static AglVal builtin_max(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 2) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "max() takes exactly 2 arguments");
        return val_nil();
    }
    if (args[0].kind == VAL_INT && args[1].kind == VAL_INT)
        return args[0].as.integer >= args[1].as.integer ? args[0] : args[1];
    if (args[0].kind == VAL_FLOAT && args[1].kind == VAL_FLOAT)
        return args[0].as.floating >= args[1].as.floating ? args[0] : args[1];
    agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                  "max() requires two numbers of the same type");
    return val_nil();
}

static AglVal builtin_read_file(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 1) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "read_file() takes exactly 1 argument");
        return val_nil();
    }
    if (args[0].kind != VAL_STRING) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "read_file() requires a string path");
        return val_nil();
    }
    int slen; const char *sd = str_content(args[0], &slen);
    char tmp[512];
    if (slen >= (int)sizeof(tmp)) slen = (int)sizeof(tmp) - 1;
    memcpy(tmp, sd, (size_t)slen); tmp[slen] = '\0';

    FILE *f = fopen(tmp, "rb");
    AglResultVal *rv = agl_gc_alloc(BGC, sizeof(AglResultVal), NULL);
    if (!rv) {
        if (f) fclose(f);
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC, "out of memory");
        return val_nil();
    }
    if (!f) {
        rv->is_ok = false;
        rv->value = val_string("cannot read file", 16);
    } else {
        fseek(f, 0, SEEK_END);
        long flen = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (flen < 0 || flen > 10 * 1024 * 1024) {
            fclose(f);
            rv->is_ok = false;
            rv->value = val_string("file too large", 14);
        } else {
            char *buf = agl_arena_alloc(BARENA, (size_t)flen);
            if (!buf) {
                fclose(f);
                agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC, "out of memory");
                return val_nil();
            }
            size_t nr = fread(buf, 1, (size_t)flen, f);
            fclose(f);
            rv->is_ok = true;
            rv->value = val_string(buf, (int)nr);
        }
    }
    AglVal v; v.kind = VAL_RESULT; v.as.result = rv;
    return v;
}

static AglVal builtin_write_file(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 2) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "write_file() takes exactly 2 arguments");
        return val_nil();
    }
    if (args[0].kind != VAL_STRING || args[1].kind != VAL_STRING) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "write_file() requires (string, string)");
        return val_nil();
    }
    int plen; const char *pd = str_content(args[0], &plen);
    int clen; const char *cd = str_content(args[1], &clen);
    char tmp[512];
    if (plen >= (int)sizeof(tmp)) plen = (int)sizeof(tmp) - 1;
    memcpy(tmp, pd, (size_t)plen); tmp[plen] = '\0';

    FILE *f = fopen(tmp, "wb");
    AglResultVal *rv = agl_gc_alloc(BGC, sizeof(AglResultVal), NULL);
    if (!rv) {
        if (f) fclose(f);
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC, "out of memory");
        return val_nil();
    }
    if (!f) {
        rv->is_ok = false;
        rv->value = val_string("cannot write file", 17);
    } else {
        fwrite(cd, 1, (size_t)clen, f);
        fclose(f);
        rv->is_ok = true;
        rv->value = val_bool(true);
    }
    AglVal v; v.kind = VAL_RESULT; v.as.result = rv;
    return v;
}

static AglVal builtin_file_exists(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 1) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "file_exists() takes exactly 1 argument");
        return val_nil();
    }
    if (args[0].kind != VAL_STRING) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "file_exists() requires a string path");
        return val_nil();
    }
    int slen; const char *sd = str_content(args[0], &slen);
    char tmp[512];
    if (slen >= (int)sizeof(tmp)) slen = (int)sizeof(tmp) - 1;
    memcpy(tmp, sd, (size_t)slen); tmp[slen] = '\0';
    FILE *f = fopen(tmp, "r");
    if (f) { fclose(f); return val_bool(true); }
    return val_bool(false);
}

static AglVal builtin_map_get(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 2) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "map_get() takes exactly 2 arguments");
        return val_nil();
    }
    if (args[0].kind != VAL_MAP) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "map_get() first argument must be a map");
        return val_nil();
    }
    if (args[1].kind != VAL_STRING) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "map_get() key must be a string");
        return val_nil();
    }
    AglMapVal *m = args[0].as.map;
    int klen; const char *kdata = str_content(args[1], &klen);
    for (int i = 0; i < m->count; i++) {
        if (m->key_lengths[i] == klen &&
            memcmp(m->keys[i], kdata, (size_t)klen) == 0)
            return m->values[i];
    }
    return val_nil();
}

static AglVal builtin_map_set(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 3) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "map_set() takes exactly 3 arguments");
        return val_nil();
    }
    if (args[0].kind != VAL_MAP) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "map_set() first argument must be a map");
        return val_nil();
    }
    if (args[1].kind != VAL_STRING) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "map_set() key must be a string");
        return val_nil();
    }
    AglMapVal *old = args[0].as.map;
    int klen; const char *kdata = str_content(args[1], &klen);
    int existing = -1;
    for (int i = 0; i < old->count; i++) {
        if (old->key_lengths[i] == klen &&
            memcmp(old->keys[i], kdata, (size_t)klen) == 0) {
            existing = i; break;
        }
    }
    int new_count = existing >= 0 ? old->count : old->count + 1;
    if (new_count > MAX_MAP_SIZE) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "map size limit exceeded (max %d)", MAX_MAP_SIZE);
        return val_nil();
    }
    AglMapVal *nm = agl_gc_alloc(BGC, sizeof(AglMapVal), map_cleanup);
    if (!nm) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC, "out of memory");
        return val_nil();
    }
    nm->count = new_count;
    nm->capacity = new_count;
    nm->keys = malloc(sizeof(char *) * (size_t)new_count);
    nm->key_lengths = malloc(sizeof(int) * (size_t)new_count);
    nm->values = malloc(sizeof(AglVal) * (size_t)new_count);
    if (!nm->keys || !nm->key_lengths || !nm->values) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC, "out of memory");
        return val_nil();
    }
    for (int i = 0; i < old->count; i++) {
        nm->keys[i] = old->keys[i];
        nm->key_lengths[i] = old->key_lengths[i];
        nm->values[i] = old->values[i];
    }
    if (existing >= 0) {
        nm->values[existing] = args[2];
    } else {
        nm->keys[old->count] = kdata;
        nm->key_lengths[old->count] = klen;
        nm->values[old->count] = args[2];
    }
    AglVal v; v.kind = VAL_MAP; v.as.map = nm;
    return v;
}

static AglVal builtin_map_keys(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 1) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "map_keys() takes exactly 1 argument");
        return val_nil();
    }
    if (args[0].kind != VAL_MAP) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "map_keys() requires a map");
        return val_nil();
    }
    AglMapVal *m = args[0].as.map;
    AglArrayVal *arr = agl_gc_alloc(BGC, sizeof(AglArrayVal), array_cleanup);
    if (!arr) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC, "out of memory");
        return val_nil();
    }
    arr->count = m->count;
    arr->elements = m->count > 0
        ? malloc(sizeof(AglVal) * (size_t)m->count) : NULL;
    for (int i = 0; i < m->count; i++) {
        arr->elements[i] = val_string(m->keys[i], m->key_lengths[i]);
    }
    AglVal v; v.kind = VAL_ARRAY; v.as.array = arr;
    return v;
}

static AglVal builtin_map_has(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 2) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "map_has() takes exactly 2 arguments");
        return val_nil();
    }
    if (args[0].kind != VAL_MAP) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "map_has() first argument must be a map");
        return val_nil();
    }
    if (args[1].kind != VAL_STRING) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "map_has() key must be a string");
        return val_nil();
    }
    AglMapVal *m = args[0].as.map;
    int klen; const char *kdata = str_content(args[1], &klen);
    for (int i = 0; i < m->count; i++) {
        if (m->key_lengths[i] == klen &&
            memcmp(m->keys[i], kdata, (size_t)klen) == 0)
            return val_bool(true);
    }
    return val_bool(false);
}

static AglVal builtin_map_del(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 2) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "map_del() takes exactly 2 arguments");
        return val_nil();
    }
    if (args[0].kind != VAL_MAP) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "map_del() first argument must be a map");
        return val_nil();
    }
    if (args[1].kind != VAL_STRING) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "map_del() key must be a string");
        return val_nil();
    }
    AglMapVal *old = args[0].as.map;
    int klen; const char *kdata = str_content(args[1], &klen);
    AglMapVal *nm = agl_gc_alloc(BGC, sizeof(AglMapVal), map_cleanup);
    if (!nm) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC, "out of memory");
        return val_nil();
    }
    int new_count = 0;
    nm->keys = old->count > 0
        ? malloc(sizeof(char *) * (size_t)old->count) : NULL;
    nm->key_lengths = old->count > 0
        ? malloc(sizeof(int) * (size_t)old->count) : NULL;
    nm->values = old->count > 0
        ? malloc(sizeof(AglVal) * (size_t)old->count) : NULL;
    for (int i = 0; i < old->count; i++) {
        if (old->key_lengths[i] == klen &&
            memcmp(old->keys[i], kdata, (size_t)klen) == 0)
            continue;
        nm->keys[new_count] = old->keys[i];
        nm->key_lengths[new_count] = old->key_lengths[i];
        nm->values[new_count] = old->values[i];
        new_count++;
    }
    nm->count = new_count;
    nm->capacity = old->count;
    AglVal v; v.kind = VAL_MAP; v.as.map = nm;
    return v;
}

static AglVal builtin_split(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 2) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "split() takes exactly 2 arguments");
        return val_nil();
    }
    if (args[0].kind != VAL_STRING || args[1].kind != VAL_STRING) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "split() requires (string, string)");
        return val_nil();
    }
    int slen, seplen;
    const char *sdata = str_content(args[0], &slen);
    const char *sepdata = str_content(args[1], &seplen);
    if (seplen == 0) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "split() separator cannot be empty");
        return val_nil();
    }
    AglVal parts[256]; int pcount = 0;
    int start = 0;
    for (int i = 0; i <= slen - seplen && pcount < 255; i++) {
        if (memcmp(sdata + i, sepdata, (size_t)seplen) == 0) {
            int plen = i - start;
            char *p = agl_arena_alloc(BARENA, (size_t)(plen > 0 ? plen : 1));
            if (p && plen > 0) memcpy(p, sdata + start, (size_t)plen);
            parts[pcount++] = val_string(p ? p : "", plen);
            start = i + seplen;
            i = start - 1;
        }
    }
    if (pcount < 256) {
        int plen = slen - start;
        char *p = agl_arena_alloc(BARENA, (size_t)(plen > 0 ? plen : 1));
        if (p && plen > 0) memcpy(p, sdata + start, (size_t)plen);
        parts[pcount++] = val_string(p ? p : "", plen);
    }
    AglArrayVal *arr = agl_gc_alloc(BGC, sizeof(AglArrayVal), array_cleanup);
    if (!arr) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC, "out of memory");
        return val_nil();
    }
    arr->count = pcount;
    arr->elements = malloc(sizeof(AglVal) * (size_t)pcount);
    memcpy(arr->elements, parts, sizeof(AglVal) * (size_t)pcount);
    AglVal v; v.kind = VAL_ARRAY; v.as.array = arr;
    return v;
}

static AglVal builtin_trim(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 1) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "trim() takes exactly 1 argument");
        return val_nil();
    }
    if (args[0].kind != VAL_STRING) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "trim() requires a string");
        return val_nil();
    }
    int slen; const char *sdata = str_content(args[0], &slen);
    int start = 0, end = slen;
    while (start < end && (sdata[start] == ' ' || sdata[start] == '\t' ||
           sdata[start] == '\n' || sdata[start] == '\r')) start++;
    while (end > start && (sdata[end-1] == ' ' || sdata[end-1] == '\t' ||
           sdata[end-1] == '\n' || sdata[end-1] == '\r')) end--;
    int rlen = end - start;
    char *buf = agl_arena_alloc(BARENA, (size_t)(rlen > 0 ? rlen : 1));
    if (buf && rlen > 0) memcpy(buf, sdata + start, (size_t)rlen);
    return val_string(buf ? buf : "", rlen);
}

static AglVal builtin_contains(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 2) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "contains() takes exactly 2 arguments");
        return val_nil();
    }
    if (args[0].kind != VAL_STRING || args[1].kind != VAL_STRING) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "contains() requires (string, string)");
        return val_nil();
    }
    int slen, sublen;
    const char *sdata = str_content(args[0], &slen);
    const char *subdata = str_content(args[1], &sublen);
    if (sublen > slen) return val_bool(false);
    if (sublen == 0) return val_bool(true);
    for (int i = 0; i <= slen - sublen; i++) {
        if (memcmp(sdata + i, subdata, (size_t)sublen) == 0)
            return val_bool(true);
    }
    return val_bool(false);
}

static AglVal builtin_replace(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 3) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "replace() takes exactly 3 arguments");
        return val_nil();
    }
    if (args[0].kind != VAL_STRING || args[1].kind != VAL_STRING ||
        args[2].kind != VAL_STRING) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "replace() requires (string, string, string)");
        return val_nil();
    }
    int slen, oldlen, newlen;
    const char *sdata = str_content(args[0], &slen);
    const char *olddata = str_content(args[1], &oldlen);
    const char *newdata = str_content(args[2], &newlen);
    if (oldlen == 0) {
        char *buf = agl_arena_alloc(BARENA, (size_t)slen);
        if (buf) memcpy(buf, sdata, (size_t)slen);
        return val_string(buf ? buf : sdata, slen);
    }
    int occ = 0;
    for (int i = 0; i <= slen - oldlen; i++) {
        if (memcmp(sdata + i, olddata, (size_t)oldlen) == 0) {
            occ++; i += oldlen - 1;
        }
    }
    int rlen = slen + occ * (newlen - oldlen);
    char *buf = agl_arena_alloc(BARENA, (size_t)(rlen > 0 ? rlen : 1));
    if (!buf) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC, "out of memory");
        return val_nil();
    }
    int wi = 0;
    for (int i = 0; i < slen; ) {
        if (i <= slen - oldlen &&
            memcmp(sdata + i, olddata, (size_t)oldlen) == 0) {
            memcpy(buf + wi, newdata, (size_t)newlen);
            wi += newlen; i += oldlen;
        } else {
            buf[wi++] = sdata[i++];
        }
    }
    return val_string(buf, rlen);
}

static AglVal builtin_starts_with(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 2) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "starts_with() takes exactly 2 arguments");
        return val_nil();
    }
    if (args[0].kind != VAL_STRING || args[1].kind != VAL_STRING) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "starts_with() requires (string, string)");
        return val_nil();
    }
    int slen, plen;
    const char *sdata = str_content(args[0], &slen);
    const char *pdata = str_content(args[1], &plen);
    return val_bool(slen >= plen && memcmp(sdata, pdata, (size_t)plen) == 0);
}

static AglVal builtin_ends_with(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 2) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "ends_with() takes exactly 2 arguments");
        return val_nil();
    }
    if (args[0].kind != VAL_STRING || args[1].kind != VAL_STRING) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "ends_with() requires (string, string)");
        return val_nil();
    }
    int slen, plen;
    const char *sdata = str_content(args[0], &slen);
    const char *pdata = str_content(args[1], &plen);
    return val_bool(slen >= plen &&
                    memcmp(sdata + slen - plen, pdata, (size_t)plen) == 0);
}

static AglVal builtin_to_upper(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 1) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "to_upper() takes exactly 1 argument");
        return val_nil();
    }
    if (args[0].kind != VAL_STRING) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "to_upper() requires a string");
        return val_nil();
    }
    int slen; const char *sdata = str_content(args[0], &slen);
    char *buf = agl_arena_alloc(BARENA, (size_t)(slen > 0 ? slen : 1));
    if (!buf) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC, "out of memory");
        return val_nil();
    }
    for (int i = 0; i < slen; i++)
        buf[i] = (char)(sdata[i] >= 'a' && sdata[i] <= 'z'
                         ? sdata[i] - 32 : sdata[i]);
    return val_string(buf, slen);
}

static AglVal builtin_to_lower(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 1) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "to_lower() takes exactly 1 argument");
        return val_nil();
    }
    if (args[0].kind != VAL_STRING) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "to_lower() requires a string");
        return val_nil();
    }
    int slen; const char *sdata = str_content(args[0], &slen);
    char *buf = agl_arena_alloc(BARENA, (size_t)(slen > 0 ? slen : 1));
    if (!buf) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC, "out of memory");
        return val_nil();
    }
    for (int i = 0; i < slen; i++)
        buf[i] = (char)(sdata[i] >= 'A' && sdata[i] <= 'Z'
                         ? sdata[i] + 32 : sdata[i]);
    return val_string(buf, slen);
}

static AglVal builtin_join(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 2) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "join() takes exactly 2 arguments");
        return val_nil();
    }
    if (args[0].kind != VAL_ARRAY || args[1].kind != VAL_STRING) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "join() requires (array, string)");
        return val_nil();
    }
    AglArrayVal *arr = args[0].as.array;
    int seplen; const char *sepdata = str_content(args[1], &seplen);
    int total = 0;
    for (int i = 0; i < arr->count; i++) {
        if (arr->elements[i].kind != VAL_STRING) {
            agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                          "join() array elements must be strings");
            return val_nil();
        }
        int elen; str_content(arr->elements[i], &elen);
        total += elen;
        if (i > 0) total += seplen;
    }
    char *buf = agl_arena_alloc(BARENA, (size_t)(total > 0 ? total : 1));
    if (!buf && total > 0) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC, "out of memory");
        return val_nil();
    }
    int wi = 0;
    for (int i = 0; i < arr->count; i++) {
        if (i > 0 && seplen > 0) {
            memcpy(buf + wi, sepdata, (size_t)seplen);
            wi += seplen;
        }
        int elen; const char *edata = str_content(arr->elements[i], &elen);
        if (elen > 0) {
            memcpy(buf + wi, edata, (size_t)elen);
            wi += elen;
        }
    }
    return val_string(buf ? buf : "", total);
}

static AglVal builtin_substr(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 3) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "substr() takes exactly 3 arguments");
        return val_nil();
    }
    if (args[0].kind != VAL_STRING || args[1].kind != VAL_INT ||
        args[2].kind != VAL_INT) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "substr() requires (string, int, int)");
        return val_nil();
    }
    int slen; const char *sdata = str_content(args[0], &slen);
    int start = (int)args[1].as.integer;
    int rlen = (int)args[2].as.integer;
    if (start < 0) start = 0;
    if (start > slen) start = slen;
    if (rlen < 0) rlen = 0;
    if (start + rlen > slen) rlen = slen - start;
    char *buf = agl_arena_alloc(BARENA, (size_t)(rlen > 0 ? rlen : 1));
    if (buf && rlen > 0) memcpy(buf, sdata + start, (size_t)rlen);
    return val_string(buf ? buf : "", rlen);
}

static AglVal builtin_json_parse(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 1) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "json_parse() takes exactly 1 argument");
        return val_nil();
    }
    if (args[0].kind != VAL_STRING) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "json_parse() requires a string");
        return val_nil();
    }
    int slen; const char *sd = str_content(args[0], &slen);
    return agl_json_parse(sd, slen, BARENA, BGC);
}

static AglVal builtin_json_stringify(AglVal *args, int argc,
                                     AglBuiltinCtx *bctx) {
    if (argc != 1) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "json_stringify() takes exactly 1 argument");
        return val_nil();
    }
    int out_len;
    const char *s = agl_json_stringify(args[0], &out_len, BARENA);
    return val_string(s, out_len);
}

static AglVal builtin_env(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 1) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "env() takes exactly 1 argument");
        return val_nil();
    }
    if (args[0].kind != VAL_STRING) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "env() requires a string");
        return val_nil();
    }
    int slen; const char *sd = str_content(args[0], &slen);
    char tmp[256];
    if (slen >= (int)sizeof(tmp)) slen = (int)sizeof(tmp) - 1;
    memcpy(tmp, sd, (size_t)slen); tmp[slen] = '\0';
    const char *val = getenv(tmp);
    AglResultVal *rv = agl_gc_alloc(BGC, sizeof(AglResultVal), NULL);
    if (!rv) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC, "out of memory");
        return val_nil();
    }
    if (val) {
        int vlen = (int)strlen(val);
        char *copy = agl_arena_alloc(BARENA, (size_t)vlen);
        if (copy) memcpy(copy, val, (size_t)vlen);
        rv->is_ok = true;
        rv->value = val_string(copy ? copy : "", copy ? vlen : 0);
    } else {
        rv->is_ok = false;
        rv->value = val_string("not set", 7);
    }
    AglVal v; v.kind = VAL_RESULT; v.as.result = rv;
    return v;
}

static AglVal builtin_env_default(AglVal *args, int argc,
                                  AglBuiltinCtx *bctx) {
    if (argc != 2) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "env_default() takes exactly 2 arguments");
        return val_nil();
    }
    if (args[0].kind != VAL_STRING || args[1].kind != VAL_STRING) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "env_default() requires (string, string)");
        return val_nil();
    }
    int slen; const char *sd = str_content(args[0], &slen);
    char tmp[256];
    if (slen >= (int)sizeof(tmp)) slen = (int)sizeof(tmp) - 1;
    memcpy(tmp, sd, (size_t)slen); tmp[slen] = '\0';
    const char *val = getenv(tmp);
    if (val) {
        int vlen = (int)strlen(val);
        char *copy = agl_arena_alloc(BARENA, (size_t)vlen);
        if (copy) memcpy(copy, val, (size_t)vlen);
        return val_string(copy ? copy : "", copy ? vlen : 0);
    } else {
        int flen; const char *fdata = str_content(args[1], &flen);
        char *copy = agl_arena_alloc(BARENA, (size_t)flen);
        if (copy) memcpy(copy, fdata, (size_t)flen);
        return val_string(copy ? copy : "", copy ? flen : 0);
    }
}

static AglVal builtin_http_get(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 2) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "http_get() takes exactly 2 arguments");
        return val_nil();
    }
    if (args[0].kind != VAL_STRING) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "http_get() first argument must be a string URL");
        return val_nil();
    }
    if (args[1].kind != VAL_MAP) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "http_get() second argument must be a map of headers");
        return val_nil();
    }
    int slen; const char *sd = str_content(args[0], &slen);
    return agl_http_request("GET", sd, slen, args[1].as.map,
                            NULL, 0, BARENA, BGC);
}

static AglVal builtin_http_post(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 3) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "http_post() takes exactly 3 arguments");
        return val_nil();
    }
    if (args[0].kind != VAL_STRING) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "http_post() first argument must be a string URL");
        return val_nil();
    }
    if (args[1].kind != VAL_MAP) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "http_post() second argument must be a map of headers");
        return val_nil();
    }
    if (args[2].kind != VAL_STRING) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "http_post() third argument must be a string body");
        return val_nil();
    }
    int ulen; const char *ud = str_content(args[0], &ulen);
    int blen; const char *bd = str_content(args[2], &blen);
    return agl_http_request("POST", ud, ulen, args[1].as.map,
                            bd, blen, BARENA, BGC);
}

static AglVal builtin_exec_cmd(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 2) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "exec() takes exactly 2 arguments");
        return val_nil();
    }
    if (args[0].kind != VAL_STRING) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "exec() first argument must be a string command");
        return val_nil();
    }
    if (args[1].kind != VAL_ARRAY) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "exec() second argument must be an array of arguments");
        return val_nil();
    }
    int clen; const char *cd = str_content(args[0], &clen);
    return agl_exec(cd, clen, args[1].as.array, BARENA, BGC);
}

static AglVal builtin_now(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    (void)args;
    if (argc != 0) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "now() takes no arguments");
        return val_nil();
    }
    return val_int(agl_now_ms());
}

static AglVal builtin_sleep_ms(AglVal *args, int argc, AglBuiltinCtx *bctx) {
    if (argc != 1) {
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "sleep() takes exactly 1 argument");
        return val_nil();
    }
    if (args[0].kind != VAL_INT) {
        agl_error_set(BCTX, AGL_ERR_TYPE, BLOC,
                      "sleep() requires an integer (milliseconds)");
        return val_nil();
    }
    agl_sleep_ms(args[0].as.integer);
    return val_nil();
}

/* ---- Central dispatch ---- */

AglVal agl_builtin_dispatch(int builtin_id, AglVal *args, int argc,
                            AglBuiltinCtx *bctx) {
    switch (builtin_id) {
    case AGL_BUILTIN_PRINT:
        for (int i = 0; i < argc; i++) {
            builtin_print(args[i]);
        }
        return val_nil();

    case AGL_BUILTIN_LEN:          return builtin_len(args, argc, bctx);
    case AGL_BUILTIN_TYPE:         return builtin_type(args, argc, bctx);
    case AGL_BUILTIN_STR:          return builtin_str(args, argc, bctx);
    case AGL_BUILTIN_INT:          return builtin_int_conv(args, argc, bctx);
    case AGL_BUILTIN_FLOAT:        return builtin_float_conv(args, argc, bctx);
    case AGL_BUILTIN_PUSH:         return builtin_push(args, argc, bctx);
    case AGL_BUILTIN_MAP:          return builtin_map_fn(args, argc, bctx);
    case AGL_BUILTIN_FILTER:       return builtin_filter_fn(args, argc, bctx);
    case AGL_BUILTIN_ABS:          return builtin_abs(args, argc, bctx);
    case AGL_BUILTIN_MIN:          return builtin_min(args, argc, bctx);
    case AGL_BUILTIN_MAX:          return builtin_max(args, argc, bctx);
    case AGL_BUILTIN_READ_FILE:    return builtin_read_file(args, argc, bctx);
    case AGL_BUILTIN_WRITE_FILE:   return builtin_write_file(args, argc, bctx);
    case AGL_BUILTIN_FILE_EXISTS:  return builtin_file_exists(args, argc, bctx);
    case AGL_BUILTIN_MAP_GET:      return builtin_map_get(args, argc, bctx);
    case AGL_BUILTIN_MAP_SET:      return builtin_map_set(args, argc, bctx);
    case AGL_BUILTIN_MAP_KEYS:     return builtin_map_keys(args, argc, bctx);
    case AGL_BUILTIN_MAP_HAS:      return builtin_map_has(args, argc, bctx);
    case AGL_BUILTIN_MAP_DEL:      return builtin_map_del(args, argc, bctx);
    case AGL_BUILTIN_SPLIT:        return builtin_split(args, argc, bctx);
    case AGL_BUILTIN_TRIM:         return builtin_trim(args, argc, bctx);
    case AGL_BUILTIN_CONTAINS:     return builtin_contains(args, argc, bctx);
    case AGL_BUILTIN_REPLACE:      return builtin_replace(args, argc, bctx);
    case AGL_BUILTIN_STARTS_WITH:  return builtin_starts_with(args, argc, bctx);
    case AGL_BUILTIN_ENDS_WITH:    return builtin_ends_with(args, argc, bctx);
    case AGL_BUILTIN_TO_UPPER:     return builtin_to_upper(args, argc, bctx);
    case AGL_BUILTIN_TO_LOWER:     return builtin_to_lower(args, argc, bctx);
    case AGL_BUILTIN_JOIN:         return builtin_join(args, argc, bctx);
    case AGL_BUILTIN_SUBSTR:       return builtin_substr(args, argc, bctx);
    case AGL_BUILTIN_JSON_PARSE:   return builtin_json_parse(args, argc, bctx);
    case AGL_BUILTIN_JSON_STRINGIFY:
        return builtin_json_stringify(args, argc, bctx);
    case AGL_BUILTIN_ENV:          return builtin_env(args, argc, bctx);
    case AGL_BUILTIN_ENV_DEFAULT:  return builtin_env_default(args, argc, bctx);
    case AGL_BUILTIN_HTTP_GET:     return builtin_http_get(args, argc, bctx);
    case AGL_BUILTIN_HTTP_POST:    return builtin_http_post(args, argc, bctx);
    case AGL_BUILTIN_EXEC:         return builtin_exec_cmd(args, argc, bctx);
    case AGL_BUILTIN_NOW:          return builtin_now(args, argc, bctx);
    case AGL_BUILTIN_SLEEP:        return builtin_sleep_ms(args, argc, bctx);

    default:
        agl_error_set(BCTX, AGL_ERR_RUNTIME, BLOC,
                      "unknown builtin %d", builtin_id);
        return val_nil();
    }
}
