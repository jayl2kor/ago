#include "ast.h"
#include "arena.h"

AgoNode *ago_ast_new(AgoArena *arena, AgoNodeKind kind, int line, int column) {
    AgoNode *node = ago_arena_alloc(arena, sizeof(AgoNode));
    if (!node) return NULL;
    memset(node, 0, sizeof(AgoNode));
    node->kind = kind;
    node->line = line;
    node->column = column;
    return node;
}
