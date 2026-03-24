#include "json.h"

/* ---- JSON Parser (recursive descent) ---- */

typedef struct {
    const char *src;
    int len;
    int pos;
    AglArena *arena;
    AglGc *gc;
    char err_msg[256];
    bool has_error;
} JsonParser;

static void jp_error(JsonParser *p, const char *msg) {
    if (!p->has_error) {
        snprintf(p->err_msg, sizeof(p->err_msg), "%s at position %d", msg, p->pos);
        p->has_error = true;
    }
}

static void jp_skip_ws(JsonParser *p) {
    while (p->pos < p->len) {
        char c = p->src[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p->pos++;
        } else {
            break;
        }
    }
}

static char jp_peek(JsonParser *p) {
    jp_skip_ws(p);
    if (p->pos >= p->len) return '\0';
    return p->src[p->pos];
}

static bool jp_match(JsonParser *p, char expected) {
    jp_skip_ws(p);
    if (p->pos < p->len && p->src[p->pos] == expected) {
        p->pos++;
        return true;
    }
    return false;
}

static bool jp_match_str(JsonParser *p, const char *s, int slen) {
    if (p->pos + slen > p->len) return false;
    if (memcmp(p->src + p->pos, s, (size_t)slen) != 0) return false;
    p->pos += slen;
    return true;
}

/* Forward declaration */
static AglVal jp_parse_value(JsonParser *p);

/* Check if current position (after whitespace) is at a string start.
 * Agl strings may contain \" (backslash-quote) where standard JSON has ".
 * We accept both " and \" as string delimiters. */
static bool jp_at_string(JsonParser *p) {
    jp_skip_ws(p);
    if (p->pos >= p->len) return false;
    if (p->src[p->pos] == '"') return true;
    if (p->src[p->pos] == '\\' && p->pos + 1 < p->len && p->src[p->pos + 1] == '"') return true;
    return false;
}

/* Skip a string-opening delimiter: either " or \" */
static void jp_skip_open_quote(JsonParser *p) {
    if (p->pos < p->len && p->src[p->pos] == '\\') p->pos++; /* skip backslash */
    p->pos++; /* skip quote */
}

static AglVal jp_parse_string(JsonParser *p) {
    /* Skip opening quote (either " or \") */
    bool escaped_delim = (p->pos < p->len && p->src[p->pos] == '\\');
    jp_skip_open_quote(p);

    /* Collect string content.
     * The closing delimiter matches the opening style:
     * - If opened with \", close with \"
     * - If opened with ", close with "
     * Inside the string, standard JSON escapes apply. */

    int start = p->pos;
    int out_len = 0;

    if (escaped_delim) {
        /* Opened with \" — content ends at next \" */
        while (p->pos < p->len) {
            if (p->src[p->pos] == '\\' && p->pos + 1 < p->len && p->src[p->pos + 1] == '"') {
                break; /* found closing \" */
            }
            out_len++;
            p->pos++;
        }
        if (p->pos >= p->len) { jp_error(p, "unterminated string"); return val_nil(); }
        /* Content is raw between the delimiters */
        char *buf = agl_arena_alloc(p->arena, (size_t)(out_len > 0 ? out_len : 1));
        if (!buf) { jp_error(p, "out of memory"); return val_nil(); }
        memcpy(buf, p->src + start, (size_t)out_len);
        p->pos += 2; /* skip closing \" */
        return val_string(buf, out_len);
    }

    /* Opened with " — standard JSON string parsing */
    while (p->pos < p->len && p->src[p->pos] != '"') {
        if (p->src[p->pos] == '\\') {
            p->pos++;
            if (p->pos >= p->len) { jp_error(p, "unterminated string escape"); return val_nil(); }
            char esc = p->src[p->pos];
            switch (esc) {
            case '"': case '\\': case '/':
            case 'n': case 't': case 'r': case 'b': case 'f':
                out_len++;
                break;
            case 'u':
                /* Skip 4 hex digits, output as '?' for simplicity */
                p->pos += 4;
                out_len++;
                continue; /* don't advance again */
            default:
                jp_error(p, "invalid escape sequence");
                return val_nil();
            }
        } else {
            out_len++;
        }
        p->pos++;
    }
    if (p->pos >= p->len) { jp_error(p, "unterminated string"); return val_nil(); }

    /* Allocate and fill */
    char *buf = agl_arena_alloc(p->arena, (size_t)(out_len > 0 ? out_len : 1));
    if (!buf) { jp_error(p, "out of memory"); return val_nil(); }

    int wi = 0;
    int ri = start;
    while (ri < p->pos) {
        if (p->src[ri] == '\\') {
            ri++;
            switch (p->src[ri]) {
            case '"':  buf[wi++] = '"'; break;
            case '\\': buf[wi++] = '\\'; break;
            case '/':  buf[wi++] = '/'; break;
            case 'n':  buf[wi++] = '\n'; break;
            case 't':  buf[wi++] = '\t'; break;
            case 'r':  buf[wi++] = '\r'; break;
            case 'b':  buf[wi++] = '\b'; break;
            case 'f':  buf[wi++] = '\f'; break;
            case 'u':  buf[wi++] = '?'; ri += 4; continue;
            default:   buf[wi++] = p->src[ri]; break;
            }
        } else {
            buf[wi++] = p->src[ri];
        }
        ri++;
    }

    p->pos++; /* skip closing '"' */
    return val_string(buf, out_len);
}

static AglVal jp_parse_number(JsonParser *p) {
    int start = p->pos;
    bool has_decimal = false;

    if (p->pos < p->len && p->src[p->pos] == '-') p->pos++;

    while (p->pos < p->len && p->src[p->pos] >= '0' && p->src[p->pos] <= '9') {
        p->pos++;
    }

    if (p->pos < p->len && p->src[p->pos] == '.') {
        has_decimal = true;
        p->pos++;
        while (p->pos < p->len && p->src[p->pos] >= '0' && p->src[p->pos] <= '9') {
            p->pos++;
        }
    }

    /* Handle exponent */
    if (p->pos < p->len && (p->src[p->pos] == 'e' || p->src[p->pos] == 'E')) {
        has_decimal = true; /* treat exponential notation as float */
        p->pos++;
        if (p->pos < p->len && (p->src[p->pos] == '+' || p->src[p->pos] == '-')) p->pos++;
        while (p->pos < p->len && p->src[p->pos] >= '0' && p->src[p->pos] <= '9') {
            p->pos++;
        }
    }

    int nlen = p->pos - start;
    char tmp[64];
    if (nlen >= (int)sizeof(tmp)) nlen = (int)sizeof(tmp) - 1;
    memcpy(tmp, p->src + start, (size_t)nlen);
    tmp[nlen] = '\0';

    if (has_decimal) {
        char *end;
        double v = strtod(tmp, &end);
        if (end == tmp) { jp_error(p, "invalid number"); return val_nil(); }
        return val_float(v);
    } else {
        char *end;
        int64_t v = strtoll(tmp, &end, 10);
        if (end == tmp) { jp_error(p, "invalid number"); return val_nil(); }
        return val_int(v);
    }
}

static AglVal jp_parse_array(JsonParser *p) {
    p->pos++; /* skip '[' */

    AglVal items[MAX_ARRAY_SIZE];
    int count = 0;

    jp_skip_ws(p);
    if (jp_peek(p) == ']') {
        p->pos++;
        AglArrayVal *arr = agl_gc_alloc(p->gc, sizeof(AglArrayVal), array_cleanup);
        if (!arr) { jp_error(p, "out of memory"); return val_nil(); }
        arr->count = 0;
        arr->elements = NULL;
        AglVal v;
        v.kind = VAL_ARRAY;
        v.as.array = arr;
        return v;
    }

    while (!p->has_error) {
        if (count >= MAX_ARRAY_SIZE) { jp_error(p, "array too large"); return val_nil(); }
        items[count++] = jp_parse_value(p);
        if (p->has_error) return val_nil();

        jp_skip_ws(p);
        if (jp_match(p, ',')) continue;
        if (jp_match(p, ']')) break;
        jp_error(p, "expected ',' or ']' in array");
        return val_nil();
    }

    AglArrayVal *arr = agl_gc_alloc(p->gc, sizeof(AglArrayVal), array_cleanup);
    if (!arr) { jp_error(p, "out of memory"); return val_nil(); }
    arr->count = count;
    arr->elements = count > 0 ? malloc(sizeof(AglVal) * (size_t)count) : NULL;
    if (count > 0 && !arr->elements) { jp_error(p, "out of memory"); return val_nil(); }
    if (count > 0) memcpy(arr->elements, items, sizeof(AglVal) * (size_t)count);

    AglVal v;
    v.kind = VAL_ARRAY;
    v.as.array = arr;
    return v;
}

static AglVal jp_parse_object(JsonParser *p) {
    p->pos++; /* skip '{' */

    const char *keys[MAX_MAP_SIZE];
    int key_lengths[MAX_MAP_SIZE];
    AglVal values[MAX_MAP_SIZE];
    int count = 0;

    jp_skip_ws(p);
    if (jp_match(p, '}')) {
        AglMapVal *m = agl_gc_alloc(p->gc, sizeof(AglMapVal), map_cleanup);
        if (!m) { jp_error(p, "out of memory"); return val_nil(); }
        m->count = 0;
        m->capacity = 0;
        m->keys = NULL;
        m->key_lengths = NULL;
        m->values = NULL;
        AglVal v;
        v.kind = VAL_MAP;
        v.as.map = m;
        return v;
    }

    while (!p->has_error) {
        if (count >= MAX_MAP_SIZE) { jp_error(p, "object too large"); return val_nil(); }

        jp_skip_ws(p);
        if (!jp_at_string(p)) { jp_error(p, "expected string key in object"); return val_nil(); }

        AglVal key_val = jp_parse_string(p);
        if (p->has_error) return val_nil();

        keys[count] = key_val.as.string.data;
        key_lengths[count] = key_val.as.string.length;

        jp_skip_ws(p);
        if (!jp_match(p, ':')) { jp_error(p, "expected ':' after key in object"); return val_nil(); }

        values[count] = jp_parse_value(p);
        if (p->has_error) return val_nil();
        count++;

        jp_skip_ws(p);
        if (jp_match(p, ',')) continue;
        if (jp_match(p, '}')) break;
        jp_error(p, "expected ',' or '}' in object");
        return val_nil();
    }

    AglMapVal *m = agl_gc_alloc(p->gc, sizeof(AglMapVal), map_cleanup);
    if (!m) { jp_error(p, "out of memory"); return val_nil(); }
    m->count = count;
    m->capacity = count;
    m->keys = count > 0 ? malloc(sizeof(char *) * (size_t)count) : NULL;
    m->key_lengths = count > 0 ? malloc(sizeof(int) * (size_t)count) : NULL;
    m->values = count > 0 ? malloc(sizeof(AglVal) * (size_t)count) : NULL;
    if (count > 0 && (!m->keys || !m->key_lengths || !m->values)) {
        jp_error(p, "out of memory");
        return val_nil();
    }
    for (int i = 0; i < count; i++) {
        m->keys[i] = keys[i];
        m->key_lengths[i] = key_lengths[i];
        m->values[i] = values[i];
    }

    AglVal v;
    v.kind = VAL_MAP;
    v.as.map = m;
    return v;
}

static AglVal jp_parse_value(JsonParser *p) {
    jp_skip_ws(p);
    if (p->pos >= p->len) { jp_error(p, "unexpected end of input"); return val_nil(); }

    /* Check for string (either " or \") */
    if (jp_at_string(p)) return jp_parse_string(p);

    char c = p->src[p->pos];

    if (c == '{') return jp_parse_object(p);
    if (c == '[') return jp_parse_array(p);
    if (c == '-' || (c >= '0' && c <= '9')) return jp_parse_number(p);

    if (jp_match_str(p, "true", 4)) return val_bool(true);
    if (jp_match_str(p, "false", 5)) return val_bool(false);
    if (jp_match_str(p, "null", 4)) return val_nil();

    jp_error(p, "unexpected character");
    return val_nil();
}

AglVal agl_json_parse(const char *input, int length, AglArena *arena, AglGc *gc) {
    JsonParser p;
    p.src = input;
    p.len = length;
    p.pos = 0;
    p.arena = arena;
    p.gc = gc;
    p.has_error = false;
    p.err_msg[0] = '\0';

    AglVal parsed = jp_parse_value(&p);

    /* Build result */
    AglResultVal *rv = agl_gc_alloc(gc, sizeof(AglResultVal), NULL);
    if (!rv) {
        /* Emergency fallback: return nil */
        return val_nil();
    }

    if (p.has_error) {
        int mlen = (int)strlen(p.err_msg);
        char *msg = agl_arena_alloc(arena, (size_t)mlen);
        if (msg) memcpy(msg, p.err_msg, (size_t)mlen);
        rv->is_ok = false;
        rv->value = val_string(msg ? msg : p.err_msg, mlen);
    } else {
        /* Check for trailing non-whitespace */
        jp_skip_ws(&p);
        if (p.pos < p.len) {
            const char *trail_msg = "unexpected trailing content";
            int mlen = (int)strlen(trail_msg);
            char *msg = agl_arena_alloc(arena, (size_t)mlen);
            if (msg) memcpy(msg, trail_msg, (size_t)mlen);
            rv->is_ok = false;
            rv->value = val_string(msg ? msg : trail_msg, mlen);
        } else {
            rv->is_ok = true;
            rv->value = parsed;
        }
    }

    AglVal result;
    result.kind = VAL_RESULT;
    result.as.result = rv;
    return result;
}

/* ---- JSON Stringify ---- */

/* Dynamic string buffer for building JSON output */
typedef struct {
    char *buf;
    int len;
    int cap;
    AglArena *arena;
    bool oom;
} JsonBuf;

static void jb_init(JsonBuf *jb, AglArena *arena) {
    jb->cap = 256;
    jb->buf = malloc((size_t)jb->cap);
    jb->len = 0;
    jb->arena = arena;
    jb->oom = (jb->buf == NULL);
}

static void jb_grow(JsonBuf *jb, int need) {
    if (jb->oom) return;
    while (jb->len + need > jb->cap) {
        jb->cap *= 2;
    }
    char *nb = realloc(jb->buf, (size_t)jb->cap);
    if (!nb) { jb->oom = true; return; }
    jb->buf = nb;
}

static void jb_append(JsonBuf *jb, const char *s, int slen) {
    if (jb->oom) return;
    if (jb->len + slen > jb->cap) jb_grow(jb, slen);
    if (jb->oom) return;
    memcpy(jb->buf + jb->len, s, (size_t)slen);
    jb->len += slen;
}

static void jb_append_char(JsonBuf *jb, char c) {
    jb_append(jb, &c, 1);
}

static void jb_append_escaped_string(JsonBuf *jb, const char *s, int slen) {
    jb_append_char(jb, '"');
    for (int i = 0; i < slen; i++) {
        char c = s[i];
        switch (c) {
        case '"':  jb_append(jb, "\\\"", 2); break;
        case '\\': jb_append(jb, "\\\\", 2); break;
        case '\n': jb_append(jb, "\\n", 2); break;
        case '\t': jb_append(jb, "\\t", 2); break;
        case '\r': jb_append(jb, "\\r", 2); break;
        case '\b': jb_append(jb, "\\b", 2); break;
        case '\f': jb_append(jb, "\\f", 2); break;
        default:
            if ((unsigned char)c < 0x20) {
                char esc[8];
                int n = snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)c);
                jb_append(jb, esc, n);
            } else {
                jb_append_char(jb, c);
            }
            break;
        }
    }
    jb_append_char(jb, '"');
}

static void jb_stringify_val(JsonBuf *jb, AglVal val);

static void jb_stringify_val(JsonBuf *jb, AglVal val) {
    if (jb->oom) return;

    switch (val.kind) {
    case VAL_NIL:
        jb_append(jb, "null", 4);
        break;
    case VAL_BOOL:
        if (val.as.boolean) jb_append(jb, "true", 4);
        else jb_append(jb, "false", 5);
        break;
    case VAL_INT: {
        char tmp[32];
        int n = snprintf(tmp, sizeof(tmp), "%lld", (long long)val.as.integer);
        jb_append(jb, tmp, n);
        break;
    }
    case VAL_FLOAT: {
        char tmp[64];
        int n = snprintf(tmp, sizeof(tmp), "%g", val.as.floating);
        jb_append(jb, tmp, n);
        break;
    }
    case VAL_STRING: {
        int slen;
        const char *sdata = str_content(val, &slen);
        jb_append_escaped_string(jb, sdata, slen);
        break;
    }
    case VAL_ARRAY: {
        AglArrayVal *arr = val.as.array;
        jb_append_char(jb, '[');
        for (int i = 0; i < arr->count; i++) {
            if (i > 0) jb_append_char(jb, ',');
            jb_stringify_val(jb, arr->elements[i]);
        }
        jb_append_char(jb, ']');
        break;
    }
    case VAL_MAP: {
        AglMapVal *m = val.as.map;
        jb_append_char(jb, '{');
        for (int i = 0; i < m->count; i++) {
            if (i > 0) jb_append_char(jb, ',');
            jb_append_escaped_string(jb, m->keys[i], m->key_lengths[i]);
            jb_append_char(jb, ':');
            jb_stringify_val(jb, m->values[i]);
        }
        jb_append_char(jb, '}');
        break;
    }
    case VAL_STRUCT: {
        AglStructVal *s = val.as.strct;
        jb_append_char(jb, '{');
        for (int i = 0; i < s->field_count; i++) {
            if (i > 0) jb_append_char(jb, ',');
            jb_append_escaped_string(jb, s->field_names[i], s->field_name_lengths[i]);
            jb_append_char(jb, ':');
            jb_stringify_val(jb, s->field_values[i]);
        }
        jb_append_char(jb, '}');
        break;
    }
    case VAL_RESULT: {
        AglResultVal *r = val.as.result;
        jb_append_char(jb, '{');
        if (r->is_ok) {
            jb_append(jb, "\"ok\":", 5);
        } else {
            jb_append(jb, "\"err\":", 6);
        }
        jb_stringify_val(jb, r->value);
        jb_append_char(jb, '}');
        break;
    }
    case VAL_FN:
        jb_append(jb, "\"<fn>\"", 6);
        break;
    }
}

const char *agl_json_stringify(AglVal val, int *out_len, AglArena *arena) {
    JsonBuf jb;
    jb_init(&jb, arena);
    jb_stringify_val(&jb, val);

    if (jb.oom || !jb.buf) {
        *out_len = 0;
        free(jb.buf);
        return "";
    }

    /* Copy into arena */
    char *result = agl_arena_alloc(arena, (size_t)jb.len);
    if (result) {
        memcpy(result, jb.buf, (size_t)jb.len);
    }
    *out_len = jb.len;
    free(jb.buf);
    return result ? result : "";
}
