/* couchbase stuff */

#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/modules.h>

#include <libcouchbase/couchbase.h>

#include "callbacks.h"

/* jsmn from https://bitbucket.org/zserge/jsmn */
#include "jsmn.h"

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
    jsmn_parser parser;     // json parser
    jsmntok_t jsmn_tokens[255];       // json token holder
    int jsmn_response;      // json parser response
    int i;                  // simple counter
    char *key;              // document key
    char *document;         // document body

    if (error == LCB_SUCCESS) {
        /* debugging */
        DEBUG("nkey => %d, nbytes => %d", resp->v.v0.nkey, resp->v.v0.nbytes);

        /* init key */
        key = rad_malloc(resp->v.v0.nkey +1);
        memset(key, 0, resp->v.v0.nkey +1);

        /* store key */
        strncpy(key, resp->v.v0.key, resp->v.v0.nkey);

        /* init document */
        document = rad_malloc(resp->v.v0.nbytes +1);
        memset(document, 0, resp->v.v0.nbytes +1);

        /* store data */
        strncpy(document, resp->v.v0.bytes, resp->v.v0.nbytes);

        /* debugging */
        DEBUG("GOT KEY: %s ", key);
        DEBUG("DATA: %s", document);

        /* initialize parser */
        jsmn_init(&parser);

        /* parse the json string */
        jsmn_response = jsmn_parse(&parser, document, jsmn_tokens, 255);

        /* check response */
        if (jsmn_response == JSMN_SUCCESS) {
            /* weeee ... almost there */
            for (i = 0; i <= 255; i++) {
                DEBUG("TOKEN %d: start: %d, end: %d, type: %d, size: %d",
                    i, jsmn_tokens[i].start, jsmn_tokens[i].end, jsmn_tokens[i].type, jsmn_tokens[i].size);
            }
        } else {
            /* debugging */
            DEBUG("Failed to parse json string: %d", jsmn_response);
        }

        /* free stuff */
        free(key);
        free(document);
    } else {
        DEBUG("GET ERROR: %s (0x%x)", lcb_strerror(instance, error), error);
    }
    /* silent compiler */;
    (void)cookie;
}
