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
    int added = 0;                      // attribute added toggle
    int keyset = 0;                     // document key toggle
    int status = 0;                     // account status type
    lcb_error_t cb_error;               // couchbase error holder
    json_object *json_out, *json_in;    // json objects
    enum json_tokener_error json_error = json_tokener_success;  // json parse error
    VALUE_PAIR *vp;                     // radius value pair linked list

    /* assert packet as not null*/
    rad_assert(request->packet != NULL);

    /* sanity check */
    if ((vp = pairfind(request->packet->vps, PW_ACCT_STATUS_TYPE, 0)) != NULL) {
        /* set status */
        status = vp->vp_integer;
        /* debugging */
        RDEBUG("status == %d", status);
    } else {
        /* log error */
        radlog(L_ERR, "rlm_couchbase: Could not find status type in packet.");
        /* return */
        return RLM_MODULE_INVALID;
    }

    /* fetch value pairs from packet */
    vp = request->packet->vps;

    /* start json output document */
    json_out = json_object_new_object();

    /* loop through value pairs */
    while (vp) {
        /* get current attribute */
        attribute = vp->name;

        /* look for document key attribute */
        if (keyset == 0 && strcmp(attribute, p->dockey) == 0) {
            /* get and store our key */
             vp_prints_value(key, sizeof(key), vp, 0);

            /* toggle key set */
            keyset = 1;

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
        vp_prints_value(value, sizeof(value), vp, 0);

        /* debugging */
        RDEBUG("%s => %s", attribute, value);

        /* init added */
        added = 0;

        /* add this attribute/value pair to our json output */
        if (!vp->flags.has_tag) {
            switch (vp->type) {
                case PW_TYPE_INTEGER:
                case PW_TYPE_BYTE:
                case PW_TYPE_SHORT:
                    /* skip if we have flags */
                    if (vp->flags.has_value) break;
                    /* add it as int */
                    json_object_object_add(json_out, attribute, json_object_new_int(vp->vp_integer));
                    /* set added */
                    added = 1;
                break;
                case PW_TYPE_SIGNED:
                    /* add it as int */
                    json_object_object_add(json_out, attribute, json_object_new_int(vp->vp_signed));
                    /* set added */
                    added = 1;
                case PW_TYPE_INTEGER64:
                    /* add it as 64 bit int */
                    json_object_object_add(json_out, attribute, json_object_new_int64(vp->vp_integer64));
                    /* set added */
                    added = 1;
                break;
            }
        }
        /* keep going if not set above */
        if (added != 1) {
            switch (vp->type) {
                case PW_TYPE_STRING:
                    /* use string value */
                    json_object_object_add(json_out, attribute, json_object_new_string(vp->vp_strvalue));
                default:
                    /* use vp_prints_value return */
                    json_object_object_add(json_out, attribute, json_object_new_string(value));
                break;
            }
        }

        /* goto next pair */
        vp = vp->next;
    }

    /* check for found document */
    if (p->cookie != '\0') {
        /* debugging */
        RDEBUG("p->cookie == %s", p->cookie);
        /* parse json body from couchbase */
        json_in = json_tokener_parse_verbose(p->cookie, &json_error);
        /* check error */
        if (json_error == json_tokener_success) {
            /* debugging */
            RDEBUG("parsed body == %s", json_object_to_json_string(json_in));
            /* switch on status type */
            switch (status) {
                case PW_STATUS_START:
                    /* handle start */
                break;
                case PW_STATUS_STOP:
                    /* handle stop */
                break;
                case PW_STATUS_ALIVE:
                    /* handle update */
                break;
                case PW_STATUS_ACCOUNTING_ON:
                    /* handle on */
                break;
                case PW_STATUS_ACCOUNTING_OFF:
                    /* handle off */
                break;
            }
        } else {
            /* debugging */
            RDEBUG("Failed to parse couchbase document: %s", json_tokener_error_desc(json_error));
        }
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
