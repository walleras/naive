/* sm_func.h  --  user-defined FUNCTION sets: stored AST + interpreter
 *
 * `CREATE f AS FUNCTION ... END` is parsed (sm_parse.c) into a statement tree
 * and stored on a per-page function map (hung off the SmWorld, so it is
 * discarded with the page). The function SET still exists in the world; the
 * map holds what it does. A function runs when an expression evaluates
 * `CALL f WITH (args)` / `f(args)` (the world's call_hook), which is also how
 * `ON CLICK CALL f WITH (...)` and `CREATE f AS FUNCTION, OnClick, Widget`
 * reach it.
 *
 * Body syntax follows Lua where Naive has no verb of its own:
 *     TAKES a, b                       parameters (sets, passed by reference)
 *     IF cond THEN .. ELSEIF cond THEN .. ELSE .. END
 *     WHILE cond DO .. END
 *     LOCAL x = expr                   a new local
 *     x = expr                         assignment (same as SET x TO expr)
 *     RETURN [expr]
 *     f(a, b)  /  CALL f WITH (a, b)   call
 * plus the Naive verbs ADD / REMOVE / SET / SWAP / MOVE / SEND. Operators
 * are the expression language of sm_expr.h (and/or/not, = == <> ~= !=, IS).
 * A number or string argument is wrapped in a fresh anonymous set; a set name
 * is passed as that very set.
 */
#ifndef SM_FUNC_H
#define SM_FUNC_H

#include "sm_doc.h"

typedef enum {
    SM_ST_ACTION,   /* act/a/b/prop/expr: one ADD/REMOVE/SET/... verb        */
    SM_ST_IF,       /* expr = condition, body = then, orelse = else chain    */
    SM_ST_WHILE,    /* expr = condition, body                                */
    SM_ST_RETURN,   /* expr = value or NULL                                  */
    SM_ST_EXPR,     /* expr evaluated for effect (a call)                    */
    SM_ST_LOCAL     /* a = name, expr = initial value or NULL                */
} SmStmtKind;

typedef struct SmStmt SmStmt;
struct SmStmt {
    SmStmtKind kind;
    int        line;
    SmActKind  act;
    char      *a;
    char      *b;
    char      *prop;
    SmExpr    *expr;
    SmStmt    *body;
    SmStmt    *orelse;
    SmStmt    *next;
};

typedef struct SmFunc {
    char   *name;
    char  **params;
    int     nparams, cparams;
    SmStmt *body;
} SmFunc;

/* --- builders (used by the parser) -------------------------------------- */
SmFunc  *sm_func_new(const char *name);
SmStatus sm_func_add_param(SmFunc *f, const char *name);
void     sm_func_free(SmFunc *f);

SmStmt  *sm_stmt_new(SmStmtKind kind, int line);
void     sm_stmt_append(SmStmt **head, SmStmt *s);   /* add at the tail */
void     sm_stmt_free(SmStmt *list);                 /* whole next-chain */

/* --- the function map --------------------------------------------------- */

/* Store `f` (the map takes ownership) under its name, replacing any previous
 * function of that name. Installs the world call hook on first use. */
SmStatus      sm_func_define(SmWorld *w, SmFunc *f);
const SmFunc *sm_func_find(SmWorld *w, const char *name);   /* case-insens. */
int           sm_func_count(SmWorld *w);

#endif /* SM_FUNC_H */
