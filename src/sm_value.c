/* sm_value.c  --  scalar value implementation (strict ISO C89). */
#include "sm_value.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

SmValue sm_value_none(void)
{
    SmValue v;
    v.kind = SM_VAL_NONE;
    v.number = 0.0;
    v.string = NULL;
    v.color = 0UL;
    v.set = NULL;
    return v;
}

SmValue sm_value_number(double n)
{
    SmValue v = sm_value_none();
    v.kind = SM_VAL_NUMBER;
    v.number = n;
    return v;
}

SmValue sm_value_string(const char *s)
{
    SmValue v = sm_value_none();
    v.kind = SM_VAL_STRING;
    v.string = sm_strdup(s != NULL ? s : "");
    return v;
}

SmValue sm_value_color(unsigned long rgb)
{
    SmValue v = sm_value_none();
    v.kind = SM_VAL_COLOR;
    v.color = rgb;
    return v;
}

SmValue sm_value_set(struct SmSet *ref)
{
    SmValue v = sm_value_none();
    v.kind = SM_VAL_SET;
    v.set = ref;
    return v;
}

void sm_value_clear(SmValue *v)
{
    if (v == NULL) {
        return;
    }
    if (v->kind == SM_VAL_STRING && v->string != NULL) {
        sm_free(v->string);
    }
    *v = sm_value_none();
}

SmValue sm_value_copy(const SmValue *v)
{
    if (v == NULL) {
        return sm_value_none();
    }
    if (v->kind == SM_VAL_STRING) {
        return sm_value_string(v->string);
    }
    return *v; /* NUMBER/COLOR/SET/NONE have no owned storage */
}

SmBool sm_value_truthy(const SmValue *v)
{
    if (v == NULL) {
        return SM_FALSE;
    }
    switch (v->kind) {
    case SM_VAL_NONE:   return SM_FALSE;
    case SM_VAL_NUMBER: return v->number != 0.0 ? SM_TRUE : SM_FALSE;
    case SM_VAL_STRING: return (v->string != NULL && v->string[0] != '\0')
                                   ? SM_TRUE : SM_FALSE;
    case SM_VAL_COLOR:  return SM_TRUE;
    case SM_VAL_SET:    return v->set != NULL ? SM_TRUE : SM_FALSE;
    }
    return SM_FALSE;
}

SmBool sm_value_equal(const SmValue *a, const SmValue *b)
{
    if (a == NULL || b == NULL) {
        return (a == b) ? SM_TRUE : SM_FALSE;
    }
    if (a->kind != b->kind) {
        return SM_FALSE;
    }
    switch (a->kind) {
    case SM_VAL_NONE:   return SM_TRUE;
    case SM_VAL_NUMBER: return a->number == b->number ? SM_TRUE : SM_FALSE;
    case SM_VAL_STRING: return strcmp(a->string ? a->string : "",
                                      b->string ? b->string : "") == 0
                                   ? SM_TRUE : SM_FALSE;
    case SM_VAL_COLOR:  return a->color == b->color ? SM_TRUE : SM_FALSE;
    case SM_VAL_SET:    return a->set == b->set ? SM_TRUE : SM_FALSE;
    }
    return SM_FALSE;
}

size_t sm_value_format(const SmValue *v, char *buf, size_t cap)
{
    /* No snprintf in C89: format numbers into a local scratch of known
     * bounded width, then copy with an explicit length cap. */
    char scratch[64];
    const char *src = "";
    size_t len;

    if (buf == NULL || cap == 0) {
        return 0;
    }
    if (v == NULL) {
        buf[0] = '\0';
        return 0;
    }

    switch (v->kind) {
    case SM_VAL_NONE:
        src = "";
        break;
    case SM_VAL_NUMBER:
        /* whole numbers print in full (3628800, not 3.6288e+06); anything else
         * as %g. Width is bounded well under 64. */
        if (fabs(v->number) < 1e15 && floor(v->number) == v->number) {
            sprintf(scratch, "%.0f", v->number);
        } else {
            sprintf(scratch, "%g", v->number);
        }
        src = scratch;
        break;
    case SM_VAL_STRING:
        src = (v->string != NULL) ? v->string : "";
        break;
    case SM_VAL_COLOR:
        sprintf(scratch, "#%06lX", v->color & 0xFFFFFFUL);
        src = scratch;
        break;
    case SM_VAL_SET:
        src = "<set>";
        break;
    }

    len = strlen(src);
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(buf, src, len);
    buf[len] = '\0';
    return len;
}
