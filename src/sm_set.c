/* sm_set.c  --  set engine implementation. */
#include "sm_set.h"
#include "sm_schema.h"

#include <string.h>

/* ---- small pointer-array helper ----------------------------------------
 * Sets keep three growable arrays (members, parents, world.all). One helper
 * grows any of them. Grows geometrically; returns SM_OK / SM_ERR_OOM.
 */
static SmStatus ptr_push(SmSet ***arr, int *n, int *cap, SmSet *item)
{
    if (*n >= *cap) {
        int newcap = (*cap == 0) ? 4 : (*cap * 2);
        SmSet **grown = (SmSet **)sm_realloc(*arr, (size_t)newcap * sizeof(SmSet *));
        if (grown == NULL) {
            return SM_ERR_OOM;
        }
        *arr = grown;
        *cap = newcap;
    }
    (*arr)[(*n)++] = item;
    return SM_OK;
}

static int ptr_index(SmSet **arr, int n, const SmSet *item)
{
    int i;
    for (i = 0; i < n; ++i) {
        if (arr[i] == item) {
            return i;
        }
    }
    return -1;
}

static void ptr_remove_at(SmSet **arr, int *n, int idx)
{
    int i;
    for (i = idx; i < *n - 1; ++i) {
        arr[i] = arr[i + 1];
    }
    --(*n);
}

/* ---- world -------------------------------------------------------------- */

SmWorld *sm_world_new(void)
{
    SmWorld *w = (SmWorld *)sm_alloc(sizeof(SmWorld));
    if (w == NULL) {
        return NULL;
    }
    w->visit_seq = 1;
    /* UNIVERSAL and VISIBLE are built-ins; create_set auto-joins UNIVERSAL,
     * so UNIVERSAL itself must be created first with the join suppressed. */
    w->universal = sm_set_create(w, "UNIVERSAL", SM_KIND_SET);
    if (w->universal == NULL) {
        sm_free(w);
        return NULL;
    }
    w->universal->flags |= SM_FLAG_BUILTIN;

    w->visible = sm_set_create(w, "VISIBLE", SM_KIND_SET);
    if (w->visible == NULL) {
        sm_world_free(w);
        return NULL;
    }
    w->visible->flags |= SM_FLAG_BUILTIN;
    return w;
}

static void set_destroy(SmSet *s)
{
    int i;
    if (s == NULL) {
        return;
    }
    sm_free(s->name);
    for (i = 0; i < s->nprops; ++i) {
        sm_free(s->props[i].key);
        sm_value_clear(&s->props[i].val);
    }
    sm_free(s->props);
    sm_free(s->members);
    sm_free(s->parents);
    sm_value_clear(&s->value);
    if (s->schema != NULL) {
        sm_schema_free(s->schema);
    }
    sm_free(s);
}

void sm_world_free(SmWorld *w)
{
    int i;
    if (w == NULL) {
        return;
    }
    if (w->funcs != NULL && w->funcs_free != NULL) {
        w->funcs_free(w->funcs);
        w->funcs = NULL;
    }
    for (i = 0; i < w->nall; ++i) {
        set_destroy(w->all[i]);
    }
    sm_free(w->all);
    sm_free(w);
}

/* ---- creation / lookup -------------------------------------------------- */

static SmSet *set_alloc(SmWorld *w, const char *name, SmKind kind)
{
    SmSet *s = (SmSet *)sm_alloc(sizeof(SmSet));
    if (s == NULL) {
        return NULL;
    }
    if (name != NULL) {
        s->name = sm_strdup(name);
        if (s->name == NULL) {
            sm_free(s);
            return NULL;
        }
    }
    s->kind = kind;
    s->value = sm_value_none();
    if (ptr_push(&w->all, &w->nall, &w->call, s) != SM_OK) {
        set_destroy(s);
        return NULL;
    }
    return s;
}

SmSet *sm_set_create(SmWorld *w, const char *name, SmKind kind)
{
    SmSet *s;
    if (w == NULL) {
        return NULL;
    }
    if (name != NULL && sm_find(w, name) != NULL) {
        return NULL; /* duplicate name in this namespace */
    }
    s = set_alloc(w, name, kind);
    if (s == NULL) {
        return NULL;
    }
    /* Auto-join UNIVERSAL (closed-world). UNIVERSAL itself has no universe to
     * join, so skip while it is being created. */
    if (w->universal != NULL && s != w->universal) {
        if (ptr_push(&s->parents, &s->nparents, &s->cparents, w->universal) != SM_OK ||
            ptr_push(&w->universal->members, &w->universal->nmembers,
                     &w->universal->cmembers, s) != SM_OK) {
            /* On failure leave the set in `all`; world_free will reclaim it. */
            return NULL;
        }
    }
    return s;
}

SmSet *sm_set_create_anon(SmWorld *w, SmKind kind)
{
    return sm_set_create(w, NULL, kind);
}

SmSet *sm_find(SmWorld *w, const char *name)
{
    int i;
    if (w == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < w->nall; ++i) {
        if (w->all[i]->name != NULL && sm_stricmp(w->all[i]->name, name) == 0) {
            return w->all[i];
        }
    }
    return NULL;
}

/* ---- membership --------------------------------------------------------- */

SmBool sm_is_member(const SmSet *parent, const SmSet *child)
{
    if (parent == NULL || child == NULL) {
        return SM_FALSE;
    }
    return ptr_index(parent->members, parent->nmembers, child) >= 0
               ? SM_TRUE : SM_FALSE;
}

/* Is `needle` reachable from `start` by walking members[] downward?
 * Used for cycle detection: adding child to parent is illegal if parent is
 * already contained (transitively) in child. Uses visit stamps, no marking
 * of external state beyond the scratch `visit` field. */
static SmBool reachable_down(SmWorld *w, SmSet *start, const SmSet *needle)
{
    unsigned stamp = ++w->visit_seq;
    /* iterative DFS over a small stack allocated on demand */
    SmSet **stack = NULL;
    int n = 0, cap = 0;
    SmBool found = SM_FALSE;

    if (ptr_push(&stack, &n, &cap, start) != SM_OK) {
        return SM_FALSE; /* treat OOM as "not reachable"; caller still safe */
    }
    while (n > 0) {
        SmSet *cur = stack[--n];
        int i;
        if (cur->visit == stamp) {
            continue;
        }
        cur->visit = stamp;
        if (cur == needle) {
            found = SM_TRUE;
            break;
        }
        for (i = 0; i < cur->nmembers; ++i) {
            if (cur->members[i]->visit != stamp) {
                if (ptr_push(&stack, &n, &cap, cur->members[i]) != SM_OK) {
                    found = SM_FALSE;
                    goto done;
                }
            }
        }
    }
done:
    sm_free(stack);
    return found;
}

/* Gather FLAT-propagation targets: parent, plus every superset reachable by
 * walking parents[] upward but only *continuing through* FLAT sets. Every
 * gathered target will receive `child` as a direct member. */
static SmStatus gather_targets(SmWorld *w, SmSet *parent,
                               SmSet ***out, int *outn)
{
    unsigned stamp = ++w->visit_seq;
    SmSet **targets = NULL;
    int tn = 0, tc = 0;
    SmSet **stack = NULL;
    int sn = 0, sc = 0;
    SmStatus st = SM_OK;

    if (ptr_push(&stack, &sn, &sc, parent) != SM_OK) {
        return SM_ERR_OOM;
    }
    while (sn > 0) {
        SmSet *cur = stack[--sn];
        int i;
        if (cur->visit == stamp) {
            continue;
        }
        cur->visit = stamp;
        if (ptr_push(&targets, &tn, &tc, cur) != SM_OK) {
            st = SM_ERR_OOM;
            goto done;
        }
        /* Continue upward only through FLAT sets. */
        if (SM_HAS_FLAG(cur, SM_FLAG_FLAT)) {
            for (i = 0; i < cur->nparents; ++i) {
                SmSet *p = cur->parents[i];
                if (p != w->universal && p->visit != stamp) {
                    if (ptr_push(&stack, &sn, &sc, p) != SM_OK) {
                        st = SM_ERR_OOM;
                        goto done;
                    }
                }
            }
        }
    }
done:
    sm_free(stack);
    if (st != SM_OK) {
        sm_free(targets);
        return st;
    }
    *out = targets;
    *outn = tn;
    return SM_OK;
}

static SmStatus link_member(SmSet *parent, SmSet *child)
{
    if (ptr_push(&parent->members, &parent->nmembers, &parent->cmembers, child) != SM_OK) {
        return SM_ERR_OOM;
    }
    if (ptr_push(&child->parents, &child->nparents, &child->cparents, parent) != SM_OK) {
        /* roll back the half-link */
        ptr_remove_at(parent->members, &parent->nmembers, parent->nmembers - 1);
        return SM_ERR_OOM;
    }
    return SM_OK;
}

SmBool sm_is_exempt_membership(const SmSet *s)
{
    if (s == NULL) { return SM_FALSE; }
    if (s->flags & SM_FLAG_BUILTIN) { return SM_TRUE; }
    if (s->kind == SM_KIND_COLOR) { return SM_TRUE; }
    if (sm_get_prop(s, "@style") != NULL || sm_get_prop(s, "@color") != NULL) {
        return SM_TRUE;
    }
    return SM_FALSE;
}

SmSet *sm_namespace_of(const SmSet *child)
{
    int i;
    if (child == NULL) { return NULL; }
    for (i = 0; i < child->nparents; ++i) {
        if (child->parents[i]->kind == SM_KIND_NAMESPACE) { return child->parents[i]; }
    }
    return NULL;
}

static SmBool has_any_nonexempt_parent(const SmSet *child)
{
    int i;
    for (i = 0; i < child->nparents; ++i) {
        if (!sm_is_exempt_membership(child->parents[i])) { return SM_TRUE; }
    }
    return SM_FALSE;
}

/* Would linking `child` under `parent` violate namespace exclusivity (a
 * namespaced set's one owning membership, besides exempt tags like VISIBLE or
 * styling, is exclusive)? This is a POLICY check, deliberately NOT built into
 * sm_add_member itself: sm_add_member is also how the engine wires a
 * formula's dependencies as membership (e.g. B1 CONTAINS A1 because B1's
 * formula reads A1), which is bookkeeping, not an author expressing "A1
 * belongs to B1" -- so callers that DO represent author intent (a MEMBER OF
 * clause, an ADD action) call this explicitly before sm_add_member; internal
 * wiring does not, and is unaffected by namespace membership either way. */
SmStatus sm_check_namespace_exclusive(SmSet *parent, SmSet *child)
{
    SmSet *ns;
    if (parent == NULL || child == NULL) { return SM_OK; }
    ns = sm_namespace_of(child);
    if (sm_is_exempt_membership(parent) || parent == ns) { return SM_OK; }
    if (ns != NULL) { return SM_ERR_NAMESPACE; }                 /* namespaced elsewhere */
    if (parent->kind == SM_KIND_NAMESPACE && has_any_nonexempt_parent(child)) {
        return SM_ERR_NAMESPACE;                                 /* already owned elsewhere */
    }
    return SM_OK;
}

SmStatus sm_add_member(SmWorld *w, SmSet *parent, SmSet *child)
{
    SmSet **targets = NULL;
    int tn = 0, i;
    SmStatus st;

    if (w == NULL || parent == NULL || child == NULL) {
        return SM_ERR_NOT_FOUND;
    }
    if (parent == child) {
        return SM_ERR_SELF_MEMBER;            /* axiom S9 */
    }
    if (sm_is_member(parent, child)) {
        return SM_OK;                          /* idempotent */
    }
    if (reachable_down(w, child, parent)) {
        return SM_ERR_CYCLE;                   /* would create a cycle */
    }

    st = gather_targets(w, parent, &targets, &tn);
    if (st != SM_OK) {
        return st;
    }

    /* Schema gate: every target's contract must accept the candidate BEFORE
     * anything is mutated. This is the all-or-nothing rollback of S6 -- if a
     * FLAT propagation would carry `child` into a schema-bearing superset that
     * rejects it, the entire insertion (including the local add) fails.
     *
     * A FLAT child is exempt: adding a flat set to a superset declares an
     * "is a kind of" subset relationship (S5), not an element. The element
     * predicate governs what the subset *contains*, which is checked when
     * those elements are inserted and propagate up -- not the subset itself. */
    if (!SM_HAS_FLAG(child, SM_FLAG_FLAT)) {
        for (i = 0; i < tn; ++i) {
            if (targets[i]->schema != NULL &&
                !sm_schema_check(targets[i]->schema, child)) {
                sm_free(targets);
                return SM_ERR_SCHEMA;
            }
        }
    }

    /* Commit: link into every target not already containing child. */
    for (i = 0; i < tn; ++i) {
        if (!sm_is_member(targets[i], child)) {
            st = link_member(targets[i], child);
            if (st != SM_OK) {
                /* Partial-commit rollback: unlink everything linked so far. */
                int j;
                for (j = 0; j < i; ++j) {
                    int idx = ptr_index(targets[j]->members, targets[j]->nmembers, child);
                    if (idx >= 0) {
                        ptr_remove_at(targets[j]->members, &targets[j]->nmembers, idx);
                    }
                    idx = ptr_index(child->parents, child->nparents, targets[j]);
                    if (idx >= 0) {
                        ptr_remove_at(child->parents, &child->nparents, idx);
                    }
                }
                sm_free(targets);
                return st;
            }
            sm_mark_dirty(w, targets[i]);
        }
    }

    sm_free(targets);
    return SM_OK;
}

SmStatus sm_remove_member(SmWorld *w, SmSet *parent, SmSet *child)
{
    int idx;
    if (w == NULL || parent == NULL || child == NULL) {
        return SM_ERR_NOT_FOUND;
    }
    idx = ptr_index(parent->members, parent->nmembers, child);
    if (idx < 0) {
        return SM_ERR_NOT_FOUND;
    }
    ptr_remove_at(parent->members, &parent->nmembers, idx);
    idx = ptr_index(child->parents, child->nparents, parent);
    if (idx >= 0) {
        ptr_remove_at(child->parents, &child->nparents, idx);
    }
    sm_mark_dirty(w, parent);
    return SM_OK;
}

/* ---- properties --------------------------------------------------------- */

SmStatus sm_set_prop(SmSet *s, const char *key, SmValue val)
{
    int i;
    if (s == NULL || key == NULL) {
        sm_value_clear(&val);
        return SM_ERR_NOT_FOUND;
    }
    for (i = 0; i < s->nprops; ++i) {
        if (sm_stricmp(s->props[i].key, key) == 0) {
            sm_value_clear(&s->props[i].val);
            s->props[i].val = val; /* take ownership */
            return SM_OK;
        }
    }
    if (s->nprops >= s->cprops) {
        int newcap = (s->cprops == 0) ? 4 : s->cprops * 2;
        SmProp *grown = (SmProp *)sm_realloc(s->props, (size_t)newcap * sizeof(SmProp));
        if (grown == NULL) {
            sm_value_clear(&val);
            return SM_ERR_OOM;
        }
        s->props = grown;
        s->cprops = newcap;
    }
    s->props[s->nprops].key = sm_strdup(key);
    if (s->props[s->nprops].key == NULL) {
        sm_value_clear(&val);
        return SM_ERR_OOM;
    }
    s->props[s->nprops].val = val;
    ++s->nprops;
    return SM_OK;
}

const SmValue *sm_get_prop(const SmSet *s, const char *key)
{
    int i;
    if (s == NULL || key == NULL) {
        return NULL;
    }
    for (i = 0; i < s->nprops; ++i) {
        if (sm_stricmp(s->props[i].key, key) == 0) {
            return &s->props[i].val;
        }
    }
    return NULL;
}

/* ---- delta propagation -------------------------------------------------- */

void sm_mark_dirty(SmWorld *w, SmSet *s)
{
    unsigned stamp;
    SmSet **stack = NULL;
    int n = 0, cap = 0;

    if (w == NULL || s == NULL) {
        return;
    }
    stamp = ++w->visit_seq;
    if (ptr_push(&stack, &n, &cap, s) != SM_OK) {
        s->flags |= SM_FLAG_DIRTY; /* best effort */
        return;
    }
    while (n > 0) {
        SmSet *cur = stack[--n];
        int i;
        if (cur->visit == stamp) {
            continue;
        }
        cur->visit = stamp;
        cur->flags |= SM_FLAG_DIRTY;
        for (i = 0; i < cur->nparents; ++i) {
            SmSet *p = cur->parents[i];
            if (p != w->universal && p->visit != stamp) {
                if (ptr_push(&stack, &n, &cap, p) != SM_OK) {
                    goto done;
                }
            }
        }
    }
done:
    sm_free(stack);
}

/* ---- GC ----------------------------------------------------------------
 * Reclaim sets that have lost every parent (nparents == 0). Built-ins are
 * exempt. Freeing a set drops it from its members' parent lists, which can
 * orphan those members in turn, so we iterate to a fixpoint. */
static void detach_from_members(SmSet *s)
{
    int i;
    for (i = 0; i < s->nmembers; ++i) {
        SmSet *m = s->members[i];
        int idx = ptr_index(m->parents, m->nparents, s);
        if (idx >= 0) {
            ptr_remove_at(m->parents, &m->nparents, idx);
        }
    }
}

int sm_gc(SmWorld *w)
{
    int reclaimed = 0;
    SmBool changed = SM_TRUE;
    if (w == NULL) {
        return 0;
    }
    while (changed) {
        int i;
        changed = SM_FALSE;
        for (i = 0; i < w->nall; ++i) {
            SmSet *s = w->all[i];
            if (s == NULL) {
                continue;
            }
            if (SM_HAS_FLAG(s, SM_FLAG_BUILTIN)) {
                continue;
            }
            if (s->nparents == 0) {
                detach_from_members(s);
                set_destroy(s);
                ptr_remove_at(w->all, &w->nall, i);
                --i;
                ++reclaimed;
                changed = SM_TRUE;
            }
        }
    }
    return reclaimed;
}

SmSet *sm_lookup(SmWorld *w, const char *name)
{
    int i;
    if (w == NULL || name == NULL) {
        return NULL;
    }
    if (w->env != NULL) {
        for (i = w->env->n - 1; i >= 0; --i) {
            if (sm_stricmp(w->env->b[i].name, name) == 0) {
                return w->env->b[i].set;
            }
        }
    }
    return sm_find(w, name);
}
