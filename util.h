/* blargs */

#ifndef _UTIL_H
#define _UTIL_H

RCSIDH(util_h, "$Id$");

#include <json/json.h>

/* maximum size of a stored value */
#define MAX_VALUE_SIZE 4096

/* maximum length of a document key */
#define MAX_KEY_SIZE 250

/* define functions */
int couchbase_attribute_to_element(const char *name, json_object *map, void *attribute);

json_object *couchbase_value_pair_to_json_object(VALUE_PAIR *vp);

int couchbase_check_start_timestamp(json_object *json, VALUE_PAIR *vps);

#endif /* _UTIL_H */
