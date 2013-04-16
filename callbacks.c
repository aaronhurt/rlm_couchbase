/* couchbase stuff */

#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/modules.h>

#include <libcouchbase/couchbase.h>

#include "callbacks.h"

/* general couchbase error callback */
void couchbase_error_callback(lcb_t instance, lcb_error_t error, const char *errinfo) {
    DEBUG("ERROR: %s (0x%x), %s", lcb_strerror(instance, error), error, errinfo);
}

/* couchbase value store callback */
void couchbase_store_callback(lcb_t instance, const void *cookie, lcb_storage_t operation, lcb_error_t error, const lcb_store_resp_t *resp) {
    if (error == LCB_SUCCESS) {
        DEBUG("KEY STORED");
    } else {
        DEBUG("STORE ERROR: %s (0x%x)", lcb_strerror(instance, error), error);
    }
    /* silent compiler */
    (void)cookie;
    (void)operation;
    (void)resp;
}

/* couchbase value get callback */
void couchbase_get_callback(lcb_t instance, const void *cookie, lcb_error_t error, const lcb_get_resp_t *resp) {
    if (error == LCB_SUCCESS) {
        void *document = cookie;    // document body

        /* allocate document body */
        document = rad_malloc(resp->v.v0.nbytes +1);

        /* zero document */
        memset(document, 0, resp->v.v0.nbytes +1);

        /* store document data */
        strncpy((char *) document, resp->v.v0.bytes, resp->v.v0.nbytes);

        /* debugging */
        DEBUG("GOT DATA: %s", (char *) document);

        /* assign cookie */
        lcb_set_cookie(instance, document);
    } else {
        DEBUG("GET ERROR: %s (0x%x)", lcb_strerror(instance, error), error);
    }
}
