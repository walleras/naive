/* sm_parse.c  --  SeSaML document parser.
 *
 * Implements sm_doc_load_string: lex the whole .sml source, group tokens into
 * indented logical lines, then recursively descend building live set state
 * (design doc S12 -- parsing IS evaluation; there is no separate AST). Block
 * structure for CREATE clauses and ON-event bodies is indentation-based;
 * FUNCTION bodies close with END. Covers the attested document surface:
 * CREATE [FLAT] .. AS .. WITH/AT/MEMBER OF/REQUIRES, ON <event> [OF][WHERE]
 * with ADD/REMOVE/SET/SWAP/MOVE/SEND/CALL actions, and top-level mutations.
 * FUNCTION bodies are parsed into a statement tree (sm_func.h) stored on the
 * page's function map and run when called (CALL f WITH (..), f(..), or an
 * event binding like "CREATE f AS FUNCTION, OnClick, Widget").
 */
#include "sm_doc.h"
#include "sm_lex.h"
#include "sm_schema.h"
#include "sm_func.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct { int start, count, indent, line; } Line;

typedef struct {
    SmDoc   *d;
    SmWorld *w;
    SmToken *toks;  int ntoks, ctoks;
    Line    *lines; int nlines, clines;
    int      li;
    char    *err;   size_t errcap;
    SmBool   failed;
} PP;

typedef struct {
    SmBool is_num;
    double num;
    char   name[32];
} CoordSpec;

/* ---- diagnostics -------------------------------------------------------- */

static void fail(PP *pp, int line, const char *msg)
{
    if (pp->failed) { return; }
    pp->failed = SM_TRUE;
    if (pp->err != NULL && pp->errcap > 0) {
        /* Format into a bounded scratch, then copy into the caller's buffer. */
        char scratch[192];
        snprintf(scratch, sizeof(scratch), "line %d: %.150s", line, msg);
        {
            size_t n = strlen(scratch);
            if (n >= pp->errcap) { n = pp->errcap - 1; }
            memcpy(pp->err, scratch, n);
            pp->err[n] = '\0';
        }
    }
}

/* ---- token / line access ------------------------------------------------ */

static SmToken *T(PP *pp, int idx) { return &pp->toks[idx]; }
static Line    *L(PP *pp, int i)   { return &pp->lines[i]; }

/* token j (0-based) of line i */
static SmToken *LT(PP *pp, int i, int j)
{
    return &pp->toks[pp->lines[i].start + j];
}

/* ---- lexing into tokens + lines ----------------------------------------- */

static SmStatus push_tok(PP *pp, const SmToken *t)
{
    if (pp->ntoks >= pp->ctoks) {
        int nc = (pp->ctoks == 0) ? 64 : pp->ctoks * 2;
        SmToken *g = (SmToken *)sm_realloc(pp->toks, (size_t)nc * sizeof(SmToken));
        if (g == NULL) { return SM_ERR_OOM; }
        pp->toks = g; pp->ctoks = nc;
    }
    pp->toks[pp->ntoks++] = *t;
    return SM_OK;
}

static SmStatus push_line(PP *pp, int start, int count, int indent, int line)
{
    if (pp->nlines >= pp->clines) {
        int nc = (pp->clines == 0) ? 32 : pp->clines * 2;
        Line *g = (Line *)sm_realloc(pp->lines, (size_t)nc * sizeof(Line));
        if (g == NULL) { return SM_ERR_OOM; }
        pp->lines = g; pp->clines = nc;
    }
    pp->lines[pp->nlines].start = start;
    pp->lines[pp->nlines].count = count;
    pp->lines[pp->nlines].indent = indent;
    pp->lines[pp->nlines].line = line;
    ++pp->nlines;
    return SM_OK;
}

static SmStatus lex_all(PP *pp, const char *src)
{
    SmLexer lx;
    int open = 0, lstart = 0, lindent = 0, lline = 0;
    sm_lex_init(&lx, src);
    for (;;) {
        SmTokType ty = lx.cur.type;
        if (ty == SM_TOK_ERROR) {
            fail(pp, lx.cur.line, lx.errmsg[0] ? lx.errmsg : "lex error");
            return SM_ERR_PARSE;
        }
        if (ty == SM_TOK_EOF) {
            if (open) { push_line(pp, lstart, pp->ntoks - lstart, lindent, lline); }
            break;
        }
        if (ty == SM_TOK_NEWLINE) {
            if (open) {
                if (push_line(pp, lstart, pp->ntoks - lstart, lindent, lline) != SM_OK) {
                    return SM_ERR_OOM;
                }
                open = 0;
            }
        } else {
            if (!open) { open = 1; lstart = pp->ntoks; lindent = lx.cur.col; lline = lx.cur.line; }
            if (push_tok(pp, &lx.cur) != SM_OK) { return SM_ERR_OOM; }
        }
        if (sm_lex_next(&lx) != SM_OK) {
            fail(pp, lx.cur.line, lx.errmsg[0] ? lx.errmsg : "lex error");
            return SM_ERR_PARSE;
        }
    }
    return SM_OK;
}

/* ---- helpers ------------------------------------------------------------ */

static SmKind kind_of(const SmToken *t)
{
    if (sm_tok_is_kw(t, "TextBox"))     return SM_KIND_TEXTBOX;
    if (sm_tok_is_kw(t, "TextInput"))   return SM_KIND_TEXTINPUT;
    if (sm_tok_is_kw(t, "Button"))      return SM_KIND_BUTTON;
    if (sm_tok_is_kw(t, "Checkbox"))    return SM_KIND_CHECKBOX;
    if (sm_tok_is_kw(t, "Radio"))       return SM_KIND_RADIO;
    if (sm_tok_is_kw(t, "Image"))       return SM_KIND_IMAGE;
    if (sm_tok_is_kw(t, "ContentPane")) return SM_KIND_CONTENTPANE;
    if (sm_tok_is_kw(t, "NavItem"))     return SM_KIND_NAVITEM;
    if (sm_tok_is_kw(t, "ToolbarItem")) return SM_KIND_TOOLBARITEM;
    if (sm_tok_is_kw(t, "Function"))    return SM_KIND_FUNCTION;
    if (sm_tok_is_kw(t, "Color"))       return SM_KIND_COLOR;
    if (sm_tok_is_kw(t, "Set"))         return SM_KIND_SET;
    if (sm_tok_is_kw(t, "Namespace"))   return SM_KIND_NAMESPACE;
    if (sm_tok_is_kw(t, "Heading"))     return SM_KIND_HEADING;
    if (sm_tok_is_kw(t, "H1") || sm_tok_is_kw(t, "H2") || sm_tok_is_kw(t, "H3") ||
        sm_tok_is_kw(t, "H4") || sm_tok_is_kw(t, "H5") || sm_tok_is_kw(t, "H6"))
        return SM_KIND_HEADING;
    if (sm_tok_is_kw(t, "Paragraph") || sm_tok_is_kw(t, "P"))  return SM_KIND_PARAGRAPH;
    if (sm_tok_is_kw(t, "HRule") || sm_tok_is_kw(t, "HR"))     return SM_KIND_HRULE;
    if (sm_tok_is_kw(t, "List") || sm_tok_is_kw(t, "UL") || sm_tok_is_kw(t, "OL"))
        return SM_KIND_LIST;
    if (sm_tok_is_kw(t, "ListItem") || sm_tok_is_kw(t, "LI")) return SM_KIND_LISTITEM;
    if (sm_tok_is_kw(t, "Table"))       return SM_KIND_TABLE;
    if (sm_tok_is_kw(t, "Row") || sm_tok_is_kw(t, "TableRow") || sm_tok_is_kw(t, "TR"))
        return SM_KIND_TABLEROW;
    if (sm_tok_is_kw(t, "Column") || sm_tok_is_kw(t, "TableCell") ||
        sm_tok_is_kw(t, "TD") || sm_tok_is_kw(t, "TH"))
        return SM_KIND_TABLECELL;
    if (sm_tok_is_kw(t, "Textarea") || sm_tok_is_kw(t, "TextArea"))
        return SM_KIND_TEXTAREA;
    if (sm_tok_is_kw(t, "Select"))  return SM_KIND_SELECT;
    if (sm_tok_is_kw(t, "Option"))  return SM_KIND_OPTION;
    return SM_KIND_OTHER;
}

/* If the type token is H1..H6 or OL, return an implied level/ordered flag so
 * parse_create can seed the right property. Returns 0 if none. */
static int implied_heading_level(const SmToken *t)
{
    if (sm_tok_is_kw(t, "H1")) return 1;
    if (sm_tok_is_kw(t, "H2")) return 2;
    if (sm_tok_is_kw(t, "H3")) return 3;
    if (sm_tok_is_kw(t, "H4")) return 4;
    if (sm_tok_is_kw(t, "H5")) return 5;
    if (sm_tok_is_kw(t, "H6")) return 6;
    return 0;
}

static SmEventType event_of(const SmToken *t, SmBool *ok)
{
    *ok = SM_TRUE;
    if (sm_tok_is_kw(t, "CLICK"))  return SM_EV_CLICK;
    if (sm_tok_is_kw(t, "DRAG"))   return SM_EV_DRAG;
    if (sm_tok_is_kw(t, "DROP"))   return SM_EV_DROP;
    if (sm_tok_is_kw(t, "TYPE"))   return SM_EV_TYPE;
    if (sm_tok_is_kw(t, "KEY"))    return SM_EV_KEY;
    if (sm_tok_is_kw(t, "CHANGE")) return SM_EV_CHANGE;
    if (sm_tok_is_kw(t, "LOAD"))   return SM_EV_LOAD;
    if (sm_tok_is_kw(t, "TIMER"))  return SM_EV_TIMER;
    *ok = SM_FALSE;
    return SM_EV_CLICK;
}

static SmSet *find_or_make(PP *pp, const char *name, SmKind kind)
{
    SmSet *s = sm_find(pp->w, name);
    if (s == NULL) { s = sm_set_create(pp->w, name, kind); }
    return s;
}

/* Reconstruct the source substring spanning tokens [t0,t1) and parse it as an
 * expression. Faithful to quotes/colours because it uses each token's true
 * source extent. */
static SmStatus parse_expr_span(PP *pp, int t0, int t1, SmExpr **out)
{
    SmToken *a, *b;
    const char *s0, *s1;
    size_t len;
    char *tmp;
    SmStatus st;
    *out = NULL;
    if (t0 >= t1) { return SM_ERR_PARSE; }
    a = T(pp, t0);
    b = T(pp, t1 - 1);
    s0 = (a->type == SM_TOK_STRING) ? a->lex - 1 : a->lex;
    s1 = (b->type == SM_TOK_STRING) ? b->lex + b->len + 1 : b->lex + b->len;
    len = (size_t)(s1 - s0);
    tmp = (char *)sm_alloc(len + 1);
    if (tmp == NULL) { return SM_ERR_OOM; }
    memcpy(tmp, s0, len);
    tmp[len] = '\0';
    st = sm_expr_parse(tmp, out, pp->err, pp->errcap);
    sm_free(tmp);
    return st;
}

/* ---- clause parsers ----------------------------------------------------- */

/* Does token `idx` begin a new CREATE clause? Used to split several clauses
 * sharing one physical line. Note MEMBER only starts a clause as "MEMBER OF" --
 * the bare word MEMBER also appears inside REQUIRES (MEMBER HAS/IS ...), where
 * it must NOT be treated as a boundary. */
static SmBool is_clause_boundary(PP *pp, int idx, int kend)
{
    const SmToken *t = T(pp, idx);
    if (sm_tok_is_kw(t, "WITH") || sm_tok_is_kw(t, "AT") ||
        sm_tok_is_kw(t, "REQUIRES") || sm_tok_is_kw(t, "ON")) {
        return SM_TRUE;
    }
    if (sm_tok_is_kw(t, "MEMBER") && idx + 1 < kend &&
        sm_tok_is_kw(T(pp, idx + 1), "OF")) {
        return SM_TRUE;
    }
    return SM_FALSE;
}

/* End (exclusive) of the clause beginning at token k: the next clause
 * boundary, or kend. */
static int clause_end(PP *pp, int k, int kend)
{
    int j = k + 1;
    while (j < kend && !is_clause_boundary(pp, j, kend)) { ++j; }
    return j;
}

/* WITH parsed over the token range [k, kend); k points at WITH. */
static void parse_with(PP *pp, SmSet *s, int k, int kend, int lineno)
{
    int i = k + 1;
    while (i < kend && !pp->failed) {
        int propidx, j, eq;
        char *prop;
        SmExpr *e = NULL;
        if (T(pp, i)->type != SM_TOK_IDENT) { fail(pp, lineno, "expected property name"); return; }
        propidx = i;
        eq = i + 1;
        if (eq >= kend || T(pp, eq)->type != SM_TOK_EQ) { fail(pp, lineno, "expected '=' in WITH"); return; }
        j = eq + 1;
        while (j < kend && T(pp, j)->type != SM_TOK_COMMA) { ++j; }
        prop = sm_tok_dup(T(pp, propidx));
        if (parse_expr_span(pp, eq + 1, j, &e) != SM_OK) {
            sm_free(prop);
            fail(pp, lineno, "bad WITH value");
            return;
        }
        sm_set_prop(s, prop, sm_expr_eval(e, pp->w, s));
        sm_expr_free(e);
        sm_free(prop);
        i = (j < kend && T(pp, j)->type == SM_TOK_COMMA) ? j + 1 : kend;
    }
}

static int parse_coord(PP *pp, int base, int k, int n, CoordSpec *cs)
{
    /* forms: AXIS '.' (IDENT|NUMBER) | NUMBER | IDENT */
    cs->is_num = SM_FALSE;
    cs->name[0] = '\0';
    if (k + 1 < n && T(pp, base + k)->type == SM_TOK_IDENT
                  && T(pp, base + k + 1)->type == SM_TOK_DOT) {
        k += 2; /* skip AXIS '.' */
    }
    if (k >= n) { return k; }
    if (T(pp, base + k)->type == SM_TOK_NUMBER) {
        cs->is_num = SM_TRUE;
        cs->num = T(pp, base + k)->number;
        ++k;
    } else if (T(pp, base + k)->type == SM_TOK_IDENT) {
        SmToken *t = T(pp, base + k);
        size_t ln = t->len < sizeof(cs->name) - 1 ? t->len : sizeof(cs->name) - 1;
        memcpy(cs->name, t->lex, ln);
        cs->name[ln] = '\0';
        ++k;
    }
    return k;
}

/* AT over range [k,kend); k points at AT. */
static void parse_at(PP *pp, int k, int kend, CoordSpec *x, CoordSpec *y)
{
    int j = parse_coord(pp, 0, k + 1, kend, x);
    if (j < kend && T(pp, j)->type == SM_TOK_COMMA) { ++j; }
    parse_coord(pp, 0, j, kend, y);
}

/* MEMBER OF over range [k,kend); k points at MEMBER. Returns the number of
 * STRUCTURAL supersets linked (styling sets, per sm_is_exempt_membership, do
 * not count). A namespace-exclusivity violation (see
 * sm_check_namespace_exclusive) is a parse error, not silently ignored. */
static int parse_member_of(PP *pp, SmSet *s, int k, int kend, int lineno)
{
    int i = k + 1;
    int structural = 0;
    if (i < kend && sm_tok_is_kw(T(pp, i), "OF")) { ++i; }
    while (i < kend && !pp->failed) {
        if (T(pp, i)->type == SM_TOK_IDENT) {
            char *nm = sm_tok_dup(T(pp, i));
            SmSet *super = find_or_make(pp, nm, SM_KIND_SET);
            sm_free(nm);
            if (super != NULL) {
                if (sm_check_namespace_exclusive(super, s) == SM_ERR_NAMESPACE) {
                    fail(pp, lineno, "set is already namespaced elsewhere (or already owned)");
                    return structural;
                }
                sm_add_member(pp->w, super, s);
                if (!sm_is_exempt_membership(super)) { ++structural; }
            }
            ++i;
        } else { break; }
        if (i < kend && T(pp, i)->type == SM_TOK_COMMA) { ++i; } else { break; }
    }
    return structural;
}

/* REQUIRES over range [k,kend); k points at REQUIRES. */
static void parse_requires(PP *pp, SmSet *s, int k, int kend, int lineno)
{
    int i = k + 1;
    SmConn conn = SM_CONN_END;
    if (s->schema == NULL) { s->schema = sm_schema_new(); }
    while (i < kend && !pp->failed) {
        SmBool neg = SM_FALSE;
        SmReqOp op;
        SmSet *target;
        char *nm;
        if (sm_tok_is_kw(T(pp, i), "NOT")) { neg = SM_TRUE; ++i; }
        if (i >= kend || !sm_tok_is_kw(T(pp, i), "MEMBER")) { fail(pp, lineno, "expected MEMBER in REQUIRES"); return; }
        ++i;
        if (i < kend && sm_tok_is_kw(T(pp, i), "HAS")) { op = SM_REQ_HAS; }
        else if (i < kend && sm_tok_is_kw(T(pp, i), "IS")) { op = SM_REQ_IS; }
        else { fail(pp, lineno, "expected HAS or IS in REQUIRES"); return; }
        ++i;
        if (i >= kend || T(pp, i)->type != SM_TOK_IDENT) { fail(pp, lineno, "expected set name in REQUIRES"); return; }
        nm = sm_tok_dup(T(pp, i));
        target = find_or_make(pp, nm, SM_KIND_SET);
        sm_free(nm);
        ++i;
        sm_schema_add(s->schema, conn, op, neg, target);
        if (i < kend && sm_tok_is_kw(T(pp, i), "AND")) { conn = SM_CONN_AND; ++i; }
        else if (i < kend && sm_tok_is_kw(T(pp, i), "OR")) { conn = SM_CONN_OR; ++i; }
        else { break; }
    }
}

/* ---- actions ------------------------------------------------------------ */

/* The operands of one parsed action (strings owned, expr owned). Shared by ON
 * handlers, top-level statements and FUNCTION bodies. */
typedef struct {
    SmActKind kind;
    char     *a;
    char     *b;
    char     *prop;
    SmExpr   *expr;
} ActFields;

static void act_clear(ActFields *f)
{
    sm_free(f->a);
    sm_free(f->b);
    sm_free(f->prop);
    sm_expr_free(f->expr);
    f->a = f->b = f->prop = NULL;
    f->expr = NULL;
}

/* Parse one action from tokens [t0,t1) into `f`. Returns SM_FALSE (after
 * fail()) if it is malformed. Accepts:
 *   ADD a TO b | ADD a, b          REMOVE a FROM b | REMOVE a, b
 *   SET a[.prop] TO <expr|name>    SWAP a WITH b
 *   MOVE a TO b.Location           MOVE a FROM b TO c   (membership transfer)
 *   SEND a                         CALL fn WITH (args) | fn(args)          */
static SmBool parse_action_fields(PP *pp, int t0, int t1, int lineno, ActFields *f)
{
    SmToken *first = T(pp, t0);
    memset(f, 0, sizeof(*f));

    if (sm_tok_is_kw(first, "ADD") || sm_tok_is_kw(first, "REMOVE")) {
        SmToken *sep;
        if (t0 + 3 >= t1) { fail(pp, lineno, "malformed ADD/REMOVE"); return SM_FALSE; }
        sep = T(pp, t0 + 2);
        if (!(sm_tok_is_kw(sep, "TO") || sm_tok_is_kw(sep, "FROM") ||
              sep->type == SM_TOK_COMMA)) {
            fail(pp, lineno, "malformed ADD/REMOVE");
            return SM_FALSE;
        }
        f->kind = sm_tok_is_kw(first, "ADD") ? SM_ACT_ADD : SM_ACT_REMOVE;
        f->a = sm_tok_dup(T(pp, t0 + 1));
        f->b = sm_tok_dup(T(pp, t0 + 3));
    } else if (sm_tok_is_kw(first, "SET")) {
        int k = t0 + 1;
        f->kind = SM_ACT_SET;
        if (k >= t1 || T(pp, k)->type != SM_TOK_IDENT) { fail(pp, lineno, "malformed SET"); return SM_FALSE; }
        f->a = sm_tok_dup(T(pp, k)); ++k;
        if (k < t1 && T(pp, k)->type == SM_TOK_DOT) {
            ++k;
            if (k < t1 && T(pp, k)->type == SM_TOK_IDENT) { f->prop = sm_tok_dup(T(pp, k)); ++k; }
        }
        if (k < t1 && (sm_tok_is_kw(T(pp, k), "TO") || T(pp, k)->type == SM_TOK_EQ)) { ++k; }
        /* RHS: single bare name => binding; otherwise an expression */
        if (f->prop == NULL && (t1 - k) == 1 && T(pp, k)->type == SM_TOK_IDENT) {
            f->b = sm_tok_dup(T(pp, k));
        } else if (parse_expr_span(pp, k, t1, &f->expr) != SM_OK) {
            act_clear(f);
            fail(pp, lineno, "bad SET value");
            return SM_FALSE;
        }
    } else if (sm_tok_is_kw(first, "SWAP")) {
        if (t0 + 3 >= t1) { fail(pp, lineno, "malformed SWAP"); return SM_FALSE; }
        f->kind = SM_ACT_SWAP;
        f->a = sm_tok_dup(T(pp, t0 + 1));
        f->b = sm_tok_dup(T(pp, t0 + 3));
    } else if (sm_tok_is_kw(first, "MOVE")) {
        if (t0 + 3 >= t1) { fail(pp, lineno, "malformed MOVE"); return SM_FALSE; }
        f->a = sm_tok_dup(T(pp, t0 + 1));
        f->b = sm_tok_dup(T(pp, t0 + 3));
        if (sm_tok_is_kw(T(pp, t0 + 2), "FROM")) {
            /* MOVE a FROM b TO c */
            if (t0 + 5 >= t1 || !sm_tok_is_kw(T(pp, t0 + 4), "TO")) {
                act_clear(f);
                fail(pp, lineno, "malformed MOVE (expected MOVE x FROM a TO b)");
                return SM_FALSE;
            }
            f->kind = SM_ACT_TRANSFER;
            f->prop = sm_tok_dup(T(pp, t0 + 5));
        } else {
            f->kind = SM_ACT_MOVE;       /* owner before .Location */
        }
    } else if (sm_tok_is_kw(first, "SEND")) {
        f->kind = SM_ACT_SEND;
        f->a = (t0 + 1 < t1) ? sm_tok_dup(T(pp, t0 + 1)) : NULL;
    } else if (sm_tok_is_kw(first, "CALL") ||
               (first->type == SM_TOK_IDENT && t0 + 1 < t1 &&
                T(pp, t0 + 1)->type == SM_TOK_LPAREN)) {
        f->kind = SM_ACT_CALL;
        if (parse_expr_span(pp, t0, t1, &f->expr) != SM_OK) {
            fail(pp, lineno, "bad function call");
            return SM_FALSE;
        }
    } else {
        fail(pp, lineno, "unknown action");
        return SM_FALSE;
    }
    return SM_TRUE;
}

/* Parse one action: appended to handler `h`, or (h == NULL, a top-level
 * statement) run now. At top level only ADD / REMOVE / SET / CALL mean
 * anything; SWAP / MOVE / SEND are handler verbs. */
static void parse_action(PP *pp, SmHandler *h, int t0, int t1, int lineno)
{
    ActFields f;
    if (!parse_action_fields(pp, t0, t1, lineno, &f)) { return; }
    if (h != NULL) {
        sm_handler_add_action(h, f.kind, f.a, f.b, f.prop, f.expr);   /* takes expr */
        f.expr = NULL;
    } else if (f.kind == SM_ACT_ADD || f.kind == SM_ACT_REMOVE ||
               f.kind == SM_ACT_SET || f.kind == SM_ACT_CALL) {
        sm_doc_exec_action(pp->d, f.kind,
                           (f.a != NULL) ? sm_find(pp->w, f.a) : NULL,
                           (f.b != NULL) ? sm_find(pp->w, f.b) : NULL,
                           NULL, f.prop, f.expr);
    }
    act_clear(&f);
}

/* ---- ON event ----------------------------------------------------------- */

/* Parse an ON header over [k,kend); k points at ON. Returns the handler and,
 * via *pk, the index where actions begin (== kend if a WHERE consumed the
 * rest). Returns NULL on error. */
static SmHandler *parse_on_header(PP *pp, SmSet *owner, int k, int kend,
                                  int *pk, int lineno)
{
    SmBool ok;
    SmEventType ev;
    char *ofname = NULL;
    SmExpr *where = NULL;
    SmHandler *h;
    int i = k + 1;

    if (i >= kend) { fail(pp, lineno, "ON needs an event name"); return NULL; }
    ev = event_of(T(pp, i), &ok);
    if (!ok) { fail(pp, lineno, "unknown event name"); return NULL; }
    ++i;
    if (i < kend && sm_tok_is_kw(T(pp, i), "OF")) {
        ++i;
        if (i < kend && T(pp, i)->type == SM_TOK_IDENT) { ofname = sm_tok_dup(T(pp, i)); ++i; }
    }
    if (i < kend && sm_tok_is_kw(T(pp, i), "WHERE")) {
        ++i;
        if (parse_expr_span(pp, i, kend, &where) != SM_OK) {
            sm_free(ofname); fail(pp, lineno, "bad WHERE expression"); return NULL;
        }
        i = kend;
    }
    h = sm_doc_add_handler(pp->d, ev, owner, ofname, where);
    sm_free(ofname);
    *pk = i;
    return h;
}

/* ON as a same-line clause (no nested action lines). */
static void parse_event_inline(PP *pp, SmSet *owner, int k, int kend, int lineno)
{
    int i;
    SmHandler *h = parse_on_header(pp, owner, k, kend, &i, lineno);
    if (h == NULL) { return; }
    if (i < kend) { parse_action(pp, h, i, kend, lineno); }
}

/* ON beginning an indented line, with optional nested (more-indented) action
 * lines. Advances pp->li past the block. */
static void parse_event(PP *pp, SmSet *owner)
{
    Line *ln = L(pp, pp->li);
    int base = ln->start, n = ln->count, baseIndent = ln->indent;
    int i;
    SmHandler *h = parse_on_header(pp, owner, base, base + n, &i, ln->line);
    if (h == NULL) { ++pp->li; return; }
    if (i < base + n) { parse_action(pp, h, i, base + n, ln->line); }
    ++pp->li;
    while (pp->li < pp->nlines && L(pp, pp->li)->indent > baseIndent && !pp->failed) {
        Line *al = L(pp, pp->li);
        parse_action(pp, h, al->start, al->start + al->count, al->line);
        ++pp->li;
    }
}

/* Dispatch a run of clauses sharing one token range [k,kend) onto a CREATE'd
 * set: WITH / AT / MEMBER OF / REQUIRES / inline ON. Accumulates coordinate
 * specs and counts MEMBER OF clauses (to decide top-level visibility). */
static void parse_clause_region(PP *pp, SmSet *s, int k, int kend,
                                CoordSpec *xs, CoordSpec *ys, int *nmem,
                                int lineno)
{
    while (k < kend && !pp->failed) {
        SmToken *f = T(pp, k);
        int end = clause_end(pp, k, kend);
        if (sm_tok_is_kw(f, "WITH"))          { parse_with(pp, s, k, end, lineno); }
        else if (sm_tok_is_kw(f, "AT"))       { parse_at(pp, k, end, xs, ys); }
        else if (sm_tok_is_kw(f, "MEMBER"))   { *nmem += parse_member_of(pp, s, k, end, lineno); }
        else if (sm_tok_is_kw(f, "REQUIRES")) { parse_requires(pp, s, k, end, lineno); }
        else if (sm_tok_is_kw(f, "ON"))       { parse_event_inline(pp, s, k, end, lineno); }
        else { fail(pp, lineno, "unexpected clause"); return; }
        k = end;
    }
}

/* ---- CREATE ------------------------------------------------------------- */

/* ---- FUNCTION bodies ----------------------------------------------------- */

/* A function body is parsed token-by-token (not line-by-line) so the Lua-style
 * one-liner `IF n <= 1 THEN RETURN 1 END` works. A simple statement runs to
 * the end of its physical line or to the next block keyword. A body closes at
 * its END; if the author omits it, the first line dedented back to the CREATE's
 * level closes it instead. */

static SmBool block_kw(const SmToken *t)
{
    return (sm_tok_is_kw(t, "END") || sm_tok_is_kw(t, "ELSE") ||
            sm_tok_is_kw(t, "ELSEIF")) ? SM_TRUE : SM_FALSE;
}

/* index one past the simple statement that starts at token i */
static int simple_end(PP *pp, int i)
{
    int line = T(pp, i)->line;
    int j = i + 1;
    while (j < pp->ntoks && T(pp, j)->line == line && !block_kw(T(pp, j))) { ++j; }
    return j;
}

/* indent of the logical line holding token ti, and whether ti starts it */
static SmBool starts_line(PP *pp, int ti, int *indent)
{
    int i;
    for (i = 0; i < pp->nlines; ++i) {
        if (pp->lines[i].start == ti) { *indent = pp->lines[i].indent; return SM_TRUE; }
        if (pp->lines[i].start > ti) { break; }
    }
    return SM_FALSE;
}

static SmStmt *parse_fn_stmt(PP *pp, int *pi, int baseIndent);

/* statements until END / ELSE / ELSEIF / the implicit dedent / end of input */
static void parse_fn_block(PP *pp, int *pi, int baseIndent, SmStmt **list)
{
    while (*pi < pp->ntoks && !pp->failed) {
        SmToken *t = T(pp, *pi);
        int indent;
        SmStmt *s;
        if (block_kw(t)) { return; }
        if (starts_line(pp, *pi, &indent) && indent <= baseIndent) { return; }
        s = parse_fn_stmt(pp, pi, baseIndent);
        if (s == NULL) { return; }
        sm_stmt_append(list, s);
    }
}

static SmBool fn_expect_end(PP *pp, int *pi, int line, const char *what)
{
    if (*pi < pp->ntoks && sm_tok_is_kw(T(pp, *pi), "END")) { ++*pi; return SM_TRUE; }
    fail(pp, line, what);
    return SM_FALSE;
}

/* IF / ELSEIF <cond> THEN block [ELSEIF.. | ELSE block] END */
static SmStmt *parse_fn_if(PP *pp, int *pi, int baseIndent)
{
    int line = T(pp, *pi)->line, j;
    SmStmt *s;
    ++*pi;                                   /* IF / ELSEIF */
    j = *pi;
    while (j < pp->ntoks && T(pp, j)->line == line && !sm_tok_is_kw(T(pp, j), "THEN")) { ++j; }
    if (j >= pp->ntoks || !sm_tok_is_kw(T(pp, j), "THEN")) {
        fail(pp, line, "expected THEN after IF condition");
        return NULL;
    }
    s = sm_stmt_new(SM_ST_IF, line);
    if (s == NULL) { fail(pp, line, "out of memory"); return NULL; }
    if (parse_expr_span(pp, *pi, j, &s->expr) != SM_OK) {
        sm_stmt_free(s);
        fail(pp, line, "bad IF condition");
        return NULL;
    }
    *pi = j + 1;
    parse_fn_block(pp, pi, baseIndent, &s->body);
    if (pp->failed) { sm_stmt_free(s); return NULL; }
    if (*pi < pp->ntoks && sm_tok_is_kw(T(pp, *pi), "ELSEIF")) {
        s->orelse = parse_fn_if(pp, pi, baseIndent);   /* consumes the END */
        if (s->orelse == NULL) { sm_stmt_free(s); return NULL; }
    } else {
        if (*pi < pp->ntoks && sm_tok_is_kw(T(pp, *pi), "ELSE")) {
            ++*pi;
            parse_fn_block(pp, pi, baseIndent, &s->orelse);
            if (pp->failed) { sm_stmt_free(s); return NULL; }
        }
        if (!fn_expect_end(pp, pi, line, "IF without END")) { sm_stmt_free(s); return NULL; }
    }
    return s;
}

static SmStmt *parse_fn_stmt(PP *pp, int *pi, int baseIndent)
{
    int i = *pi;
    SmToken *t = T(pp, i);
    int line = t->line;
    int e;
    SmStmt *s;

    if (sm_tok_is_kw(t, "IF")) {
        return parse_fn_if(pp, pi, baseIndent);
    }
    if (sm_tok_is_kw(t, "WHILE")) {
        int j = i + 1;
        while (j < pp->ntoks && T(pp, j)->line == line && !sm_tok_is_kw(T(pp, j), "DO")) { ++j; }
        if (j >= pp->ntoks || !sm_tok_is_kw(T(pp, j), "DO")) {
            fail(pp, line, "expected DO after WHILE condition");
            return NULL;
        }
        s = sm_stmt_new(SM_ST_WHILE, line);
        if (s == NULL) { fail(pp, line, "out of memory"); return NULL; }
        if (parse_expr_span(pp, i + 1, j, &s->expr) != SM_OK) {
            sm_stmt_free(s);
            fail(pp, line, "bad WHILE condition");
            return NULL;
        }
        *pi = j + 1;
        parse_fn_block(pp, pi, baseIndent, &s->body);
        if (pp->failed || !fn_expect_end(pp, pi, line, "WHILE without END")) {
            sm_stmt_free(s);
            return NULL;
        }
        return s;
    }

    e = simple_end(pp, i);

    if (sm_tok_is_kw(t, "RETURN")) {
        s = sm_stmt_new(SM_ST_RETURN, line);
        if (s == NULL) { fail(pp, line, "out of memory"); return NULL; }
        if (i + 1 < e && parse_expr_span(pp, i + 1, e, &s->expr) != SM_OK) {
            sm_stmt_free(s);
            fail(pp, line, "bad RETURN value");
            return NULL;
        }
    } else if (sm_tok_is_kw(t, "LOCAL")) {
        int k = i + 1;
        if (k >= e || T(pp, k)->type != SM_TOK_IDENT) { fail(pp, line, "LOCAL needs a name"); return NULL; }
        s = sm_stmt_new(SM_ST_LOCAL, line);
        if (s == NULL) { fail(pp, line, "out of memory"); return NULL; }
        s->a = sm_tok_dup(T(pp, k)); ++k;
        if (k < e && T(pp, k)->type == SM_TOK_EQ) {
            ++k;
            if (parse_expr_span(pp, k, e, &s->expr) != SM_OK) {
                sm_stmt_free(s);
                fail(pp, line, "bad LOCAL value");
                return NULL;
            }
        }
    } else if (t->type == SM_TOK_IDENT && !sm_tok_is_kw(t, "ADD") &&
               !sm_tok_is_kw(t, "REMOVE") && !sm_tok_is_kw(t, "SET") &&
               !sm_tok_is_kw(t, "SWAP") && !sm_tok_is_kw(t, "MOVE") &&
               !sm_tok_is_kw(t, "SEND") && !sm_tok_is_kw(t, "CALL") &&
               ((i + 1 < e && T(pp, i + 1)->type == SM_TOK_EQ) ||
                (i + 3 < e && T(pp, i + 1)->type == SM_TOK_DOT &&
                 T(pp, i + 3)->type == SM_TOK_EQ))) {
        /* Lua assignment:  x = expr   /   x.prop = expr */
        int k = i + 1;
        s = sm_stmt_new(SM_ST_ACTION, line);
        if (s == NULL) { fail(pp, line, "out of memory"); return NULL; }
        s->act = SM_ACT_SET;
        s->a = sm_tok_dup(t);
        if (T(pp, k)->type == SM_TOK_DOT) { s->prop = sm_tok_dup(T(pp, k + 1)); k += 2; }
        ++k;                                  /* the '=' */
        if (parse_expr_span(pp, k, e, &s->expr) != SM_OK) {
            sm_stmt_free(s);
            fail(pp, line, "bad assignment");
            return NULL;
        }
    } else if (sm_tok_is_kw(t, "CALL") ||
               (t->type == SM_TOK_IDENT && i + 1 < e && T(pp, i + 1)->type == SM_TOK_LPAREN)) {
        s = sm_stmt_new(SM_ST_EXPR, line);
        if (s == NULL) { fail(pp, line, "out of memory"); return NULL; }
        if (parse_expr_span(pp, i, e, &s->expr) != SM_OK) {
            sm_stmt_free(s);
            fail(pp, line, "bad function call");
            return NULL;
        }
    } else if (sm_tok_is_kw(t, "ADD") || sm_tok_is_kw(t, "REMOVE") ||
               sm_tok_is_kw(t, "SET") || sm_tok_is_kw(t, "SWAP") ||
               sm_tok_is_kw(t, "MOVE") || sm_tok_is_kw(t, "SEND")) {
        ActFields f;
        if (!parse_action_fields(pp, i, e, line, &f)) { return NULL; }
        s = sm_stmt_new(SM_ST_ACTION, line);
        if (s == NULL) { act_clear(&f); fail(pp, line, "out of memory"); return NULL; }
        s->act = f.kind; s->a = f.a; s->b = f.b; s->prop = f.prop; s->expr = f.expr;
    } else {
        fail(pp, line, "unexpected statement in function body");
        return NULL;
    }
    *pi = e;
    return s;
}

/* Event-set names a FUNCTION can join: "CREATE f AS FUNCTION, OnClick, Btn"
 * runs f when Btn is clicked (OnLoad: when the page loads). */
static SmBool fn_event_of(const char *name, SmEventType *ev)
{
    static const struct { const char *a, *b; SmEventType ev; } T_EV[] = {
        { "OnClick",  "ON_CLICK",  SM_EV_CLICK  },
        { "OnLoad",   "ON_LOAD",   SM_EV_LOAD   },
        { "OnChange", "ON_CHANGE", SM_EV_CHANGE },
        { "OnKey",    "ON_KEY",    SM_EV_KEY    },
        { "OnTimer",  "ON_TIMER",  SM_EV_TIMER  },
        { "OnDrag",   "ON_DRAG",   SM_EV_DRAG   },
        { "OnDrop",   "ON_DROP",   SM_EV_DROP   },
        { "OnType",   "ON_TYPE",   SM_EV_TYPE   }
    };
    size_t i;
    for (i = 0; i < sizeof(T_EV) / sizeof(T_EV[0]); ++i) {
        if (sm_stricmp(name, T_EV[i].a) == 0 || sm_stricmp(name, T_EV[i].b) == 0) {
            *ev = T_EV[i].ev;
            return SM_TRUE;
        }
    }
    return SM_FALSE;
}

/* Attach `fn` to the events named in its ", OnClick, Widget" tail
 * (tokens [t0,t1), the commas included). */
static void bind_function_events(PP *pp, const char *fn, int t0, int t1)
{
    SmBool have_ev = SM_FALSE;
    SmEventType ev = SM_EV_CLICK;
    int i, ntargets = 0;
    char *targets[16];

    for (i = t0; i + 1 < t1; i += 2) {
        char *nm;
        SmEventType e2;
        if (T(pp, i)->type != SM_TOK_COMMA || T(pp, i + 1)->type != SM_TOK_IDENT) { break; }
        nm = sm_tok_dup(T(pp, i + 1));
        if (nm == NULL) { break; }
        if (fn_event_of(nm, &e2)) { ev = e2; have_ev = SM_TRUE; sm_free(nm); }
        else if (ntargets < 16) { targets[ntargets++] = nm; }
        else { sm_free(nm); }
    }
    if (have_ev) {
        int n = (ntargets > 0) ? ntargets : 1;
        for (i = 0; i < n; ++i) {
            char src[400];
            SmExpr *call = NULL;
            SmSet *owner = NULL;
            SmHandler *h;
            snprintf(src, sizeof(src), "CALL %.100s WITH (%.100s)", fn, (ntargets > 0) ? targets[i] : "");
            if (ntargets > 0) { owner = find_or_make(pp, targets[i], SM_KIND_SET); }
            if (sm_expr_parse(src, &call, NULL, 0) != SM_OK) { continue; }
            h = sm_doc_add_handler(pp->d, ev, owner, NULL, NULL);
            if (h == NULL) { sm_expr_free(call); continue; }
            sm_handler_add_action(h, SM_ACT_CALL, NULL, NULL, NULL, call);
        }
    }
    for (i = 0; i < ntargets; ++i) { sm_free(targets[i]); }
}

/* Parse the body that follows "CREATE <set> AS FUNCTION ..." (pp->li is the
 * CREATE line; the body starts on the next line) and store it on the page's
 * function map. */
static void parse_function(PP *pp, const SmSet *fset, int baseIndent,
                           int tail0, int tail1)
{
    SmFunc *fn;
    int i, line = L(pp, pp->li)->line;

    fn = sm_func_new(fset->name);
    if (fn == NULL) { fail(pp, line, "out of memory"); return; }
    ++pp->li;
    i = (pp->li < pp->nlines) ? L(pp, pp->li)->start : pp->ntoks;

    if (i < pp->ntoks && sm_tok_is_kw(T(pp, i), "TAKES")) {
        int e = simple_end(pp, i);
        ++i;
        while (i < e) {
            if (T(pp, i)->type == SM_TOK_IDENT) {
                char *p = sm_tok_dup(T(pp, i));
                if (p != NULL) { (void)sm_func_add_param(fn, p); sm_free(p); }
            }
            ++i;       /* names and the commas between them */
        }
    }
    parse_fn_block(pp, &i, baseIndent, &fn->body);
    if (!pp->failed && i < pp->ntoks && sm_tok_is_kw(T(pp, i), "END")) { ++i; }
    else if (!pp->failed && i < pp->ntoks && block_kw(T(pp, i))) {
        fail(pp, T(pp, i)->line, "unexpected ELSE/ELSEIF outside an IF");
    }
    if (pp->failed) { sm_func_free(fn); return; }

    while (pp->li < pp->nlines && L(pp, pp->li)->start < i) { ++pp->li; }
    if (sm_func_define(pp->w, fn) != SM_OK) { fail(pp, line, "could not store function"); return; }
    bind_function_events(pp, fset->name, tail0, tail1);
}

static void parse_create(PP *pp)
{
    Line *ln = L(pp, pp->li);
    int base = ln->start, n = ln->count, baseIndent = ln->indent;
    int k = 1;
    SmBool flat = SM_FALSE;
    char *name;
    SmKind kind;
    SmSet *s;
    int nmemberof = 0;
    CoordSpec xs, ys;

    xs.is_num = SM_FALSE; strcpy(xs.name, "Left");
    ys.is_num = SM_FALSE; strcpy(ys.name, "Top");

    if (k < n && sm_tok_is_kw(T(pp, base + k), "FLAT")) { flat = SM_TRUE; ++k; }
    if (k >= n || T(pp, base + k)->type != SM_TOK_IDENT) { fail(pp, ln->line, "CREATE needs a name"); return; }
    name = sm_tok_dup(T(pp, base + k)); ++k;
    if (k >= n || !sm_tok_is_kw(T(pp, base + k), "AS")) { sm_free(name); fail(pp, ln->line, "expected AS"); return; }
    ++k;
    if (k >= n || T(pp, base + k)->type != SM_TOK_IDENT) { sm_free(name); fail(pp, ln->line, "expected a type after AS"); return; }
    kind = kind_of(T(pp, base + k));

    s = sm_find(pp->w, name);
    if (s != NULL && (s->flags & SM_FLAG_BUILTIN)) {
        /* names are case-insensitive, so this catches CREATE Red / Bold / etc.
         * colliding with a built-in colour or style set. */
        sm_free(name);
        fail(pp, ln->line, "name is reserved (a built-in colour/style set)");
        return;
    }
    if (s == NULL) { s = sm_set_create(pp->w, name, kind); }
    else { s->kind = kind; } /* upgrade a forward-referenced placeholder */
    sm_free(name);
    if (s == NULL) { fail(pp, ln->line, "could not create set"); return; }
    if (flat) { s->flags |= SM_FLAG_FLAT; }

    /* implied properties from the type spelling: H1..H6 set a heading level,
     * OL marks a list ordered */
    {
        int lvl = implied_heading_level(T(pp, base + k));
        if (lvl > 0) { sm_set_prop(s, "level", sm_value_number((double)lvl)); }
        if (kind == SM_KIND_LIST && sm_tok_is_kw(T(pp, base + k), "OL")) {
            sm_set_prop(s, "ordered", sm_value_number(1.0));
        }
    }

    if (kind == SM_KIND_FUNCTION) {
        /* the tail ", OnClick, Widget" names the events it handles */
        parse_function(pp, s, baseIndent, base + k + 1, base + n);
        return;
    }

    /* "CREATE X AS Type, SetA, SetB": each name after a comma is a set the new
     * set is made a member of (same as MEMBER OF SetA, SetB). */
    while (base + k + 2 < base + n && T(pp, base + k + 1)->type == SM_TOK_COMMA &&
           T(pp, base + k + 2)->type == SM_TOK_IDENT && !pp->failed) {
        nmemberof += parse_member_of(pp, s, base + k + 1, base + k + 3, ln->line);
        k += 2;
    }

    /* trailing clauses sharing the CREATE line (e.g. CREATE X AS Y WITH ..
     * AT .. MEMBER OF ..) */
    if (base + k + 1 < base + n) {
        parse_clause_region(pp, s, base + k + 1, base + n, &xs, &ys, &nmemberof, ln->line);
    }
    ++pp->li;

    /* indented clause lines below the CREATE */
    while (pp->li < pp->nlines && L(pp, pp->li)->indent > baseIndent && !pp->failed) {
        SmToken *f = LT(pp, pp->li, 0);
        if (sm_tok_is_kw(f, "ON")) {
            parse_event(pp, s);            /* consumes nested action lines */
        } else if (sm_tok_is_kw(f, "WITH") || sm_tok_is_kw(f, "AT") ||
                   sm_tok_is_kw(f, "MEMBER") || sm_tok_is_kw(f, "REQUIRES")) {
            Line *cl = L(pp, pp->li);
            parse_clause_region(pp, s, cl->start, cl->start + cl->count,
                                &xs, &ys, &nmemberof, cl->line);
            ++pp->li;
        } else {
            break;
        }
    }

    sm_doc_place(pp->d, s,
                 xs.is_num, xs.num, xs.name,
                 ys.is_num, ys.num, ys.name,
                 (nmemberof == 0) ? SM_TRUE : SM_FALSE);
}

/* ---- top level ---------------------------------------------------------- */

/* DEFINE GRID WITH width = <expr>: establishes a virtual coordinate width
 * for this page. X.<number> then scales to whatever the actual window width
 * is (X is "fixed" -- always spans the window); Y scales by the SAME factor
 * (screen size ratio), so nothing stretches unevenly per axis, but there is
 * no virtual height to clamp against -- Y "floats", content can run as far
 * down as it needs and the page becomes scrollable past the visible area
 * (design doc extension; no engine-level widget kind, just a page-level
 * coordinate-scaling setting, same tier as the nav bar/history). */
/* Parse one "<expr>" axis size over [i,end) (stopping at a comma if `stop_at_comma`),
 * requiring a positive number. Returns SM_OK/vx set, or SM_ERR_PARSE/SM_ERR_TYPE. */
static SmStatus parse_grid_axis(PP *pp, int i, int end, SmBool stop_at_comma,
                                int *out_next, double *out_num)
{
    SmExpr *e = NULL;
    SmValue v;
    int j = i;
    if (stop_at_comma) {
        while (j < end && T(pp, j)->type != SM_TOK_COMMA) { ++j; }
    } else {
        j = end;
    }
    if (parse_expr_span(pp, i, j, &e) != SM_OK) { return SM_ERR_PARSE; }
    v = sm_expr_eval(e, pp->w, NULL);
    sm_expr_free(e);
    if (v.kind != SM_VAL_NUMBER || v.number <= 0.0) {
        sm_value_clear(&v);
        return SM_ERR_TYPE;
    }
    *out_num = v.number;
    *out_next = j;
    return SM_OK;
}

/* DEFINE GRID X <number> [, Y <number>]: X is the required, always-fixed
 * virtual width (it always spans the actual window width, whatever that is).
 * Y is optional -- given, it is an INDEPENDENT virtual height, scaled against
 * the actual window height (so a "cell" need not be square); omitted, Y
 * floats: unbounded, using X's own scale to stay square. Neither is a hard
 * boundary -- placing something past either just puts it off the currently
 * visible area, which is what makes the page scrollable, not an error. */
static void parse_define(PP *pp)
{
    Line *ln = L(pp, pp->li);
    int end = ln->start + ln->count;
    int i = ln->start + 1;
    double width = 0.0, height = 0.0;

    if (i >= end || !sm_tok_is_kw(T(pp, i), "GRID")) {
        fail(pp, ln->line, "expected GRID after DEFINE");
        return;
    }
    ++i;
    if (i >= end || !sm_tok_is_kw(T(pp, i), "X")) {
        fail(pp, ln->line, "expected 'X <number>' after DEFINE GRID");
        return;
    }
    ++i;
    if (parse_grid_axis(pp, i, end, SM_TRUE, &i, &width) != SM_OK) {
        fail(pp, ln->line, "grid X size must be a positive number");
        return;
    }
    if (i < end && T(pp, i)->type == SM_TOK_COMMA) {
        ++i;
        if (i >= end || !sm_tok_is_kw(T(pp, i), "Y")) {
            fail(pp, ln->line, "expected 'Y <number>' after ','");
            return;
        }
        ++i;
        if (parse_grid_axis(pp, i, end, SM_FALSE, &i, &height) != SM_OK) {
            fail(pp, ln->line, "grid Y size must be a positive number");
            return;
        }
    }
    sm_doc_set_grid(pp->d, width, height);
    ++pp->li;
}

static void parse_statement(PP *pp)
{
    Line *ln = L(pp, pp->li);
    SmToken *f = LT(pp, pp->li, 0);

    if (sm_tok_is_kw(f, "CREATE")) {
        parse_create(pp);
    } else if (sm_tok_is_kw(f, "DEFINE")) {
        parse_define(pp);
    } else if (sm_tok_is_kw(f, "ON")) {
        parse_event(pp, NULL);
    } else if (sm_tok_is_kw(f, "ADD") || sm_tok_is_kw(f, "REMOVE") ||
               sm_tok_is_kw(f, "SET") || sm_tok_is_kw(f, "SWAP") ||
               sm_tok_is_kw(f, "MOVE") || sm_tok_is_kw(f, "SEND")) {
        parse_action(pp, NULL, ln->start, ln->start + ln->count, ln->line);
        ++pp->li;
    } else if (sm_tok_is_kw(f, "LOAD") || sm_tok_is_kw(f, "END")) {
        ++pp->li; /* LOAD (no transport in v1) / stray END: skip */
    } else {
        fail(pp, ln->line, "unexpected statement");
    }
}

SmStatus sm_doc_load_string(SmDoc *d, const char *src, char *err, size_t errcap)
{
    PP pp;
    SmStatus st;

    memset(&pp, 0, sizeof(pp));
    pp.d = d;
    pp.w = sm_doc_world(d);
    pp.err = err;
    pp.errcap = errcap;
    if (err != NULL && errcap > 0) { err[0] = '\0'; }

    st = lex_all(&pp, src);
    if (st == SM_OK && !pp.failed) {
        while (pp.li < pp.nlines && !pp.failed) {
            parse_statement(&pp);
        }
    }

    sm_free(pp.toks);
    sm_free(pp.lines);
    if (st == SM_OK && !pp.failed) { sm_doc_fire_load(d); }
    return pp.failed ? SM_ERR_PARSE : st;
}
