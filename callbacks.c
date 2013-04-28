/* couchbase stuff */
RCSID("$Id$")

#include <freeradius-devel/radiusd.h>

#include <libcouchbase/couchbase.h>

#include "callbacks.h"
#include "util.h"

/* general couchbase error callback */
void couchbase_error_callback(lcb_t instance, lcb_error_t error, const char *errinfo) {
    /* log error */
    ERROR("rlm_couchbase: (error_callback) %s (0x%x), %s", lcb_strerror(instance, error), error, errinfo);
}

/* couchbase value store callback */
void couchbase_store_callback(lcb_t instance, const void *cookie, lcb_storage_t operation, lcb_error_t error, const lcb_store_resp_t *resp) {
    char key[MAX_KEY_SIZE];     /* key name for this operation */

    if (error == LCB_SUCCESS) {
        /* check buffer size */
        if (resp->v.v0.nkey > MAX_KEY_SIZE -1) {
            /* log error */
            ERROR("rlm_couchbase: Key larger than MAX_KEY_SIZE (%d) - not showing", MAX_KEY_SIZE);
        } else {
            /* clear key buff */
            memset(key, 0, sizeof(key));
            /* format key */
            strncpy(key, resp->v.v0.key, resp->v.v0.nkey);
            /* debugging */
            DEBUG("rlm_couchbase: Stored key '%s' to Couchbase server", key);
        }
    } else {
        /* log error */
        ERROR("rlm_couchbase: (store_callback) %s (0x%x)", lcb_strerror(instance, error), error);
    }
    /* silent compiler */
    (void)cookie;
    (void)operation;
}

/* couchbase value get callback */
void couchbase_get_callback(lcb_t instance, const void *cookie, lcb_error_t error, const lcb_get_resp_t *resp) {
    /* ensure clean cookie buffer */
    memset((char *) cookie, 0, MAX_VALUE_SIZE);

    /* check error */
    if (error == LCB_SUCCESS) {
        /* check that we have enoug space in buffer */
        if (resp->v.v0.nbytes > MAX_VALUE_SIZE - 1) {
            /* log error */
            ERROR("rlm_couchbase: Returned document larger than MAX_VALUE_SIZE (%d) - not copying", MAX_VALUE_SIZE);
        } else {
            /* debugging */
            DEBUG("rlm_couchbase: Got %zu bytes from Couchbase server", resp->v.v0.nbytes);
            /* store document data */
            strncpy((char *) cookie, resp->v.v0.bytes, resp->v.v0.nbytes);
        }
    } else {
        /* log error */
        ERROR("rlm_couchbase: (get_callback) %s (0x%x)", lcb_strerror(instance, error), error);
    }
}
