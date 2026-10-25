/* sm_set.h  --  the one primitive: sets and membership (strict ISO C89).
 *
 * "Everything is a set. Membership is the only state. Migration (moving an
 *  element into or out of a set) is the only mutation." (design doc S2)
 *
 * Each set keeps a forward member list and a reverse parent list, mirroring
 * CantorDB's has_element / member_of. The reverse list is what makes delta
 * propagation (S12) and membership-based GC (S8) O(affected), not O(all).
 */
#ifndef SM_SET_H
#define SM_SET_H

#include "sm_common.h"
#include "sm_value.h"

/* --- Kinds ---------------------------------------------------------------
 * In the pure model a "Button" is just a set that happens to be a member of
 * the Button class. For the runtime we tag the concrete kind so the renderer
 * and event loop can dispatch without a class-membership lookup on every
 * frame. SM_KIND_SET is the plain, untyped set.
 */
typedef enum {
    SM_KIND_SET = 0,
    SM_KIND_FUNCTION,
    SM_KIND_COLOR,
    SM_KIND_NAMESPACE,   /* a scoping container: members are namespaced under
                          * it (design doc extension) -- general-purpose, like
                          * SET/COLOR/FUNCTION, not tied to any one feature   */
    SM_KIND_TEXTBOX,
    SM_KIND_TEXTINPUT,
    SM_KIND_BUTTON,
    SM_KIND_CHECKBOX,
    SM_KIND_RADIO,
    SM_KIND_IMAGE,
    SM_KIND_CONTENTPANE,
    SM_KIND_NAVITEM,
    SM_KIND_TOOLBARITEM,
    SM_KIND_HEADING,     /* h1..h6 (level property)                          */
    SM_KIND_PARAGRAPH,   /* a block of body text                            */
    SM_KIND_HRULE,       /* horizontal rule                                 */
    SM_KIND_LIST,        /* ordered/unordered list container                */
    SM_KIND_LISTITEM,    /* an item within a List                          */
    SM_KIND_TABLE,       /* table container of rows                        */
    SM_KIND_TABLEROW,    /* a row of cells                                 */
    SM_KIND_TABLECELL,   /* a cell within a row                            */
    SM_KIND_TEXTAREA,    /* multi-line text input                          */
    SM_KIND_SELECT,      /* dropdown; its members are options             */
    SM_KIND_OPTION,      /* one choice within a Select                    */
    SM_KIND_OTHER        /* any other type_name; resolved lazily             */
} SmKind;

/* --- Flags (bit set) ----------------------------------------------------- */
#define SM_FLAG_FLAT      0x0001u  /* membership propagates up to supersets  */
#define SM_FLAG_ORDERED   0x0002u  /* member order is significant            */
#define SM_FLAG_DIRTY     0x0004u  /* needs re-eval / redraw                 */
#define SM_FLAG_BUILTIN   0x0008u  /* VISIBLE, STACK, QUEUE, colours, ...    */
#define SM_FLAG_EPHEMERAL 0x0010u  /* result of a set-algebra op (cache)     */

struct SmSchema; /* defined in sm_schema.h; opaque here */

typedef struct SmProp {
    char   *key;   /* owned                                                  */
    SmValue val;
} SmProp;

typedef struct SmSet {
    char    *name;           /* owned; NULL for anonymous/result sets        */
    SmKind   kind;
    unsigned flags;

    struct SmSet **members;  /* forward: elements                            */
    int       nmembers;
    int       cmembers;

    struct SmSet **parents;  /* reverse: the sets this is a member of        */
    int       nparents;
    int       cparents;

    SmProp   *props;
    int       nprops;
    int       cprops;

    SmValue   value;         /* convenience payload (cell result, N.value)   */

    struct SmSchema *schema; /* REQUIRES contract, or NULL                   */

    unsigned  visit;         /* scratch stamp for traversal (see world)      */
} SmSet;

/* --- World ---------------------------------------------------------------
 * The container that owns every set. Creation auto-joins UNIVERSAL, giving a
 * closed-world namespace for complement and for GC's "lost every non-
 * universal parent" test.
 */
typedef struct SmBinding { char *name; SmSet *set; } SmBinding;

/* A function call frame: parameter and LOCAL names bound to sets, consulted
 * before the world by sm_lookup (so a parameter shadows a global set). */
typedef struct SmEnv { SmBinding *b; int n, c; } SmEnv;

typedef struct SmWorld {
    SmSet **all;         /* owns every live set                              */
    int     nall;
    int     call;

    SmSet  *universal;   /* built-in UNIVERSAL                               */
    SmSet  *visible;     /* built-in VISIBLE (membership == visibility)      */

    unsigned visit_seq;  /* monotonically increasing traversal stamp source  */

    /* user-function support (sm_func.c). `funcs` is the page's function map
     * (freed by `funcs_free` with the world); `env` is the active call frame
     * or NULL; `call_hook` runs a user function for CALL fn WITH (...) /
     * fn(...) expressions (args are the unevaluated SmExpr *); `doc` is the
     * owning SmDoc. */
    void    *funcs;
    void   (*funcs_free)(void *);
    SmEnv   *env;
    SmBool (*call_hook)(struct SmWorld *w, const char *name, void **args,
                        int nargs, SmSet *self, SmValue *out);
    void    *doc;
} SmWorld;

/* --- World lifecycle ----------------------------------------------------- */
SmWorld *sm_world_new(void);
void     sm_world_free(SmWorld *w);

/* --- Set creation / lookup ----------------------------------------------- */
SmSet   *sm_set_create(SmWorld *w, const char *name, SmKind kind);
SmSet   *sm_set_create_anon(SmWorld *w, SmKind kind);
SmSet   *sm_find(SmWorld *w, const char *name);      /* case-insensitive     */
/* Like sm_find, but the active function frame (parameters, LOCALs) is searched
 * first. */
SmSet   *sm_lookup(SmWorld *w, const char *name);

/* --- Membership ----------------------------------------------------------
 * sm_add_member enforces the axioms: no self-membership (S9), no cycles,
 * FLAT upward propagation (S5), and schema contracts (S6). If a schema on
 * any superset rejects the (possibly propagated) insertion, the WHOLE
 * operation is rolled back and SM_ERR_SCHEMA is returned (S6 worked
 * example). It does NOT enforce namespace exclusivity (see
 * sm_check_namespace_exclusive) -- that is a policy for callers representing
 * author intent, not a property of the membership primitive itself, since
 * sm_add_member is also how the engine wires internal bookkeeping (e.g. a
 * formula's dependencies) that is not "belonging to" in the namespace sense.
 */
SmStatus sm_add_member(SmWorld *w, SmSet *parent, SmSet *child);
SmStatus sm_remove_member(SmWorld *w, SmSet *parent, SmSet *child);
SmBool   sm_is_member(const SmSet *parent, const SmSet *child); /* direct only */

/* Is `s` a "tagging" set rather than a structural owner/container -- built-in
 * (VISIBLE, UNIVERSAL), a Colour, or carrying @style/@color? Membership in one
 * of these is orthogonal state (styling, visibility) and is exempt from both
 * the default-top-level-visibility rule and namespace exclusivity. */
SmBool   sm_is_exempt_membership(const SmSet *s);

/* If `child` is currently namespaced (a member of some SM_KIND_NAMESPACE
 * set), return that set; else NULL. */
SmSet   *sm_namespace_of(const SmSet *child);

/* Would `child` joining `parent` violate namespace exclusivity? Returns
 * SM_ERR_NAMESPACE if so, else SM_OK. A pure check -- callers representing
 * author intent (a MEMBER OF clause, an ADD action) call this before
 * sm_add_member; the engine's own internal membership wiring (formula
 * dependencies) does not, and is unaffected either way. */
SmStatus sm_check_namespace_exclusive(SmSet *parent, SmSet *child);

/* --- Properties ---------------------------------------------------------- */
SmStatus       sm_set_prop(SmSet *s, const char *key, SmValue val); /* takes ownership of val */
const SmValue *sm_get_prop(const SmSet *s, const char *key);        /* NULL if absent */

/* --- Delta propagation (S12) --------------------------------------------
 * Mark a set dirty and walk parents[] upward, marking each reachable
 * ancestor dirty exactly once. This is the structural reactivity behind
 * "no DOM reflow": a localized change stays O(affected).
 */
void sm_mark_dirty(SmWorld *w, SmSet *s);

/* --- Membership-based GC (S8) -------------------------------------------
 * Destroy every set that has lost all non-UNIVERSAL parents, cascading to
 * dependents that thereby lose their last parent. Built-in sets are exempt.
 * Returns the number of sets reclaimed.
 */
int sm_gc(SmWorld *w);

/* Convenience for flag tests. */
#define SM_HAS_FLAG(s, f) (((s)->flags & (f)) != 0u)

#endif /* SM_SET_H */
