/* junk */

#include <freeradius-devel/ident.h>
RCSID("$Id$")

#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/libradius.h>
#include <freeradius-devel/modules.h>
#include <freeradius-devel/rad_assert.h>

#include <libcouchbase/couchbase.h>

#include <json/json.h>

#include "callbacks.h"

/* maximum size of a stored value */
#define MAX_VALUE_SIZE 4096

/* maximum length of a document key */
#define MAX_KEY_SIZE 250

/* configuration struct */
typedef struct rlm_couchbase_t {
    char *dockey;
    char *host;
    char *bucket;
    char *user;
    char *pass;
    void *cookie;
    lcb_t couchbase;
} rlm_couchbase_t;

/* map config to internal variables */
static const CONF_PARSER module_config[] = {
    {"dockey", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, dockey), NULL, "Acct-Unique-Session-Id"},
    {"host", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, host), NULL, "localhost"},
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

    /* clear data */
    memset(data, 0, sizeof(*data));

    /* fail on bad config */
    if (cf_section_parse(conf, data, module_config) < 0) {
        radlog(L_ERR, "rlm_couchbase: Failed to parse config!");
        free(data);
        return -1;
    }

    /* debugging */
    DEBUG("dockey = %s | host = %s | bucket = %s | user = %s | pass = %s",
           data->dockey, data->host, data->bucket, data->user, data->pass);

    /* allocate couchbase instance creation options */
    memset(&create_options, 0, sizeof(create_options));

    /* assign couchbase connection options */
    create_options.v.v0.host = data->host;
    create_options.v.v0.bucket = data->bucket;

    /* assign user and password if they were both passed */
    if (data->user != NULL || data->pass != NULL) {
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

    /* set error callback */
    lcb_set_error_callback(data->couchbase, couchbase_error_callback);

    /* initiate connection */
    if ((cb_error = lcb_connect(data->couchbase)) != LCB_SUCCESS) {
        radlog(L_ERR, "rlm_couchbase: Failed to initiate connect: %s", lcb_strerror(NULL, cb_error));
        lcb_destroy(data->couchbase);
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

/* write accounting data to couchbase */
static int couchbase_accounting(void *instance, REQUEST *request) {
    rlm_couchbase_t *p = instance;      // couchbase instance
    const char *attribute;              // radius attribute key
    char value[255];                    // radius attribute value
    char key[MAX_KEY_SIZE];             // couchbase document key
    char document[MAX_VALUE_SIZE];      // couchbase document body
    int length;                         // returned value length holder
    lcb_error_t cb_error;               // couchbase error holder
    json_object *json_out, *json_in;    // json objects

    /* assert packet as not null*/
    rad_assert(request->packet != NULL);

    /* fetch value pairs from packet */
    VALUE_PAIR *vp = request->packet->vps;

    /* init document */
    memset(document, 0, sizeof(document));

    /* start json output document */
    json_out = json_object_new_object();

    /* loop through value pairs */
    while (vp) {
        /* get current attribute */
        attribute = vp->name;

        /* look for document key attribute */
        if (strcmp(attribute, p->dockey) == 0) {
            /* get and store our key */
             vp_prints_value(key, sizeof(key), vp, 0);

            /* debugging */
            RDEBUG("Found key attribute: '%s' => '%s'", attribute, key);

            /* setup get callback to check for this key in couchbase */
            lcb_set_get_callback(p->couchbase, couchbase_get_callback);

            /* enter event loop */
            lcb_wait(p->couchbase);

            /* prevent variable conflicts in local space */
            {
                /* init command structs */
                lcb_get_cmd_t cmd;
                const lcb_get_cmd_t *commands[1];

                /* setup commands for get operation */
                commands[0] = &cmd;
                memset(&cmd, 0 , sizeof(cmd));

                /* populate command struct */
                cmd.v.v0.key = key;
                cmd.v.v0.nkey = strlen(key);

                /* get document */
                cb_error = lcb_get(p->couchbase, NULL, 1, commands);

                /* check return */
                if (cb_error == LCB_SUCCESS) {
                    /* get cookie */
                    p->cookie = lcb_get_cookie(p->couchbase);
                } else {
                    /* debuggimg */
                    RDEBUG("Failed to get document (%s): %s", key, lcb_strerror(NULL, cb_error));
                    /* null cooike */
                    p->cookie = '\0';
                }
            }

            /* run the event loop */
            lcb_wait(p->couchbase);
        }

        /* get and store value */
        length = vp_prints_value(value, sizeof(value), vp, 0);

        /* debugging */
        RDEBUG("%s => %s", attribute, value);

        /* add this attribute/value pair to our json output */
        switch (vp->type) {
            case PW_TYPE_INTEGER:
            case PW_TYPE_BYTE:
            case PW_TYPE_SHORT:
            case PW_TYPE_SIGNED:
            case PW_TYPE_OCTETS:
            case PW_TYPE_IPV6PREFIX:
                /* add it as an int */
                json_object_object_add(json_out, attribute, json_object_new_int(atoi(value)));
            break;
            case PW_TYPE_INTEGER64:
                /* add a long int */
                json_object_object_add(json_out, attribute, json_object_new_int64(atol(value)));
            break;
            default:
                /* it must be a string */
                json_object_object_add(json_out, attribute, json_object_new_string(value));
            break;
        }

        /* goto next pair */
        vp = vp->next;
    }

    /* check for found document */
    if (p->cookie != '\0') {
        /* debugging */
        RDEBUG("in accounting - p->cookie == %s", p->cookie);
    }

    /* make sure we have enough room in our document buffer */
    if (json_object_get_string_len(json_out) > sizeof(document) - 1) {
        /* this isn't good */
        radlog(L_ERR, "rlm_couchbase: Could not write json document.  Insufficient buffer space!");
        /* free json_out */
        json_object_put(json_out);
        /* return */
        return RLM_MODULE_FAIL;
    } else {
        /* copy json string to document */
        strncpy(document, json_object_to_json_string(json_out), sizeof(document));
        /* free json object */
        json_object_put(json_out);
    }

    /* setup store callback */
    lcb_set_store_callback(p->couchbase, couchbase_store_callback);

    /* run the event loop */
    lcb_wait(p->couchbase);

    /* prevent variable conflicts in local space */
    {
        /* init command structs */
        lcb_store_cmd_t cmd;
        const lcb_store_cmd_t *commands[1];

        /* setup commands for storage operation */
        commands[0] = &cmd;
        memset(&cmd, 0, sizeof(cmd));

        /* set commands */
        cmd.v.v0.operation = LCB_SET;
        cmd.v.v0.key = key;
        cmd.v.v0.nkey = strlen(key);
        cmd.v.v0.bytes = document;
        cmd.v.v0.nbytes = strlen(document);

        /* debugging */
        RDEBUG("setting '%s' => '%s'", key, document);

        /* store document */
        cb_error = lcb_store(p->couchbase, NULL, 1, commands);

        /* check return */
        if (cb_error != LCB_SUCCESS) {
            radlog(L_ERR, "rlm_couchbase: Failed to store document (%s): %s", key, lcb_strerror(NULL, cb_error));
        }
    }

    /* run the event loop */
    lcb_wait(p->couchbase);

    /* return */
    return RLM_MODULE_OK;
}

/* free any memory we allocated */
static int couchbase_detach(void *instance)
{
    rlm_couchbase_t *p = instance;  // instance struct

    /* destroy/free couchbase instance */
    lcb_destroy(p->couchbase);

    /* free cookie */
    free(p->cookie);

    /* free radius instance */
    free(p);

    /* return okay */
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
        NULL,                   /* preaccounting */
        couchbase_accounting,   /* accounting */
        NULL,                   /* checksimul */
        NULL,                   /* pre-proxy */
        NULL,                   /* post-proxy */
        NULL                    /* post-auth */
    },
};
