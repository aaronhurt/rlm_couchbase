/* workarounds missing functions in old json-c libraries */

#ifndef _jsonc_missing_h_
#define _jsonc_missing_h_

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

#ifndef HAVE_JSON_TOKENER_PARSE_VERBOSE
    struct json_object* json_tokener_parse_verbose(const char *str, enum json_tokener_error *error);
#endif

#ifndef HAVE_JSON_TOKENER_ERROR_DESC
    const char *json_tokener_error_desc(enum json_tokener_error jerr);
#endif

#ifndef HAVE_JSON_TOKENER_GET_ERROR
    enum json_tokener_error json_tokener_get_error(json_tokener *tok);
#endif

/* correct poor const handling within json-c library */
#ifdef json_object_object_foreach
    #undef json_object_object_foreach
#endif

/* redefine with correct handling of const pointers */
#if defined(__GNUC__) && !defined(__STRICT_ANSI__)
    #define json_object_object_foreach(obj, key, val) \
    char *key; struct json_object *val; void const *src; void *dst; \
    for (struct lh_entry *entry = json_object_get_object(obj)->head; \
        ({ if (entry) { key = (char *)entry->k; src = entry->v; \
        memcpy(&dst, &src, sizeof(dst)); val = (struct json_object *) dst; }; entry; }); \
        entry = entry->next)
#else /* ANSI C or MSC */
    #define json_object_object_foreach(obj,key,val) \
    char *key; struct json_object *val; struct lh_entry *entry; void const *src; void *dst; \
    for (entry = json_object_get_object(obj)->head; \
        (entry ? (key = (char *)entry->k, src = entry->v, memcpy(&dst, &src, sizeof(dst)), \
        val = (struct json_object *)dst, entry) : 0); entry = entry->next)
#endif /* defined(__GNUC__) && !defined(__STRICT_ANSI__) */

#endif  /* _jsonc_missing_h_ */
