#include "common.h"
#include "error.h"
#include "interpreter.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("ago %s\n", AGO_VERSION);
        printf("Usage: ago <file>\n");
        return 0;
    }

    /* Read source file */
    FILE *f = fopen(argv[1], "r");
    if (!f) {
        fprintf(stderr, "ago: cannot open '%s'\n", argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0) {
        fprintf(stderr, "ago: cannot read '%s'\n", argv[1]);
        fclose(f);
        return 1;
    }
    fseek(f, 0, SEEK_SET);
    char *source = malloc((size_t)len + 1);
    if (!source) { fclose(f); return 1; }
    size_t read_len = fread(source, 1, (size_t)len, f);
    source[read_len] = '\0';
    fclose(f);

    /* Run */
    AgoCtx *ctx = ago_ctx_new();
    if (!ctx) { free(source); return 1; }

    int result = ago_run(source, argv[1], ctx);

    if (ago_error_occurred(ctx)) {
        ago_error_print(ago_error_get(ctx));
        result = 1;
    }

    ago_ctx_free(ctx);
    free(source);
    return result;
}
