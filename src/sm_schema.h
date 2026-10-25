/* sm_schema.h  --  forced-schema sets: validation via intersection contracts
 *  Design doc S6, grammar sketch S9.
 *
 *   CREATE Clickable AS SET
 *       REQUIRES MEMBER HAS FUNCTION
 *
 * A schema is a contract on the candidate being inserted:
 *   HAS <T> : the candidate must CONTAIN a member that is in T.
 *   IS  <T> : the candidate must itself BE a member of T.
 * Requirements chain with AND / OR (left-associative for v0.1), and any one
 * may be negated with NOT. Checked at insertion time; a FLAT propagation that
 * would carry a violating element into a schema-bearing superset fails the
 * whole insertion (enforced by sm_add_member, not here).
 */
#ifndef SM_SCHEMA_H
#define SM_SCHEMA_H

#include "sm_common.h"

struct SmSet;

typedef enum {
    SM_REQ_HAS = 0,   /* candidate contains a member of target */
    SM_REQ_IS         /* candidate is a member of target       */
} SmReqOp;

typedef enum {
    SM_CONN_END = 0,  /* no following requirement              */
    SM_CONN_AND,
    SM_CONN_OR
} SmConn;

typedef struct SmReq {
    SmReqOp        op;
    SmBool         negate;      /* NOT applied to this requirement          */
    struct SmSet  *target;      /* the set named after HAS / IS             */
    SmConn         conn;        /* connective joining this to `next`        */
    struct SmReq  *next;
} SmReq;

typedef struct SmSchema {
    SmReq *reqs;   /* head of the requirement chain                         */
} SmSchema;

SmSchema *sm_schema_new(void);
void      sm_schema_free(SmSchema *sc);

/* Append a requirement. `conn` is the connective linking the PREVIOUS tail
 * to this new one (ignored for the first). Returns the new node or NULL. */
SmReq *sm_schema_add(SmSchema *sc, SmConn conn, SmReqOp op,
                     SmBool negate, struct SmSet *target);

/* SM_TRUE if `candidate` satisfies the contract. A NULL schema is vacuously
 * satisfied. */
SmBool sm_schema_check(const SmSchema *sc, const struct SmSet *candidate);

#endif /* SM_SCHEMA_H */
