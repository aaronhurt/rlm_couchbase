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

/* configuration struct */
typedef struct rlm_couchbase_t {
    char *dockey;
    char *host;
    char *bucket;
    char *user;
    char *pass;
    char *cookie;
    lcb_t couchbase;
} rlm_couchbase_t;

/* map config to internal variables */
static const CONF_PARSER module_config[] = {
    {"dockey", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, dockey), NULL, "Acct-Session-Id"},
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

    /* allocate cookie */
    data->cookie = rad_malloc(MAX_VALUE_SIZE);
    if (!data->cookie) {
        return -1;
    }

    /* clear cookie */
    memset(data->cookie, 0, MAX_VALUE_SIZE);

    /* initiate connection */
    if ((cb_error = lcb_connect(data->couchbase)) != LCB_SUCCESS) {
        radlog(L_ERR, "rlm_couchbase: Failed to initiate connect: %s", lcb_strerror(NULL, cb_error));
        lcb_destroy(data->couchbase);
        free(data);
        return -1;
    }

    /* run synchronous */
    //lcb_behavior_set_syncmode(data->couchbase, LCB_SYNCHRONOUS);

    /* set general method callbacks */
    lcb_set_error_callback(data->couchbase, couchbase_error_callback);
    lcb_set_get_callback(data->couchbase, couchbase_get_callback);
    lcb_set_store_callback(data->couchbase, couchbase_store_callback);

    /* wait on connection */
    lcb_wait(data->couchbase);

    /* assign instance */
    *instance = data;

    /* return okay */
    return 0;
}

/* add value/pair to json object */
static json_object *value_pair_to_json_object(VALUE_PAIR *vp) {
    char value[255];    // radius attribute value

    /* add this attribute/value pair to our json output */
    if (!vp->flags.has_tag) {
        switch (vp->type) {
            case PW_TYPE_INTEGER:
            case PW_TYPE_BYTE:
            case PW_TYPE_SHORT:
                /* skip if we have flags */
                if (vp->flags.has_value) break;
                /* return as int */
                return json_object_new_int(vp->vp_integer);
            break;
            case PW_TYPE_SIGNED:
                /* return as int */
                return json_object_new_int(vp->vp_signed);
            break;
            case PW_TYPE_INTEGER64:
                /* return as 64 bit int */
                return json_object_new_int64(vp->vp_integer64);
            break;
        }
    }

    /* keep going if not set above */
    switch (vp->type) {
        case PW_TYPE_STRING:
            /* return string value */
            return json_object_new_string(vp->vp_strvalue);
        default:
            /* get standard value */
            vp_prints_value(value, sizeof(value), vp, 0);
            /* return string value from above */
            return json_object_new_string(value);
        break;
    }
}

/* write accounting data to couchbase */
static int couchbase_accounting(void *instance, REQUEST *request) {
    rlm_couchbase_t *p = instance;      // couchbase instance
    char key[MAX_KEY_SIZE];             // couchbase document key
    char document[MAX_VALUE_SIZE];      // couchbase document body
    int status = 0;                     // account status type
    int docfound = 0;                   // document get toggle
    lcb_error_t cb_error = LCB_SUCCESS; // couchbase error holder
    json_object *json;                  // json object
    enum json_tokener_error json_error = json_tokener_success;  // json parse error
    VALUE_PAIR *vp;                     // radius value pair linked list
    DICT_ATTR *da;                      // radius dictionary attribute

    /* assert packet as not null*/
    rad_assert(request->packet != NULL);

    /* sanity check */
    if ((vp = pairfind(request->packet->vps, PW_ACCT_STATUS_TYPE, 0)) != NULL) {
        /* set status */
        status = vp->vp_integer;
    } else {
        /* log error */
        radlog(L_ERR, "rlm_couchbase: Could not find status type in packet.");
        /* return */
        return RLM_MODULE_INVALID;
    }

    /* return on status we don't handle */
    if (status == PW_STATUS_ACCOUNTING_ON || status == PW_STATUS_ACCOUNTING_OFF) {
        return RLM_MODULE_NOOP;
    }

    /* lookup document key attribute value */
    da = dict_attrbyname(p->dockey);

    /* look for document key attribute */
    if ((vp = pairfind(request->packet->vps, da->attr, 0)) != NULL) {
        /* store key */
        vp_prints_value(key, sizeof(key), vp, 0);

        /* debugging */
        RDEBUG("found document key: '%s' => '%s'", vp->name, key);

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
            cb_error = lcb_get(p->couchbase, p->cookie, 1, commands);

            /* wait on get */
            lcb_wait(p->couchbase);

            /* check return */
            if (cb_error != LCB_SUCCESS) {
                 /* debugging ... not a real error as document may not exist */
                RDEBUG("failed to get document (%s): %s", key, lcb_strerror(NULL, cb_error));
            } else {
                /* check for valid pointer */
                if (p->cookie[0] != '\0') {
                    /* parse json body from couchbase */
                    json = json_tokener_parse_verbose(p->cookie, &json_error);
                    /* check error */
                    if (json_error == json_tokener_success) {
                        /* set doc found */
                        docfound = 1;
                    } else {
                        /* log error */
                        radlog(L_ERR, "rlm_couchbase: Failed to parse couchbase document: %s", json_tokener_error_desc(json_error));
                        /* cleanup json object */
                        json_object_put(json);
                    }
                }
            }
        }
    } else {
        /* log error */
        radlog(L_ERR, "rlm_couchbase: Could not find key attribute (%s) in packet!", p->dockey);
        /* return */
        return RLM_MODULE_INVALID;
    }

    /* start json document if needed */
    if (docfound != 1) {
        json = json_object_new_object();
    }

    /* status specific replacements */
    switch (status) {
        case PW_STATUS_START:
            /* add start time */
            if ((vp = pairfind(request->packet->vps, PW_EVENT_TIMESTAMP, 0)) != NULL) {
                json_object_object_add(json, "startTimestamp", value_pair_to_json_object(vp));
            }
        break;
        case PW_STATUS_STOP:
            /* add stop time */
            if ((vp = pairfind(request->packet->vps, PW_EVENT_TIMESTAMP, 0)) != NULL) {
                json_object_object_add(json, "stopTimestamp", value_pair_to_json_object(vp));
            }
        break;
    }

    /* session id */
    if ((vp = pairfind(request->packet->vps, PW_ACCT_SESSION_ID, 0)) != NULL) {
        json_object_object_add(json, "sessionId", value_pair_to_json_object(vp));
    }

    /* authentication source */
    if ((vp = pairfind(request->packet->vps, PW_ACCT_AUTHENTIC, 0)) != NULL) {
        json_object_object_add(json, "authentic", value_pair_to_json_object(vp));
    }

    /* user name */
    if ((vp = pairfind(request->packet->vps, PW_USER_NAME, 0)) != NULL) {
        json_object_object_add(json, "userName", value_pair_to_json_object(vp));
    }

    /* nas ip address */
    if ((vp = pairfind(request->packet->vps, PW_NAS_IP_ADDRESS, 0)) != NULL) {
        json_object_object_add(json, "nasIpAddress", value_pair_to_json_object(vp));
    }

    /* nas identifier */
    if ((vp = pairfind(request->packet->vps, PW_NAS_IDENTIFIER, 0)) != NULL) {
        json_object_object_add(json, "nasIdentifier", value_pair_to_json_object(vp));
    }

    /* nas port */
    if ((vp = pairfind(request->packet->vps, PW_NAS_PORT, 0)) != NULL) {
        json_object_object_add(json, "nasPort", value_pair_to_json_object(vp));
    }

    /* called station id */
    if ((vp = pairfind(request->packet->vps, PW_CALLED_STATION_ID, 0)) != NULL) {
        json_object_object_add(json, "calledStationId", value_pair_to_json_object(vp));
    }

    /* calling station id */
    if ((vp = pairfind(request->packet->vps, PW_CALLING_STATION_ID, 0)) != NULL) {
        json_object_object_add(json, "callingStationId", value_pair_to_json_object(vp));
    }

    /* framed ip address */
    if ((vp = pairfind(request->packet->vps, PW_FRAMED_IP_ADDRESS, 0)) != NULL) {
        json_object_object_add(json, "framedIpAddress", value_pair_to_json_object(vp));
    }

    /* nas port type */
    if ((vp = pairfind(request->packet->vps, PW_NAS_PORT_TYPE, 0)) != NULL) {
        json_object_object_add(json, "nasPortType", value_pair_to_json_object(vp));
    }

    /* connect info */
    if ((vp = pairfind(request->packet->vps, PW_CONNECT_INFO, 0)) != NULL) {
        json_object_object_add(json, "connectInfo", value_pair_to_json_object(vp));
    }

    /* session time */
    if ((vp = pairfind(request->packet->vps, PW_ACCT_SESSION_TIME, 0)) != NULL) {
        json_object_object_add(json, "sessionTime", value_pair_to_json_object(vp));
    }

    /* input packets */
    if ((vp = pairfind(request->packet->vps, PW_ACCT_INPUT_PACKETS, 0)) != NULL) {
        json_object_object_add(json, "inputPackets", value_pair_to_json_object(vp));
    }

    /* input octets */
    if ((vp = pairfind(request->packet->vps, PW_ACCT_INPUT_OCTETS, 0)) != NULL) {
        json_object_object_add(json, "inputOctets", value_pair_to_json_object(vp));
    }

    /* output packets */
    if ((vp = pairfind(request->packet->vps, PW_ACCT_OUTPUT_PACKETS, 0)) != NULL) {
        json_object_object_add(json, "outputPackets", value_pair_to_json_object(vp));
    }

    /* output octets */
    if ((vp = pairfind(request->packet->vps, PW_ACCT_OUTPUT_OCTETS, 0)) != NULL) {
        json_object_object_add(json, "outputOctets", value_pair_to_json_object(vp));
    }

    /* make sure we have enough room in our document buffer */
    if ((unsigned int) json_object_get_string_len(json) > sizeof(document) - 1) {
        /* this isn't good */
        radlog(L_ERR, "rlm_couchbase: Could not write json document.  Insufficient buffer space!");
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

        /* debugging */
        RDEBUG("setting '%s' => '%s'", key, document);

        /* store document */
        cb_error = lcb_store(p->couchbase, NULL, 1, commands);

        /* wait on set */
        lcb_wait(p->couchbase);

        /* check return */
        if (cb_error != LCB_SUCCESS) {
            radlog(L_ERR, "rlm_couchbase: Failed to store document (%s): %s", key, lcb_strerror(NULL, cb_error));
        }
    }

    /* return */
    return RLM_MODULE_OK;
}

/* free any memory we allocated */
static int couchbase_detach(void *instance)
{
    rlm_couchbase_t *p = instance;  // instance struct

    /* destroy/free couchbase instance */
    lcb_destroy(p->couchbase);

    /* free couchbase cookie */
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
