#pragma once

#include "common.h"
#include "lexer.h"

/* ---- AST Node Kinds ---- */

typedef enum {
    /* Expressions */
    AGL_NODE_INT_LIT,       /* 42 */
    AGL_NODE_FLOAT_LIT,     /* 3.14 */
    AGL_NODE_STRING_LIT,    /* "hello" */
    AGL_NODE_BOOL_LIT,      /* true, false */
    AGL_NODE_IDENT,         /* x, foo */
    AGL_NODE_UNARY,         /* !x, -x */
    AGL_NODE_BINARY,        /* a + b */
    AGL_NODE_CALL,          /* foo(a, b) */
    AGL_NODE_INDEX,         /* arr[0] */
    AGL_NODE_ARRAY_LIT,     /* [1, 2, 3] */
    AGL_NODE_MAP_LIT,       /* {"key": value, ...} */
    AGL_NODE_STRUCT_LIT,    /* Point { x: 1, y: 2 } */
    AGL_NODE_LAMBDA,        /* fn(x: int) -> int { ... } (anonymous function) */
    AGL_NODE_RESULT_OK,     /* ok(expr) */
    AGL_NODE_RESULT_ERR,    /* err(expr) */
    AGL_NODE_MATCH_EXPR,    /* match expr { ok(n) -> expr, err(n) -> expr } */
    AGL_NODE_TRY_EXPR,     /* expr? — unwrap ok or propagate err */

    /* Statements */
    AGL_NODE_EXPR_STMT,     /* expression as statement */
    AGL_NODE_ASSIGN_STMT,   /* x = expr */
    AGL_NODE_LET_STMT,      /* let x = expr */
    AGL_NODE_VAR_STMT,      /* var x = expr */
    AGL_NODE_RETURN_STMT,   /* return expr */
    AGL_NODE_IF_STMT,       /* if cond { ... } else { ... } */
    AGL_NODE_WHILE_STMT,    /* while cond { ... } */
    AGL_NODE_FOR_STMT,      /* for item in collection { ... } */
    AGL_NODE_BREAK_STMT,    /* break */
    AGL_NODE_CONTINUE_STMT, /* continue */
    AGL_NODE_BLOCK,         /* { stmt; stmt; ... } */

    /* Top-level declarations */
    AGL_NODE_FN_DECL,       /* fn name(params) -> type { body } */
    AGL_NODE_STRUCT_DECL,   /* struct Name { fields } */

    /* Module */
    AGL_NODE_IMPORT,        /* import "path" */

    /* Program root */
    AGL_NODE_PROGRAM,       /* list of top-level statements/declarations */
} AglNodeKind;

/* ---- Forward declaration ---- */
typedef struct AglNode AglNode;

/* ---- AST Node ---- */

struct AglNode {
    AglNodeKind kind;
    int line;
    int column;

    union {
        /* AGL_NODE_INT_LIT */
        struct { int64_t value; } int_lit;

        /* AGL_NODE_FLOAT_LIT */
        struct { double value; } float_lit;

        /* AGL_NODE_STRING_LIT (start includes quotes, length includes quotes) */
        struct { const char *value; int length; } string_lit;

        /* AGL_NODE_BOOL_LIT */
        struct { bool value; } bool_lit;

        /* AGL_NODE_IDENT */
        struct { const char *name; int length; } ident;

        /* AGL_NODE_UNARY */
        struct { AglTokenKind op; AglNode *operand; } unary;

        /* AGL_NODE_BINARY */
        struct { AglTokenKind op; AglNode *left; AglNode *right; } binary;

        /* AGL_NODE_CALL */
        struct {
            AglNode *callee;
            AglNode **args;
            int arg_count;
        } call;

        /* AGL_NODE_INDEX */
        struct { AglNode *object; AglNode *index; } index_expr;

        /* AGL_NODE_ARRAY_LIT */
        struct { AglNode **elements; int count; } array_lit;

        /* AGL_NODE_MAP_LIT */
        struct {
            const char **keys;          /* arena-allocated */
            int *key_lengths;
            AglNode **values;           /* arena-allocated */
            int count;
        } map_lit;

        /* AGL_NODE_STRUCT_LIT */
        struct {
            const char *name;
            int name_length;
            const char **field_names;
            int *field_name_lengths;
            AglNode **field_values;
            int field_count;
        } struct_lit;

        /* AGL_NODE_RESULT_OK / AGL_NODE_RESULT_ERR */
        struct { AglNode *value; } result_val;

        /* AGL_NODE_MATCH_EXPR */
        struct {
            AglNode *subject;
            const char *ok_name;
            int ok_name_length;
            AglNode *ok_body;
            const char *err_name;
            int err_name_length;
            AglNode *err_body;
        } match_expr;

        /* AGL_NODE_TRY_EXPR */
        struct { AglNode *expr; } try_expr;

        /* AGL_NODE_ASSIGN_STMT */
        struct {
            const char *name;
            int name_length;
            AglNode *value;
        } assign_stmt;

        /* AGL_NODE_EXPR_STMT */
        struct { AglNode *expr; } expr_stmt;

        /* AGL_NODE_LET_STMT / AGL_NODE_VAR_STMT */
        struct {
            const char *name;
            int name_length;
            const char *type_name;   /* NULL if inferred */
            int type_name_length;
            AglNode *initializer;    /* NULL if none */
        } var_decl;

        /* AGL_NODE_RETURN_STMT */
        struct { AglNode *value; /* NULL for bare return */ } return_stmt;

        /* AGL_NODE_IF_STMT */
        struct {
            AglNode *condition;
            AglNode *then_block;
            AglNode *else_block;    /* NULL if no else */
        } if_stmt;

        /* AGL_NODE_WHILE_STMT */
        struct { AglNode *condition; AglNode *body; } while_stmt;

        /* AGL_NODE_FOR_STMT */
        struct {
            const char *var_name;
            int var_name_length;
            AglNode *iterable;
            AglNode *body;
        } for_stmt;

        /* AGL_NODE_BLOCK */
        struct { AglNode **stmts; int stmt_count; } block;

        /* AGL_NODE_FN_DECL */
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
            AglNode *body;              /* block node */
        } fn_decl;

        /* AGL_NODE_STRUCT_DECL */
        struct {
            const char *name;
            int name_length;
            const char **field_names;
            int *field_name_lengths;
            const char **field_types;
            int *field_type_lengths;
            int field_count;
        } struct_decl;

        /* AGL_NODE_IMPORT */
        struct { const char *path; int path_length; } import_stmt;

        /* AGL_NODE_PROGRAM */
        struct { AglNode **decls; int decl_count; } program;
    } as;
};

/* ---- AST construction (arena-allocated) ---- */

AglNode *agl_ast_new(AglArena *arena, AglNodeKind kind, int line, int column);
