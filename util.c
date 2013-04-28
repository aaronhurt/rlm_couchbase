/* blargs */
RCSID("$Id$")

#include <freeradius-devel/radiusd.h>

#include <json/json.h>

#include "util.h"

/* map free radius attribute to user defined json element name */
int couchbase_attribute_to_element(const char *name, json_object *map, void *buf) {
    json_object *jval;      /* json object */
    int length;             /* json value length */

    /* clear buffer */
    memset((char *) buf, 0, MAX_KEY_SIZE);

    /* attempt to map attribute */
    if (json_object_object_get_ex(map, name, &jval)) {
        /* get value length */
        length = json_object_get_string_len(jval);
        /* check buffer size */
        if (length > MAX_KEY_SIZE -1) {
            /* oops ... this value is bigger than our buffer ... error out */
            ERROR("rlm_couchbase: Error, json value larger than MAX_KEY_SIZE - %d", MAX_KEY_SIZE);
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
    DEBUG("rlm_couchbase: Skipping attribute with no map entry - %s", name);

    /* default return */
    return -1;
}

/* convert freeradius value/pair to json object
 * basic structure taken from freeradius function
 * vp_prints_value_json in src/lib/print.c */
json_object *couchbase_value_pair_to_json_object(VALUE_PAIR *vp) {
    char value[255];    /* radius attribute value */

    /* add this attribute/value pair to our json output */
    if (!vp->da->flags.has_tag) {
        switch (vp->type) {
            case PW_TYPE_INTEGER:
            case PW_TYPE_BYTE:
            case PW_TYPE_SHORT:
                /* skip if we have flags */
                if (vp->da->flags.has_value) break;
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
            default:
                /* do nothing */
            break;
        }
    }

    /* keep going if not set above */
    switch (vp->da->type) {
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

/* check current value of start timestamp in json body and update if needed */
int couchbase_ensure_start_timestamp(json_object *json, VALUE_PAIR *vps) {
    json_object *jval;      /* json object value */
    struct tm tm;           /* struct to hold event time */
    struct tm *tp;          /* struct pointer to hold caclulated start time */
    time_t ts = 0;          /* values to hold time in seconds */
    VALUE_PAIR *vp;         /* values to hold value pairs */
    char value[255];        /* store radius attribute values and our timestamp */

    /* get our current start timestamp from our json body */
    if ((json_object_object_get_ex(json, "startTimestamp", &jval)) == 0) {
        /* debugging */
        DEBUG("rlm_couchbase: failed to find start timestamp in current json body");
        /* return */
        return -1;
    }

    /* check the value */
    if (strcmp(json_object_get_string(jval), "null") != 0) {
        /* debugging */
        DEBUG("rlm_couchbase: start timestamp looks good - nothing to do");
        /* already not null - nothing else to do */
        return 0;
    }

    /* get current event timestamp */
    if ((vp = pairfind(vps, PW_EVENT_TIMESTAMP, 0, TAG_ANY)) != NULL) {
        /* get value */
        vp_prints_value(value, sizeof(value), vp, 0);
        /* parse timestamp - Apr 27 2013 13:02:29 CDT */
        if (strptime(value, "%b %d %Y %T %Z", &tm) != NULL) {
            /* get unix timestamp - seconds since the epoc */
            ts = mktime(&tm);
        }
    }

    /* check for valid ts */
    if (ts < 1) return -1;

    /* clear value */
    memset(value, sizeof(value), 0);

    /* get elapsed session time */
    if ((vp = pairfind(vps, PW_ACCT_SESSION_TIME, 0, TAG_ANY)) != NULL) {
        /* calculate diff */
        ts = (ts - vp->vp_integer);
        /* store offset in time struct */
        tp = localtime(&ts);
        /* calculate start time */
        strftime(value, sizeof(value), "%b %d %Y %T CDT", tp);
        /* debugging */
        DEBUG("rlm_couchbase: calculated start timestamp: %s", value);
        /* store new value in json body */
        json_object_object_add(json, "startTimestamp", json_object_new_string(value));
    }

    /* default return */
    return 0;
}
