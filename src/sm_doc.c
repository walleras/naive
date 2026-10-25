/* sm_doc.c  --  SeSaML document runtime implementation. */
#include "sm_doc.h"
#include "sm_server.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* layout constants (pixels) */
#define DOC_MARGIN   16
#define DOC_TOPBAR   56
#define DOC_WROW     26   /* default widget row height   */
#define DOC_PADX     12   /* horizontal text padding     */
#define DOC_MINW     48

/* palette */
#define DC_BG        0xF4F4F0UL
#define DC_TEXT      0x101014UL
#define DC_BTN_BG    0xDDE3EEUL
#define DC_BTN_LINE  0x8090A8UL
#define DC_PANE_BG   0xFFFFFFUL
#define DC_PANE_LINE 0xC0C0C8UL
#define DC_INPUT_BG  0xFFFFFFUL
#define DC_INPUT_LN  0x707078UL
#define DC_FOCUS_LN  0x1560D0UL
#define DC_HINT      0x909098UL
#define DC_LINK      0x1550C8UL   /* hyperlink text/underline */

/* navigation bar (browser chrome) */
#define NAV_H        34
#define NB_BG        0xE6E8ECUL
#define NB_LINE      0xB0B4BCUL
#define NB_BTN_BG    0xDCE1EAUL
#define NB_BTN_DIS   0xEDEEF0UL
#define NB_TXT       0x202428UL
#define NB_TXT_DIS   0xA8ACB4UL
#define NB_ADDR_BG   0xFFFFFFUL

typedef struct {
    SmActKind kind;
    char     *a;
    char     *b;
    char     *prop;
    SmExpr   *expr;
} Action;

struct SmHandler {
    SmEventType ev;
    SmSet      *owner;
    char       *of_name;
    SmExpr     *where;
    Action     *acts;
    int         nacts, cacts;
    SmHandler  *next;
};

typedef struct { SmSet *set; SmRect r; } Hit;

/* deferred navigation kinds (resolved at the next paint) */
#define NAV_NONE    0
#define NAV_ROUTE   1
#define NAV_BACK    2
#define NAV_FORWARD 3
#define NAV_POST    4

struct SmDoc {
    SmWorld   *w;
    SmHandler *handlers;   /* singly-linked, appended in source order */
    Hit       *hits;
    int        nhits, chits;
    SmSet     *focus;      /* focused TextInput, or NULL */

    /* back/forward history: routes visited, with a cursor at the current one */
    char     **hist;
    int        hist_count, hist_cap, hist_cur;

    /* a click may request navigation; it is deferred to the next paint so the
     * world is not torn down mid-handler */
    int        pending_kind;
    char      *pending_route;
    char      *pending_body;   /* urlencoded body for a deferred POST */

    /* address-bar editing */
    int        addr_editing;
    int        addr_fresh;   /* buffer shows the current URL; first key replaces */
    char       addr_buf[160];

    int        last_w, last_h; /* client size from the last paint (for hit-test) */

    SmSet     *open_select;    /* the Select whose dropdown is open, or NULL */
    SmRect     open_rect;      /* that Select's closed-box rect (for the list) */
    char      *last_submit;    /* query string from the most recent form submit */

    int        change_depth;   /* bounds SET-triggered CHANGE cascades (below) */

    double     grid_width;      /* DEFINE GRID X n; 0 = no grid (raw pixels) */
    double     grid_height;     /* ... , Y n; 0 = not given (Y floats, uses X's scale) */
    int        scroll_x, scroll_y;       /* current scroll offset, actual pixels */
    int        content_right, content_bottom; /* max extent seen in the last paint */

    char       status[160];
};

/* ---- lifecycle ---------------------------------------------------------- */

/* Seed the built-in styling and colour sets. Styling is membership (design
 * doc S10): a widget that is MEMBER OF BOLD renders bold; MEMBER OF RED renders
 * red. Each carries an @style bit or an @color value the renderer reads off the
 * widget's parents. */
static void seed_style(SmDoc *d, const char *name, unsigned bit)
{
    SmSet *s = sm_set_create(d->w, name, SM_KIND_SET);
    if (s != NULL) {
        s->flags |= SM_FLAG_BUILTIN;
        sm_set_prop(s, "@style", sm_value_number((double)bit));
    }
}

static void seed_color(SmDoc *d, const char *name, unsigned long rgb)
{
    SmSet *s = sm_set_create(d->w, name, SM_KIND_SET);
    if (s != NULL) {
        SmSet *colors = sm_find(d->w, "COLORS");
        s->flags |= SM_FLAG_BUILTIN;
        sm_set_prop(s, "@color", sm_value_color(rgb));
        if (colors != NULL) { sm_add_member(d->w, colors, s); }
    }
}

static void seed_builtins(SmDoc *d)
{
    seed_style(d, "BOLD", SM_FS_BOLD);
    seed_style(d, "ITALICS", SM_FS_ITALIC);
    seed_style(d, "ITALIC", SM_FS_ITALIC);
    seed_style(d, "UNDERLINE", SM_FS_UNDERLINE);
    seed_style(d, "STRIKETHROUGH", SM_FS_STRIKE);
    seed_style(d, "MONOSPACE", SM_FS_MONO);

    /* FOCUS: the set holding the widget that currently has keyboard focus (at
     * most one member). The runtime keeps it current; a page only reads it
     * ("W IS FOCUS") or reacts to it (ON CHANGE OF FOCUS ...). */
    {
        SmSet *focus = sm_set_create(d->w, "FOCUS", SM_KIND_SET);
        if (focus != NULL) { focus->flags |= SM_FLAG_BUILTIN; }
    }

    /* COLORS: the set of all colour sets below (each colour is a member) */
    {
        SmSet *colors = sm_set_create(d->w, "COLORS", SM_KIND_SET);
        if (colors != NULL) { colors->flags |= SM_FLAG_BUILTIN; }
    }

    /* the 16 CGA colours named in the design doc */
    seed_color(d, "BLACK",        0x000000UL);
    seed_color(d, "WHITE",        0xFFFFFFUL);
    seed_color(d, "RED",          0xC00000UL);
    seed_color(d, "GREEN",        0x008000UL);
    seed_color(d, "BLUE",         0x0000C0UL);
    seed_color(d, "CYAN",         0x00A0A0UL);
    seed_color(d, "MAGENTA",      0xB000B0UL);
    seed_color(d, "YELLOW",       0xB8A000UL);
    seed_color(d, "GRAY",         0x808080UL);
    seed_color(d, "DARK_RED",     0x600000UL);
    seed_color(d, "DARK_GREEN",   0x004000UL);
    seed_color(d, "DARK_BLUE",    0x000060UL);
    seed_color(d, "DARK_CYAN",    0x005050UL);
    seed_color(d, "DARK_MAGENTA", 0x500050UL);
    seed_color(d, "DARK_YELLOW",  0x606000UL);
    seed_color(d, "DARK_GRAY",    0x404040UL);
}

SmDoc *sm_doc_new(void)
{
    SmDoc *d = (SmDoc *)sm_alloc(sizeof(SmDoc));
    if (d == NULL) { return NULL; }
    d->w = sm_world_new();
    if (d->w == NULL) { sm_free(d); return NULL; }
    d->w->doc = d;
    seed_builtins(d);
    return d;
}

static void action_clear(Action *ac)
{
    sm_free(ac->a);
    sm_free(ac->b);
    sm_free(ac->prop);
    sm_expr_free(ac->expr);
}

static void free_handlers(SmDoc *d)
{
    SmHandler *h = d->handlers;
    while (h != NULL) {
        SmHandler *next = h->next;
        int i;
        for (i = 0; i < h->nacts; ++i) { action_clear(&h->acts[i]); }
        sm_free(h->acts);
        sm_free(h->of_name);
        sm_expr_free(h->where);
        sm_free(h);
        h = next;
    }
    d->handlers = NULL;
}

void sm_doc_free(SmDoc *d)
{
    int i;
    if (d == NULL) { return; }
    free_handlers(d);
    sm_free(d->hits);
    sm_free(d->pending_route);
    sm_free(d->pending_body);
    sm_free(d->last_submit);
    for (i = 0; i < d->hist_count; ++i) { sm_free(d->hist[i]); }
    sm_free(d->hist);
    sm_world_free(d->w);
    sm_free(d);
}

/* Tear down the current page (handlers, hit list, set world) and start a fresh
 * empty world, ready to parse a new document into. The SmDoc shell survives so
 * the window keeps running across a navigation. */
static void sm_doc_reset(SmDoc *d)
{
    free_handlers(d);
    sm_free(d->hits);
    d->hits = NULL;
    d->nhits = 0;
    d->chits = 0;
    d->focus = NULL;
    d->open_select = NULL;
    d->change_depth = 0;
    d->grid_width = 0.0;
    d->grid_height = 0.0;
    d->scroll_x = 0;
    d->scroll_y = 0;
    d->status[0] = '\0';
    sm_world_free(d->w);
    d->w = sm_world_new();
    if (d->w != NULL) { d->w->doc = d; }
    seed_builtins(d);
}

void sm_doc_set_grid(SmDoc *d, double width, double height)
{
    if (d == NULL || width <= 0.0) { return; }
    d->grid_width = width;
    d->grid_height = (height > 0.0) ? height : 0.0;
}

SmWorld *sm_doc_world(SmDoc *d) { return (d != NULL) ? d->w : NULL; }

/* ---- builder API -------------------------------------------------------- */

SmHandler *sm_doc_add_handler(SmDoc *d, SmEventType ev, SmSet *owner,
                              const char *of_name, SmExpr *where)
{
    SmHandler *h, *tail;
    if (d == NULL) { return NULL; }
    h = (SmHandler *)sm_alloc(sizeof(SmHandler));
    if (h == NULL) { return NULL; }
    h->ev = ev;
    h->owner = owner;
    h->of_name = (of_name != NULL) ? sm_strdup(of_name) : NULL;
    h->where = where;
    if (d->handlers == NULL) {
        d->handlers = h;
    } else {
        tail = d->handlers;
        while (tail->next != NULL) { tail = tail->next; }
        tail->next = h;
    }
    return h;
}

SmStatus sm_handler_add_action(SmHandler *h, SmActKind kind,
                               const char *a, const char *b,
                               const char *prop, SmExpr *expr)
{
    Action *ac;
    if (h == NULL) { sm_expr_free(expr); return SM_ERR_NOT_FOUND; }
    if (h->nacts >= h->cacts) {
        int nc = (h->cacts == 0) ? 4 : h->cacts * 2;
        Action *grown = (Action *)sm_realloc(h->acts, (size_t)nc * sizeof(Action));
        if (grown == NULL) { sm_expr_free(expr); return SM_ERR_OOM; }
        h->acts = grown;
        h->cacts = nc;
    }
    ac = &h->acts[h->nacts++];
    ac->kind = kind;
    ac->a = (a != NULL) ? sm_strdup(a) : NULL;
    ac->b = (b != NULL) ? sm_strdup(b) : NULL;
    ac->prop = (prop != NULL) ? sm_strdup(prop) : NULL;
    ac->expr = expr;
    return SM_OK;
}

void sm_doc_place(SmDoc *d, SmSet *s,
                  SmBool x_is_num, double x_num, const char *x_name,
                  SmBool y_is_num, double y_num, const char *y_name,
                  SmBool top_level)
{
    if (d == NULL || s == NULL) { return; }
    sm_set_prop(s, "@x", x_is_num ? sm_value_number(x_num)
                                  : sm_value_string(x_name != NULL ? x_name : "Left"));
    sm_set_prop(s, "@y", y_is_num ? sm_value_number(y_num)
                                  : sm_value_string(y_name != NULL ? y_name : "Top"));
    /* Top-level widgets are visible by default; nested ones inherit through
     * their container's membership in VISIBLE. */
    if (top_level && d->w->visible != NULL) {
        sm_add_member(d->w, d->w->visible, s);
    }
}

/* ---- visibility (transitive membership in VISIBLE) ---------------------- */

static SmBool visible_reaches(SmWorld *w, SmSet *from, const SmSet *target)
{
    unsigned stamp = ++w->visit_seq;
    SmSet **stack = NULL;
    int n = 0, cap = 0;
    SmBool found = SM_FALSE;
    int i;
    stack = (SmSet **)sm_alloc(sizeof(SmSet *) * 8);
    if (stack == NULL) { return SM_FALSE; }
    cap = 8;
    stack[n++] = from;
    while (n > 0) {
        SmSet *cur = stack[--n];
        if (cur->visit == stamp) { continue; }
        cur->visit = stamp;
        if (cur == target) { found = SM_TRUE; break; }
        for (i = 0; i < cur->nmembers; ++i) {
            if (n >= cap) {
                SmSet **g = (SmSet **)sm_realloc(stack, sizeof(SmSet *) * (size_t)(cap * 2));
                if (g == NULL) { goto done; }
                stack = g; cap *= 2;
            }
            stack[n++] = cur->members[i];
        }
    }
done:
    sm_free(stack);
    return found;
}

SmBool sm_doc_is_visible(SmDoc *d, const char *name)
{
    SmSet *s;
    if (d == NULL) { return SM_FALSE; }
    s = sm_find(d->w, name);
    if (s == NULL || d->w->visible == NULL) { return SM_FALSE; }
    return visible_reaches(d->w, d->w->visible, s);
}

/* ---- action execution --------------------------------------------------- */

static SmBool fire_handlers(SmDoc *d, SmEventType ev, SmSet *owner); /* below */

/* The member of Namespace-kind `owner` named `member_name`, or NULL. Mirrors
 * EX_DOT's read-side fallback (sm_expr.c) so a namespaced cell's value can be
 * both read (`Sheet1.A1` in an expression) and written (`SET Sheet1.A1 TO
 * ...`) the same way. */
static SmSet *resolve_namespaced_member(SmSet *owner, const char *member_name)
{
    int i;
    if (owner == NULL || owner->kind != SM_KIND_NAMESPACE || member_name == NULL) {
        return NULL;
    }
    for (i = 0; i < owner->nmembers; ++i) {
        if (owner->members[i]->name != NULL &&
            sm_stricmp(owner->members[i]->name, member_name) == 0) {
            return owner->members[i];
        }
    }
    return NULL;
}

static void bind_single(SmWorld *w, SmSet *holder, SmSet *target)
{
    while (holder->nmembers > 0) {
        sm_remove_member(w, holder, holder->members[0]);
    }
    if (target != NULL) { sm_add_member(w, holder, target); }
}

/* A SET action's target fires its own CHANGE handlers, same as a real HTML
 * input firing "change" on assignment -- this is what lets a page (e.g. a
 * hand-authored spreadsheet, design doc S15) cascade a dependency chain using
 * nothing but ordinary ON CHANGE / SET handlers, no engine-level formula
 * concept. Depth-bounded so a page with an accidental handler cycle (A's
 * change handler SETs B, B's change handler SETs A) can't hang the app. */
#define CHANGE_CASCADE_MAX 64
static void cascade_change(SmDoc *d, SmSet *s)
{
    if (s == NULL || d->change_depth >= CHANGE_CASCADE_MAX) { return; }
    ++d->change_depth;
    fire_handlers(d, SM_EV_CHANGE, s);
    --d->change_depth;
}

void sm_doc_exec_action(SmDoc *d, SmActKind kind, SmSet *a, SmSet *b,
                        SmSet *c, const char *prop, SmExpr *expr)
{
    SmWorld *w = d->w;

    switch (kind) {
    case SM_ACT_ADD:
        /* ADD a TO b represents author intent ("a belongs to b"), so it is
         * subject to namespace exclusivity, unlike the engine's own internal
         * dependency wiring (see sm_check_namespace_exclusive). */
        if (a != NULL && b != NULL && sm_check_namespace_exclusive(b, a) == SM_OK) {
            sm_add_member(w, b, a);
        }
        break;
    case SM_ACT_REMOVE:
        if (a != NULL && b != NULL) { sm_remove_member(w, b, a); }
        break;
    case SM_ACT_SET:
        if (a != NULL && a->kind == SM_KIND_NAMESPACE && prop != NULL) {
            /* SET Namespace.member TO <expr> -- unlike an ordinary a.prop,
             * a Namespace's dotted access always means "the member named
             * this", never a property on the namespace itself (see EX_DOT's
             * evaluator, which resolves reads the same way). */
            SmSet *target = resolve_namespaced_member(a, prop);
            if (target != NULL) {
                SmValue v = (expr != NULL) ? sm_expr_eval(expr, w, target)
                                               : sm_value_none();
                sm_value_clear(&target->value);
                target->value = v;
                cascade_change(d, target);
            }
        } else if (a != NULL && prop != NULL && a->kind != SM_KIND_COLOR &&
                   sm_stricmp(prop, "value") == 0) {
            /* SET x.value TO e: x's own value, the mirror of reading x.value */
            SmValue v = (expr != NULL) ? sm_expr_eval(expr, w, a) : sm_value_none();
            sm_value_clear(&a->value);
            a->value = v;
            cascade_change(d, a);
        } else if (a != NULL && prop != NULL) {
            SmValue v = (expr != NULL) ? sm_expr_eval(expr, w, a)
                                           : sm_value_none();
            sm_set_prop(a, prop, v);
            cascade_change(d, a);
        } else if (a != NULL && b != NULL) {
            bind_single(w, a, b);            /* SET x TO <bare set name> */
            cascade_change(d, a);
        } else if (a != NULL && expr != NULL) {
            SmValue v = sm_expr_eval(expr, w, a);   /* SET x TO <expression>; evaluate
                                                     * BEFORE clearing: x = x + 1 */
            sm_value_clear(&a->value);
            a->value = v;
            cascade_change(d, a);
        }
        break;
    case SM_ACT_SWAP:
        if (a != NULL && b != NULL) { bind_single(w, a, b); }
        break;
    case SM_ACT_MOVE:
        if (a != NULL && b != NULL) {
            const SmValue *bx = sm_get_prop(b, "@x");
            const SmValue *by = sm_get_prop(b, "@y");
            if (bx != NULL) { sm_set_prop(a, "@x", sm_value_copy(bx)); }
            if (by != NULL) { sm_set_prop(a, "@y", sm_value_copy(by)); }
        }
        break;
    case SM_ACT_TRANSFER:
        if (a != NULL && b != NULL && c != NULL &&
            sm_check_namespace_exclusive(c, a) == SM_OK) {
            sm_remove_member(w, b, a);
            sm_add_member(w, c, a);
        }
        break;
    case SM_ACT_CALL:
        if (expr != NULL) {
            SmValue v = sm_expr_eval(expr, w, NULL);   /* runs the user function */
            sm_value_clear(&v);
        }
        break;
    case SM_ACT_SEND:
        /* recorded but not transported in v1 */
        break;
    }
}

static void run_action(SmDoc *d, const Action *ac)
{
    SmWorld *w = d->w;
    SmSet *a = (ac->a != NULL) ? sm_lookup(w, ac->a) : NULL;
    SmSet *b = (ac->b != NULL) ? sm_lookup(w, ac->b) : NULL;
    SmSet *c = (ac->kind == SM_ACT_TRANSFER && ac->prop != NULL)
                   ? sm_lookup(w, ac->prop) : NULL;
    sm_doc_exec_action(d, ac->kind, a, b, c,
                       (ac->kind == SM_ACT_TRANSFER) ? NULL : ac->prop, ac->expr);
}

static SmBool fire_handlers(SmDoc *d, SmEventType ev, SmSet *owner)
{
    SmHandler *h;
    SmBool ran = SM_FALSE;
    for (h = d->handlers; h != NULL; h = h->next) {
        if (h->ev != ev) { continue; }
        if (h->of_name != NULL) {
            /* "ON CHANGE OF FOCUS ...": an OF clause names the set the handler
             * listens on, wherever the handler was written */
            if (owner == NULL || owner->name == NULL ||
                sm_stricmp(h->of_name, owner->name) != 0) { continue; }
        } else if (h->owner != owner) {
            continue;
        }
        if (h->where != NULL) {
            SmValue g = sm_expr_eval(h->where, d->w, owner);
            SmBool ok = sm_value_truthy(&g);
            sm_value_clear(&g);
            if (!ok) { continue; }
        }
        {
            int i;
            SmEnv *saved = d->w->env;   /* handlers never see a function's locals */
            d->w->env = NULL;
            for (i = 0; i < h->nacts; ++i) { run_action(d, &h->acts[i]); }
            d->w->env = saved;
        }
        ran = SM_TRUE;
    }
    return ran;
}

static void commit_content(SmDoc *d, SmSet *cell); /* defined below, in rendering */

/* Fire standalone (page-level) ON LOAD handlers once, called by the parser
 * right after a document finishes loading -- the generic "the page is ready"
 * hook (real HTML/JS's onload). First, every widget with a "content" property
 * is committed (see commit_content): a plain `WITH content = "5"` becomes a
 * usable `.value` with no ceremony, and `WITH content = "=A1+A2"` wires its
 * dependency on A1/A2 as membership and computes its result -- a page needs
 * no author-written ON LOAD or ON CHANGE handler for this at all; it falls
 * out of dependency-as-membership + native delta propagation (recompute_from,
 * called by commit_content), same mechanism the retired standalone Webcel
 * used, just general to any widget's own "content" instead of a bespoke
 * kind. Order among cells does not matter: each commit's own trailing
 * recompute re-triggers correctly regardless of declaration order. */
void sm_doc_fire_load(SmDoc *d)
{
    int i;
    for (i = 0; i < d->w->nall; ++i) {
        SmSet *s = d->w->all[i];
        if (s->value.kind == SM_VAL_NONE && sm_get_prop(s, "content") != NULL) {
            commit_content(d, s);
        }
    }
    fire_handlers(d, SM_EV_LOAD, NULL);
}

SmBool sm_doc_fire_click(SmDoc *d, const char *widget_name)
{
    SmSet *s;
    if (d == NULL) { return SM_FALSE; }
    s = sm_find(d->w, widget_name);
    if (s == NULL) { return SM_FALSE; }
    return fire_handlers(d, SM_EV_CLICK, s);
}

/* ---- loading ------------------------------------------------------------ */
/* sm_doc_load_string is defined in sm_parse.c (it needs the parser); the file
 * loader is here since it is pure I/O. */

SmStatus sm_doc_load_file(SmDoc *d, const char *path, char *err, size_t errcap)
{
    FILE *f;
    long size;
    char *buf;
    size_t got;
    SmStatus st;

    f = fopen(path, "rb");
    if (f == NULL) {
        if (err != NULL && errcap) { snprintf(err, errcap, "cannot open file"); }
        return SM_ERR_NOT_FOUND;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return SM_ERR_RANGE; }
    buf = (char *)sm_alloc((size_t)size + 1);
    if (buf == NULL) { fclose(f); return SM_ERR_OOM; }
    got = fread(buf, 1, (size_t)size, f);
    buf[got] = '\0';
    fclose(f);
    st = sm_doc_load_string(d, buf, err, errcap);
    sm_free(buf);
    return st;
}

/* ---- navigation --------------------------------------------------------- */

/* Replace the page with a generated error page carrying `msg` and a link home.
 * Built at runtime so there is no compile-time string-literal length limit. */
static void load_error_page(SmDoc *d, const char *msg)
{
    char buf[512];
    snprintf(buf, sizeof(buf),
        "CREATE ErrTitle AS TextBox WITH text = \"Request failed\""
        " AT X.Center, Y.Top\n"
        "CREATE ErrMsg AS TextBox WITH text = \"%.200s\""
        " AT X.Center, Y.Center\n"
        "CREATE HomeLink AS TextBox WITH link = \"/\", text = \"<- back home\""
        " AT X.Center, Y.300\n",
        (msg != NULL) ? msg : "unknown error");
    sm_doc_load_string(d, buf, NULL, 0);
}

/* Fetch a route and swap it in as the current page. Does NOT touch history --
 * callers decide whether this is a new visit (push) or a back/forward move. */
static SmStatus load_route(SmDoc *d, const char *route)
{
    char *body = NULL;
    char err[192];
    SmStatus st;

    st = sm_server_get(route, &body, err, sizeof(err));
    sm_doc_reset(d);
    if (st == SM_OK) {
        char perr[192];
        if (sm_doc_load_string(d, body, perr, sizeof(perr)) != SM_OK) {
            load_error_page(d, perr);
            snprintf(d->status, sizeof(d->status), "GET %.100s  500 malformed page", route ? route : "");
        } else {
            snprintf(d->status, sizeof(d->status), "GET %.120s  200 OK", route ? route : "");
        }
        sm_free(body);
    } else {
        load_error_page(d, err);
        snprintf(d->status, sizeof(d->status), "GET %.120s  404 Not Found", route ? route : "");
        st = SM_ERR_NOT_FOUND;
    }
    return st;
}

static void history_push(SmDoc *d, const char *route)
{
    /* drop any forward entries: a new navigation forks the timeline */
    while (d->hist_count > d->hist_cur + 1) {
        sm_free(d->hist[--d->hist_count]);
    }
    if (d->hist_count >= d->hist_cap) {
        int nc = (d->hist_cap == 0) ? 8 : d->hist_cap * 2;
        char **g = (char **)sm_realloc(d->hist, (size_t)nc * sizeof(char *));
        if (g == NULL) { return; }
        d->hist = g;
        d->hist_cap = nc;
    }
    d->hist[d->hist_count++] = sm_strdup(route != NULL ? route : "/");
    d->hist_cur = d->hist_count - 1;
}

SmStatus sm_doc_navigate(SmDoc *d, const char *route)
{
    if (d == NULL) { return SM_ERR_NOT_FOUND; }
    history_push(d, route);
    return load_route(d, route);
}

SmStatus sm_doc_post(SmDoc *d, const char *route, const char *body)
{
    char *resp = NULL;
    char err[192];
    SmStatus st;
    if (d == NULL) { return SM_ERR_NOT_FOUND; }
    history_push(d, route);
    st = sm_server_post(route, body, &resp, err, sizeof(err));
    sm_doc_reset(d);
    if (st == SM_OK) {
        char perr[192];
        if (sm_doc_load_string(d, resp, perr, sizeof(perr)) != SM_OK) {
            load_error_page(d, perr);
            snprintf(d->status, sizeof(d->status), "POST %.90s  500 malformed", route ? route : "");
        } else {
            snprintf(d->status, sizeof(d->status), "POST %.110s  200 OK", route ? route : "");
        }
        sm_free(resp);
    } else {
        load_error_page(d, err);
        snprintf(d->status, sizeof(d->status), "POST %.110s  404 Not Found", route ? route : "");
        st = SM_ERR_NOT_FOUND;
    }
    return st;
}

/* Open a local .sml file directly (drag-and-drop, or an OPEN action), as a new
 * history entry -- so Back returns to whatever page was showing before. */
SmStatus sm_doc_open_file(SmDoc *d, const char *path)
{
    char perr[192];
    SmStatus st;
    if (d == NULL || path == NULL) { return SM_ERR_NOT_FOUND; }
    history_push(d, path);
    sm_doc_reset(d);
    st = sm_doc_load_file(d, path, perr, sizeof(perr));
    if (st != SM_OK) {
        load_error_page(d, perr);
        snprintf(d->status, sizeof(d->status), "OPEN %.140s  failed", path);
        return SM_ERR_NOT_FOUND;
    }
    snprintf(d->status, sizeof(d->status), "OPEN %.150s", path);
    return SM_OK;
}

static SmBool can_back(const SmDoc *d)    { return d->hist_cur > 0 ? SM_TRUE : SM_FALSE; }
static SmBool can_forward(const SmDoc *d) { return d->hist_cur + 1 < d->hist_count ? SM_TRUE : SM_FALSE; }

static const char *current_route(const SmDoc *d)
{
    return (d->hist_count > 0) ? d->hist[d->hist_cur] : "/";
}

const char *sm_doc_route(SmDoc *d)
{
    return (d != NULL) ? current_route(d) : "/";
}

void sm_doc_back(SmDoc *d)
{
    if (d != NULL && can_back(d)) { --d->hist_cur; load_route(d, d->hist[d->hist_cur]); }
}

void sm_doc_forward(SmDoc *d)
{
    if (d != NULL && can_forward(d)) { ++d->hist_cur; load_route(d, d->hist[d->hist_cur]); }
}

/* Give a set a native `.value` derived from its "content" text: a number if
 * it parses as one, else the string itself. This is what makes `.value` (the
 * field expressions read via a bare name, e.g. `A1 + A2`) agree with whatever
 * text a field is actually showing -- generic numeric-input coercion, not
 * specific to any one page. Used both when an edit commits (Enter) and once
 * for every widget when a page finishes loading, so a plain `WITH content =
 * "5"` is already usable in an expression with no extra ceremony. */
static void coerce_value_from_content(SmSet *s)
{
    const SmValue *content = sm_get_prop(s, "content");
    char *end;
    double num;
    if (content == NULL || content->kind != SM_VAL_STRING) { return; }
    num = strtod(content->string, &end);
    while (*end == ' ' || *end == '\t') { ++end; }
    sm_value_clear(&s->value);
    if (content->string[0] != '\0' && *end == '\0') {
        s->value = sm_value_number(num);
    } else {
        s->value = sm_value_string(content->string);
    }
}

/* ---- formulas: dependency-as-membership + native delta propagation ------
 * A widget's "content" may start with '=' to become a live formula, exactly
 * the mechanism the retired standalone Webcel used internally (a formula cell
 * literally CONTAINS its referenced cells as members), just generalized to
 * ANY widget's ordinary "content" property instead of a bespoke kind. No
 * page-authored ON CHANGE handler is needed for the cascade: recompute_from
 * uses sm_mark_dirty -- the same core engine primitive that already drives
 * VISIBLE-reachability -- to walk up parent pointers and find every
 * transitive dependent automatically. */

static void recompute_from(SmDoc *d, SmSet *changed)
{
    SmWorld *w = d->w;
    int pass, i;
    if (changed == NULL) { return; }
    sm_mark_dirty(w, changed);
    for (pass = 0; pass <= w->nall + 1; ++pass) {
        int any = 0;
        for (i = 0; i < w->nall; ++i) {
            SmSet *s = w->all[i];
            const SmValue *content;
            if (!SM_HAS_FLAG(s, SM_FLAG_DIRTY)) { continue; }
            s->flags &= ~SM_FLAG_DIRTY;
            content = sm_get_prop(s, "content");
            if (content != NULL && content->kind == SM_VAL_STRING &&
                content->string[0] == '=') {
                SmExpr *e = NULL;
                char err[96];
                SmValue nv;
                if (sm_expr_parse(content->string + 1, &e, err, sizeof(err)) != SM_OK) {
                    nv = sm_value_string("#ERR");
                } else {
                    nv = sm_expr_eval(e, w, s);
                    sm_expr_free(e);
                }
                if (!sm_value_equal(&nv, &s->value)) {
                    sm_value_clear(&s->value);
                    s->value = nv;
                    { int k; for (k = 0; k < s->nparents; ++k) { sm_mark_dirty(w, s->parents[k]); } }
                    any = 1;
                } else {
                    sm_value_clear(&nv);
                }
            }
        }
        if (!any) { break; }
    }
}

/* Commit a widget's CURRENT "content" text as its value: a leading '=' means
 * a formula (parse; wire dependencies as membership, resolving a namespaced
 * "Sheet1.A1" reference to the actual member A1 -- sm_expr_collect_names does
 * that resolution generically; #CYCLE/#ERR on a rejected or bad formula),
 * else the existing numeric/string coercion. Then recompute_from propagates
 * to everything that (transitively) depends on this cell. This one function
 * is what both page-load seeding and an interactive Enter-commit funnel
 * through, so a spreadsheet needs no author-written ON CHANGE handler at all
 * -- wiring + native delta propagation does the entire cascade. */
static void commit_content(SmDoc *d, SmSet *cell)
{
    SmWorld *w = d->w;
    const SmValue *content;
    if (cell == NULL) { return; }
    content = sm_get_prop(cell, "content");

    while (cell->nmembers > 0) { sm_remove_member(w, cell, cell->members[0]); }

    if (content != NULL && content->kind == SM_VAL_STRING && content->string[0] == '=') {
        SmExpr *e = NULL;
        char err[96];
        sm_value_clear(&cell->value);
        if (sm_expr_parse(content->string + 1, &e, err, sizeof(err)) != SM_OK) {
            cell->value = sm_value_string("#ERR");
        } else {
            const char *names[64];
            int nn, k;
            SmBool cyc = SM_FALSE;
            nn = sm_expr_collect_names(e, w, names, 64);
            if (nn > 64) { nn = 64; }
            for (k = 0; k < nn; ++k) {
                SmSet *dep = sm_find(w, names[k]);
                if (dep != NULL && dep != cell) {
                    if (sm_add_member(w, cell, dep) == SM_ERR_CYCLE) { cyc = SM_TRUE; }
                }
            }
            if (cyc) {
                sm_expr_free(e);
                while (cell->nmembers > 0) { sm_remove_member(w, cell, cell->members[0]); }
                cell->value = sm_value_string("#CYCLE");
            } else {
                cell->value = sm_expr_eval(e, w, cell);
                sm_expr_free(e);
            }
        }
    } else {
        coerce_value_from_content(cell);
    }
    recompute_from(d, cell);
}

/* ---- rendering ---------------------------------------------------------- */

static const char *widget_text(SmSet *s)
{
    const SmValue *v;
    v = sm_get_prop(s, "label");
    if (v == NULL) { v = sm_get_prop(s, "text"); }
    if (v == NULL) { v = sm_get_prop(s, "content"); }
    if (v != NULL && v->kind == SM_VAL_STRING) { return v->string; }
    return NULL;
}

/* DEFINE GRID's per-axis scale factors. X: grid_width virtual units always
 * span the actual window width. Y, if grid_height was given, independently
 * spans the actual window height -- so a "cell" need not be square; if not
 * given, Y floats and just reuses X's own scale (stays square, unbounded).
 * No grid defined at all -> 1.0 on both axes (raw pixels, unchanged
 * pre-existing behavior). Neither scale implies a boundary; a coordinate
 * past either edge is simply off the currently visible area (see the scroll
 * mechanism below), never rejected or clamped. */
static double grid_scale_x(const SmDoc *d, int win_w)
{
    return (d->grid_width > 0.0) ? (double)win_w / d->grid_width : 1.0;
}

static double grid_scale_y(const SmDoc *d, int win_w, int win_h)
{
    if (d->grid_height > 0.0) { return (double)win_h / d->grid_height; }
    return grid_scale_x(d, win_w);
}

static int resolve_x(const SmDoc *d, SmSet *s, int win_w, int ww)
{
    const SmValue *v = sm_get_prop(s, "@x");
    if (v == NULL) { return DOC_MARGIN - d->scroll_x; }
    if (v->kind == SM_VAL_NUMBER) {
        return (int)(v->number * grid_scale_x(d, win_w)) - d->scroll_x;
    }
    if (v->kind == SM_VAL_STRING) {
        const char *n = v->string;
        if (sm_stricmp(n, "Center") == 0) { return (win_w - ww) / 2 - d->scroll_x; }
        if (sm_stricmp(n, "Right") == 0)  { return win_w - ww - DOC_MARGIN - d->scroll_x; }
        /* Left / Nav / Toolbar / default */
        return DOC_MARGIN - d->scroll_x;
    }
    return DOC_MARGIN - d->scroll_x;
}

static int resolve_y(const SmDoc *d, SmSet *s, int win_w, int win_h, int wh)
{
    const SmValue *v = sm_get_prop(s, "@y");
    if (v == NULL) { return DOC_MARGIN - d->scroll_y; }
    if (v->kind == SM_VAL_NUMBER) {
        return (int)(v->number * grid_scale_y(d, win_w, win_h)) - d->scroll_y;
    }
    if (v->kind == SM_VAL_STRING) {
        const char *n = v->string;
        if (sm_stricmp(n, "Center") == 0 || sm_stricmp(n, "Mid") == 0) {
            return (win_h - wh) / 2 - d->scroll_y;
        }
        if (sm_stricmp(n, "Main") == 0)   { return DOC_TOPBAR - d->scroll_y; }
        if (sm_stricmp(n, "Bottom") == 0) { return win_h - wh - DOC_MARGIN - d->scroll_y; }
        return DOC_MARGIN - d->scroll_y; /* Top / default */
    }
    return DOC_MARGIN;
}

/* Kinds the main paint loop lays out directly. LISTITEM / TABLEROW / TABLECELL
 * are drawn by their container (List / Table), so they are excluded here. */
static SmBool is_widget_kind(SmKind k)
{
    switch (k) {
    case SM_KIND_TEXTBOX: case SM_KIND_TEXTINPUT: case SM_KIND_BUTTON:
    case SM_KIND_CHECKBOX: case SM_KIND_RADIO: case SM_KIND_IMAGE:
    case SM_KIND_CONTENTPANE: case SM_KIND_NAVITEM: case SM_KIND_TOOLBARITEM:
    case SM_KIND_HEADING: case SM_KIND_PARAGRAPH: case SM_KIND_HRULE:
    case SM_KIND_LIST: case SM_KIND_TABLE:
    case SM_KIND_TEXTAREA: case SM_KIND_SELECT:
        return SM_TRUE;
    default:
        return SM_FALSE;
    }
}

/* Font style bits from a widget's membership in styling sets (BOLD, ITALICS,
 * ...) plus convenience boolean properties (bold, italic, ...). */
static unsigned widget_style(const SmSet *s)
{
    unsigned st = 0;
    int i;
    const SmValue *v;
    for (i = 0; i < s->nparents; ++i) {
        v = sm_get_prop(s->parents[i], "@style");
        if (v != NULL && v->kind == SM_VAL_NUMBER) { st |= (unsigned)v->number; }
    }
    if (sm_value_truthy(sm_get_prop(s, "bold")))          { st |= SM_FS_BOLD; }
    if (sm_value_truthy(sm_get_prop(s, "italic")))        { st |= SM_FS_ITALIC; }
    if (sm_value_truthy(sm_get_prop(s, "underline")))     { st |= SM_FS_UNDERLINE; }
    if (sm_value_truthy(sm_get_prop(s, "strikethrough"))) { st |= SM_FS_STRIKE; }
    return st;
}

/* Text colour from membership in a colour set (@color), a custom Color set's
 * `value`, or a direct `color` property; falls back to `def`. */
static SmColor widget_color(const SmSet *s, SmColor def)
{
    SmColor c = def;
    int i;
    const SmValue *v;
    for (i = 0; i < s->nparents; ++i) {
        v = sm_get_prop(s->parents[i], "@color");
        if (v != NULL && v->kind == SM_VAL_COLOR) { c = v->color; }
        else {
            v = sm_get_prop(s->parents[i], "value");   /* custom Color set */
            if (s->parents[i]->kind == SM_KIND_COLOR &&
                v != NULL && v->kind == SM_VAL_COLOR) { c = v->color; }
        }
    }
    v = sm_get_prop(s, "color");
    if (v != NULL && v->kind == SM_VAL_COLOR) { c = v->color; }
    return c;
}

/* Optional background colour from a `bgcolor` property. */
static SmBool widget_bg(const SmSet *s, SmColor *out)
{
    const SmValue *v = sm_get_prop(s, "bgcolor");
    if (v != NULL && v->kind == SM_VAL_COLOR) { *out = v->color; return SM_TRUE; }
    return SM_FALSE;
}

/* Optional text alignment from an `align` property ("left" / "center" /
 * "right"); `def` when absent. */
static int widget_align(const SmSet *s, int def)
{
    const SmValue *v = sm_get_prop(s, "align");
    if (v != NULL && v->kind == SM_VAL_STRING) {
        if (sm_stricmp(v->string, "right") == 0)  { return SM_ALIGN_RIGHT; }
        if (sm_stricmp(v->string, "center") == 0) { return SM_ALIGN_CENTER; }
        if (sm_stricmp(v->string, "left") == 0)   { return SM_ALIGN_LEFT; }
    }
    return def;
}

static int heading_px(const SmSet *s)
{
    const SmValue *v = sm_get_prop(s, "level");
    int lvl = (v != NULL && v->kind == SM_VAL_NUMBER) ? (int)v->number : 1;
    switch (lvl) {
    case 1:  return 26;
    case 2:  return 22;
    case 3:  return 19;
    case 4:  return 16;
    case 5:  return 14;
    default: return 12;
    }
}

static void request_route(SmDoc *d, const char *route); /* defined below */

/* --- forms ---------------------------------------------------------------
 * The input kinds a Submit gathers. TextBox / Heading / etc. are NOT inputs and
 * are ignored, per the requirement that submit collects only input boxes. */
static SmBool is_input_kind(SmKind k)
{
    return (k == SM_KIND_TEXTINPUT || k == SM_KIND_TEXTAREA ||
            k == SM_KIND_SELECT || k == SM_KIND_CHECKBOX ||
            k == SM_KIND_RADIO) ? SM_TRUE : SM_FALSE;
}

/* The currently-selected option of a Select (its members are options). */
static SmSet *select_option(SmSet *sel)
{
    const SmValue *v = sm_get_prop(sel, "selected");
    int idx = (v != NULL && v->kind == SM_VAL_NUMBER) ? (int)v->number : 0;
    if (sel->nmembers == 0) { return NULL; }
    if (idx < 0 || idx >= sel->nmembers) { idx = 0; }
    return sel->members[idx];
}

static const char *option_value(SmSet *opt)
{
    const SmValue *v = sm_get_prop(opt, "value");
    if (v != NULL && v->kind == SM_VAL_STRING) { return v->string; }
    return widget_text(opt); /* falls back to text/label/content */
}

/* Write an input widget's submitted value into buf. */
static void input_value(SmSet *w, char *buf, size_t cap)
{
    const SmValue *c;
    buf[0] = '\0';
    switch (w->kind) {
    case SM_KIND_TEXTINPUT:
    case SM_KIND_TEXTAREA:
        c = sm_get_prop(w, "content");
        if (c != NULL && c->kind == SM_VAL_STRING) {
            strncpy(buf, c->string, cap - 1); buf[cap - 1] = '\0';
        }
        break;
    case SM_KIND_SELECT: {
        SmSet *o = select_option(w);
        const char *v = (o != NULL) ? option_value(o) : "";
        strncpy(buf, (v != NULL) ? v : "", cap - 1); buf[cap - 1] = '\0';
        break;
    }
    case SM_KIND_CHECKBOX:
    case SM_KIND_RADIO: {
        const SmValue *v = sm_get_prop(w, "value");
        if (v != NULL && v->kind == SM_VAL_STRING) {
            strncpy(buf, v->string, cap - 1); buf[cap - 1] = '\0';
        } else {
            strncpy(buf, "on", cap - 1); buf[cap - 1] = '\0';
        }
        break;
    }
    default:
        break;
    }
}

static const char *field_name(SmSet *w)
{
    const SmValue *v = sm_get_prop(w, "name");
    if (v != NULL && v->kind == SM_VAL_STRING) { return v->string; }
    return (w->name != NULL) ? w->name : "field";
}

/* Append `s` to a query buffer, turning spaces into '+' (minimal encoding). */
static void enc_append(char *q, size_t cap, const char *s)
{
    size_t len = strlen(q);
    while (*s != '\0' && len < cap - 1) {
        q[len++] = (*s == ' ') ? '+' : *s;
        ++s;
    }
    q[len] = '\0';
}

/* Gather every input widget in the form set(s) that were added to `submit`,
 * build a query string, remember it, and navigate to the submit's `action`.
 * Only input kinds contribute; a form set that is NOT a member of the submit
 * box contributes nothing (so nothing is gathered -- as required). */
static void form_submit(SmDoc *d, SmSet *submit)
{
    char q[512];
    int first = 1, i, j;
    const SmValue *act;

    q[0] = '\0';
    for (i = 0; i < submit->nmembers; ++i) {
        SmSet *fs = submit->members[i];   /* a form set added to the submit */
        for (j = 0; j < fs->nmembers; ++j) {
            SmSet *w = fs->members[j];
            char val[256];
            if (!is_input_kind(w->kind)) { continue; }
            if ((w->kind == SM_KIND_CHECKBOX || w->kind == SM_KIND_RADIO) &&
                !sm_value_truthy(sm_get_prop(w, "checked"))) {
                continue; /* unchecked boxes are not submitted (as in HTML) */
            }
            input_value(w, val, sizeof(val));
            if (!first) { size_t l = strlen(q); if (l < sizeof(q) - 1) { q[l] = '&'; q[l + 1] = '\0'; } }
            first = 0;
            enc_append(q, sizeof(q), field_name(w));
            { size_t l = strlen(q); if (l < sizeof(q) - 1) { q[l] = '='; q[l + 1] = '\0'; } }
            enc_append(q, sizeof(q), val);
        }
    }

    sm_free(d->last_submit);
    d->last_submit = sm_strdup(q);

    act = sm_get_prop(submit, "action");
    if (act != NULL && act->kind == SM_VAL_STRING) {
        /* POST the urlencoded body to the action route (deferred to next paint) */
        snprintf(d->status, sizeof(d->status), "POST %.60s  (%.80s)", act->string, q);
        sm_free(d->pending_route);
        d->pending_route = sm_strdup(act->string);
        sm_free(d->pending_body);
        d->pending_body = sm_strdup(q);
        d->pending_kind = NAV_POST;
    } else {
        snprintf(d->status, sizeof(d->status), "submit: %.140s", q);
    }
}

const char *sm_doc_last_submit(SmDoc *d)
{
    return (d != NULL && d->last_submit != NULL) ? d->last_submit : "";
}

/* Draw a box's text across multiple lines split on '\n' (for Textarea). */
static void draw_multiline(SmCanvas *cv, SmRect box, const char *text,
                           SmColor col, unsigned style)
{
    int lh = cv->line_height(cv) + 2;
    int y = box.y + 3;
    const char *p = (text != NULL) ? text : "";
    while (*p != '\0' && y + lh <= box.y + box.h) {
        char line[256];
        int n = 0;
        while (p[n] != '\0' && p[n] != '\n' && n < 255) { ++n; }
        memcpy(line, p, (size_t)n);
        line[n] = '\0';
        {
            SmRect lr;
            lr.x = box.x; lr.y = y; lr.w = box.w; lr.h = lh;
            cv->text_ex(cv, lr, line, col, SM_ALIGN_LEFT, style, 0);
        }
        y += lh;
        p += n;
        if (*p == '\n') { ++p; }
    }
}

static void record_hit(SmDoc *d, SmSet *s, SmRect r)
{
    if (d->nhits >= d->chits) {
        int nc = (d->chits == 0) ? 8 : d->chits * 2;
        Hit *g = (Hit *)sm_realloc(d->hits, (size_t)nc * sizeof(Hit));
        if (g == NULL) { return; }
        d->hits = g; d->chits = nc;
    }
    d->hits[d->nhits].set = s;
    d->hits[d->nhits].r = r;
    ++d->nhits;
}

/* nav-bar button/address rectangles (chrome, not page widgets) */
static SmRect nb_back_rect(void)  { SmRect r; r.x = 8;  r.y = 6; r.w = 30; r.h = 22; return r; }
static SmRect nb_fwd_rect(void)   { SmRect r; r.x = 42; r.y = 6; r.w = 30; r.h = 22; return r; }
static SmRect nb_home_rect(void)  { SmRect r; r.x = 76; r.y = 6; r.w = 52; r.h = 22; return r; }
static SmRect nb_addr_rect(int w)
{
    SmRect r;
    r.x = 134; r.y = 6; r.w = w - 142; r.h = 22;
    if (r.w < 40) { r.w = 40; }
    return r;
}

static void draw_navbar(SmDoc *d, SmCanvas *cv, int width)
{
    SmRect bar, line, r;

    bar.x = 0; bar.y = 0; bar.w = width; bar.h = NAV_H;
    cv->fill_rect(cv, bar, NB_BG);
    line.x = 0; line.y = NAV_H - 1; line.w = width; line.h = 1;
    cv->fill_rect(cv, line, NB_LINE);

    r = nb_back_rect();
    cv->fill_rect(cv, r, can_back(d) ? NB_BTN_BG : NB_BTN_DIS);
    cv->frame_rect(cv, r, NB_LINE);
    cv->text(cv, r, "<", can_back(d) ? NB_TXT : NB_TXT_DIS, SM_ALIGN_CENTER);

    r = nb_fwd_rect();
    cv->fill_rect(cv, r, can_forward(d) ? NB_BTN_BG : NB_BTN_DIS);
    cv->frame_rect(cv, r, NB_LINE);
    cv->text(cv, r, ">", can_forward(d) ? NB_TXT : NB_TXT_DIS, SM_ALIGN_CENTER);

    r = nb_home_rect();
    cv->fill_rect(cv, r, NB_BTN_BG);
    cv->frame_rect(cv, r, NB_LINE);
    cv->text(cv, r, "Home", NB_TXT, SM_ALIGN_CENTER);

    r = nb_addr_rect(width);
    cv->fill_rect(cv, r, NB_ADDR_BG);
    cv->frame_rect(cv, r, d->addr_editing ? DC_FOCUS_LN : NB_LINE);
    if (d->addr_editing) {
        char buf[176];
        int L = (int)strlen(d->addr_buf);
        if (L > 160) { L = 160; }
        memcpy(buf, d->addr_buf, (size_t)L);
        buf[L] = '|';
        buf[L + 1] = '\0';
        cv->text(cv, r, buf, NB_TXT, SM_ALIGN_LEFT);
    } else {
        cv->text(cv, r, current_route(d), NB_TXT, SM_ALIGN_LEFT);
    }
}

/* Render a List: stack its member items vertically from the list's position,
 * each with a bullet (unordered) or "n." (ordered). Items may be links. */
static void render_list(SmDoc *d, SmCanvas *cv, SmSet *s, int width, int height)
{
    int ordered = sm_value_truthy(sm_get_prop(s, "ordered"));
    int lh = cv->line_height(cv) + 6;
    int x = resolve_x(d, s, width, 240);
    int y = NAV_H + resolve_y(d, s, width, height - NAV_H, lh);
    int idx = 1, i;

    for (i = 0; i < s->nmembers; ++i) {
        SmSet *it = s->members[i];
        const char *itxt = widget_text(it);
        unsigned st = widget_style(it);
        SmBool link = (sm_get_prop(it, "link") != NULL) ? SM_TRUE : SM_FALSE;
        SmColor col = link ? DC_LINK : widget_color(it, DC_TEXT);
        SmRect tr;

        if (ordered) {
            char num[12];
            SmRect nr;
            snprintf(num, sizeof(num), "%d.", idx);
            nr.x = x; nr.y = y; nr.w = 22; nr.h = lh;
            cv->text_ex(cv, nr, num, DC_TEXT, SM_ALIGN_LEFT, st, 0);
        } else {
            SmRect mark;
            mark.x = x + 5; mark.y = y + lh / 2 - 2; mark.w = 5; mark.h = 5;
            cv->fill_rect(cv, mark, DC_TEXT);
        }
        tr.x = x + 24; tr.y = y; tr.w = width - (x + 24) - DOC_MARGIN; tr.h = lh;
        cv->text_ex(cv, tr, itxt, col, SM_ALIGN_LEFT, st, 0);
        if (link) {
            SmRect ul;
            int tw = cv->text_width_ex(cv, (itxt != NULL) ? itxt : "", st, 0);
            ul.x = tr.x + 4; ul.y = tr.y + tr.h - 7; ul.w = tw; ul.h = 1;
            cv->fill_rect(cv, ul, DC_LINK);
        }
        record_hit(d, it, tr);
        y += lh;
        ++idx;
    }
}

#define TBL_MAXCOL 16

/* Render a Table: rows are the table's members, cells are each row's members.
 * Column widths are the max cell width in that column; the first row is shaded
 * and bolded as a header. */
static void render_table(SmDoc *d, SmCanvas *cv, SmSet *s, int width, int height)
{
    int colw[TBL_MAXCOL];
    int ncols = 0, i, j;
    int rowh = cv->line_height(cv) + 8;
    int totalw = 0, x0, y, cx;

    for (j = 0; j < TBL_MAXCOL; ++j) { colw[j] = 0; }
    for (i = 0; i < s->nmembers; ++i) {
        SmSet *row = s->members[i];
        for (j = 0; j < row->nmembers && j < TBL_MAXCOL; ++j) {
            SmSet *cell = row->members[j];
            const char *ct = widget_text(cell);
            int cw = cv->text_width_ex(cv, (ct != NULL) ? ct : "", widget_style(cell), 0) + 16;
            if (cw > colw[j]) { colw[j] = cw; }
            if (j + 1 > ncols) { ncols = j + 1; }
        }
    }
    for (j = 0; j < ncols; ++j) { if (colw[j] < 48) { colw[j] = 48; } totalw += colw[j]; }
    x0 = resolve_x(d, s, width, totalw);
    y = NAV_H + resolve_y(d, s, width, height - NAV_H, rowh);

    for (i = 0; i < s->nmembers; ++i) {
        SmSet *row = s->members[i];
        cx = x0;
        for (j = 0; j < row->nmembers && j < TBL_MAXCOL; ++j) {
            SmSet *cell = row->members[j];
            const char *ct = widget_text(cell);
            unsigned st = widget_style(cell);
            SmColor col = widget_color(cell, DC_TEXT);
            SmColor bg;
            SmRect cr;
            cr.x = cx; cr.y = y; cr.w = colw[j]; cr.h = rowh;
            if (widget_bg(cell, &bg)) { cv->fill_rect(cv, cr, bg); }
            else { cv->fill_rect(cv, cr, (i == 0) ? DC_BTN_BG : DC_PANE_BG); }
            cv->frame_rect(cv, cr, DC_PANE_LINE);
            if (i == 0) { st |= SM_FS_BOLD; }
            cv->text_ex(cv, cr, ct, col, (i == 0) ? SM_ALIGN_CENTER : SM_ALIGN_LEFT, st, 0);
            record_hit(d, cell, cr);
            cx += colw[j];
        }
        y += rowh;
    }
}

static void doc_paint(void *state, SmCanvas *cv, int width, int height)
{
    SmDoc *d = (SmDoc *)state;
    SmRect full;
    int i;

    /* A click deferred navigation to here, so the page is not torn down while
     * we are still inside the click handler. */
    if (d->pending_kind == NAV_ROUTE && d->pending_route != NULL) {
        char *rt = d->pending_route;
        d->pending_route = NULL;
        d->pending_kind = NAV_NONE;
        sm_doc_navigate(d, rt);
        sm_free(rt);
    } else if (d->pending_kind == NAV_POST && d->pending_route != NULL) {
        char *rt = d->pending_route;
        char *bd = d->pending_body;
        d->pending_route = NULL;
        d->pending_body = NULL;
        d->pending_kind = NAV_NONE;
        sm_doc_post(d, rt, (bd != NULL) ? bd : "");
        sm_free(rt);
        sm_free(bd);
    } else if (d->pending_kind == NAV_BACK) {
        d->pending_kind = NAV_NONE;
        sm_doc_back(d);
    } else if (d->pending_kind == NAV_FORWARD) {
        d->pending_kind = NAV_NONE;
        sm_doc_forward(d);
    }

    d->last_w = width;
    d->last_h = height;
    d->content_right = 0;
    d->content_bottom = 0;

    full.x = 0; full.y = 0; full.w = width; full.h = height;
    cv->fill_rect(cv, full, DC_BG);

    d->nhits = 0;

    for (i = 0; i < d->w->nall; ++i) {
        SmSet *s = d->w->all[i];
        const char *txt;
        char valbuf[64];
        int tw, ww, wh, x, y, px;
        unsigned style;
        SmColor tcol, bg;
        SmRect r;
        if (!is_widget_kind(s->kind)) { continue; }
        if (!visible_reaches(d->w, d->w->visible, s)) { continue; }

        /* block/container kinds with their own layout */
        if (s->kind == SM_KIND_HRULE) {
            SmRect ln;
            ln.x = DOC_MARGIN;
            ln.y = NAV_H + resolve_y(d, s, width, height - NAV_H, 2);
            ln.w = width - 2 * DOC_MARGIN;
            ln.h = 2;
            cv->fill_rect(cv, ln, DC_PANE_LINE);
            continue;
        }
        if (s->kind == SM_KIND_LIST)  { render_list(d, cv, s, width, height); continue; }
        if (s->kind == SM_KIND_TABLE) { render_table(d, cv, s, width, height); continue; }

        /* A set's native `.value` (the same field expressions read via a bare
         * name, e.g. SET B1 TO A1 + A2) takes priority over its display
         * properties when present -- this is what lets a page built purely
         * from generic widgets + SET actions show a computed result with no
         * spreadsheet-specific rendering: any widget that ends up with a
         * `.value` just displays it, exactly like Webcel's own cell display
         * did, but as a fallback available to every kind, not a special one. */
        if (s->value.kind != SM_VAL_NONE) {
            sm_value_format(&s->value, valbuf, sizeof(valbuf));
            txt = valbuf;
        } else {
            txt = widget_text(s);
        }
        if (txt == NULL &&
            (s->kind == SM_KIND_TEXTINPUT || s->kind == SM_KIND_TEXTAREA)) {
            const SmValue *ph = sm_get_prop(s, "placeholder");
            txt = (ph != NULL && ph->kind == SM_VAL_STRING) ? ph->string : "";
        }
        style = widget_style(s);
        px = (s->kind == SM_KIND_HEADING) ? heading_px(s) : 0;
        if (s->kind == SM_KIND_HEADING) { style |= SM_FS_BOLD; }
        tcol = widget_color(s, DC_TEXT);

        tw = cv->text_width_ex(cv, (txt != NULL) ? txt : "", style, px);
        ww = tw + DOC_PADX * 2;
        if (s->kind == SM_KIND_CHECKBOX || s->kind == SM_KIND_RADIO) { ww += 24; }
        if (ww < DOC_MINW) { ww = DOC_MINW; }
        if (s->kind == SM_KIND_TEXTINPUT && ww < 160) { ww = 160; }
        wh = (px > DOC_WROW) ? px + 8 : DOC_WROW;
        if (s->kind == SM_KIND_TEXTAREA) {
            const SmValue *rv = sm_get_prop(s, "rows");
            int rows = (rv != NULL && rv->kind == SM_VAL_NUMBER) ? (int)rv->number : 4;
            if (rows < 1) { rows = 1; }
            ww = 260;
            wh = rows * (cv->line_height(cv) + 2) + 6;
        } else if (s->kind == SM_KIND_SELECT) {
            ww = 190;
            wh = DOC_WROW;
        }
        /* an explicit `width` / `height` (in grid units, like X.n / Y.n) fixes the
         * box; Image reads its own size below */
        if (s->kind != SM_KIND_IMAGE) {
            const SmValue *wv = sm_get_prop(s, "width");
            const SmValue *hv = sm_get_prop(s, "height");
            if (wv != NULL && wv->kind == SM_VAL_NUMBER && wv->number > 0.0) {
                ww = (int)(wv->number * grid_scale_x(d, width));
            }
            if (hv != NULL && hv->kind == SM_VAL_NUMBER && hv->number > 0.0) {
                wh = (int)(hv->number * grid_scale_y(d, width, height));
            }
        }
        /* page content sits below the navigation bar */
        x = resolve_x(d, s, width, ww);
        y = NAV_H + resolve_y(d, s, width, height - NAV_H, wh);
        r.x = x; r.y = y; r.w = ww; r.h = wh;

        /* track the tallest/widest extent seen, for scroll clamping */
        if (x + ww > d->content_right)  { d->content_right = x + ww + d->scroll_x; }
        if (y + wh > d->content_bottom) { d->content_bottom = y + wh + d->scroll_y; }

        if (widget_bg(s, &bg)) { cv->fill_rect(cv, r, bg); }

        /* A widget with a `link` property is a hyperlink (design doc S10):
         * rendered as its colour + underlined, click navigates to its route. */
        if (sm_get_prop(s, "link") != NULL) {
            SmRect ul;
            int tw2 = cv->text_width_ex(cv, (txt != NULL) ? txt : "", style, px);
            cv->text_ex(cv, r, txt, DC_LINK, SM_ALIGN_LEFT, style, px);
            ul.x = r.x + 4; ul.y = r.y + r.h - 7; ul.w = tw2; ul.h = 1;
            cv->fill_rect(cv, ul, DC_LINK);
            record_hit(d, s, r);
            continue;
        }

        switch (s->kind) {
        case SM_KIND_BUTTON:
        case SM_KIND_NAVITEM:
        case SM_KIND_TOOLBARITEM:
            cv->fill_rect(cv, r, DC_BTN_BG);
            cv->frame_rect(cv, r, DC_BTN_LINE);
            cv->text_ex(cv, r, txt, tcol, widget_align(s, SM_ALIGN_CENTER), style, px);
            break;
        case SM_KIND_CONTENTPANE:
            cv->fill_rect(cv, r, DC_PANE_BG);
            cv->frame_rect(cv, r, DC_PANE_LINE);
            cv->text_ex(cv, r, txt, tcol, SM_ALIGN_LEFT, style, px);
            break;
        case SM_KIND_TEXTINPUT: {
            const SmValue *content = sm_get_prop(s, "content");
            SmBool has_value = (s->value.kind != SM_VAL_NONE) ? SM_TRUE : SM_FALSE;
            SmBool empty = (!has_value &&
                            (content == NULL || content->kind != SM_VAL_STRING ||
                             content->string[0] == '\0')) ? SM_TRUE : SM_FALSE;
            cv->fill_rect(cv, r, DC_INPUT_BG);
            cv->frame_rect(cv, r, (d->focus == s) ? DC_FOCUS_LN : DC_INPUT_LN);
            if (!empty) {
                cv->text_ex(cv, r, txt, DC_TEXT, widget_align(s, SM_ALIGN_LEFT), style, 0);
            } else {
                cv->text_ex(cv, r, txt, DC_HINT, widget_align(s, SM_ALIGN_LEFT), style, 0);
            }
            break;
        }
        case SM_KIND_CHECKBOX: {
            SmRect box, lab;
            SmBool on = sm_value_truthy(sm_get_prop(s, "checked"));
            box.x = r.x; box.y = r.y + (r.h - 16) / 2; box.w = 16; box.h = 16;
            cv->fill_rect(cv, box, DC_INPUT_BG);
            cv->frame_rect(cv, box, DC_INPUT_LN);
            if (on) {
                SmRect chk;
                chk.x = box.x + 3; chk.y = box.y + 3; chk.w = 10; chk.h = 10;
                cv->fill_rect(cv, chk, DC_FOCUS_LN);
            }
            lab.x = r.x + 24; lab.y = r.y; lab.w = r.w - 24; lab.h = r.h;
            cv->text_ex(cv, lab, txt, tcol, SM_ALIGN_LEFT, style, 0);
            break;
        }
        case SM_KIND_RADIO: {
            SmRect box, lab;
            SmBool on = sm_value_truthy(sm_get_prop(s, "checked"));
            box.x = r.x; box.y = r.y + (r.h - 16) / 2; box.w = 16; box.h = 16;
            cv->fill_rect(cv, box, DC_INPUT_BG);
            cv->frame_rect(cv, box, DC_INPUT_LN);
            if (on) {
                SmRect dot;
                dot.x = box.x + 4; dot.y = box.y + 4; dot.w = 8; dot.h = 8;
                cv->fill_rect(cv, dot, DC_FOCUS_LN);
            }
            lab.x = r.x + 24; lab.y = r.y; lab.w = r.w - 24; lab.h = r.h;
            cv->text_ex(cv, lab, txt, tcol, SM_ALIGN_LEFT, style, 0);
            break;
        }
        case SM_KIND_IMAGE: {
            const SmValue *src = sm_get_prop(s, "src");
            const SmValue *wv = sm_get_prop(s, "width");
            const SmValue *hv = sm_get_prop(s, "height");
            const char *path = (src != NULL && src->kind == SM_VAL_STRING) ? src->string : NULL;
            SmRect ib;
            SmBool drawn = SM_FALSE;
            ib.x = r.x; ib.y = r.y;
            ib.w = (wv != NULL && wv->kind == SM_VAL_NUMBER) ? (int)wv->number : 120;
            ib.h = (hv != NULL && hv->kind == SM_VAL_NUMBER) ? (int)hv->number : 60;
            if (path != NULL && cv->draw_image != NULL) {
                drawn = cv->draw_image(cv, ib, path);
            }
            if (!drawn) { /* placeholder box if the bitmap could not be loaded */
                cv->fill_rect(cv, ib, DC_PANE_BG);
                cv->frame_rect(cv, ib, DC_PANE_LINE);
                cv->text_ex(cv, ib, (path != NULL) ? path : "[image]",
                            DC_HINT, SM_ALIGN_CENTER, SM_FS_ITALIC, 0);
            }
            r = ib;
            break;
        }
        case SM_KIND_TEXTAREA: {
            const SmValue *content = sm_get_prop(s, "content");
            cv->fill_rect(cv, r, DC_INPUT_BG);
            cv->frame_rect(cv, r, (d->focus == s) ? DC_FOCUS_LN : DC_INPUT_LN);
            if (content != NULL && content->kind == SM_VAL_STRING &&
                content->string[0] != '\0') {
                draw_multiline(cv, r, content->string, DC_TEXT, style);
            } else {
                SmRect hr;
                hr.x = r.x; hr.y = r.y; hr.w = r.w; hr.h = DOC_WROW;
                cv->text_ex(cv, hr, txt, DC_HINT, SM_ALIGN_LEFT, style, 0);
            }
            break;
        }
        case SM_KIND_SELECT: {
            SmSet *opt = select_option(s);
            SmRect tr, ar;
            cv->fill_rect(cv, r, DC_INPUT_BG);
            cv->frame_rect(cv, r, (d->open_select == s) ? DC_FOCUS_LN : DC_INPUT_LN);
            tr.x = r.x; tr.y = r.y; tr.w = r.w - 20; tr.h = r.h;
            cv->text_ex(cv, tr, (opt != NULL) ? widget_text(opt) : "", tcol,
                        SM_ALIGN_LEFT, style, 0);
            ar.x = r.x + r.w - 18; ar.y = r.y; ar.w = 16; ar.h = r.h;
            cv->text_ex(cv, ar, "v", DC_TEXT, SM_ALIGN_CENTER, 0u, 0);
            if (d->open_select == s) { d->open_rect = r; }
            break;
        }
        default: /* TEXTBOX, HEADING, PARAGRAPH */
            cv->text_ex(cv, r, txt, tcol, widget_align(s, SM_ALIGN_LEFT), style, px);
            break;
        }
        record_hit(d, s, r);
    }

    /* Open dropdown list, drawn last so it sits above the page and its option
     * hits are the topmost (win the click). */
    if (d->open_select != NULL) {
        SmSet *sel = d->open_select;
        SmRect box = d->open_rect;
        int lh = DOC_WROW, j;
        for (j = 0; j < sel->nmembers; ++j) {
            SmSet *opt = sel->members[j];
            SmRect orow;
            orow.x = box.x; orow.y = box.y + box.h + j * lh; orow.w = box.w; orow.h = lh;
            cv->fill_rect(cv, orow, DC_INPUT_BG);
            cv->frame_rect(cv, orow, DC_INPUT_LN);
            cv->text_ex(cv, orow, widget_text(opt), DC_TEXT, SM_ALIGN_LEFT, 0u, 0);
            record_hit(d, opt, orow);
        }
    }

    draw_navbar(d, cv, width);

    if (d->status[0] != '\0') {
        SmRect sr;
        sr.x = 0; sr.y = height - 20; sr.w = width; sr.h = 20;
        cv->text(cv, sr, d->status, DC_HINT, SM_ALIGN_LEFT);
    }
}

static SmBool pt_in(SmRect r, int x, int y)
{
    return (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
               ? SM_TRUE : SM_FALSE;
}

static void request_route(SmDoc *d, const char *route)
{
    sm_free(d->pending_route);
    d->pending_route = sm_strdup(route);
    d->pending_kind = NAV_ROUTE;
}

/* Move keyboard focus to `s` (NULL clears it) and mirror it in the built-in
 * FOCUS set, then fire ON CHANGE OF FOCUS handlers so a page can react. */
static void set_focus(SmDoc *d, SmSet *s)
{
    SmSet *fs;
    if (d->focus == s) { return; }
    d->focus = s;
    fs = sm_find(d->w, "FOCUS");
    if (fs == NULL) { return; }
    while (fs->nmembers > 0) { sm_remove_member(d->w, fs, fs->members[0]); }
    if (s != NULL) { sm_add_member(d->w, fs, s); }
    fire_handlers(d, SM_EV_CHANGE, fs);
}

static SmBool doc_click(void *state, int x, int y)
{
    SmDoc *d = (SmDoc *)state;
    int i;

    /* navigation bar (chrome) takes clicks in the top strip */
    if (y < NAV_H) {
        if (pt_in(nb_back_rect(), x, y)) {
            if (can_back(d)) { d->pending_kind = NAV_BACK; }
            return SM_TRUE;
        }
        if (pt_in(nb_fwd_rect(), x, y)) {
            if (can_forward(d)) { d->pending_kind = NAV_FORWARD; }
            return SM_TRUE;
        }
        if (pt_in(nb_home_rect(), x, y)) {
            request_route(d, "/");
            d->addr_editing = 0;
            return SM_TRUE;
        }
        if (pt_in(nb_addr_rect(d->last_w), x, y)) {
            const char *cr = current_route(d);
            size_t n = strlen(cr);
            if (n >= sizeof(d->addr_buf)) { n = sizeof(d->addr_buf) - 1; }
            memcpy(d->addr_buf, cr, n);
            d->addr_buf[n] = '\0';
            d->addr_editing = 1;
            d->addr_fresh = 1;   /* first keystroke replaces the shown URL */
            set_focus(d, NULL);
            return SM_TRUE;
        }
        d->addr_editing = 0;
        return SM_TRUE;
    }
    d->addr_editing = 0;

    /* topmost (last drawn) wins */
    for (i = d->nhits - 1; i >= 0; --i) {
        if (pt_in(d->hits[i].r, x, y)) {
            SmSet *s = d->hits[i].set;
            const SmValue *lnk;

            if (s->kind == SM_KIND_SELECT) {
                d->open_select = (d->open_select == s) ? NULL : s;
                set_focus(d, NULL);
                return SM_TRUE;
            }
            if (s->kind == SM_KIND_OPTION) {
                int p;
                for (p = 0; p < s->nparents; ++p) {
                    SmSet *sel = s->parents[p];
                    if (sel->kind == SM_KIND_SELECT) {
                        int q, idx = 0;
                        for (q = 0; q < sel->nmembers; ++q) {
                            if (sel->members[q] == s) { idx = q; break; }
                        }
                        sm_set_prop(sel, "selected", sm_value_number((double)idx));
                        fire_handlers(d, SM_EV_CHANGE, sel);
                    }
                }
                d->open_select = NULL;
                return SM_TRUE;
            }
            d->open_select = NULL; /* any other click closes an open dropdown */

            lnk = sm_get_prop(s, "link");
            if (lnk != NULL && lnk->kind == SM_VAL_STRING) {
                /* Defer the actual navigation to the next paint (see doc_paint);
                 * tearing down the world here would free `s` mid-handler. */
                request_route(d, lnk->string);
                set_focus(d, NULL);
                return SM_TRUE;
            }
            if (s->kind == SM_KIND_CHECKBOX) {
                SmBool on = sm_value_truthy(sm_get_prop(s, "checked"));
                sm_set_prop(s, "checked", sm_value_number(on ? 0.0 : 1.0));
                set_focus(d, NULL);
                fire_handlers(d, SM_EV_CHANGE, s);
                return SM_TRUE;
            }
            if (s->kind == SM_KIND_RADIO) {
                const SmValue *grp = sm_get_prop(s, "group");
                if (grp != NULL && grp->kind == SM_VAL_STRING) {
                    int k;
                    for (k = 0; k < d->w->nall; ++k) {
                        SmSet *o = d->w->all[k];
                        const SmValue *og;
                        if (o->kind != SM_KIND_RADIO) { continue; }
                        og = sm_get_prop(o, "group");
                        if (og != NULL && og->kind == SM_VAL_STRING &&
                            strcmp(og->string, grp->string) == 0) {
                            sm_set_prop(o, "checked", sm_value_number(0.0));
                        }
                    }
                }
                sm_set_prop(s, "checked", sm_value_number(1.0));
                set_focus(d, NULL);
                fire_handlers(d, SM_EV_CHANGE, s);
                return SM_TRUE;
            }
            if (sm_get_prop(s, "action") != NULL) {
                /* Submit: gather the input widgets in the form set(s) added to
                 * this button, then navigate to the action route. */
                form_submit(d, s);
                set_focus(d, NULL);
                return SM_TRUE;
            }
            if (s->kind == SM_KIND_TEXTINPUT || s->kind == SM_KIND_TEXTAREA) {
                set_focus(d, s);
                return SM_TRUE;
            }
            set_focus(d, NULL);
            fire_handlers(d, SM_EV_CLICK, s);
            return SM_TRUE;
        }
    }
    set_focus(d, NULL);
    d->open_select = NULL;
    return SM_TRUE;
}

SmBool sm_doc_click_widget(SmDoc *d, const char *name)
{
    int i;
    if (d == NULL) { return SM_FALSE; }
    for (i = 0; i < d->nhits; ++i) {
        SmSet *s = d->hits[i].set;
        if (s->name != NULL && sm_stricmp(s->name, name) == 0) {
            SmRect r = d->hits[i].r;
            return doc_click(d, r.x + r.w / 2, r.y + r.h / 2);
        }
    }
    return SM_FALSE;
}

SmRect sm_doc_widget_rect(SmDoc *d, const char *name)
{
    int i;
    SmRect zero;
    zero.x = 0; zero.y = 0; zero.w = 0; zero.h = 0;
    if (d == NULL) { return zero; }
    for (i = 0; i < d->nhits; ++i) {
        SmSet *s = d->hits[i].set;
        if (s->name != NULL && sm_stricmp(s->name, name) == 0) { return d->hits[i].r; }
    }
    return zero;
}

static SmBool doc_char(void *state, int ch)
{
    SmDoc *d = (SmDoc *)state;
    const SmValue *cur;
    char buf[256];
    size_t n;

    if (d->addr_editing) {
        int L;
        if (d->addr_fresh) { d->addr_buf[0] = '\0'; d->addr_fresh = 0; }
        L = (int)strlen(d->addr_buf);
        if (L < (int)sizeof(d->addr_buf) - 1) {
            d->addr_buf[L] = (char)ch;
            d->addr_buf[L + 1] = '\0';
        }
        return SM_TRUE;
    }
    if (d->focus == NULL) { return SM_FALSE; }
    cur = sm_get_prop(d->focus, "content");
    n = (cur != NULL && cur->kind == SM_VAL_STRING) ? strlen(cur->string) : 0;
    if (n >= sizeof(buf) - 1) { return SM_FALSE; }
    if (n > 0) { memcpy(buf, cur->string, n); }
    buf[n] = (char)ch;
    buf[n + 1] = '\0';
    sm_set_prop(d->focus, "content", sm_value_string(buf));
    fire_handlers(d, SM_EV_TYPE, d->focus);
    return SM_TRUE;
}

static SmBool doc_key(void *state, SmKey key)
{
    SmDoc *d = (SmDoc *)state;

    if (d->addr_editing) {
        if (key == SM_KEY_ENTER) {
            request_route(d, d->addr_buf);
            d->addr_editing = 0;
            return SM_TRUE;
        }
        if (key == SM_KEY_BACKSPACE) {
            int L;
            d->addr_fresh = 0;
            L = (int)strlen(d->addr_buf);
            if (L > 0) { d->addr_buf[L - 1] = '\0'; }
            return SM_TRUE;
        }
        if (key == SM_KEY_ESCAPE) { d->addr_editing = 0; return SM_TRUE; }
        return SM_FALSE;
    }

    /* Enter commits a TextInput, same as a real HTML <input> firing "change"
     * on commit. commit_content both coerces the typed text into `.value`
     * (or, for a leading '=', wires it as a live formula) AND propagates via
     * native delta propagation to every transitive dependent -- no page-
     * authored ON CHANGE needed for that part. cascade_change additionally
     * fires SM_EV_CHANGE, a separate, general hook for a page's own custom
     * side effects on edit (unrelated to value propagation). */
    if (key == SM_KEY_ENTER && d->focus != NULL &&
        d->focus->kind == SM_KIND_TEXTINPUT) {
        commit_content(d, d->focus);
        cascade_change(d, d->focus);
        return SM_TRUE;
    }

    if (key == SM_KEY_ENTER && d->focus != NULL &&
        d->focus->kind == SM_KIND_TEXTAREA) {
        const SmValue *cur = sm_get_prop(d->focus, "content");
        char buf[512];
        size_t n = (cur != NULL && cur->kind == SM_VAL_STRING) ? strlen(cur->string) : 0;
        if (n < sizeof(buf) - 2) {
            if (n > 0) { memcpy(buf, cur->string, n); }
            buf[n] = '\n'; buf[n + 1] = '\0';
            sm_set_prop(d->focus, "content", sm_value_string(buf));
        }
        return SM_TRUE;
    }
    if (key == SM_KEY_BACKSPACE && d->focus != NULL) {
        const SmValue *cur = sm_get_prop(d->focus, "content");
        if (cur != NULL && cur->kind == SM_VAL_STRING && cur->string[0] != '\0') {
            char buf[256];
            size_t n = strlen(cur->string);
            if (n >= sizeof(buf)) { n = sizeof(buf) - 1; }
            memcpy(buf, cur->string, n);
            buf[n - 1] = '\0';
            sm_set_prop(d->focus, "content", sm_value_string(buf));
            return SM_TRUE;
        }
    }
    if (key == SM_KEY_ESCAPE) { set_focus(d, NULL); return SM_TRUE; }
    return SM_FALSE;
}

static SmBool doc_open_file_cb(void *state, const char *path)
{
    sm_doc_open_file((SmDoc *)state, path);
    return SM_TRUE; /* always repaint, even on failure (shows the error page) */
}

#define SCROLL_STEP_PX 40

/* Scroll the page: content beyond the visible width/height (whether from raw
 * pixel coordinates or a DEFINE GRID page taller than the window) is reached
 * by scrolling, not clipped away. Clamped to [0, content extent - viewport],
 * using the extent measured during the last paint. */
static SmBool doc_scroll(void *state, int notches)
{
    SmDoc *d = (SmDoc *)state;
    int max_y = d->content_bottom - d->last_h;
    int max_x = d->content_right - d->last_w;
    if (max_y < 0) { max_y = 0; }
    if (max_x < 0) { max_x = 0; }
    d->scroll_y -= notches * SCROLL_STEP_PX;
    if (d->scroll_y < 0) { d->scroll_y = 0; }
    if (d->scroll_y > max_y) { d->scroll_y = max_y; }
    if (d->scroll_x > max_x) { d->scroll_x = max_x; }
    return SM_TRUE;
}

void sm_doc_fill_app(SmDoc *d, SmApp *app)
{
    app->state = d;
    app->on_paint = doc_paint;
    app->on_char = doc_char;
    app->on_key = doc_key;
    app->on_click = doc_click;
    app->on_scroll = doc_scroll;
    app->on_resize = NULL;
    app->on_open_file = doc_open_file_cb;
}
