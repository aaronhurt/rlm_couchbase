/* couchbase callbacks */

#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/modules.h>

#include <libcouchbase/couchbase.h>

#include "callbacks.h"

void couchbase_error_callback(lcb_t instance, lcb_error_t error, const char *errinfo) {
    /* log error */
    DEBUG("ERROR: %s (0x%x), %s", lcb_strerror(instance, error), error, errinfo);
}

void couchbase_store_callback(lcb_t instance, const void *cookie, lcb_storage_t operation, lcb_error_t error, const lcb_store_resp_t *item) {
    /* log error */
    if (error != LCB_SUCCESS) {
        DEBUG("STORE ERROR: %s (0x%x)", lcb_strerror(instance, error), error);
    }
    /* silent compiler */
    (void)cookie;
    (void)operation;
    (void)item;
}

void couchbase_get_callback(lcb_t instance, const void *cookie, lcb_error_t error, const lcb_get_resp_t *item) {
    /* log error */
    if (error != LCB_SUCCESS) {
        DEBUG("GET ERROR: %s (0x%x)", lcb_strerror(instance, error), error);
    }
    /* silent compiler */;
    (void)cookie;
    (void)item;
}
