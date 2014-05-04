/* couchbase */

#ifndef _couchbase_h_
#define _couchbase_h_

RCSIDH(couchbase_h, "$Id$");

#include <libcouchbase/couchbase.h>
#include <json/json.h>

/* struct to hold cookie data for couchbase callbacks */
typedef struct cookie_t {
    json_object *jobj;              /* json object */
    json_tokener *jtok;             /* json tokener */
    enum json_tokener_error jerr;   /* tokener error */
} cookie_t;

/* union of const and non const pointers */
typedef union cookie_u {
    const void *cdata;
    void *data;
} cookie_u;

/* general error callback */
void couchbase_error_callback(lcb_t instance, lcb_error_t error, const char *errinfo);

/* store a key/document in couchbase */
void couchbase_store_callback(lcb_t instance, const void *cookie, lcb_storage_t operation,
    lcb_error_t error, const lcb_store_resp_t *item);

/* get a document by key from couchbase */
void couchbase_get_callback(lcb_t instance, const void *cookie, lcb_error_t error,
    const lcb_get_resp_t *item);

/* create a couchbase instance and connect to the cluster */
lcb_t couchbase_init_connection(const char *host, const char *bucket, const char *pass);

/* store document/key in couchbase */
lcb_error_t couchbase_set_key(lcb_t instance, const char *key, const char *document, int expire);

/* pull document from couchbase by key */
lcb_error_t couchbase_get_key(lcb_t instance, const void *cookie, const char *key);

#endif /* _couchbase_h_ */
