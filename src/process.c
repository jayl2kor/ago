#include "process.h"
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

/* ---- Read all data from a file descriptor into an arena-allocated buffer ---- */

static char *read_fd_to_arena(int fd, int *out_len, AglArena *arena) {
    /* Read into a temporary heap buffer, then copy to arena */
    size_t capacity = 4096;
    size_t used = 0;
    char *tmp = malloc(capacity);
    if (!tmp) { *out_len = 0; return NULL; }

    for (;;) {
        if (used >= capacity) {
            capacity *= 2;
            char *new_tmp = realloc(tmp, capacity);
            if (!new_tmp) { free(tmp); *out_len = 0; return NULL; }
            tmp = new_tmp;
        }
        ssize_t n = read(fd, tmp + used, capacity - used);
        if (n <= 0) break;
        used += (size_t)n;
    }

    *out_len = (int)used;
    if (used == 0) {
        free(tmp);
        return NULL;
    }

    char *result = agl_arena_alloc(arena, used);
    if (result) {
        memcpy(result, tmp, used);
    }
    free(tmp);
    return result;
}

/* ---- Public API ---- */

AglVal agl_exec(const char *cmd, int cmd_len,
                AglArrayVal *args,
                AglArena *arena, AglGc *gc) {
    /* Build null-terminated command string */
    char *cmd_z = malloc((size_t)cmd_len + 1);
    if (!cmd_z) {
        AglResultVal *rv = agl_gc_alloc(gc, sizeof(AglResultVal), NULL);
        if (!rv) return val_nil();
        rv->is_ok = false;
        rv->value = val_string("out of memory", 13);
        return (AglVal){VAL_RESULT, {.result = rv}};
    }
    memcpy(cmd_z, cmd, (size_t)cmd_len);
    cmd_z[cmd_len] = '\0';

    /* Build argv array: [cmd, arg0, arg1, ..., NULL] */
    int argc = args ? args->count : 0;
    char **argv = malloc(sizeof(char *) * (size_t)(argc + 2));
    if (!argv) {
        free(cmd_z);
        AglResultVal *rv = agl_gc_alloc(gc, sizeof(AglResultVal), NULL);
        if (!rv) return val_nil();
        rv->is_ok = false;
        rv->value = val_string("out of memory", 13);
        return (AglVal){VAL_RESULT, {.result = rv}};
    }
    argv[0] = cmd_z;

    /* Convert each argument to a null-terminated string */
    for (int i = 0; i < argc; i++) {
        if (args->elements[i].kind == VAL_STRING) {
            int slen;
            const char *sdata = str_content(args->elements[i], &slen);
            char *arg_z = malloc((size_t)slen + 1);
            if (!arg_z) {
                /* Cleanup previously allocated args */
                for (int j = 0; j < i; j++) free(argv[j + 1]);
                free(argv);
                free(cmd_z);
                AglResultVal *rv = agl_gc_alloc(gc, sizeof(AglResultVal), NULL);
                if (!rv) return val_nil();
                rv->is_ok = false;
                rv->value = val_string("out of memory", 13);
                return (AglVal){VAL_RESULT, {.result = rv}};
            }
            memcpy(arg_z, sdata, (size_t)slen);
            arg_z[slen] = '\0';
            argv[i + 1] = arg_z;
        } else {
            argv[i + 1] = strdup("");
        }
    }
    argv[argc + 1] = NULL;

    /* Create pipes for stdout and stderr */
    int stdout_pipe[2];
    int stderr_pipe[2];
    if (pipe(stdout_pipe) != 0) {
        for (int i = 0; i < argc; i++) free(argv[i + 1]);
        free(argv); free(cmd_z);
        AglResultVal *rv = agl_gc_alloc(gc, sizeof(AglResultVal), NULL);
        if (!rv) return val_nil();
        rv->is_ok = false;
        rv->value = val_string("failed to create pipes", 22);
        return (AglVal){VAL_RESULT, {.result = rv}};
    }
    if (pipe(stderr_pipe) != 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        for (int i = 0; i < argc; i++) free(argv[i + 1]);
        free(argv); free(cmd_z);
        AglResultVal *rv = agl_gc_alloc(gc, sizeof(AglResultVal), NULL);
        if (!rv) return val_nil();
        rv->is_ok = false;
        rv->value = val_string("failed to create pipes", 22);
        return (AglVal){VAL_RESULT, {.result = rv}};
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        for (int i = 0; i < argc; i++) free(argv[i + 1]);
        free(argv);
        free(cmd_z);
        AglResultVal *rv = agl_gc_alloc(gc, sizeof(AglResultVal), NULL);
        if (!rv) return val_nil();
        rv->is_ok = false;
        rv->value = val_string("fork failed", 11);
        return (AglVal){VAL_RESULT, {.result = rv}};
    }

    if (pid == 0) {
        /* Child process */
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        execvp(cmd_z, argv);
        /* If execvp returns, it failed */
        _exit(127);
    }

    /* Parent process */
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    /* Read stdout and stderr */
    int out_len = 0;
    char *out_data = read_fd_to_arena(stdout_pipe[0], &out_len, arena);
    close(stdout_pipe[0]);

    int err_len = 0;
    char *err_data = read_fd_to_arena(stderr_pipe[0], &err_len, arena);
    close(stderr_pipe[0]);

    /* Wait for child */
    int wstatus = 0;
    waitpid(pid, &wstatus, 0);

    /* Free argv */
    for (int i = 0; i < argc; i++) free(argv[i + 1]);
    free(argv);
    free(cmd_z);

    int exit_code = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1;

    /* Build result map: {"stdout": string, "stderr": string, "status": int} */
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

    result_map->keys[0] = "stdout";
    result_map->key_lengths[0] = 6;
    result_map->values[0] = val_string(out_data ? out_data : "", out_len);

    result_map->keys[1] = "stderr";
    result_map->key_lengths[1] = 6;
    result_map->values[1] = val_string(err_data ? err_data : "", err_len);

    result_map->keys[2] = "status";
    result_map->key_lengths[2] = 6;
    result_map->values[2] = val_int(exit_code);

    /* Wrap in ok Result */
    AglResultVal *rv = agl_gc_alloc(gc, sizeof(AglResultVal), NULL);
    if (!rv) return val_nil();
    rv->is_ok = true;
    rv->value = (AglVal){VAL_MAP, {.map = result_map}};
    return (AglVal){VAL_RESULT, {.result = rv}};
}
