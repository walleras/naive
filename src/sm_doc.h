/* sm_doc.h  --  SeSaML document runtime: a .sml file as a live web page.
 *
 * A SeSaML document is a flat sequence of CREATE / mutation / ON-event
 * statements. Parsing produces live set state directly (design doc S12: no
 * separate AST). This module owns:
 *   - the SmWorld the document builds,
 *   - the event handlers (ON CLICK/DRAG/... with WHERE guards and actions),
 *   - coordinate resolution, hit-testing, painting, and event dispatch.
 *
 * Widget geometry lives on each set as the properties "@x" / "@y" (a number
 * literal, or a named position like "Center"/"Left"/"Top"). Visibility is
 * membership in the built-in VISIBLE set: a top-level widget is auto-joined to
 * VISIBLE on creation, so REMOVE ... FROM VISIBLE hides it, and hiding a
 * container hides everything reachable through it.
 *
 * The parser (sm_parse.c) builds a document through the builder API below; the
 * backend (sm_win32.c) drives it through sm_doc_fill_app().
 */
#ifndef SM_DOC_H
#define SM_DOC_H

#include "sm_render.h"
#include "sm_set.h"
#include "sm_expr.h"

typedef struct SmDoc SmDoc;

/* ---- event / action model (shared with the parser) ---------------------- */

typedef enum {
    SM_EV_CLICK = 0, SM_EV_DRAG, SM_EV_DROP, SM_EV_TYPE,
    SM_EV_KEY, SM_EV_CHANGE, SM_EV_LOAD, SM_EV_TIMER
} SmEventType;

typedef enum {
    SM_ACT_ADD = 0,  /* ADD a TO b       -> a becomes a member of b          */
    SM_ACT_REMOVE,   /* REMOVE a FROM b                                       */
    SM_ACT_SET,      /* SET a TO <expr>, or SET a.prop TO <expr>              */
    SM_ACT_SWAP,     /* SWAP a WITH b                                         */
    SM_ACT_MOVE,     /* MOVE a TO b.Location                                  */
    SM_ACT_SEND,     /* SEND a TO path (recorded; no transport in v1)         */
    SM_ACT_CALL,     /* CALL fn WITH (...) / fn(...): runs a user FUNCTION    */
    SM_ACT_TRANSFER  /* MOVE a FROM b TO c -> leave b, join c (c is in `prop`) */
} SmActKind;

typedef struct SmHandler SmHandler; /* opaque */

/* ---- lifecycle ---------------------------------------------------------- */

SmDoc   *sm_doc_new(void);
void     sm_doc_free(SmDoc *d);

/* Parse `src` (a whole .sml document) into the doc. Returns SM_ERR_PARSE and
 * fills err on failure. May be called once per doc. */
SmStatus sm_doc_load_string(SmDoc *d, const char *src, char *err, size_t errcap);

/* Read a file and load it. */
SmStatus sm_doc_load_file(SmDoc *d, const char *path, char *err, size_t errcap);

/* Navigate to a route: ask the mock server (sm_server) for the route's SML,
 * replace the current page with it, and on any failure show an error page.
 * Safe to call from outside event dispatch (e.g. the initial page load); a
 * link click defers to this via the runtime. */
SmStatus sm_doc_navigate(SmDoc *d, const char *route);

/* POST an urlencoded body to a route (used by form submit); the mock server
 * echoes the fields back as the new page. */
SmStatus sm_doc_post(SmDoc *d, const char *route, const char *body);

/* Open a local .sml file directly, replacing the current page (drag-and-drop
 * target on the Win32 backend). Pushed onto history like a navigation. */
SmStatus sm_doc_open_file(SmDoc *d, const char *path);

/* Back/forward through the visited-route history (no-ops at the ends). The nav
 * bar drives these; also callable directly. sm_doc_route returns the current
 * route. */
void        sm_doc_back(SmDoc *d);
void        sm_doc_forward(SmDoc *d);
const char *sm_doc_route(SmDoc *d);

SmWorld *sm_doc_world(SmDoc *d);

/* ---- builder API (used by the parser) ----------------------------------- */

/* Record a handler; returns it so actions can be appended. `owner` is the set
 * the ON clause is attached to (NULL for a standalone ON). `of_name`/`where`
 * are optional (may be NULL); the doc takes ownership of `where`. */
SmHandler *sm_doc_add_handler(SmDoc *d, SmEventType ev, SmSet *owner,
                              const char *of_name, SmExpr *where);

/* Append an action to a handler. Strings are copied; `expr` is owned by the
 * handler. Unused operands may be NULL. */
SmStatus   sm_handler_add_action(SmHandler *h, SmActKind kind,
                                 const char *a, const char *b,
                                 const char *prop, SmExpr *expr);

/* Mark a set as a renderable widget with the given coordinate specs. A spec
 * is either a bare number (is_num=SM_TRUE, num=value) or a named position
 * (is_num=SM_FALSE, name="Center" etc.). Also auto-joins VISIBLE unless the
 * widget was declared MEMBER OF something (top_level=SM_FALSE). */
void       sm_doc_place(SmDoc *d, SmSet *s,
                        SmBool x_is_num, double x_num, const char *x_name,
                        SmBool y_is_num, double y_num, const char *y_name,
                        SmBool top_level);

/* DEFINE GRID X <w> [, Y <h>]: a numeric X.<n> scales so `w` virtual units
 * always spans the actual window width. `h` (0 if not given) independently
 * scales Y against the actual window height -- given, "cells" need not be
 * square; omitted, Y floats: unbounded, using X's own scale to stay square.
 * Neither is a hard boundary; content placed past either just ends up off
 * the visible area, which is what makes the page scrollable. width == 0 (the
 * default) means no grid at all: numbers are raw pixels, pre-existing
 * behavior, unaffected. */
void       sm_doc_set_grid(SmDoc *d, double width, double height);

/* Run one resolved action now (the engine behind both ON-event handlers and
 * FUNCTION bodies). `c` is TRANSFER's destination set; `prop` is SET's property
 * name; `expr` is SET's value / CALL's call expression (borrowed, not owned).
 * Unresolved operands (NULL) make the action a no-op. */
void       sm_doc_exec_action(SmDoc *d, SmActKind kind, SmSet *a, SmSet *b,
                              SmSet *c, const char *prop, SmExpr *expr);

/* Fire standalone ON LOAD handlers once; called by the parser right after a
 * document finishes loading successfully (real HTML/JS's onload). */
void       sm_doc_fire_load(SmDoc *d);

/* ---- runtime queries / driving (also used by headless tests) ------------ */

/* Fire the CLICK handlers attached to the named widget; returns SM_TRUE if any
 * handler ran (WHERE guards respected). */
SmBool     sm_doc_fire_click(SmDoc *d, const char *widget_name);

/* Is the named set currently visible (reachable from VISIBLE by membership)? */
SmBool     sm_doc_is_visible(SmDoc *d, const char *name);

/* Automation/testing: dispatch a click at the centre of the named widget's
 * last-painted rectangle. Requires a paint first to populate hit rects.
 * Returns SM_TRUE if a widget was found and clicked. */
SmBool     sm_doc_click_widget(SmDoc *d, const char *name);

/* Automation/testing: the named widget's last-painted rectangle (all zero if
 * not found or not yet painted) -- for asserting grid scaling/scroll. */
SmRect     sm_doc_widget_rect(SmDoc *d, const char *name);

/* The query string gathered by the most recent form submit (name=value&...),
 * or "" if none. */
const char *sm_doc_last_submit(SmDoc *d);

/* ---- backend integration ------------------------------------------------ */

void       sm_doc_fill_app(SmDoc *d, SmApp *app);

#endif /* SM_DOC_H */
