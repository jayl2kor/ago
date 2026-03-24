#include "http.h"
#include <string.h>

#ifdef AGL_HAS_CURL
#include <curl/curl.h>

/* ---- Response buffer ---- */

typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} HttpBuf;

static void buf_init(HttpBuf *buf) {
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
}

static void buf_free(HttpBuf *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
}

static bool buf_append(HttpBuf *buf, const char *data, size_t len) {
    if (buf->size + len > buf->capacity) {
        size_t new_cap = (buf->capacity == 0) ? 4096 : buf->capacity * 2;
        while (new_cap < buf->size + len) new_cap *= 2;
        char *new_data = realloc(buf->data, new_cap);
        if (!new_data) return false;
        buf->data = new_data;
        buf->capacity = new_cap;
    }
    memcpy(buf->data + buf->size, data, len);
    buf->size += len;
    return true;
}

/* ---- curl callbacks ---- */

static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    HttpBuf *buf = userdata;
    size_t total = size * nmemb;
    if (!buf_append(buf, ptr, total)) return 0;
    return total;
}

/* Header buffer: accumulates response headers as "Name: Value\r\n" lines */
static size_t header_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    HttpBuf *buf = userdata;
    size_t total = size * nmemb;
    if (!buf_append(buf, ptr, total)) return 0;
    return total;
}

/* ---- Parse response headers into a map ---- */

static AglMapVal *parse_response_headers(const char *raw, size_t raw_len,
                                          AglArena *arena, AglGc *gc) {
    AglMapVal *m = agl_gc_alloc(gc, sizeof(AglMapVal), map_cleanup);
    if (!m) return NULL;
    m->count = 0;
    m->capacity = 32;
    m->keys = malloc(sizeof(char *) * 32);
    m->key_lengths = malloc(sizeof(int) * 32);
    m->values = malloc(sizeof(AglVal) * 32);
    if (!m->keys || !m->key_lengths || !m->values) return m;

    const char *p = raw;
    const char *end = raw + raw_len;
    while (p < end) {
        /* Find end of line */
        const char *eol = p;
        while (eol < end && *eol != '\r' && *eol != '\n') eol++;

        /* Find colon separator */
        const char *colon = p;
        while (colon < eol && *colon != ':') colon++;

        if (colon < eol && colon > p) {
            int klen = (int)(colon - p);
            const char *vstart = colon + 1;
            while (vstart < eol && *vstart == ' ') vstart++;
            int vlen = (int)(eol - vstart);

            if (m->count < m->capacity) {
                char *key_copy = agl_arena_alloc(arena, (size_t)klen);
                if (key_copy) memcpy(key_copy, p, (size_t)klen);
                char *val_copy = agl_arena_alloc(arena, (size_t)(vlen > 0 ? vlen : 1));
                if (val_copy && vlen > 0) memcpy(val_copy, vstart, (size_t)vlen);

                m->keys[m->count] = key_copy ? key_copy : "";
                m->key_lengths[m->count] = klen;
                m->values[m->count] = val_string(val_copy ? val_copy : "", vlen);
                m->count++;
            }
        }

        /* Skip past line ending */
        while (p < end && p <= eol) {
            if (*p == '\r' || *p == '\n') { p++; }
            else break;
        }
        if (p == eol) p = eol + 1;
    }
    return m;
}

/* ---- Public API ---- */

AglVal agl_http_request(const char *method,
                        const char *url, int url_len,
                        AglMapVal *headers,
                        const char *body, int body_len,
                        AglArena *arena, AglGc *gc) {
    /* Null-terminate URL */
    char *url_z = malloc((size_t)url_len + 1);
    if (!url_z) {
        AglResultVal *rv = agl_gc_alloc(gc, sizeof(AglResultVal), NULL);
        if (!rv) return val_nil();
        rv->is_ok = false;
        rv->value = val_string("out of memory", 13);
        return (AglVal){VAL_RESULT, {.result = rv}};
    }
    memcpy(url_z, url, (size_t)url_len);
    url_z[url_len] = '\0';

    CURL *curl = curl_easy_init();
    if (!curl) {
        free(url_z);
        AglResultVal *rv = agl_gc_alloc(gc, sizeof(AglResultVal), NULL);
        if (!rv) return val_nil();
        rv->is_ok = false;
        rv->value = val_string("failed to initialize HTTP client", 32);
        return (AglVal){VAL_RESULT, {.result = rv}};
    }

    HttpBuf resp_body;
    buf_init(&resp_body);
    HttpBuf resp_headers;
    buf_init(&resp_headers);

    curl_easy_setopt(curl, CURLOPT_URL, url_z);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp_headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    /* Set method */
    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (body && body_len > 0) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
        } else {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
        }
    }

    /* Set request headers from map */
    struct curl_slist *header_list = NULL;
    if (headers) {
        for (int i = 0; i < headers->count; i++) {
            /* Build "Key: Value" string */
            int klen = headers->key_lengths[i];
            const char *kdata = headers->keys[i];
            /* Strip quotes from key if present */
            if (klen >= 2 && kdata[0] == '"') {
                kdata = kdata + 1;
                klen = klen - 2;
            }
            int vlen = 0;
            const char *vdata = "";
            if (headers->values[i].kind == VAL_STRING) {
                AglVal vval = headers->values[i];
                vdata = str_content(vval, &vlen);
            }
            int hlen = klen + 2 + vlen; /* "Key: Value" */
            char *h = malloc((size_t)hlen + 1);
            if (h) {
                memcpy(h, kdata, (size_t)klen);
                h[klen] = ':';
                h[klen + 1] = ' ';
                if (vlen > 0) memcpy(h + klen + 2, vdata, (size_t)vlen);
                h[hlen] = '\0';
                header_list = curl_slist_append(header_list, h);
                free(h);
            }
        }
        if (header_list) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
        }
    }

    CURLcode res = curl_easy_perform(curl);

    long status_code = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    }

    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
    free(url_z);

    if (res != CURLE_OK) {
        const char *err_msg = curl_easy_strerror(res);
        int err_len = (int)strlen(err_msg);
        char *err_copy = agl_arena_alloc(arena, (size_t)err_len);
        if (err_copy) memcpy(err_copy, err_msg, (size_t)err_len);
        buf_free(&resp_body);
        buf_free(&resp_headers);

        AglResultVal *rv = agl_gc_alloc(gc, sizeof(AglResultVal), NULL);
        if (!rv) return val_nil();
        rv->is_ok = false;
        rv->value = val_string(err_copy ? err_copy : "HTTP request failed",
                               err_copy ? err_len : 19);
        return (AglVal){VAL_RESULT, {.result = rv}};
    }

    /* Copy response body to arena */
    char *body_copy = NULL;
    int body_copy_len = (int)resp_body.size;
    if (body_copy_len > 0) {
        body_copy = agl_arena_alloc(arena, resp_body.size);
        if (body_copy) memcpy(body_copy, resp_body.data, resp_body.size);
    }

    /* Parse response headers */
    AglMapVal *resp_hdr_map = parse_response_headers(
        resp_headers.data, resp_headers.size, arena, gc);

    buf_free(&resp_body);
    buf_free(&resp_headers);

    /* Build result map: {"status": int, "body": string, "headers": map} */
    AglMapVal *result_map = agl_gc_alloc(gc, sizeof(AglMapVal), map_cleanup);
    if (!result_map) return val_nil();
    result_map->count = 3;
    result_map->capacity = 3;
    result_map->keys = malloc(sizeof(char *) * 3);
    result_map->key_lengths = malloc(sizeof(int) * 3);
    result_map->values = malloc(sizeof(AglVal) * 3);
    if (!result_map->keys || !result_map->key_lengths || !result_map->values) {
        return val_nil();
    }

    result_map->keys[0] = "status";
    result_map->key_lengths[0] = 6;
    result_map->values[0] = val_int(status_code);

    result_map->keys[1] = "body";
    result_map->key_lengths[1] = 4;
    result_map->values[1] = val_string(body_copy ? body_copy : "", body_copy_len);

    result_map->keys[2] = "headers";
    result_map->key_lengths[2] = 7;
    if (resp_hdr_map) {
        result_map->values[2] = (AglVal){VAL_MAP, {.map = resp_hdr_map}};
    } else {
        result_map->values[2] = val_nil();
    }

    /* Wrap in ok Result */
    AglResultVal *rv = agl_gc_alloc(gc, sizeof(AglResultVal), NULL);
    if (!rv) return val_nil();
    rv->is_ok = true;
    rv->value = (AglVal){VAL_MAP, {.map = result_map}};
    return (AglVal){VAL_RESULT, {.result = rv}};
}

#else /* !AGL_HAS_CURL */

AglVal agl_http_request(const char *method,
                        const char *url, int url_len,
                        AglMapVal *headers,
                        const char *body, int body_len,
                        AglArena *arena, AglGc *gc) {
    (void)method; (void)url; (void)url_len;
    (void)headers; (void)body; (void)body_len;

    AglResultVal *rv = agl_gc_alloc(gc, sizeof(AglResultVal), NULL);
    if (!rv) return val_nil();
    rv->is_ok = false;
    const char *msg = "HTTP not available: libcurl required";
    int msg_len = (int)strlen(msg);
    char *msg_copy = agl_arena_alloc(arena, (size_t)msg_len);
    if (msg_copy) memcpy(msg_copy, msg, (size_t)msg_len);
    rv->value = val_string(msg_copy ? msg_copy : msg, msg_len);
    return (AglVal){VAL_RESULT, {.result = rv}};
}

#endif /* AGL_HAS_CURL */
