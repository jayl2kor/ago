#include "ast.h"
#include "arena.h"

AglNode *agl_ast_new(AglArena *arena, AglNodeKind kind, int line, int column) {
    AglNode *node = agl_arena_alloc(arena, sizeof(AglNode));
    if (!node) return NULL;
    memset(node, 0, sizeof(AglNode));
    node->kind = kind;
    node->line = line;
    node->column = column;
    return node;
}
