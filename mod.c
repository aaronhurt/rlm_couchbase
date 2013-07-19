/* blargs */
RCSID("$Id$")

#include <freeradius-devel/radiusd.h>

#include <json/json.h>

#include "couchbase.h"

#include "mod.h"

/* create new connection pool handle */
void *mod_conn_create(void *instance) {
    rlm_couchbase_t *inst = instance;           /* module instance pointer */
    rlm_couchbase_handle_t *handle_t = NULL;    /* connection handle pointer */
    cookie_t *cookie = NULL;                    /* couchbase cookie */
    lcb_t cb_inst;                              /* couchbase connection instance */
    lcb_error_t cb_error = LCB_SUCCESS;         /* couchbase error status */

    /* create instance */
    cb_inst = couchbase_init_connection(inst->server, inst->bucket, inst->pass);

    /* check couchbase instance status */
    if ((cb_error = lcb_get_last_error(cb_inst)) != LCB_SUCCESS) {
        ERROR("rlm_couchbase: failed to initiate couchbase connection: %s (0x%x)", lcb_strerror(NULL, cb_error), cb_error);
        /* fail */
        return NULL;
    }

    /* allocate memory for couchbase connection instance abstraction */
    handle_t = talloc_zero(inst, rlm_couchbase_handle_t);
    cookie = talloc_zero(handle_t, cookie_t);

    /* populate handle with allocated structs */
    handle_t->cookie = cookie;
    handle_t->handle = cb_inst;

    /* return handle struct */
    return handle_t;
}

/* free couchbase instance handle and any additional context memory */
int mod_conn_delete(UNUSED void *instance, void *handle) {
    rlm_couchbase_handle_t *handle_t = handle;      /* connection instance handle */
    lcb_t cb_inst = handle_t->handle;               /* couchbase instance */

    /* destroy/free couchbase instance */
    lcb_destroy(cb_inst);

    /* free handle */
    talloc_free(handle_t);

    /* return */
    return true;
}

/* map free radius attribute to user defined json element name */
int mod_attribute_to_element(const char *name, json_object *map, void *buf) {
    json_object *jval;      /* json object */

    /* clear buffer */
    memset((char *) buf, 0, MAX_KEY_SIZE);

    /* attempt to map attribute */
    if (json_object_object_get_ex(map, name, &jval)) {
        int length;     /* json value length */
        /* get value length */
        length = json_object_get_string_len(jval);
        /* check buffer size */
        if (length > MAX_KEY_SIZE -1) {
            /* oops ... this value is bigger than our buffer ... error out */
            ERROR("rlm_couchbase: json map value larger than MAX_KEY_SIZE - %d", MAX_KEY_SIZE);
            /* return fail */
            return -1;
        } else {
            /* copy string value to buffer */
            strncpy(buf, json_object_get_string(jval), length);
            /* return good */
            return 0;
        }
    }

    /* debugging */
    DEBUG("rlm_couchbase: skipping attribute with no map entry - %s", name);

    /* default return */
    return -1;
}

/* inject value pairs into given request
 * that are defined in the passed json object
 */
void *mod_json_object_to_value_pairs(json_object *json, const char *section, REQUEST *request) {
    json_object *jobj, *jval, *jval2;   /* json object pointers */
    json_object_iter iter;              /* json object iterator */
    TALLOC_CTX *ctx;                    /* talloc context for pairmake */
    VALUE_PAIR *vp, **ptr;              /* value pair and value pair pointer for pairmake */

    /* assign ctx and vps for pairmake based on section */
    if (strcmp(section, "config") == 0) {
        ctx = request;
        ptr = &(request->config_items);
    } else if (strcmp(section, "reply") == 0) {
        ctx = request->reply;
        ptr = &(request->reply->vps);
    } else {
        /* log error - this shouldn't happen */
        RERROR("rlm_couchbase: invalid section passed for pairmake");
        /* return */
        return NULL;
    }

    /* get config payload */
    if (json_object_object_get_ex(json, section, &jobj)) {
        /* loop through object */
        json_object_object_foreachC(jobj, iter) {
            /* debugging */
            RDEBUG("parsing %s attribute %s => %s", section, iter.key, json_object_to_json_string(iter.val));
            /* create pair from json object */
            if (json_object_object_get_ex(iter.val, "value", &jval) &&
            json_object_object_get_ex(iter.val, "op", &jval2)) {
                /* make correct pairs based on json object type */
                switch (json_object_get_type(jval)) {
                    case json_type_double:
                    case json_type_int:
                    case json_type_string:
                        /* debugging */
                        RDEBUG("adding %s attribute %s", section, iter.key);
                        /* add pair */
                        vp = pairmake(ctx, ptr, iter.key, json_object_get_string(jval),
                            fr_str2int(fr_tokens, json_object_get_string(jval2), 0));
                        /* check pair */
                        if (!vp) {
                            RERROR("rlm_couchbase: could not build attribute %s", fr_strerror());
                            /* return */
                            return NULL;
                        }
                    break;
                    case json_type_object:
                    case json_type_array:
                        /* log error - we want to handle these eventually */
                        RERROR("rlm_couchbase: skipping unhandled json type object or array in value pair object");
                    break;
                    default:
                        /* log error - this shouldn't ever happen */
                        RERROR("rlm_couchbase: skipping unhandled json type in value pair object");
                    break;
                }
            }
        }
    }

    /* return NULL */
    return NULL;
}

/* convert freeradius value/pair to json object
 * basic structure taken from freeradius function
 * vp_prints_value_json in src/lib/print.c */
json_object *mod_value_pair_to_json_object(VALUE_PAIR *vp) {
    char value[255];    /* radius attribute value */

    /* add this attribute/value pair to our json output */
    if (!vp->da->flags.has_tag) {
        switch (vp->da->type) {
            case PW_TYPE_INTEGER:
            case PW_TYPE_BYTE:
            case PW_TYPE_SHORT:
                /* skip if we have flags */
                if (vp->da->flags.has_value) break;
                /* debug */
                DEBUG("rlm_couchbase: assigning int/byte/short '%s' as integer", vp->da->name);
                /* return as int */
                return json_object_new_int(vp->vp_integer);
            break;
            case PW_TYPE_SIGNED:
                /* debug */
                DEBUG("rlm_couchbase: assigning signed '%s' as integer", vp->da->name);
                /* return as int */
                return json_object_new_int(vp->vp_signed);
            break;
            case PW_TYPE_INTEGER64:
                /* debug */
                DEBUG("rlm_couchbase: assigning int64 '%s' as 64 bit integer", vp->da->name);
                /* return as 64 bit int */
                return json_object_new_int64(vp->vp_integer64);
            break;
            default:
                /* do nothing */
            break;
        }
    }

    /* keep going if not set above */
    switch (vp->da->type) {
        case PW_TYPE_STRING:
            /* debug */
            DEBUG("rlm_couchbase: assigning string '%s' as string", vp->da->name);
            /* return string value */
            return json_object_new_string(vp->vp_strvalue);
        default:
            /* debug */
            DEBUG("rlm_couchbase: assigning unhandled '%s' as string", vp->da->name);
            /* get standard value */
            vp_prints_value(value, sizeof(value), vp, 0);
            /* return string value from above */
            return json_object_new_string(value);
        break;
    }
}

/* check current value of start timestamp in json body and update if needed */
int mod_ensure_start_timestamp(json_object *json, VALUE_PAIR *vps) {
    json_object *jval;      /* json object value */
    struct tm tm;           /* struct to hold event time */
    time_t ts = 0;          /* values to hold time in seconds */
    VALUE_PAIR *vp;         /* values to hold value pairs */
    char value[255];        /* store radius attribute values and our timestamp */

    /* get our current start timestamp from our json body */
    if (json_object_object_get_ex(json, "startTimestamp", &jval) == 0) {
        /* debugging ... this shouldn't ever happen */
        DEBUG("rlm_couchbase: failed to find start timestamp in current json body");
        /* return */
        return -1;
    }

    /* check the value */
    if (strcmp(json_object_get_string(jval), "null") != 0) {
        /* debugging */
        DEBUG("rlm_couchbase: start timestamp looks good - nothing to do");
        /* already set - nothing else to do */
        return 0;
    }

    /* get current event timestamp */
    if ((vp = pairfind(vps, PW_EVENT_TIMESTAMP, 0, TAG_ANY)) != NULL) {
        /* get seconds value from attribute */
        ts = vp->vp_date;
    } else {
        /* debugging */
        DEBUG("rlm_couchbase: failed to find event timestamp in current request");
        /* return */
        return -1;
    }

    /* clear value */
    memset(value, 0, sizeof(value));

    /* get elapsed session time */
    if ((vp = pairfind(vps, PW_ACCT_SESSION_TIME, 0, TAG_ANY)) != NULL) {
        /* calculate diff */
        ts = (ts - vp->vp_integer);
        /* calculate start time */
        size_t length = strftime(value, sizeof(value), "%b %e %Y %H:%M:%S %Z", localtime_r(&ts, &tm));
        /* check length */
        if (length > 0) {
            /* debugging */
            DEBUG("rlm_couchbase: calculated start timestamp: %s", value);
            /* store new value in json body */
            json_object_object_add(json, "startTimestamp", json_object_new_string(value));
        } else {
            /* debugging */
            DEBUG("rlm_couchbase: failed to format calculated timestamp");
            /* return */
            return -1;
        }
    }

    /* default return */
    return 0;
}

/* split username and domain from passed user name string */
char *mod_split_user_domain(const char *instring, char *outstring, size_t size, char **domain) {
    char *ptr = NULL;   /* position pointer */
    *domain = NULL;     /* domain portion */

    /* copy input to output and ensure null termination */
    strlcpy(outstring, instring, size);

    /* check for domain prefix */
    if ((ptr = strstr(outstring, "\\")) != NULL) {
        *domain = outstring;
        *ptr = '\0';
        outstring = ptr + 1;
    }
    /* check for domain suffix */
    else if ((ptr = strstr(outstring, "@")) != NULL) {
        *ptr = '\0';
        *domain = ptr + 1;
    }

    /* return username without domain */
    return outstring;
}
