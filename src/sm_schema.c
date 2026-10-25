/* sm_schema.c  --  forced-schema contract checking. */
#include "sm_schema.h"
#include "sm_set.h"

SmSchema *sm_schema_new(void)
{
    return (SmSchema *)sm_alloc(sizeof(SmSchema));
}

void sm_schema_free(SmSchema *sc)
{
    SmReq *r, *next;
    if (sc == NULL) {
        return;
    }
    r = sc->reqs;
    while (r != NULL) {
        next = r->next;
        sm_free(r);
        r = next;
    }
    sm_free(sc);
}

SmReq *sm_schema_add(SmSchema *sc, SmConn conn, SmReqOp op,
                     SmBool negate, struct SmSet *target)
{
    SmReq *r, *tail;
    if (sc == NULL) {
        return NULL;
    }
    r = (SmReq *)sm_alloc(sizeof(SmReq));
    if (r == NULL) {
        return NULL;
    }
    r->op = op;
    r->negate = negate;
    r->target = target;
    r->conn = SM_CONN_END;
    r->next = NULL;

    if (sc->reqs == NULL) {
        sc->reqs = r;
    } else {
        tail = sc->reqs;
        while (tail->next != NULL) {
            tail = tail->next;
        }
        tail->conn = (conn == SM_CONN_END) ? SM_CONN_AND : conn;
        tail->next = r;
    }
    return r;
}

/* Evaluate a single requirement against the candidate. */
static SmBool req_eval(const SmReq *r, const SmSet *candidate)
{
    SmBool result = SM_FALSE;
    int i;

    if (r->target == NULL || candidate == NULL) {
        result = SM_FALSE;
    } else if (r->op == SM_REQ_IS) {
        /* candidate must itself be a member of target */
        result = sm_is_member(r->target, candidate);
    } else { /* SM_REQ_HAS: candidate contains a member that is in target */
        result = SM_FALSE;
        for (i = 0; i < candidate->nmembers; ++i) {
            if (sm_is_member(r->target, candidate->members[i])) {
                result = SM_TRUE;
                break;
            }
        }
    }

    if (r->negate) {
        result = result ? SM_FALSE : SM_TRUE;
    }
    return result;
}

SmBool sm_schema_check(const SmSchema *sc, const SmSet *candidate)
{
    const SmReq *r;
    SmBool acc;

    if (sc == NULL || sc->reqs == NULL) {
        return SM_TRUE; /* vacuously satisfied */
    }

    /* Left-associative fold over the AND/OR chain (no precedence in v0.1). */
    r = sc->reqs;
    acc = req_eval(r, candidate);
    while (r->conn != SM_CONN_END && r->next != NULL) {
        SmBool rhs;
        SmConn conn = r->conn;
        r = r->next;
        rhs = req_eval(r, candidate);
        if (conn == SM_CONN_OR) {
            acc = (acc || rhs) ? SM_TRUE : SM_FALSE;
        } else {
            acc = (acc && rhs) ? SM_TRUE : SM_FALSE;
        }
    }
    return acc;
}
