/* sm_expr.h  --  Naive expression parser + evaluator.
 *                Grammar sketch S8 (C-style precedence).
 *
 * An expression is parsed once into a small tree and retained; a Webcel cell
 * re-evaluates its tree on every recompute, resolving bareword identifiers as
 * set references *at eval time* so live value changes are seen. Naive has no
 * booleans: comparisons and logical ops yield the number 1 or 0, and
 * truthiness is "nonzero number / nonempty string / nonempty set".
 *
 * Built-in aggregate calls (CALL SUM WITH (Col), also AVG/MIN/MAX/COUNT)
 * operate over the members of a set. User-defined FUNCTION execution is not
 * handled here: a non-builtin CALL is forwarded to the world's call_hook
 * (sm_func.c).
 */
#ifndef SM_EXPR_H
#define SM_EXPR_H

#include "sm_common.h"
#include "sm_value.h"
#include "sm_set.h"

typedef struct SmExpr SmExpr; /* opaque tree node */

/* Parse `src` into an expression tree. On error returns SM_ERR_PARSE and, if
 * errbuf != NULL, writes a message. *out is set only on success; free it with
 * sm_expr_free. The tree does not reference `src` after parsing (names are
 * copied), so `src` need not outlive it. */
SmStatus sm_expr_parse(const char *src, SmExpr **out,
                       char *errbuf, size_t errcap);

void sm_expr_free(SmExpr *e);

/* Collect the set names an expression references (identifiers and dotted
 * owners -- or, if `w` resolves a dotted owner to a Namespace, the specific
 * MEMBER referenced, e.g. "Sheet1.A1" yields "A1"), writing borrowed pointers
 * (valid while `e` lives) into out[] up to `cap`. Returns the number of names
 * found; may exceed `cap` (only `cap` are written). `w` may be NULL, in which
 * case dotted references always yield the owner name. A page can wire a
 * widget's dependencies as membership from this, so delta propagation
 * (sm_mark_dirty) reaches everything that must recompute -- with no
 * engine-level "formula" concept, just this general name-reference utility. */
int sm_expr_collect_names(const SmExpr *e, SmWorld *w, const char **out, int cap);

/* Evaluate against a world. `self` is the set the expression belongs to (a
 * cell), used to resolve any self-relative references; may be NULL. Returns
 * an owned SmValue -- clear it with sm_value_clear. Unresolvable names
 * evaluate to NONE (Naive has no null; an absent reference contributes
 * nothing) rather than raising. */
SmValue sm_expr_eval(const SmExpr *e, SmWorld *w, SmSet *self);

/* If `e` is just a bare name (no dot, no operator) return that name, else
 * NULL. Used to pass sets *by reference* as function arguments. Valid while
 * `e` lives. */
const char *sm_expr_bare_name(const SmExpr *e);

#endif /* SM_EXPR_H */
