#include "runtime.h"
#include "parser.h"
#include "sema.h"

/* ---- Path helpers ---- */

/* Extract directory from a file path. Returns "" for bare filenames. */
void path_dir(const char *filepath, char *buf, size_t bufsize) {
    const char *last_sep = NULL;
    for (const char *p = filepath; *p; p++) {
        if (*p == '/') last_sep = p;
    }
    if (!last_sep) {
        buf[0] = '\0';
        return;
    }
    size_t len = (size_t)(last_sep - filepath);
    if (len >= bufsize) len = bufsize - 1;
    memcpy(buf, filepath, len);
    buf[len] = '\0';
}

/* Resolve import path relative to current file. Appends .ago extension.
 * Returns false if path escapes the base directory (path traversal). */
bool resolve_import_path(const char *base_file, const char *import_path,
                                int import_len, char *buf, size_t bufsize) {
    /* Reject paths containing ".." to prevent directory traversal */
    for (int i = 0; i < import_len - 1; i++) {
        if (import_path[i] == '.' && import_path[i + 1] == '.') return false;
    }

    char dir[512];
    path_dir(base_file, dir, sizeof(dir));
    int written;
    if (dir[0]) {
        written = snprintf(buf, bufsize, "%s/%.*s.ago", dir, import_len, import_path);
    } else {
        written = snprintf(buf, bufsize, "%.*s.ago", import_len, import_path);
    }
    if (written < 0 || (size_t)written >= bufsize) return false;

    /* Canonicalize and verify path stays within base directory */
    char real_base[PATH_MAX], real_resolved[PATH_MAX];
    if (!realpath(dir[0] ? dir : ".", real_base)) return false;
    if (!realpath(buf, real_resolved)) return false;
    size_t base_len = strlen(real_base);
    if (strncmp(real_resolved, real_base, base_len) != 0) return false;
    if (real_resolved[base_len] != '/' && real_resolved[base_len] != '\0') return false;
    return true;
}

/* Read entire file into malloc'd buffer. Returns NULL on failure. */
char *ago_read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t nread = fread(buf, 1, (size_t)len, f);
    buf[nread] = '\0';
    fclose(f);
    return buf;
}

/* ---- Module cache ---- */

static bool module_loaded(AgoInterp *interp, const char *path) {
    for (int i = 0; i < interp->module_count; i++) {
        if (strcmp(interp->modules[i].path, path) == 0) return true;
    }
    return false;
}

static bool module_register(AgoInterp *interp, const char *path,
                            char *source, AgoArena *arena) {
    if (interp->module_count >= MAX_MODULES) return false;
    AgoModule *m = &interp->modules[interp->module_count++];
    m->path = strdup(path);
    m->source = source;
    m->arena = arena;
    return true;
}

void module_cache_free(AgoInterp *interp) {
    for (int i = 0; i < interp->module_count; i++) {
        free(interp->modules[i].path);
        free(interp->modules[i].source);
        ago_arena_free(interp->modules[i].arena);
    }
}

/* ---- Import execution ---- */

void exec_import(AgoInterp *interp, AgoNode *node) {
    /* Resolve path relative to current file — rejects path traversal */
    char resolved[512];
    if (!resolve_import_path(interp->file,
                             node->as.import_stmt.path,
                             node->as.import_stmt.path_length,
                             resolved, sizeof(resolved))) {
        ago_error_set(interp->ctx, AGO_ERR_IO,
                      ago_loc(NULL, node->line, node->column),
                      "invalid import path '%.*s'",
                      node->as.import_stmt.path_length,
                      node->as.import_stmt.path);
        return;
    }

    /* Skip if already loaded */
    if (module_loaded(interp, resolved)) return;

    /* Read module file */
    char *mod_source = ago_read_file(resolved);
    if (!mod_source) {
        ago_error_set(interp->ctx, AGO_ERR_IO,
                      ago_loc(NULL, node->line, node->column),
                      "cannot open module '%.*s'",
                      node->as.import_stmt.path_length,
                      node->as.import_stmt.path);
        return;
    }

    /* Parse module */
    AgoArena *mod_arena = ago_arena_new();
    if (!mod_arena) { free(mod_source); return; }

    AgoParser mod_parser;
    ago_parser_init(&mod_parser, mod_source, resolved, mod_arena, interp->ctx);
    AgoNode *mod_program = ago_parser_parse(&mod_parser);

    if (!mod_program || ago_error_occurred(interp->ctx)) {
        ago_arena_free(mod_arena);
        free(mod_source);
        return;
    }

    /* Run sema on module (skip if it has imports — imported names unavailable) */
    bool mod_has_imports = false;
    for (int i = 0; i < mod_program->as.program.decl_count; i++) {
        if (mod_program->as.program.decls[i] &&
            mod_program->as.program.decls[i]->kind == AGO_NODE_IMPORT) {
            mod_has_imports = true;
            break;
        }
    }
    if (!mod_has_imports) {
        AgoSema *mod_sema = ago_sema_new(interp->ctx, mod_arena);
        if (mod_sema) {
            ago_sema_check(mod_sema, mod_program);
            ago_sema_free(mod_sema);
        }
        if (ago_error_occurred(interp->ctx)) {
            ago_arena_free(mod_arena);
            free(mod_source);
            return;
        }
    }

    /* Register module before execution to prevent circular imports */
    if (!module_register(interp, resolved, mod_source, mod_arena)) {
        ago_arena_free(mod_arena);
        free(mod_source);
        ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                      ago_loc(NULL, node->line, node->column),
                      "too many modules (max %d)", MAX_MODULES);
        return;
    }

    /* Execute module in current interpreter (shares env, gc) */
    const char *saved_file = interp->file;
    interp->file = resolved;
    for (int i = 0; i < mod_program->as.program.decl_count; i++) {
        exec_stmt(interp, mod_program->as.program.decls[i]);
        if (ago_error_occurred(interp->ctx)) break;
    }
    interp->file = saved_file;
}
