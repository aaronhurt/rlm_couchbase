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

    /* check error */
    switch (error) {
        case LCB_SUCCESS:
            /* check buffer size */
            if (resp->v.v0.nkey > MAX_KEY_SIZE -1) {
                /* log error */
                ERROR("rlm_couchbase: key larger than MAX_KEY_SIZE (%d) - not showing", MAX_KEY_SIZE);
            } else {
                /* clear key buff */
                memset(key, 0, sizeof(key));
                /* format key */
                strncpy(key, resp->v.v0.key, resp->v.v0.nkey);
                /* debugging */
                DEBUG("rlm_couchbase: stored key '%s' to couchbase database", key);
            }
        break;
        default:
            /* log error */
            ERROR("rlm_couchbase: (store_callback) %s (0x%x)", lcb_strerror(instance, error), error);
        break;
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
    switch (error) {
        case LCB_SUCCESS:
            /* check that we have enoug space in buffer */
            if (resp->v.v0.nbytes > MAX_VALUE_SIZE - 1) {
                /* log error */
                ERROR("rlm_couchbase: returned document larger than MAX_VALUE_SIZE (%d) - not copying", MAX_VALUE_SIZE);
            } else {
                /* debugging */
                DEBUG("rlm_couchbase: got %zu bytes from couchbase database", resp->v.v0.nbytes);
                /* store document data */
                strncpy((char *) cookie, resp->v.v0.bytes, resp->v.v0.nbytes);
            }
        break;
        case LCB_KEY_NOENT:
            /* only need an info message here */
            INFO("rlm_couchbase: (get_callback) %s (0x%x)", lcb_strerror(instance, error), error);
        default:
            /* log error */
            ERROR("rlm_couchbase: (get_callback) %s (0x%x)", lcb_strerror(instance, error), error);
        break;
}
