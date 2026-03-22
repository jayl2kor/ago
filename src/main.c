#include "common.h"
#include "error.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("ago %s\n", AGO_VERSION);
        printf("Usage: ago <file>\n");
        return 0;
    }

    AgoCtx *ctx = ago_ctx_new();
    if (!ctx) {
        fprintf(stderr, "ago: failed to allocate context\n");
        return 1;
    }

    printf("TODO: interpret %s\n", argv[1]);

    ago_ctx_free(ctx);
    return 0;
}
