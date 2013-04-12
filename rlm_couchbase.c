/* junk */

#include <freeradius-devel/ident.h>
RCSID("$Id$")

#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/modules.h>
#include <freeradius-devel/rad_assert.h>

#include <libcouchbase/couchbase.h>

/* maximum size of a stored value */
#define MAX_VALUE_SIZE 20971520

/* maximum length of a document key */
#define MAX_KEY_SIZE 250

/* configuration struct */
typedef struct rlm_couchbase_t {
    char *dockey;
    char *host;
    char *bucket;
    char *user;
    char *pass;
    lcb_t couchbase;
} rlm_couchbase_t;

/* map config to internal variables */
static const CONF_PARSER module_config[] = {
    {"key", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, dockey), NULL, "Framed-IP-Address"},
    {"server", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, host), NULL, "localhost"},
    {"bucket", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, bucket), NULL, "default"},
    {"user", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, user), NULL, NULL},
    {"pass", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, pass), NULL, NULL},
    { NULL, -1, 0, NULL, NULL }       /* end the list */
};


/* initialize couchbase connection */
static int couchbase_instantiate(CONF_SECTION *conf, void **instance) {
    rlm_couchbase_t *data;                  // module configuration struct
    lcb_error_t cb_error;                   // couchbase error holder
    struct lcb_create_st create_options;    // couchbase connection options

    /* storage for instance data */
    data = rad_malloc(sizeof(*data));
    if (!data) {
        return -1;
    }
    memset(data, 0, sizeof(*data));

    /* fail on bad config */
    if (cf_section_parse(conf, data, module_config) < 0) {
        radlog(L_ERR, "rlm_couchbase: Failed to parse config!");
        free(data);
        return -1;
    }

    /* allocate couchbase instance creation options */
    memset(&create_options, 0, sizeof(create_options));

    /* assign couchbase connection options */
    create_options.v.v0.host = data->host;
    create_options.v.v0.bucket = data->bucket;

    /* assign user and password if they were both passed */
    if (data->user !== NULL && data-pass !== NULL) {
        create_options.v.v0.user = data->user;
        create_options.v.v0.passwd = data->pass;
    }

    /* create couchbase connection instance */
    cb_error = lcb_create(&data->couchbase, &create_options);

    /* check error status */
    if (cb_error != LCB_SUCCESS) {
        radlog(L_ERR, "rlm_couchbase: Failed to create libcouchbase instance: %s", lcb_strerror(NULL, cb_error));
        free(data);
        return -1;
    }

    /* run event loop and wait for connection */
    lcb_wait(data->couchbase);

    /* assign instance */
    *instance = data;

    /* return okay */
    return 0;
}

/* handle pre accounting requests */
static int couchbase_preacct(void *instance, REQUEST *request) {
    /* just pass this off to the accounting function .... nothing special here */
    couchbase_accounting(instance, request);
}

/* write accounting data to couchbase */
static int couchbase_accounting(void *instance, REQUEST *request) {
    rlm_couchbase_t *p = instance;
    char *value_pair;
    const char *attribute;

    return RLM_MODULE_OK;
}

/* free any memory we allocated */
static int couchbase_detach(void *instance)
{
    lcb_destroy(instance->couchbase);
    free(instance);
    return 0;
}

/* hook the module into freeradius */
module_t rlm_couchbase = {
    RLM_MODULE_INIT,
    "couchbase",
    RLM_TYPE_THREAD_SAFE,       /* type */
    couchbase_instantiate,      /* instantiation */
    couchbase_detach,           /* detach */
    {
        NULL,                   /* authentication */
        NULL,                   /* authorization */
        couchbase_preacct,      /* preaccounting */
        couchbase_accounting,   /* accounting */
        NULL,                   /* checksimul */
        NULL,                   /* pre-proxy */
        NULL,                   /* post-proxy */
        NULL                    /* post-auth */
    },
};
