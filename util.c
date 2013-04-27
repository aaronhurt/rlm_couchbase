/* blargs */
RCSID("$Id$")

#include <freeradius-devel/radiusd.h>

#include <json/json.h>

#include "util.h"

/* map free radius attribute to user defined json element name */
int couchbase_attribute_to_element(const char *name, json_object *map, void *attribute) {
    json_object *jval;      /* json object */
    int length;             /* json value length */

    /* clear attribute */
    memset((char *) attribute, 0, MAX_KEY_SIZE);

    /* attempt to map attribute */
    if (json_object_object_get_ex(map, name, &jval)) {
        /* get value length */
        length = json_object_get_string_len(jval);
        /* check buffer size */
        if (length > MAX_KEY_SIZE -1) {
            /* oops ... this value is bigger than our buffer ... error out */
            radlog(L_ERR, "rlm_couchbase: Error, json value larger than MAX_KEY_SIZE - %d", MAX_KEY_SIZE);
            /* return fail */
            return -1;
        } else {
            /* copy string value to attribute */
            strncpy(attribute, json_object_get_string(jval), length);
            /* return good */
            return 0;
        }
    } else {
        /* debugging */
        DEBUG("rlm_couchbase: Skipping attribute with no map entry - %s", name);
        /* return fail */
        return -1;
    }
}

/* convert freeradius value/pair to json object */
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
