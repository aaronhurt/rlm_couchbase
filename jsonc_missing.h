/* workarounds missing functions in old json-c libraries */

RCSIDH(jsonc_missing_h, "$Id$");

#include <json/json.h>

#include "config.h"

#ifndef HAVE_JSON_OBJECT_OBJECT_GET_EX
#include <json/json_object_private.h>
#endif

#ifndef HAVE_JSON_OBJECT_GET_STRING_LEN
int json_object_get_string_len(struct json_object *obj);
#endif

#ifndef HAVE_JSON_OBJECT_OBJECT_GET_EX
int json_object_object_get_ex(struct json_object* jso, const char *key, struct json_object **value);
#endif

#ifndef HAVE_LH_TABLE_LOOKUP_EX
int lh_table_lookup_ex(struct lh_table* t, const void* k, void **v);
#endif

#ifndef HAVE_JSON_TOKENER_PARSE_VERBOSE
struct json_object* json_tokener_parse_verbose(const char *str, enum json_tokener_error *error);
#endif

#ifndef HAVE_JSON_TOKENER_ERROR_DESC
const char *json_tokener_error_desc(enum json_tokener_error jerr);
#endif

#ifndef HAVE_JSON_TOKENER_GET_ERROR
enum json_tokener_error json_tokener_get_error(json_tokener *tok);
#endif
