/* workarounds missing functions in old json-c libraries */

RCSID("$Id$");

#include <string.h>

#include "jsonc_missing.h"

#ifndef HAVE_JSON_OBJECT_GET_STRING_LEN
int json_object_get_string_len(json_object *obj) {
    return strlen(json_object_to_json_string(obj));
}
#endif

#ifndef HAVE_JSON_OBJECT_OBJECT_GET_EX
int json_object_object_get_ex(json_object *jso, const char *key, json_object **value) {
    if (NULL == jso) return 0;

    switch(jso->o_type) {
        case json_type_object:
            return lh_table_lookup_ex(jso->o.c_object, (void*)key, (void**)value);
        break;
        default:
            if (value != NULL) {
                *value = NULL;
            }
            return 0;
        break;
    }
}
#endif

#ifndef HAVE_LH_TABLE_LOOKUP_EX
int lh_table_lookup_ex(struct lh_table* t, const void* k, void **v) {
    struct lh_entry *e = lh_table_lookup_entry(t, k);
    if (e != NULL) {
        if (v != NULL) *v = (void *)e->v;
        return 1; /* key found */
    }
    if (v != NULL) *v = NULL;
    return 0; /* key not found */
}
#endif

#ifndef HAVE_JSON_TOKENER_PARSE_VERBOSE
struct json_object* json_tokener_parse_verbose(const char *str, enum json_tokener_error *error) {
    struct json_tokener* tok;
    struct json_object* obj;

    tok = json_tokener_new();
    if (!tok)
      return NULL;
    obj = json_tokener_parse_ex(tok, str, -1);
    *error = tok->err;
    if(tok->err != json_tokener_success) {
    if (obj != NULL)
      json_object_put(obj);
        obj = NULL;
    }

    json_tokener_free(tok);
    return obj;
}
#endif

#ifndef HAVE_JSON_TOKENER_ERROR_DESC
const char *json_tokener_error_desc(enum json_tokener_error jerr) {
    if (NULL == json_tokener_errors[jerr]) {
        return "Unknown error, invalid json_tokener_error value passed to json_tokener_error_desc()";
    }
    return json_tokener_errors[jerr];
}
#endif

#ifndef HAVE_JSON_TOKENER_GET_ERROR
enum json_tokener_error json_tokener_get_error(json_tokener *tok) {
    return tok->err;
}
#endif
