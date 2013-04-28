/* junk */
RCSID("$Id$")

#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/libradius.h>
#include <freeradius-devel/modules.h>
#include <freeradius-devel/rad_assert.h>

#include <libcouchbase/couchbase.h>

#include <json/json.h>

#include "callbacks.h"
#include "util.h"

/* configuration struct */
typedef struct rlm_couchbase_t {
    char *key;                      /* document key */
    char *host;                     /* couchbase connection host */
    char *bucket;                   /* couchbase bucket */
    char *user;                     /* couchbase bucket user name */
    char *pass;                     /* couchbase bucket password */
    unsigned int expire;            /* document expire time in seconds */
    char *map;                      /* user defined attribute map */
    json_object *map_object;        /* json object for parsed attribute map */
    char cb_cookie[MAX_VALUE_SIZE]; /* buffer to hold documents returned from couchbase */
    lcb_t cb_instance;              /* couchbase connection instance */
} rlm_couchbase_t;

/* map config to internal variables */
static const CONF_PARSER module_config[] = {
    {"key", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, key), NULL, "Acct-Session-Id"},
    {"host", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, host), NULL, "localhost"},
    {"bucket", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, bucket), NULL, "default"},
    {"user", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, user), NULL, NULL},
    {"pass", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, pass), NULL, NULL},
    {"expire", PW_TYPE_INTEGER, offsetof(rlm_couchbase_t, expire), NULL, 0},
    {"map", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, map), NULL, "{null}"},
    {NULL, -1, 0, NULL, NULL}     /* end the list */
};

/* initialize couchbase connection */
static int couchbase_instantiate(CONF_SECTION *conf, void *instance) {
    lcb_error_t cb_error;                   /* couchbase error holder */
    struct lcb_create_st create_options;    /* couchbase connection options */
    enum json_tokener_error json_error = json_tokener_success;  /* json parse error */

    /* build instance */
    rlm_couchbase_t *inst = instance;

    /* fail on bad config */
    if (cf_section_parse(conf, inst, module_config) < 0) {
        ERROR("rlm_couchbase: Failed to parse config!");
        return -1;
    }

    /* parse json body from config */
    inst->map_object = json_tokener_parse_verbose(inst->map, &json_error);

    /* check error */
    if (json_error != json_tokener_success) {
        /* log error */
        ERROR("rlm_couchbase: Failed to parse attribute map: %s", json_tokener_error_desc(json_error));
        /* cleanup json object */
        json_object_put(inst->map_object);
        /* fail */
        return -1;
    }

    /* allocate couchbase instance creation options */
    memset(&create_options, 0, sizeof(create_options));

    /* assign couchbase connection options */
    create_options.v.v0.host = inst->host;
    create_options.v.v0.bucket = inst->bucket;

    /* assign user and password if they were both passed */
    if (inst->user != NULL || inst->pass != NULL) {
        create_options.v.v0.user = inst->user;
        create_options.v.v0.passwd = inst->pass;
    }

    /* create couchbase connection instance */
    cb_error = lcb_create(&inst->cb_instance, &create_options);

    /* check error status */
    if (cb_error != LCB_SUCCESS) {
        ERROR("rlm_couchbase: Failed to create libcouchbase instance: %s", lcb_strerror(NULL, cb_error));
        return -1;
    }

    /* clear cookie */
    memset(inst->cb_cookie, 0, MAX_VALUE_SIZE);

    /* initiate connection */
    if ((cb_error = lcb_connect(inst->cb_instance)) != LCB_SUCCESS) {
        ERROR("rlm_couchbase: Failed to initiate connect: %s", lcb_strerror(NULL, cb_error));
        lcb_destroy(inst->cb_instance);
        return -1;
    }

    /* set general method callbacks */
    lcb_set_error_callback(inst->cb_instance, couchbase_error_callback);
    lcb_set_get_callback(inst->cb_instance, couchbase_get_callback);
    lcb_set_store_callback(inst->cb_instance, couchbase_store_callback);

    /* wait on connection */
    lcb_wait(inst->cb_instance);

    /* return okay */
    return 0;
}

/* write accounting data to couchbase */
static rlm_rcode_t couchbase_accounting(UNUSED void *instance, UNUSED REQUEST *request) {
    rlm_couchbase_t *p = instance;      /* our module instance */
    const DICT_ATTR *da;                /* radius dictionary attribute */
    VALUE_PAIR *vp;                     /* radius value pair linked list */
    char key[MAX_KEY_SIZE];             /* our document key */
    char document[MAX_VALUE_SIZE];      /* our document body */
    char element[MAX_KEY_SIZE];         /* mapped radius attribute to element name */
    int status = 0;                     /* account status type */
    int docfound = 0;                   /* document get toggle */
    lcb_error_t cb_error = LCB_SUCCESS; /* couchbase error holder */
    json_object *json, *jval;           /* json objects */
    enum json_tokener_error json_error = json_tokener_success;  /* json parse error */

    /* assert packet as not null*/
    rad_assert(request->packet != NULL);

    /* sanity check */
    if ((vp = pairfind(request->packet->vps, PW_ACCT_STATUS_TYPE, 0, TAG_ANY)) != NULL) {
        /* set status */
        status = vp->vp_integer;
    } else {
        /* log error */
        RERROR("rlm_couchbase: Could not find status type in packet.");
        /* return */
        return RLM_MODULE_INVALID;
    }

    /* return on status we don't handle */
    if (status == PW_STATUS_ACCOUNTING_ON || status == PW_STATUS_ACCOUNTING_OFF) {
        return RLM_MODULE_NOOP;
    }

    /* lookup document key attribute value */
    da = dict_attrbyname(p->key);

    /* look for document key attribute */
    if ((vp = pairfind(request->packet->vps, da->attr, 0, TAG_ANY)) != NULL) {
        /* store key */
        vp_prints_value(key, sizeof(key), vp, 0);

        /* debugging */
        RDEBUG("found document key: '%s' => '%s'", vp->da->name, key);

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
            cb_error = lcb_get(p->cb_instance, &p->cb_cookie, 1, commands);

            /* wait on get */
            lcb_wait(p->cb_instance);

            /* check return */
            if (cb_error == LCB_SUCCESS) {
                /* check for valid pointer */
                if (p->cb_cookie[0] != '\0') {
                    /* parse json body from couchbase */
                    json = json_tokener_parse_verbose(p->cb_cookie, &json_error);
                    /* check error */
                    if (json_error == json_tokener_success) {
                        /* debugging */
                        RDEBUG("parsed json body from couchbase: %s", json_object_to_json_string(json));
                        /* set doc found */
                        docfound = 1;
                    } else {
                        /* log error */
                        RERROR("rlm_couchbase: Failed to parse couchbase document: %s", json_tokener_error_desc(json_error));
                        /* cleanup json object */
                        json_object_put(json);
                    }
                }
            }
        }
    } else {
        /* log error */
        RERROR("rlm_couchbase: Could not find key attribute (%s) in packet!", p->key);
        /* return */
        return RLM_MODULE_INVALID;
    }

    /* start json document if needed */
    if (docfound != 1) {
        json = json_object_new_object();
        /* initialize start and stop times ... ensure we always have these elements */
        json_object_object_add(json, "startTimestamp", json_object_new_string("null"));
        json_object_object_add(json, "stopTimestamp", json_object_new_string("null"));
    }

    /* status specific replacements */
    switch (status) {
        case PW_STATUS_START:
            /* add start time */
            if ((vp = pairfind(request->packet->vps, PW_EVENT_TIMESTAMP, 0, TAG_ANY)) != NULL) {
                /* add to json object */
                json_object_object_add(json, "startTimestamp", couchbase_value_pair_to_json_object(vp));
            }
        break;
        case PW_STATUS_STOP:
            /* add stop time */
            if ((vp = pairfind(request->packet->vps, PW_EVENT_TIMESTAMP, 0, TAG_ANY)) != NULL) {
                /* add to json object */
                json_object_object_add(json, "stopTimestamp", couchbase_value_pair_to_json_object(vp));
            }
            /* check start timestamp and adjust if needed */
            couchbase_ensure_start_timestamp(json, request->packet->vps);
        case PW_STATUS_ALIVE:
            /* check start timestamp and adjust if needed */
            couchbase_ensure_start_timestamp(json, request->packet->vps);
        break;
        default:
            /* do nothing */
        break;
    }

    /* loop through pairs */
    for (vp = request->packet->vps; vp; vp = vp->next) {
        /* map attribute to element */
        if (couchbase_attribute_to_element(vp->da->name, p->map_object, &element) == 0) {
            /* debug */
            RDEBUG("mapped attribute %s => %s", vp->da->name, element);
            /* add to json object with mapped name */
            json_object_object_add(json, element, couchbase_value_pair_to_json_object(vp));
        }
    }

    /* make sure we have enough room in our document buffer */
    if ((unsigned int) json_object_get_string_len(json) > sizeof(document) - 1) {
        /* this isn't good */
        RERROR("rlm_couchbase: Could not write json document.  Insufficient buffer space!");
        /* free json output */
        json_object_put(json);
        /* return */
        return RLM_MODULE_FAIL;
    } else {
        /* copy json string to document */
        strncpy(document, json_object_to_json_string(json), sizeof(document));
        /* free json output */
        json_object_put(json);
    }

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
        /* set expire if config value greater than 0 */
        if (p->expire > 0) {
            cmd.v.v0.exptime = (lcb_time_t) p->expire;
        }

        /* debugging */
        RDEBUG("setting '%s' => '%s'", key, document);

        /* store document */
        cb_error = lcb_store(p->cb_instance, NULL, 1, commands);

        /* wait on set */
        lcb_wait(p->cb_instance);

        /* check return */
        if (cb_error != LCB_SUCCESS) {
            RERROR("rlm_couchbase: Failed to store document (%s): %s", key, lcb_strerror(NULL, cb_error));
        }
    }

    /* return */
    return RLM_MODULE_OK;
}

/* free any memory we allocated */
static int couchbase_detach(UNUSED void *instance)
{
    rlm_couchbase_t *p = instance;  /* instance struct */

    /* free map object */
    json_object_put(p->map_object);

    /* destroy/free couchbase instance */
    lcb_destroy(p->cb_instance);

    /* return okay */
    return 0;
}

/* hook the module into freeradius */
module_t rlm_couchbase = {
    RLM_MODULE_INIT,
    "couchbase",
    RLM_TYPE_THREAD_SAFE,       /* type */
    sizeof(rlm_couchbase_t),
    module_config,
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
