/* sm_expr.c  --  SeSaML expression parser + evaluator. */
#include "sm_expr.h"
#include "sm_lex.h"

#include <string.h>
#include <stdlib.h>

/* ---- AST ---------------------------------------------------------------- */

typedef enum {
    EX_NUMBER, EX_STRING, EX_COLOR,
    EX_NAME, EX_DOT,
    EX_NEG, EX_NOT,
    EX_BIN, EX_AND, EX_OR,
    EX_CALL,
    EX_IS, EX_ISEMPTY
} ExKind;

typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_LT, OP_GT, OP_LE, OP_GE, OP_EQ, OP_NE, OP_CONCAT
} BinOp;

struct SmExpr {
    ExKind        kind;
    double        number;   /* EX_NUMBER                                     */
    unsigned long color;    /* EX_COLOR                                      */
    char         *name;     /* EX_STRING/NAME/DOT-owner/CALL-fn/IS-left      */
    char         *prop;     /* EX_DOT prop / EX_IS right name                */
    BinOp         op;       /* EX_BIN                                        */
    SmBool        negate;   /* EX_IS / EX_ISEMPTY                            */
    SmExpr       *a;
    SmExpr       *b;
    SmExpr      **args;     /* EX_CALL                                       */
    int           nargs;
};

static SmExpr *ex_new(ExKind k)
{
    SmExpr *e = (SmExpr *)sm_alloc(sizeof(SmExpr));
    if (e != NULL) {
        e->kind = k;
    }
    return e;
}

void sm_expr_free(SmExpr *e)
{
    int i;
    if (e == NULL) {
        return;
    }
    sm_free(e->name);
    sm_free(e->prop);
    sm_expr_free(e->a);
    sm_expr_free(e->b);
    for (i = 0; i < e->nargs; ++i) {
        sm_expr_free(e->args[i]);
    }
    sm_free(e->args);
    sm_free(e);
}

static void collect(const SmExpr *e, SmWorld *w, const char **out, int cap, int *n)
{
    int i;
    if (e == NULL) { return; }
    switch (e->kind) {
    case EX_NAME:
        if (e->name != NULL) {
            if (*n < cap) { out[*n] = e->name; }
            ++(*n);
        }
        break;
    case EX_DOT: {
        /* Owner.member: ordinarily a dependency on `owner` itself (e.g.
         * A1.color still depends on A1). But if `owner` is a Namespace, the
         * real dependency is the specific MEMBER named e->prop -- that is the
         * cell delta propagation actually needs to reach, same idea as the
         * retired Webcel's own dependency wiring, just resolved through a
         * namespace instead of a bare global name. */
        SmSet *owner = (w != NULL && e->name != NULL) ? sm_find(w, e->name) : NULL;
        const char *emit = (owner != NULL && owner->kind == SM_KIND_NAMESPACE &&
                            e->prop != NULL) ? e->prop : e->name;
        if (emit != NULL) {
            if (*n < cap) { out[*n] = emit; }
            ++(*n);
        }
        break;
    }
    case EX_IS:
        /* left operand (e->a) is a name expr; right name is e->prop */
        if (e->prop != NULL) {
            if (*n < cap) { out[*n] = e->prop; }
            ++(*n);
        }
        break;
    default:
        break;
    }
    collect(e->a, w, out, cap, n);
    collect(e->b, w, out, cap, n);
    for (i = 0; i < e->nargs; ++i) {
        collect(e->args[i], w, out, cap, n);
    }
}

int sm_expr_collect_names(const SmExpr *e, SmWorld *w, const char **out, int cap)
{
    int n = 0;
    collect(e, w, out, cap, &n);
    return n;
}

/* ---- parser ------------------------------------------------------------- */

typedef struct {
    SmLexer lx;
    SmBool  failed;
    char   *err;
    size_t  errcap;
} P;

static void perr(P *p, const char *msg)
{
    if (p->failed) {
        return; /* keep the first error */
    }
    p->failed = SM_TRUE;
    if (p->err != NULL && p->errcap > 0) {
        size_t n = strlen(msg);
        if (n >= p->errcap) {
            n = p->errcap - 1;
        }
        memcpy(p->err, msg, n);
        p->err[n] = '\0';
    }
}

static SmToken *cur(P *p) { return &p->lx.cur; }

static void adv(P *p)
{
    if (sm_lex_next(&p->lx) != SM_OK) {
        perr(p, p->lx.errmsg[0] ? p->lx.errmsg : "lex error");
    }
}

static SmBool at_end(P *p)
{
    SmTokType t = cur(p)->type;
    return (t == SM_TOK_EOF || t == SM_TOK_NEWLINE) ? SM_TRUE : SM_FALSE;
}

static SmExpr *parse_expr(P *p);

/* '(' [expr {, expr}] ')' -> the args of EX_CALL node `e`; cur is '(' */
static SmBool parse_call_args(P *p, SmExpr *e)
{
    adv(p); /* consume '(' */
    if (cur(p)->type != SM_TOK_RPAREN) {
        for (;;) {
            SmExpr *arg = parse_expr(p);
            SmExpr **grown;
            if (arg == NULL) { return SM_FALSE; }
            grown = (SmExpr **)sm_realloc(e->args,
                        (size_t)(e->nargs + 1) * sizeof(SmExpr *));
            if (grown == NULL) {
                perr(p, "out of memory");
                sm_expr_free(arg);
                return SM_FALSE;
            }
            e->args = grown;
            e->args[e->nargs++] = arg;
            if (cur(p)->type == SM_TOK_COMMA) { adv(p); continue; }
            break;
        }
    }
    if (cur(p)->type != SM_TOK_RPAREN) {
        perr(p, "expected ')' to close call arguments");
        return SM_FALSE;
    }
    adv(p);
    return SM_TRUE;
}

/* CALL fn WITH ( args ); a bare "CALL fn" with nothing after it calls with no
 * arguments. */
static SmExpr *parse_call(P *p)
{
    SmExpr *e;
    adv(p); /* consume CALL */
    if (cur(p)->type != SM_TOK_IDENT) {
        perr(p, "expected function name after CALL");
        return NULL;
    }
    e = ex_new(EX_CALL);
    if (e == NULL) { perr(p, "out of memory"); return NULL; }
    e->name = sm_tok_dup(cur(p));
    adv(p);
    if (at_end(p)) { return e; }
    if (!sm_tok_is_kw(cur(p), "WITH")) {
        perr(p, "expected WITH after function name");
        sm_expr_free(e);
        return NULL;
    }
    adv(p);
    if (cur(p)->type != SM_TOK_LPAREN) {
        perr(p, "expected '(' after WITH");
        sm_expr_free(e);
        return NULL;
    }
    if (!parse_call_args(p, e)) { sm_expr_free(e); return NULL; }
    return e;
}

/* a name reference, possibly dotted, possibly followed by IS ... */
static SmExpr *parse_name(P *p)
{
    SmExpr *e;
    char *owner = sm_tok_dup(cur(p));
    if (owner == NULL) { perr(p, "out of memory"); return NULL; }
    adv(p);

    if (cur(p)->type == SM_TOK_LPAREN) {   /* Lua-style call: fn(args) */
        e = ex_new(EX_CALL);
        if (e == NULL) { perr(p, "out of memory"); sm_free(owner); return NULL; }
        e->name = owner;
        if (!parse_call_args(p, e)) { sm_expr_free(e); return NULL; }
        return e;
    }

    if (cur(p)->type == SM_TOK_DOT) {
        adv(p);
        if (cur(p)->type != SM_TOK_IDENT) {
            perr(p, "expected property name after '.'");
            sm_free(owner);
            return NULL;
        }
        e = ex_new(EX_DOT);
        if (e == NULL) { perr(p, "out of memory"); sm_free(owner); return NULL; }
        e->name = owner;
        e->prop = sm_tok_dup(cur(p));
        adv(p);
    } else {
        e = ex_new(EX_NAME);
        if (e == NULL) { perr(p, "out of memory"); sm_free(owner); return NULL; }
        e->name = owner;
    }

    /* is-expr:  <name> IS [NOT] (EMPTY | <name>) */
    if (sm_tok_is_kw(cur(p), "IS")) {
        SmBool neg = SM_FALSE;
        SmExpr *isx;
        adv(p);
        if (sm_tok_is_kw(cur(p), "NOT")) { neg = SM_TRUE; adv(p); }
        if (sm_tok_is_kw(cur(p), "EMPTY")) {
            adv(p);
            isx = ex_new(EX_ISEMPTY);
            if (isx == NULL) { perr(p, "out of memory"); sm_expr_free(e); return NULL; }
            isx->a = e;
            isx->negate = neg;
            return isx;
        }
        if (cur(p)->type != SM_TOK_IDENT) {
            perr(p, "expected a set name or EMPTY after IS");
            sm_expr_free(e);
            return NULL;
        }
        isx = ex_new(EX_IS);
        if (isx == NULL) { perr(p, "out of memory"); sm_expr_free(e); return NULL; }
        isx->a = e;
        isx->negate = neg;
        isx->prop = sm_tok_dup(cur(p));
        adv(p);
        return isx;
    }
    return e;
}

static SmExpr *parse_primary(P *p)
{
    SmToken *t = cur(p);
    SmExpr *e;

    switch (t->type) {
    case SM_TOK_NUMBER:
        e = ex_new(EX_NUMBER);
        if (e == NULL) { perr(p, "out of memory"); return NULL; }
        e->number = t->number;
        adv(p);
        return e;
    case SM_TOK_STRING:
        e = ex_new(EX_STRING);
        if (e == NULL) { perr(p, "out of memory"); return NULL; }
        e->name = sm_tok_dup(t);
        adv(p);
        return e;
    case SM_TOK_COLOR:
        e = ex_new(EX_COLOR);
        if (e == NULL) { perr(p, "out of memory"); return NULL; }
        e->color = t->color;
        adv(p);
        return e;
    case SM_TOK_LPAREN:
        adv(p);
        e = parse_expr(p);
        if (e == NULL) { return NULL; }
        if (cur(p)->type != SM_TOK_RPAREN) {
            perr(p, "expected ')'");
            sm_expr_free(e);
            return NULL;
        }
        adv(p);
        return e;
    case SM_TOK_IDENT:
        if (sm_tok_is_kw(t, "CALL")) {
            return parse_call(p);
        }
        return parse_name(p);
    default:
        perr(p, "expected an expression");
        return NULL;
    }
}

static SmExpr *parse_unary(P *p)
{
    if (cur(p)->type == SM_TOK_MINUS) {
        SmExpr *e, *sub;
        adv(p);
        sub = parse_unary(p);
        if (sub == NULL) { return NULL; }
        e = ex_new(EX_NEG);
        if (e == NULL) { perr(p, "out of memory"); sm_expr_free(sub); return NULL; }
        e->a = sub;
        return e;
    }
    return parse_primary(p);
}

/* generic left-associative binary level */
static SmExpr *bin_level(P *p, SmExpr *(*sub)(P *), const SmTokType *toks,
                         const BinOp *ops, int ntok)
{
    SmExpr *left = sub(p);
    if (left == NULL) { return NULL; }
    for (;;) {
        int i, matched = -1;
        for (i = 0; i < ntok; ++i) {
            if (cur(p)->type == toks[i]) { matched = i; break; }
        }
        if (matched < 0) { break; }
        adv(p);
        {
            SmExpr *right = sub(p);
            SmExpr *node;
            if (right == NULL) { sm_expr_free(left); return NULL; }
            node = ex_new(EX_BIN);
            if (node == NULL) {
                perr(p, "out of memory");
                sm_expr_free(left); sm_expr_free(right); return NULL;
            }
            node->op = ops[matched];
            node->a = left;
            node->b = right;
            left = node;
        }
    }
    return left;
}

static SmExpr *parse_mul(P *p)
{
    static const SmTokType toks[3] = { SM_TOK_STAR, SM_TOK_SLASH, SM_TOK_PERCENT };
    static const BinOp ops[3] = { OP_MUL, OP_DIV, OP_MOD };
    return bin_level(p, parse_unary, toks, ops, 3);
}

static SmExpr *parse_add(P *p)
{
    static const SmTokType toks[2] = { SM_TOK_PLUS, SM_TOK_MINUS };
    static const BinOp ops[2] = { OP_ADD, OP_SUB };
    return bin_level(p, parse_mul, toks, ops, 2);
}

/* a .. b : string concatenation, looser than + - and tighter than comparison
 * (Lua's precedence) */
static SmExpr *parse_concat(P *p)
{
    static const SmTokType toks[1] = { SM_TOK_CONCAT };
    static const BinOp ops[1] = { OP_CONCAT };
    return bin_level(p, parse_add, toks, ops, 1);
}

static SmExpr *parse_rel(P *p)
{
    static const SmTokType toks[4] = { SM_TOK_LT, SM_TOK_GT, SM_TOK_LE, SM_TOK_GE };
    static const BinOp ops[4] = { OP_LT, OP_GT, OP_LE, OP_GE };
    return bin_level(p, parse_concat, toks, ops, 4);
}

static SmExpr *parse_eq(P *p)
{
    static const SmTokType toks[2] = { SM_TOK_EQ, SM_TOK_NE };
    static const BinOp ops[2] = { OP_EQ, OP_NE };
    return bin_level(p, parse_rel, toks, ops, 2);
}

static SmExpr *parse_not(P *p)
{
    if (sm_tok_is_kw(cur(p), "NOT")) {
        SmExpr *e, *sub;
        adv(p);
        sub = parse_not(p);
        if (sub == NULL) { return NULL; }
        e = ex_new(EX_NOT);
        if (e == NULL) { perr(p, "out of memory"); sm_expr_free(sub); return NULL; }
        e->a = sub;
        return e;
    }
    return parse_eq(p);
}

static SmExpr *parse_and(P *p)
{
    SmExpr *left = parse_not(p);
    if (left == NULL) { return NULL; }
    while (sm_tok_is_kw(cur(p), "AND")) {
        SmExpr *right, *node;
        adv(p);
        right = parse_not(p);
        if (right == NULL) { sm_expr_free(left); return NULL; }
        node = ex_new(EX_AND);
        if (node == NULL) { perr(p, "out of memory");
            sm_expr_free(left); sm_expr_free(right); return NULL; }
        node->a = left; node->b = right; left = node;
    }
    return left;
}

static SmExpr *parse_expr(P *p)
{
    SmExpr *left = parse_and(p);
    if (left == NULL) { return NULL; }
    while (sm_tok_is_kw(cur(p), "OR")) {
        SmExpr *right, *node;
        adv(p);
        right = parse_and(p);
        if (right == NULL) { sm_expr_free(left); return NULL; }
        node = ex_new(EX_OR);
        if (node == NULL) { perr(p, "out of memory");
            sm_expr_free(left); sm_expr_free(right); return NULL; }
        node->a = left; node->b = right; left = node;
    }
    return left;
}

SmStatus sm_expr_parse(const char *src, SmExpr **out, char *errbuf, size_t errcap)
{
    P p;
    SmExpr *e;
    if (out == NULL) {
        return SM_ERR_NOT_FOUND;
    }
    *out = NULL;
    p.failed = SM_FALSE;
    p.err = errbuf;
    p.errcap = errcap;
    if (errbuf != NULL && errcap > 0) { errbuf[0] = '\0'; }
    sm_lex_init(&p.lx, src);

    e = parse_expr(&p);
    if (e == NULL || p.failed) {
        sm_expr_free(e);
        if (!p.failed) { perr(&p, "empty expression"); }
        return SM_ERR_PARSE;
    }
    if (!at_end(&p)) {
        perr(&p, "unexpected trailing tokens in expression");
        sm_expr_free(e);
        return SM_ERR_PARSE;
    }
    *out = e;
    return SM_OK;
}

/* ---- evaluation --------------------------------------------------------- */

static double to_number(const SmValue *v)
{
    if (v == NULL) { return 0.0; }
    switch (v->kind) {
    case SM_VAL_NUMBER: return v->number;
    case SM_VAL_STRING: return (v->string != NULL) ? strtod(v->string, NULL) : 0.0;
    case SM_VAL_COLOR:  return (double)v->color;
    case SM_VAL_NONE:
    case SM_VAL_SET:    return 0.0;
    }
    return 0.0;
}

static SmValue num(double d) { return sm_value_number(d); }

/* Resolve an expression that is expected to denote a set (for IS / CALL). */
static SmSet *resolve_set(const SmExpr *e, SmWorld *w)
{
    if (e == NULL) { return NULL; }
    if (e->kind == EX_NAME) { return sm_lookup(w, e->name); }
    return NULL;
}

static SmValue eval_call(const SmExpr *e, SmWorld *w, SmSet *self)
{
    SmSet *target;
    double acc;
    int i, count;

    if (sm_stricmp(e->name, "COUNT") != 0 && sm_stricmp(e->name, "SUM") != 0 &&
        sm_stricmp(e->name, "AVG") != 0 && sm_stricmp(e->name, "MIN") != 0 &&
        sm_stricmp(e->name, "MAX") != 0) {
        /* not a built-in aggregate: a user-defined FUNCTION */
        SmValue out;
        if (w != NULL && w->call_hook != NULL &&
            w->call_hook(w, e->name, (void **)e->args, e->nargs, self, &out)) {
            return out;
        }
        return sm_value_none();
    }

    if (e->nargs < 1) { return sm_value_none(); }
    target = resolve_set(e->args[0], w);
    if (target == NULL) { return sm_value_none(); }

    count = target->nmembers;
    if (sm_stricmp(e->name, "COUNT") == 0) {
        return num((double)count);
    }
    if (count == 0) { return sm_value_none(); }

    if (sm_stricmp(e->name, "SUM") == 0 || sm_stricmp(e->name, "AVG") == 0) {
        acc = 0.0;
        for (i = 0; i < count; ++i) {
            acc += to_number(&target->members[i]->value);
        }
        if (sm_stricmp(e->name, "AVG") == 0) { acc /= (double)count; }
        return num(acc);
    }
    if (sm_stricmp(e->name, "MIN") == 0 || sm_stricmp(e->name, "MAX") == 0) {
        SmBool wantmax = (sm_stricmp(e->name, "MAX") == 0) ? SM_TRUE : SM_FALSE;
        acc = to_number(&target->members[0]->value);
        for (i = 1; i < count; ++i) {
            double v = to_number(&target->members[i]->value);
            if ((wantmax && v > acc) || (!wantmax && v < acc)) { acc = v; }
        }
        return num(acc);
    }
    return sm_value_none();
}

SmValue sm_expr_eval(const SmExpr *e, SmWorld *w, SmSet *self)
{
    if (e == NULL) { return sm_value_none(); }

    switch (e->kind) {
    case EX_NUMBER: return num(e->number);
    case EX_STRING: return sm_value_string(e->name);
    case EX_COLOR:  return sm_value_color(e->color);

    case EX_NAME: {
        SmSet *s = sm_lookup(w, e->name);
        if (s == NULL) { return sm_value_none(); }
        return sm_value_copy(&s->value);
    }
    case EX_DOT: {
        SmSet *s = sm_lookup(w, e->name);
        const SmValue *pv;
        if (s == NULL) { return sm_value_none(); }
        pv = sm_get_prop(s, e->prop);
        if (pv != NULL) { return sm_value_copy(pv); }
        /* S.value with no such property is the set's own value (what a bare S
         * reads), so "N.value" works on any set, not just ones given a
         * `value` property */
        if (sm_stricmp(e->prop, "value") == 0) { return sm_value_copy(&s->value); }
        /* No such property: if `s` is a Namespace, `owner.member` means the
         * member of `s` named `member` -- scoped access, same idea as a
         * property, just resolving into the container's own members instead
         * of its property list. */
        if (s->kind == SM_KIND_NAMESPACE) {
            int i;
            for (i = 0; i < s->nmembers; ++i) {
                if (s->members[i]->name != NULL &&
                    sm_stricmp(s->members[i]->name, e->prop) == 0) {
                    return sm_value_copy(&s->members[i]->value);
                }
            }
        }
        return sm_value_none();
    }
    case EX_NEG: {
        SmValue a = sm_expr_eval(e->a, w, self);
        double d = to_number(&a);
        sm_value_clear(&a);
        return num(-d);
    }
    case EX_NOT: {
        SmValue a = sm_expr_eval(e->a, w, self);
        SmBool t = sm_value_truthy(&a);
        sm_value_clear(&a);
        return num(t ? 0.0 : 1.0);
    }
    case EX_AND: {
        SmValue a = sm_expr_eval(e->a, w, self);
        SmBool ta = sm_value_truthy(&a);
        sm_value_clear(&a);
        if (!ta) { return num(0.0); }
        {
            SmValue b = sm_expr_eval(e->b, w, self);
            SmBool tb = sm_value_truthy(&b);
            sm_value_clear(&b);
            return num(tb ? 1.0 : 0.0);
        }
    }
    case EX_OR: {
        SmValue a = sm_expr_eval(e->a, w, self);
        SmBool ta = sm_value_truthy(&a);
        sm_value_clear(&a);
        if (ta) { return num(1.0); }
        {
            SmValue b = sm_expr_eval(e->b, w, self);
            SmBool tb = sm_value_truthy(&b);
            sm_value_clear(&b);
            return num(tb ? 1.0 : 0.0);
        }
    }
    case EX_BIN: {
        SmValue a = sm_expr_eval(e->a, w, self);
        SmValue b = sm_expr_eval(e->b, w, self);
        double x = to_number(&a), y = to_number(&b);
        SmBool bothstr = (a.kind == SM_VAL_STRING && b.kind == SM_VAL_STRING)
                             ? SM_TRUE : SM_FALSE;
        SmValue r;
        switch (e->op) {
        case OP_ADD: r = num(x + y); break;
        case OP_SUB: r = num(x - y); break;
        case OP_MUL: r = num(x * y); break;
        case OP_DIV: r = (y == 0.0) ? sm_value_none() : num(x / y); break;
        case OP_MOD: {
            long yi = (long)y;
            r = (yi == 0) ? sm_value_none() : num((double)((long)x % yi));
            break;
        }
        case OP_LT: r = num(x <  y ? 1.0 : 0.0); break;
        case OP_GT: r = num(x >  y ? 1.0 : 0.0); break;
        case OP_LE: r = num(x <= y ? 1.0 : 0.0); break;
        case OP_GE: r = num(x >= y ? 1.0 : 0.0); break;
        case OP_EQ:
            r = num((bothstr ? (strcmp(a.string, b.string) == 0)
                             : (x == y)) ? 1.0 : 0.0);
            break;
        case OP_NE:
            r = num((bothstr ? (strcmp(a.string, b.string) != 0)
                             : (x != y)) ? 1.0 : 0.0);
            break;
        case OP_CONCAT: {
            char sa[256], sb[256];
            char *cat;
            sm_value_format(&a, sa, sizeof(sa));
            sm_value_format(&b, sb, sizeof(sb));
            cat = (char *)sm_alloc(strlen(sa) + strlen(sb) + 1);
            if (cat == NULL) { r = sm_value_none(); break; }
            strcpy(cat, sa);
            strcat(cat, sb);
            r = sm_value_string(cat);
            sm_free(cat);
            break;
        }
        default: r = sm_value_none(); break;
        }
        sm_value_clear(&a);
        sm_value_clear(&b);
        return r;
    }
    case EX_IS: {
        SmSet *left = resolve_set(e->a, w);
        SmSet *right = sm_lookup(w, e->prop);
        SmBool yes = SM_FALSE;
        if (left != NULL && right != NULL) {
            yes = (left == right || sm_is_member(right, left)) ? SM_TRUE : SM_FALSE;
        }
        if (e->negate) { yes = yes ? SM_FALSE : SM_TRUE; }
        return num(yes ? 1.0 : 0.0);
    }
    case EX_ISEMPTY: {
        SmSet *left = resolve_set(e->a, w);
        SmBool empty = SM_TRUE;
        if (left != NULL) {
            empty = (left->nmembers == 0 && left->value.kind == SM_VAL_NONE)
                        ? SM_TRUE : SM_FALSE;
        }
        if (e->negate) { empty = empty ? SM_FALSE : SM_TRUE; }
        return num(empty ? 1.0 : 0.0);
    }
    case EX_CALL:
        return eval_call(e, w, self);
    }
    return sm_value_none();
}

const char *sm_expr_bare_name(const SmExpr *e)
{
    return (e != NULL && e->kind == EX_NAME) ? e->name : NULL;
}
