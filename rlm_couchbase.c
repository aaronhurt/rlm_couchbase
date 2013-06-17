/* junk */
RCSID("$Id$")

#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/libradius.h>
#include <freeradius-devel/modules.h>
#include <freeradius-devel/rad_assert.h>

#include <libcouchbase/couchbase.h>

#include <json/json.h>

#include "couchbase.h"
#include "util.h"

/* configuration struct */
typedef struct rlm_couchbase_t {
    const char *key;                /* document key */
    const char *doctype;            /* value of 'docType' element name */
    const char *host;               /* couchbase connection host */
    const char *bucket;             /* couchbase bucket */
    const char *pass;               /* couchbase bucket password */
    unsigned int expire;            /* document expire time in seconds */
    const char *map;                /* user defined attribute map */
    json_object *map_object;        /* json object for parsed attribute map */
    lcb_t cb_instance;              /* couchbase connection instance */
} rlm_couchbase_t;

/* map config to internal variables */
static const CONF_PARSER module_config[] = {
    {"key", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, key), NULL, "radacct_%{Acct-Session-Id}"},
    {"doctype", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, doctype), NULL, "radacct"},
    {"host", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, host), NULL, "localhost"},
    {"bucket", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, bucket), NULL, "default"},
    {"pass", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, pass), NULL, NULL},
    {"expire", PW_TYPE_INTEGER, offsetof(rlm_couchbase_t, expire), NULL, 0},
    {"map", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, map), NULL, "{null}"},
    {NULL, -1, 0, NULL, NULL}     /* end the list */
};

/* initialize couchbase connection */
static int rlm_couchbase_instantiate(CONF_SECTION *conf, void *instance) {
    lcb_error_t cb_error = LCB_SUCCESS;                         /* couchbase error status */
    enum json_tokener_error json_error = json_tokener_success;  /* json parse error */

    /* build instance */
    rlm_couchbase_t *inst = instance;

    /* fail on bad config */
    if (cf_section_parse(conf, inst, module_config) < 0) {
        ERROR("rlm_couchbase: failed to parse config!");
        return -1;
    }

    /* parse json body from config */
    inst->map_object = json_tokener_parse_verbose(inst->map, &json_error);

    /* check error */
    if (json_error != json_tokener_success) {
        /* log error */
        ERROR("rlm_couchbase: failed to parse attribute map: %s", json_tokener_error_desc(json_error));
        /* cleanup json object */
        json_object_put(inst->map_object);
        /* fail */
        return -1;
    }

    /* create instance */
    inst->cb_instance = couchbase_init_connection(inst->host, inst->bucket, inst->pass);

    /* check couchbase instance status */
    if ((cb_error = lcb_get_last_error(inst->cb_instance)) != LCB_SUCCESS) {
        ERROR("rlm_couchbase: failed to initiate couchbase connection: %s", lcb_strerror(NULL, cb_error));
        /* fail */
        return -1;
    }

    /* return okay */
    return 0;
}

/* authentiacte given username and password against couchbase */
static rlm_rcode_t rlm_couchbase_authenticate(UNUSED void *instance, UNUSED REQUEST *request) {
    /* return okay */
    return RLM_MODULE_OK;
}

/* authorize users via couchbase */
static rlm_rcode_t rlm_couchbase_authorize(UNUSED void *instance, UNUSED REQUEST *request) {
    /* return handled */
    return RLM_MODULE_HANDLED;
}

/* misc data manipulation before recording accounting data */
static rlm_rcode_t rlm_couchbase_preacct(UNUSED void *instance, UNUSED REQUEST *request) {
    /* nothing here yet ... return noop */
    return RLM_MODULE_NOOP;
}

/* write accounting data to couchbase */
static rlm_rcode_t rlm_couchbase_accounting(UNUSED void *instance, UNUSED REQUEST *request) {
    rlm_couchbase_t *p = instance;      /* our module instance */
    VALUE_PAIR *vp;                     /* radius value pair linked list */
    char key[MAX_KEY_SIZE];             /* our document key */
    char document[MAX_VALUE_SIZE];      /* our document body */
    char element[MAX_KEY_SIZE];         /* mapped radius attribute to element name */
    int status = 0;                     /* account status type */
    int docfound = 0;                   /* document get toggle */
    lcb_error_t cb_error = LCB_SUCCESS; /* couchbase error holder */

    /* assert packet as not null */
    rad_assert(request->packet != NULL);

    /* sanity check */
    if ((vp = pairfind(request->packet->vps, PW_ACCT_STATUS_TYPE, 0, TAG_ANY)) != NULL) {
        /* set status */
        status = vp->vp_integer;
    } else {
        /* log error */
        RERROR("rlm_couchbase: could not find status type in packet.");
        /* return */
        return RLM_MODULE_INVALID;
    }

    /* return on status we don't handle */
    if (status == PW_STATUS_ACCOUNTING_ON || status == PW_STATUS_ACCOUNTING_OFF) {
        return RLM_MODULE_NOOP;
    }

    /* initialize cookie */
    cookie_t *cookie = calloc(1, sizeof(cookie_t));

    /* check allocation */
    if (cookie == NULL) {
        /* log error and return error */
        ERROR("rlm_couchbase: failed to allocate cookie");
        return RLM_MODULE_FAIL;
    }

    /* attempt to build document key */
    if (radius_xlat(key, sizeof(key), request, p->key, NULL, NULL) < 0) {
        /* log error */
        RERROR("rlm_couchbase: could not find key attribute (%s) in packet!", p->key);
        /* free cookie */
        free(cookie);
        /* return */
        return RLM_MODULE_INVALID;
    } else {
        /* debugging */
        RDEBUG("built document key: '%s' => '%s'", p->key, key);

        /* init cookie error status */
        cookie->jerr = json_tokener_success;

        /* fetch document */
        cb_error = couchbase_get_key(p->cb_instance, cookie, key);

        /* check error */
        if (cb_error != LCB_SUCCESS || cookie->jerr != json_tokener_success) {
            /* log error */
            RERROR("rlm_couchbase: failed to execute get request or parse returned json object");
            /* free json object */
            json_object_put(cookie->jobj);
            /* free cookie */
            free(cookie);
        } else {
            /* check cookie json object */
            if (cookie->jobj != NULL) {
                /* set doc found */
                docfound = 1;
                /* debugging */
                RDEBUG("parsed json body from couchbase: %s", json_object_to_json_string(cookie->jobj));
            }
        }
    }

    /* start json document if needed */
    if (docfound != 1) {
        /* debugging */
        RDEBUG("document not found - creating new json document");
        /* create new json object */
        cookie->jobj = json_object_new_object();
        /* set 'docType' element for new document */
        json_object_object_add(cookie->jobj, "docType", json_object_new_string(p->doctype));
        /* set start and stop times ... ensure we always have these elements */
        json_object_object_add(cookie->jobj, "startTimestamp", json_object_new_string("null"));
        json_object_object_add(cookie->jobj, "stopTimestamp", json_object_new_string("null"));
    }

    /* debug */
    RDEBUG("beginning status switch");

    /* status specific replacements for start/stop time */
    switch (status) {
        case PW_STATUS_START:
            /* add start time */
            if ((vp = pairfind(request->packet->vps, PW_EVENT_TIMESTAMP, 0, TAG_ANY)) != NULL) {
                /* add to json object */
                json_object_object_add(cookie->jobj, "startTimestamp", couchbase_value_pair_to_json_object(vp));
            }
        break;
        case PW_STATUS_STOP:
            /* add stop time */
            if ((vp = pairfind(request->packet->vps, PW_EVENT_TIMESTAMP, 0, TAG_ANY)) != NULL) {
                /* add to json object */
                json_object_object_add(cookie->jobj, "stopTimestamp", couchbase_value_pair_to_json_object(vp));
            }
            /* check start timestamp and adjust if needed */
            couchbase_ensure_start_timestamp(cookie->jobj, request->packet->vps);
        case PW_STATUS_ALIVE:
            /* check start timestamp and adjust if needed */
            couchbase_ensure_start_timestamp(cookie->jobj, request->packet->vps);
        break;
        default:
            /* do nothing */
        break;
    }

    /* debug */
    RDEBUG("finished status switch");

    /* loop through pairs and add to json document */
    for (vp = request->packet->vps; vp; vp = vp->next) {
        /* map attribute to element */
        if (couchbase_attribute_to_element(vp->da->name, p->map_object, &element) == 0) {
            /* debug */
            RDEBUG("mapped attribute %s => %s", vp->da->name, element);
            /* add to json object with mapped name */
            json_object_object_add(cookie->jobj, element, couchbase_value_pair_to_json_object(vp));
        }
    }

    /* make sure we have enough room in our document buffer */
    if ((unsigned int) json_object_get_string_len(cookie->jobj) > sizeof(document) - 1) {
        /* this isn't good */
        RERROR("rlm_couchbase: could not write json document - insufficient buffer space");
        /* free json output */
        json_object_put(cookie->jobj);
        /* free cookie */
        free(cookie);
        /* return */
        return RLM_MODULE_FAIL;
    } else {
        /* copy json string to document */
        strncpy(document, json_object_to_json_string(cookie->jobj), sizeof(document));
        /* free json output */
        json_object_put(cookie->jobj);
        /* free cookie */
        free(cookie);
    }

    /* debugging */
    RDEBUG("setting '%s' => '%s'", key, document);

    /* store document/key in couchbase */
    cb_error = couchbase_set_key(p->cb_instance, key, document, p->expire);

    /* check return */
    if (cb_error != LCB_SUCCESS) {
        RERROR("rlm_couchbase: failed to store document (%s): %s", key, lcb_strerror(NULL, cb_error));
    }

    /* return */
    return RLM_MODULE_OK;
}

/* check for multiple simultaneous active sessions */
static rlm_rcode_t rlm_couchbase_checksimul(UNUSED void *instance, UNUSED REQUEST *request)
{
    /* nothing yet ... always set to 0 */
    request->simul_count = 0;

    /* return okay */
    return RLM_MODULE_OK;
}

/* free any memory we allocated */
static int rlm_couchbase_detach(UNUSED void *instance)
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
    RLM_TYPE_THREAD_SAFE,           /* type */
    sizeof(rlm_couchbase_t),
    module_config,
    rlm_couchbase_instantiate,      /* instantiation */
    rlm_couchbase_detach,           /* detach */
    {
        rlm_couchbase_authenticate, /* authentication */
        rlm_couchbase_authorize,    /* authorization */
        rlm_couchbase_preacct,      /* preaccounting */
        rlm_couchbase_accounting,   /* accounting */
        rlm_couchbase_checksimul,   /* checksimul */
        NULL,                       /* pre-proxy */
        NULL,                       /* post-proxy */
        NULL                        /* post-auth */
    },
};
