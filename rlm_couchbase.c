/* junk */
RCSID("$Id$")

#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/libradius.h>
#include <freeradius-devel/modules.h>
#include <freeradius-devel/rad_assert.h>

#include <libcouchbase/couchbase.h>

#include <json/json.h>

#include "util.h"
#include "couchbase.h"

/* map config to internal variables */
static const CONF_PARSER module_config[] = {
    {"key", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, key), NULL, "radacct_%{Acct-Session-Id}"},
    {"doctype", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, doctype), NULL, "radacct"},
    {"host", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, host), NULL, "localhost"},
    {"bucket", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, bucket), NULL, "default"},
    {"pass", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, pass), NULL, NULL},
    {"expire", PW_TYPE_INTEGER, offsetof(rlm_couchbase_t, expire), NULL, 0},
    {"authview", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, authview), NULL, "_design/client/_view/by_name"},
    {"map", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, map), NULL, "{null}"},
    {NULL, -1, 0, NULL, NULL}     /* end the list */
};

/* initialize couchbase connection */
static int rlm_couchbase_instantiate(CONF_SECTION *conf, void *instance) {
    enum json_tokener_error json_error = json_tokener_success;  /* json parse error */

    /* build instance */
    rlm_couchbase_t *inst = instance;

    /* fail on bad config */
    if (cf_section_parse(conf, inst, module_config) < 0) {
        ERROR("rlm_couchbase: failed to parse config!");
        /* fail */
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

    /* initiate connection pool */
    inst->pool = fr_connection_pool_init(conf, inst, mod_conn_create, NULL, mod_conn_delete, NULL);

    /* check connection pool */
    if (!inst->pool) {
        /* fail */
        return -1;
    }

    /* return okay */
    return 0;
}

/* authorize users via couchbase */
static rlm_rcode_t rlm_couchbase_authorize(void *instance, REQUEST *request) {
    rlm_couchbase_t *inst = instance;       /* our module instance */
    void *handle = NULL;                    /* connection pool handle */
    VALUE_PAIR *vp;                         /* value pair pointer */
    char vpath[256], docid[256];            /* view path and document id */
    const char *uname = NULL;               /* username pointer */
    size_t length;                          /* string length buffer */
    lcb_error_t cb_error = LCB_SUCCESS;     /* couchbase error holder */
    json_object *json, *jval, *jval2;       /* json object holders */
    json_object_iter iter;                  /* json object iterator */

    /* assert packet as not null */
    rad_assert(request->packet != NULL);

    /* prefer stripped user name */
    if ((vp = pairfind(request->packet->vps, PW_STRIPPED_USER_NAME, 0, TAG_ANY)) != NULL) {
        uname = vp->vp_strvalue;
    /* fallback to user-name */
    } else if ((vp = pairfind(request->packet->vps, PW_USER_NAME, 0, TAG_ANY)) != NULL) {
        uname = vp->vp_strvalue;
    /* fail */
    } else {
        /* log error */
        ERROR("rlm_couchbase: failed to find valid username for authorization");
        /* return */
        return RLM_MODULE_INVALID;
    }

    /* get handle */
    handle = fr_connection_get(inst->pool);

    /* check handle */
    if (!handle) return RLM_MODULE_FAIL;

    /* set handle pointer */
    rlm_couchbase_handle_t *handle_t = handle;

    /* set couchbase instance */
    lcb_t cb_inst = handle_t->handle;

    /* set cookie */
    cookie_t *cookie = handle_t->cookie;

    /* check cookie */
    if (cookie) {
        /* clear cookie */
        memset(cookie, 0, sizeof(cookie_t));
    } else {
        /* free connection */
        fr_connection_release(inst->pool, handle);
        /* log error */
        RERROR("rlm_couchbase: could not zero cookie");
        /* return */
        return RLM_MODULE_FAIL;
    }

    /* build view path */
    snprintf(vpath, sizeof(vpath), "%s?stale=false&limit=1&connection_timeout=500&key=\"%s\"",
        inst->authview, uname);

    /* init cookie error status */
    cookie->jerr = json_tokener_success;

    /* setup cookie tokener */
    cookie->jtok = json_tokener_new();

    /* query view for document */
    cb_error = couchbase_query_view(cb_inst, cookie, vpath, NULL);

    /* free json token */
    json_tokener_free(cookie->jtok);

    /* check error */
    if (cb_error != LCB_SUCCESS || cookie->jerr != json_tokener_success) {
        /* log error */
        ERROR("rlm_couchbase: failed to execute view request or parse return");
        /* free json object */
        json_object_put(cookie->jobj);
        /* release handle */
        fr_connection_release(inst->pool, handle);
        /* return */
        return RLM_MODULE_FAIL;
    }

    /* debugging */
    RDEBUG("cookie->jobj == %s", json_object_to_json_string(cookie->jobj));

    /* check for error in json object */
    if (cookie->jobj != NULL && json_object_object_get_ex(cookie->jobj, "error", &json)) {
        char error[512];
        /* get length */
        length = json_object_get_string_len(json);
        /* check length and copy to error buffer */
        if (length < sizeof(error) -1) {
            strncpy(error, json_object_get_string(json), length);
        }
        /* get error reason */
        if (json_object_object_get_ex(cookie->jobj, "reason", &json)) {
            /* get length */
            length = json_object_get_string_len(json);
            /* check length and add to error buffer */
            if (length + strlen(error) < sizeof(error) -4) {
                /* add spacing */
                strncat(error, " - ", 3);
                /* append reason */
                strncat(error, json_object_get_string(json), length);
            }
        }
        /* log error */
        ERROR("view request failed: %s", error);
        /* free json object */
        json_object_put(cookie->jobj);
        /* release handle */
        fr_connection_release(inst->pool, handle);
        /* return */
        return RLM_MODULE_FAIL;
    }

    /* clear docid */
    memset(docid, 0, sizeof(docid));

    /* check for document id in return */
    if (cookie->jobj != NULL && json_object_object_get_ex(cookie->jobj, "rows", &json)) {
        /* check for valid row value */
        if (json_object_is_type(json, json_type_array) && json_object_array_length(json) > 0) {
            /* attempt to get id of first index of array */
            json = json_object_array_get_idx(json, 0);
            /* get document id */
            if (json_object_object_get_ex(json, "id", &jval)) {
                /* get length */
                length = json_object_get_string_len(jval);
                /* check length and copy string */
                if (length < sizeof(docid) -1) {
                    strncpy(docid, json_object_get_string(jval), length);
                }
            }
        }
    }

    /* free json object */
    json_object_put(cookie->jobj);

    /* check for valid doc id */
    if (docid[0] != 0) {
        /* reset  cookie error status */
        cookie->jerr = json_tokener_success;

        /* fetch document */
        cb_error = couchbase_get_key(cb_inst, cookie, docid);

        /* check error */
        if (cb_error != LCB_SUCCESS || cookie->jerr != json_tokener_success) {
            /* log error */
            ERROR("failed to execute get request or parse return");
            /* free json object */
            json_object_put(cookie->jobj);
            /* release handle */
            fr_connection_release(inst->pool, handle);
            /* return */
            return RLM_MODULE_FAIL;
        }

        /* debugging */
        RDEBUG("cookie->jobj == %s", json_object_to_json_string(cookie->jobj));

        /* get config payload */
        if (json_object_object_get_ex(cookie->jobj, "config", &json)) {
            /* loop through object */
            json_object_object_foreachC(json, iter) {
                /* debugging */
                RDEBUG("%s => %s", iter.key, json_object_to_json_string(iter.val));
                /* create pair from json object */
                if (json_object_object_get_ex(iter.val, "value", &jval) &&
                json_object_object_get_ex(iter.val, "op", &jval2)) {
                    pairmake_config(iter.key, json_object_get_string(jval),
                        fr_str2int(fr_tokens, json_object_get_string(jval2), 0));
                }
            }
        }

        /* get reply payload */
        if (json_object_object_get_ex(cookie->jobj, "reply", &json)) {
            /* loop through object */
            json_object_object_foreachC(json, iter) {
                /* debugging */
                RDEBUG("%s => %s", iter.key, json_object_to_json_string(iter.val));
                /* create pair from json object */
                if (json_object_object_get_ex(iter.val, "value", &jval) &&
                json_object_object_get_ex(iter.val, "op", &jval2)) {
                    pairmake_reply(iter.key, json_object_get_string(jval),
                        fr_str2int(fr_tokens, json_object_get_string(jval2), 0));
                }
            }
        }

        /* release handle */
        fr_connection_release(inst->pool, handle);

        /* return okay */
        return RLM_MODULE_OK;
    }

    /* release handle */
    fr_connection_release(inst->pool, handle);

    /* default noop */
    return RLM_MODULE_NOOP;
}

/* misc data manipulation before recording accounting data */
static rlm_rcode_t rlm_couchbase_preacct(UNUSED void *instance, REQUEST *request) {
    VALUE_PAIR *vp;     /* radius value pair linked list */

    /* assert packet as not null */
    rad_assert(request->packet != NULL);

    /* check if stripped-user-name already set */
    if (pairfind(request->packet->vps, PW_STRIPPED_USER_NAME, 0, TAG_ANY) != NULL) {
        /* already set - do nothing */
        return RLM_MODULE_NOOP;
    }

    /* get user string */
    if ((vp = pairfind(request->packet->vps, PW_USER_NAME, 0, TAG_ANY)) != NULL) {
        char *realm = NULL, *uname = NULL, *buff = NULL;   /* username and realm containers */
        size_t size;                                       /* size of user name string */

        /* allocate buffer and get size */
        buff = rad_calloc((size = (strlen(vp->vp_strvalue) + 1)));

        /* pass to our split function */
        uname = couchbase_split_user_realm(vp->vp_strvalue, buff, size, &realm);

        /* check uname and set if needed */
        if (uname != NULL) {
            pairmake_packet("Stripped-User-Name", uname, T_OP_SET);
        }

        /* check realm and set if needed */
        if (realm != NULL) {
            pairmake_packet("Realm", realm, T_OP_SET);
        }

        /* free uname */
        free(buff);

        /* return okay */
        return RLM_MODULE_OK;
    }

    /* return noop */
    return RLM_MODULE_NOOP;
}

/* write accounting data to couchbase */
static rlm_rcode_t rlm_couchbase_accounting(void *instance, REQUEST *request) {
    rlm_couchbase_t *inst = instance;   /* our module instance */
    void *handle = NULL;                /* connection pool handle */
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

    /* get handle */
    handle = fr_connection_get(inst->pool);

    /* check handle */
    if (!handle) return RLM_MODULE_FAIL;

    /* set handle pointer */
    rlm_couchbase_handle_t *handle_t = handle;

    /* set couchbase instance */
    lcb_t cb_inst = handle_t->handle;

    /* set cookie */
    cookie_t *cookie = handle_t->cookie;

    /* check cookie */
    if (cookie) {
        /* clear cookie */
        memset(cookie, 0, sizeof(cookie_t));
    } else {
        /* free connection */
        fr_connection_release(inst->pool, handle);
        /* log error */
        RERROR("rlm_couchbase: could not zero cookie");
        /* return */
        return RLM_MODULE_FAIL;
    }

    /* attempt to build document key */
    if (radius_xlat(key, sizeof(key), request, inst->key, NULL, NULL) < 0) {
        /* log error */
        RERROR("rlm_couchbase: could not find key attribute (%s) in packet!", inst->key);
        /* release handle */
        fr_connection_release(inst->pool, handle);
        /* return */
        return RLM_MODULE_INVALID;
    } else {
        /* init cookie error status */
        cookie->jerr = json_tokener_success;

        /* attempt to fetch document */
        cb_error = couchbase_get_key(cb_inst, cookie, key);

        /* check error */
        if (cb_error != LCB_SUCCESS || cookie->jerr != json_tokener_success) {
            /* log error */
            RERROR("rlm_couchbase: failed to execute get request or parse returned json object");
            /* free json object */
            json_object_put(cookie->jobj); 
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
        json_object_object_add(cookie->jobj, "docType", json_object_new_string(inst->doctype));
        /* set start and stop times ... ensure we always have these elements */
        json_object_object_add(cookie->jobj, "startTimestamp", json_object_new_string("null"));
        json_object_object_add(cookie->jobj, "stopTimestamp", json_object_new_string("null"));
    }

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

    /* loop through pairs and add to json document */
    for (vp = request->packet->vps; vp; vp = vp->next) {
        /* map attribute to element */
        if (couchbase_attribute_to_element(vp->da->name, inst->map_object, &element) == 0) {
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
        /* release handle */
        fr_connection_release(inst->pool, handle);
        /* return */
        return RLM_MODULE_FAIL;
    } else {
        /* copy json string to document */
        strncpy(document, json_object_to_json_string(cookie->jobj), sizeof(document));
        /* free json output */
        json_object_put(cookie->jobj);
    }

    /* debugging */
    RDEBUG("setting '%s' => '%s'", key, document);

    /* store document/key in couchbase */
    cb_error = couchbase_set_key(cb_inst, key, document, inst->expire);

    /* check return */
    if (cb_error != LCB_SUCCESS) {
        RERROR("rlm_couchbase: failed to store document (%s): %s", key, lcb_strerror(NULL, cb_error));
    }

    /* release handle */
    fr_connection_release(inst->pool, handle);

    /* return */
    return RLM_MODULE_OK;
}

/* free any memory we allocated */
static int rlm_couchbase_detach(void *instance) {
    rlm_couchbase_t *inst = instance;  /* instance struct */

    /* free map object */
    json_object_put(inst->map_object);

    /* destroy connection pool */
    fr_connection_pool_delete(inst->pool);

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
        NULL,                       /* authentication */
        rlm_couchbase_authorize,    /* authorization */
        rlm_couchbase_preacct,      /* preaccounting */
        rlm_couchbase_accounting,   /* accounting */
        NULL,                       /* checksimul */
        NULL,                       /* pre-proxy */
        NULL,                       /* post-proxy */
        NULL                        /* post-auth */
    },
};
