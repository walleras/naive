/* sm_common.c  --  shared primitives implementation. */
#include "sm_common.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

const char *sm_status_str(SmStatus s)
{
    switch (s) {
    case SM_OK:            return "ok";
    case SM_ERR_OOM:       return "out of memory";
    case SM_ERR_NOT_FOUND: return "not found";
    case SM_ERR_DUPLICATE: return "duplicate name";
    case SM_ERR_SELF_MEMBER: return "set may not contain itself";
    case SM_ERR_SCHEMA:    return "schema contract rejected insertion";
    case SM_ERR_CYCLE:     return "membership cycle";
    case SM_ERR_TYPE:      return "type / kind mismatch";
    case SM_ERR_PARSE:     return "parse error";
    case SM_ERR_RANGE:     return "out of range";
    case SM_ERR_NAMESPACE: return "namespace exclusivity violated";
    }
    return "unknown status";
}

void *sm_alloc(size_t n)
{
    void *p;
    if (n == 0) {
        n = 1; /* avoid the implementation-defined malloc(0) return */
    }
    p = malloc(n);
    if (p != NULL) {
        memset(p, 0, n);
    }
    return p;
}

void *sm_realloc(void *p, size_t n)
{
    if (n == 0) {
        n = 1;
    }
    return realloc(p, n);
}

void sm_free(void *p)
{
    if (p != NULL) {
        free(p);
    }
}

char *sm_strdup(const char *s)
{
    size_t n;
    char *d;
    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    d = (char *)malloc(n);
    if (d != NULL) {
        memcpy(d, s, n);
    }
    return d;
}

int sm_stricmp(const char *a, const char *b)
{
    unsigned char ca, cb;
    if (a == b) {
        return 0;
    }
    if (a == NULL) {
        return -1;
    }
    if (b == NULL) {
        return 1;
    }
    for (;;) {
        ca = (unsigned char)tolower((unsigned char)*a);
        cb = (unsigned char)tolower((unsigned char)*b);
        if (ca != cb) {
            return (int)ca - (int)cb;
        }
        if (ca == 0) {
            return 0;
        }
        ++a;
        ++b;
    }
}
