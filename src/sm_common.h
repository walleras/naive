/* sm_common.h  --  SeSaML runtime: shared primitives (strict ISO C89).
 *
 * This header and every module that includes it must compile clean under
 *   gcc -std=c89 -pedantic-errors -Wall -Wextra
 * so nothing later than 1990-vintage ANSI C is permitted here: no // comments,
 * no long long, no snprintf, no mixed declarations-and-code, no inline.
 */
#ifndef SM_COMMON_H
#define SM_COMMON_H

#include <stddef.h>

/* --- Boolean spelling (C89 has no <stdbool.h>) --------------------------- */
typedef int SmBool;
#define SM_TRUE  1
#define SM_FALSE 0

/* --- Result codes --------------------------------------------------------
 * The engine never uses C++-style exceptions; every fallible call returns an
 * SmStatus.  SM_OK is zero so `if (status)` reads as "if something went wrong".
 */
typedef enum {
    SM_OK = 0,
    SM_ERR_OOM,            /* allocation failed                              */
    SM_ERR_NOT_FOUND,      /* named set / member / property does not exist   */
    SM_ERR_DUPLICATE,      /* name already declared in this namespace        */
    SM_ERR_SELF_MEMBER,    /* axiom violation: a set may not contain itself  */
    SM_ERR_SCHEMA,         /* a REQUIRES contract rejected an insertion      */
    SM_ERR_CYCLE,          /* membership cycle detected                      */
    SM_ERR_TYPE,           /* value used where an incompatible kind expected */
    SM_ERR_PARSE,          /* surface-syntax error                           */
    SM_ERR_RANGE,          /* numeric / index out of range                   */
    SM_ERR_NAMESPACE       /* axiom violation: a namespaced set's one owning */
                           /* membership is exclusive (see sm_add_member)    */
} SmStatus;

const char *sm_status_str(SmStatus s);

/* --- Allocation ----------------------------------------------------------
 * Thin wrappers so allocation can later be pooled or instrumented in one
 * place.  sm_alloc zeroes; sm_strdup duplicates a NUL-terminated string.
 */
void *sm_alloc(size_t n);
void *sm_realloc(void *p, size_t n);
void  sm_free(void *p);
char *sm_strdup(const char *s);

/* Case-insensitive compare: the language is case-insensitive throughout,
 * so identifier and keyword matching goes through this rather than strcmp. */
int sm_stricmp(const char *a, const char *b);

#endif /* SM_COMMON_H */
