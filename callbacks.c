/* couchbase stuff */

#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/modules.h>

#include <libcouchbase/couchbase.h>

#include "callbacks.h"

/* general couchbase error callback */
void couchbase_error_callback(lcb_t instance, lcb_error_t error, const char *errinfo) {
    DEBUG("rlm_couchbase: Error, %s (0x%x), %s", lcb_strerror(instance, error), error, errinfo);
}

/* couchbase value store callback */
void couchbase_store_callback(lcb_t instance, const void *cookie, lcb_storage_t operation, lcb_error_t error, const lcb_store_resp_t *resp) {
    if (error != LCB_SUCCESS) {
        DEBUG("rlm_couchbase: Error, %s (0x%x)", lcb_strerror(instance, error), error);
    }
    /* silent compiler */
    (void)cookie;
    (void)operation;
    (void)resp;
}

/* couchbase value get callback */
void couchbase_get_callback(lcb_t instance, const void *cookie, lcb_error_t error, const lcb_get_resp_t *resp) {
    /* clear cookie */
    memset((char *) cookie, 0, MAX_VALUE_SIZE);
    /* check error */
    if (error == LCB_SUCCESS) {
        /* check that we have enoug space in buffer */
        if (resp->v.v0.nbytes > MAX_VALUE_SIZE -1) {
            /* debugging */
            DEBUG("rlm_couchbase: Error, returned document too large for buffer!");
        } else {
            /* store document data */ 
            strncpy((char *) cookie, resp->v.v0.bytes, resp->v.v0.nbytes);
        }
    } else {
        DEBUG("rlm_couchbase: Error, %s (0x%x)", lcb_strerror(instance, error), error);
    }
}
