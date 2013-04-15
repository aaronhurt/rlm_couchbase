/* blargs */

#ifndef _CALLBACKS_H
#define _CALLBACKS_H

#include <freeradius-devel/ident.h>
RCSIDH(callbacks_h, "$Id$")

/* define functions */
void couchbase_error_callback(lcb_t instance, lcb_error_t error, const char *errinfo);

void couchbase_store_callback(lcb_t instance, const void *cookie, lcb_storage_t operation,
    lcb_error_t error, const lcb_store_resp_t *item);

void couchbase_get_callback(lcb_t instance, const void *cookie, lcb_error_t error,
    const lcb_get_resp_t *item);


#endif /* _CALLBACKS_H */
