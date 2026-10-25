/* sm_value.h  --  scalar payloads carried by sets.
 *
 * In the pure model every scalar is really an ordered set (design doc S4),
 * but a shippable runtime keeps numbers/strings/colours as concrete values
 * attached to a set via WITH (`value`, `text`, `content`, `checked`, ...).
 * A value may also *be* a reference to another set (SET kind), which is how
 * a property points at a coordinate set, a colour set, or a function set.
 */
#ifndef SM_VALUE_H
#define SM_VALUE_H

#include "sm_common.h"

struct SmSet; /* forward: a value can reference a set without a hard dep */

typedef enum {
    SM_VAL_NONE = 0,   /* absent -- Naive has no null; this means "unset"   */
    SM_VAL_NUMBER,     /* IEEE double; Naive integers are exact within it   */
    SM_VAL_STRING,     /* owned, NUL-terminated                              */
    SM_VAL_COLOR,      /* 0x00RRGGBB                                         */
    SM_VAL_SET         /* borrowed reference to another set (not owned)      */
} SmValueKind;

typedef struct {
    SmValueKind kind;
    double        number; /* SM_VAL_NUMBER                                   */
    char         *string; /* SM_VAL_STRING (owned)                          */
    unsigned long color;  /* SM_VAL_COLOR                                    */
    struct SmSet *set;    /* SM_VAL_SET (borrowed)                          */
} SmValue;

SmValue sm_value_none(void);
SmValue sm_value_number(double n);
SmValue sm_value_string(const char *s);   /* copies s                       */
SmValue sm_value_color(unsigned long rgb);
SmValue sm_value_set(struct SmSet *ref);

/* Free any owned storage and reset to NONE. Safe on an already-NONE value. */
void    sm_value_clear(SmValue *v);

/* Deep copy (duplicates an owned string; a set ref stays a borrow).        */
SmValue sm_value_copy(const SmValue *v);

SmBool  sm_value_truthy(const SmValue *v);      /* nonzero number, nonempty  */
SmBool  sm_value_equal(const SmValue *a, const SmValue *b);

/* Format for display into caller's buffer; always NUL-terminates. Returns
 * the number of characters written (excluding the terminator).             */
size_t  sm_value_format(const SmValue *v, char *buf, size_t cap);

#endif /* SM_VALUE_H */
