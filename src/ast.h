#pragma once

#include "common.h"
#include "lexer.h"

/* ---- AST Node Kinds ---- */

typedef enum {
    /* Expressions */
    AGO_NODE_INT_LIT,       /* 42 */
    AGO_NODE_FLOAT_LIT,     /* 3.14 */
    AGO_NODE_STRING_LIT,    /* "hello" */
    AGO_NODE_BOOL_LIT,      /* true, false */
    AGO_NODE_IDENT,         /* x, foo */
    AGO_NODE_UNARY,         /* !x, -x */
    AGO_NODE_BINARY,        /* a + b */
    AGO_NODE_CALL,          /* foo(a, b) */
    AGO_NODE_ARRAY_LIT,     /* [1, 2, 3] */
    AGO_NODE_STRUCT_LIT,    /* Point { x: 1, y: 2 } */

    /* Statements */
    AGO_NODE_EXPR_STMT,     /* expression as statement */
    AGO_NODE_ASSIGN_STMT,   /* x = expr */
    AGO_NODE_LET_STMT,      /* let x = expr */
    AGO_NODE_VAR_STMT,      /* var x = expr */
    AGO_NODE_RETURN_STMT,   /* return expr */
    AGO_NODE_IF_STMT,       /* if cond { ... } else { ... } */
    AGO_NODE_WHILE_STMT,    /* while cond { ... } */
    AGO_NODE_FOR_STMT,      /* for item in collection { ... } */
    AGO_NODE_BLOCK,         /* { stmt; stmt; ... } */

    /* Top-level declarations */
    AGO_NODE_FN_DECL,       /* fn name(params) -> type { body } */
    AGO_NODE_STRUCT_DECL,   /* struct Name { fields } */

    /* Program root */
    AGO_NODE_PROGRAM,       /* list of top-level statements/declarations */
} AgoNodeKind;

/* ---- Forward declaration ---- */
typedef struct AgoNode AgoNode;

/* ---- AST Node ---- */

struct AgoNode {
    AgoNodeKind kind;
    int line;
    int column;

    union {
        /* AGO_NODE_INT_LIT */
        struct { int64_t value; } int_lit;

        /* AGO_NODE_FLOAT_LIT */
        struct { double value; } float_lit;

        /* AGO_NODE_STRING_LIT (start includes quotes, length includes quotes) */
        struct { const char *value; int length; } string_lit;

        /* AGO_NODE_BOOL_LIT */
        struct { bool value; } bool_lit;

        /* AGO_NODE_IDENT */
        struct { const char *name; int length; } ident;

        /* AGO_NODE_UNARY */
        struct { AgoTokenKind op; AgoNode *operand; } unary;

        /* AGO_NODE_BINARY */
        struct { AgoTokenKind op; AgoNode *left; AgoNode *right; } binary;

        /* AGO_NODE_CALL */
        struct {
            AgoNode *callee;
            AgoNode **args;
            int arg_count;
        } call;

        /* AGO_NODE_ASSIGN_STMT */
        struct {
            const char *name;
            int name_length;
            AgoNode *value;
        } assign_stmt;

        /* AGO_NODE_EXPR_STMT */
        struct { AgoNode *expr; } expr_stmt;

        /* AGO_NODE_LET_STMT / AGO_NODE_VAR_STMT */
        struct {
            const char *name;
            int name_length;
            const char *type_name;   /* NULL if inferred */
            int type_name_length;
            AgoNode *initializer;    /* NULL if none */
        } var_decl;

        /* AGO_NODE_RETURN_STMT */
        struct { AgoNode *value; /* NULL for bare return */ } return_stmt;

        /* AGO_NODE_IF_STMT */
        struct {
            AgoNode *condition;
            AgoNode *then_block;
            AgoNode *else_block;    /* NULL if no else */
        } if_stmt;

        /* AGO_NODE_WHILE_STMT */
        struct { AgoNode *condition; AgoNode *body; } while_stmt;

        /* AGO_NODE_FOR_STMT */
        struct {
            const char *var_name;
            int var_name_length;
            AgoNode *iterable;
            AgoNode *body;
        } for_stmt;

        /* AGO_NODE_BLOCK */
        struct { AgoNode **stmts; int stmt_count; } block;

        /* AGO_NODE_FN_DECL */
        struct {
            const char *name;
            int name_length;
            const char **param_names;
            int *param_name_lengths;
            const char **param_types;
            int *param_type_lengths;
            int param_count;
            const char *return_type;    /* NULL if void */
            int return_type_length;
            AgoNode *body;              /* block node */
        } fn_decl;

        /* AGO_NODE_STRUCT_DECL */
        struct {
            const char *name;
            int name_length;
            const char **field_names;
            int *field_name_lengths;
            const char **field_types;
            int *field_type_lengths;
            int field_count;
        } struct_decl;

        /* AGO_NODE_PROGRAM */
        struct { AgoNode **decls; int decl_count; } program;
    } as;
};

/* ---- AST construction (arena-allocated) ---- */

AgoNode *ago_ast_new(AgoArena *arena, AgoNodeKind kind, int line, int column);
