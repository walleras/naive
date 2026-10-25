/* sm_func.c  --  FUNCTION map + statement interpreter (strict ISO C89). */
#include "sm_func.h"

#include <string.h>

/* Bounds so a runaway page cannot hang the viewer. */
#define FUNC_MAX_DEPTH 100
#define FUNC_MAX_STEPS 1000000L

/* ---- builders ----------------------------------------------------------- */

SmFunc *sm_func_new(const char *name)
{
    SmFunc *f = (SmFunc *)sm_alloc(sizeof(SmFunc));
    if (f == NULL) { return NULL; }
    f->name = sm_strdup(name);
    if (f->name == NULL) { sm_free(f); return NULL; }
    return f;
}

SmStatus sm_func_add_param(SmFunc *f, const char *name)
{
    if (f->nparams >= f->cparams) {
        int nc = (f->cparams == 0) ? 4 : f->cparams * 2;
        char **g = (char **)sm_realloc(f->params, (size_t)nc * sizeof(char *));
        if (g == NULL) { return SM_ERR_OOM; }
        f->params = g;
        f->cparams = nc;
    }
    f->params[f->nparams] = sm_strdup(name);
    if (f->params[f->nparams] == NULL) { return SM_ERR_OOM; }
    ++f->nparams;
    return SM_OK;
}

void sm_func_free(SmFunc *f)
{
    int i;
    if (f == NULL) { return; }
    for (i = 0; i < f->nparams; ++i) { sm_free(f->params[i]); }
    sm_free(f->params);
    sm_stmt_free(f->body);
    sm_free(f->name);
    sm_free(f);
}

SmStmt *sm_stmt_new(SmStmtKind kind, int line)
{
    SmStmt *s = (SmStmt *)sm_alloc(sizeof(SmStmt));
    if (s != NULL) { s->kind = kind; s->line = line; }
    return s;
}

void sm_stmt_append(SmStmt **head, SmStmt *s)
{
    SmStmt *t;
    if (s == NULL) { return; }
    if (*head == NULL) { *head = s; return; }
    for (t = *head; t->next != NULL; t = t->next) { }
    t->next = s;
}

void sm_stmt_free(SmStmt *list)
{
    while (list != NULL) {
        SmStmt *next = list->next;
        sm_free(list->a);
        sm_free(list->b);
        sm_free(list->prop);
        sm_expr_free(list->expr);
        sm_stmt_free(list->body);
        sm_stmt_free(list->orelse);
        sm_free(list);
        list = next;
    }
}

/* ---- the map ------------------------------------------------------------ */

typedef struct { SmFunc **f; int n, c; } FuncMap;

static void map_free(void *p)
{
    FuncMap *m = (FuncMap *)p;
    int i;
    if (m == NULL) { return; }
    for (i = 0; i < m->n; ++i) { sm_func_free(m->f[i]); }
    sm_free(m->f);
    sm_free(m);
}

static SmBool call_hook(SmWorld *w, const char *name, void **args, int nargs,
                        SmSet *self, SmValue *out);

static FuncMap *map_get(SmWorld *w, SmBool create)
{
    if (w->funcs == NULL && create) {
        FuncMap *m = (FuncMap *)sm_alloc(sizeof(FuncMap));
        if (m == NULL) { return NULL; }
        w->funcs = m;
        w->funcs_free = map_free;
        w->call_hook = call_hook;
    }
    return (FuncMap *)w->funcs;
}

SmStatus sm_func_define(SmWorld *w, SmFunc *f)
{
    FuncMap *m = map_get(w, SM_TRUE);
    int i;
    if (m == NULL) { sm_func_free(f); return SM_ERR_OOM; }
    for (i = 0; i < m->n; ++i) {
        if (sm_stricmp(m->f[i]->name, f->name) == 0) {
            sm_func_free(m->f[i]);
            m->f[i] = f;
            return SM_OK;
        }
    }
    if (m->n >= m->c) {
        int nc = (m->c == 0) ? 8 : m->c * 2;
        SmFunc **g = (SmFunc **)sm_realloc(m->f, (size_t)nc * sizeof(SmFunc *));
        if (g == NULL) { sm_func_free(f); return SM_ERR_OOM; }
        m->f = g;
        m->c = nc;
    }
    m->f[m->n++] = f;
    return SM_OK;
}

const SmFunc *sm_func_find(SmWorld *w, const char *name)
{
    FuncMap *m = map_get(w, SM_FALSE);
    int i;
    if (m == NULL || name == NULL) { return NULL; }
    for (i = 0; i < m->n; ++i) {
        if (sm_stricmp(m->f[i]->name, name) == 0) { return m->f[i]; }
    }
    return NULL;
}

int sm_func_count(SmWorld *w)
{
    FuncMap *m = map_get(w, SM_FALSE);
    return (m != NULL) ? m->n : 0;
}

/* ---- call frames -------------------------------------------------------- */

static SmStatus env_bind(SmEnv *env, const char *name, SmSet *set)
{
    if (env->n >= env->c) {
        int nc = (env->c == 0) ? 8 : env->c * 2;
        SmBinding *g = (SmBinding *)sm_realloc(env->b, (size_t)nc * sizeof(SmBinding));
        if (g == NULL) { return SM_ERR_OOM; }
        env->b = g;
        env->c = nc;
    }
    env->b[env->n].name = sm_strdup(name);
    if (env->b[env->n].name == NULL) { return SM_ERR_OOM; }
    env->b[env->n].set = set;
    ++env->n;
    return SM_OK;
}

static void env_free(SmEnv *env)
{
    int i;
    for (i = 0; i < env->n; ++i) { sm_free(env->b[i].name); }
    sm_free(env->b);
    env->b = NULL;
    env->n = env->c = 0;
}

/* An anonymous set that carries `v` (taking ownership of it): how a number or
 * string becomes something a parameter / LOCAL can be bound to. */
static SmSet *value_set(SmWorld *w, SmValue v)
{
    SmSet *s = sm_set_create_anon(w, SM_KIND_SET);
    if (s == NULL) { sm_value_clear(&v); return NULL; }
    sm_value_clear(&s->value);
    s->value = v;
    return s;
}

/* ---- interpreter -------------------------------------------------------- */

typedef struct {
    SmValue ret;
    SmBool  returned;
    long    steps;
} Exec;

static void exec_block(SmWorld *w, const SmStmt *s, Exec *x);

static SmBool truthy_expr(SmWorld *w, const SmExpr *e)
{
    SmValue v = sm_expr_eval(e, w, NULL);
    SmBool t = sm_value_truthy(&v);
    sm_value_clear(&v);
    return t;
}

static void exec_stmt(SmWorld *w, const SmStmt *s, Exec *x)
{
    switch (s->kind) {
    case SM_ST_ACTION: {
        SmSet *a = (s->a != NULL) ? sm_lookup(w, s->a) : NULL;
        SmSet *b = (s->b != NULL) ? sm_lookup(w, s->b) : NULL;
        SmSet *c = (s->act == SM_ACT_TRANSFER && s->prop != NULL)
                       ? sm_lookup(w, s->prop) : NULL;
        sm_doc_exec_action((SmDoc *)w->doc, s->act, a, b, c,
                           (s->act == SM_ACT_TRANSFER) ? NULL : s->prop, s->expr);
        break;
    }
    case SM_ST_IF:
        if (truthy_expr(w, s->expr)) { exec_block(w, s->body, x); }
        else { exec_block(w, s->orelse, x); }
        break;
    case SM_ST_WHILE:
        while (!x->returned && ++x->steps < FUNC_MAX_STEPS && truthy_expr(w, s->expr)) {
            exec_block(w, s->body, x);
        }
        break;
    case SM_ST_RETURN:
        sm_value_clear(&x->ret);
        x->ret = (s->expr != NULL) ? sm_expr_eval(s->expr, w, NULL) : sm_value_none();
        x->returned = SM_TRUE;
        break;
    case SM_ST_EXPR: {
        SmValue v = sm_expr_eval(s->expr, w, NULL);
        sm_value_clear(&v);
        break;
    }
    case SM_ST_LOCAL: {
        SmValue v = (s->expr != NULL) ? sm_expr_eval(s->expr, w, NULL) : sm_value_none();
        SmSet *ls = value_set(w, v);
        if (ls != NULL && w->env != NULL) { (void)env_bind(w->env, s->a, ls); }
        break;
    }
    }
}

static void exec_block(SmWorld *w, const SmStmt *s, Exec *x)
{
    for (; s != NULL && !x->returned; s = s->next) {
        if (++x->steps > FUNC_MAX_STEPS) { x->returned = SM_TRUE; return; }
        exec_stmt(w, s, x);
    }
}

static int g_depth = 0;

static SmBool call_hook(SmWorld *w, const char *name, void **args, int nargs,
                        SmSet *self, SmValue *out)
{
    const SmFunc *f = sm_func_find(w, name);
    SmExpr **argv = (SmExpr **)args;
    SmSet **bound;
    SmEnv env, *saved;
    Exec x;
    int i;

    if (f == NULL) { return SM_FALSE; }
    *out = sm_value_none();
    if (g_depth >= FUNC_MAX_DEPTH || w->doc == NULL) { return SM_TRUE; }

    /* bind arguments in the CALLER's frame: a set name is passed as that very
     * set; any other expression becomes a fresh set holding its value */
    bound = (SmSet **)sm_alloc((size_t)(f->nparams + 1) * sizeof(SmSet *));
    if (bound == NULL) { return SM_TRUE; }
    for (i = 0; i < f->nparams; ++i) {
        SmSet *s = NULL;
        if (i < nargs) {
            const char *bn = sm_expr_bare_name(argv[i]);
            if (bn != NULL) { s = sm_lookup(w, bn); }
            if (s == NULL) { s = value_set(w, sm_expr_eval(argv[i], w, self)); }
        } else {
            s = value_set(w, sm_value_none());
        }
        bound[i] = s;
    }

    memset(&env, 0, sizeof(env));
    memset(&x, 0, sizeof(x));
    x.ret = sm_value_none();
    for (i = 0; i < f->nparams; ++i) {
        if (bound[i] != NULL) { (void)env_bind(&env, f->params[i], bound[i]); }
    }
    sm_free(bound);

    saved = w->env;
    w->env = &env;
    ++g_depth;
    exec_block(w, f->body, &x);
    --g_depth;
    w->env = saved;
    env_free(&env);

    *out = x.ret;
    return SM_TRUE;
}
