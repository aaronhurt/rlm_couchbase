/* junk */

#include <freeradius-devel/ident.h>
RCSID("$Id$")

#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/libradius.h>
#include <freeradius-devel/modules.h>
#include <freeradius-devel/rad_assert.h>

#include <libcouchbase/couchbase.h>

#include "callbacks.h"

/* maximum size of a stored value */
#define MAX_VALUE_SIZE 4096

/* maximum length of a document key */
#define MAX_KEY_SIZE 250

/* configuration struct */
typedef struct rlm_couchbase_t {
    char *dockey;
    char *host;
    char *bucket;
    char *user;
    char *pass;
    lcb_t couchbase;
} rlm_couchbase_t;

/* map config to internal variables */
static const CONF_PARSER module_config[] = {
    {"dockey", PW_TYPE_STRING_PTR, offsetof(rlm_couchbase_t, dockey), NULL, "Acct-Unique-Session-Id"},
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
    memset(data, 0, sizeof(*data));

    /* fail on bad config */
    if (cf_section_parse(conf, data, module_config) < 0) {
        radlog(L_ERR, "rlm_couchbase: Failed to parse config!");
        free(data);
        return -1;
    }

    /* debugging */
    DEBUG("dockey = %s | host = %s | bucket = %s | user = %s | pass = %s",
           data->dockey, data->host, data->bucket, data->user, data->pass);

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

    /* set error callback */
    lcb_set_error_callback(data->couchbase, couchbase_error_callback);

    /* initiate connection */
    if ((cb_error = lcb_connect(data->couchbase)) != LCB_SUCCESS) {
        radlog(L_ERR, "rlm_couchbase: Failed to initiate connect: %s", lcb_strerror(NULL, cb_error));
        lcb_destroy(data->couchbase);
        free(data);
        return -1;
    }

    /* run event loop and wait for connection */
    lcb_wait(data->couchbase);

    /* assign instance */
    *instance = data;

    /* return okay */
    return 0;
}

/* write accounting data to couchbase */
static int couchbase_accounting(void *instance, REQUEST *request) {
    rlm_couchbase_t *p = instance;      // couchbase instance
    const char *attribute;              // radius attribute key
    char value[255];                    // radius attribute value
    char key[MAX_KEY_SIZE];             // couchbase document key
    char document[MAX_VALUE_SIZE];      // couchbase document body
    char keya[255];                     // first part of document key
    char keyb[255];                     // second part of document key
    int length;                         // returned value length holder
    int count = 0;                      // value/pair counter
    int remaining;                      // buffer space check
    lcb_error_t cb_error;               // couchbase error holder
    lcb_store_cmd_t cmd;                // couchbase command
    const lcb_store_cmd_t *commands[1]; // couchbase command set holder

    /* assert packet as not null*/
    rad_assert(request->packet != NULL);

    /* fetch value pairs from packet */
    VALUE_PAIR *vp = request->packet->vps;

    /* init document */
    memset(document, 0, sizeof(document));

    /* start json document body */
    char *vptr = document; *vptr++ = '{';

    /* loop through value pairs */
    while (vp) {
        /* get current attribute */
        attribute = vp->name;

        /* look for document key attribute */
        if (strcmp(attribute, p->dockey) == 0) {
            /* get and store our key */
             vp_prints_value(keya, sizeof(keya), vp, 0);
        }

        /* look for status attribute */
        if (strcmp(attribute, "Acct-Status-Type") == 0) {
            ///* get and store status */
            vp_prints_value(value, sizeof(value), vp, 0);
            /* clear key */
            memset(keyb, 0, sizeof(keyb));
            /* force lower case */
            int i=0; char *cp = keyb;
            while (i < strlen(value)) {
                *cp++ = tolower(value[i]);
                i++;
            }
            /* terminate pointer */
            *cp = '\0';
        }

        /* get and store value */
        length = vp_prints_value_json(value, sizeof(value), vp);

        /* debugging */
        RDEBUG("%s => %s", attribute, value);

        /* calculate buffere space remaining */
        remaining = MAX_VALUE_SIZE - (vptr - document);

        /* check remaining space */
        if (remaining <= 0) {
            /* uhh ohh ... we're out of space */
            remaining = 0;
            break;
        }

        /* add a comma if this is not our first value */
        if (count > 0) {
            *vptr++ = ',';
            remaining--;
        }

        /* append this attribute/value pair */
        snprintf(vptr, remaining, "\"%s\":%s", attribute, value);

        /* advance pointer length of value + attribute + 3 for quotes and colon */
        vptr += length + strlen(attribute) + 3;

        /* increment counter */
        count++;

        /* goto next value pair */
        vp = vp->next;
    }

    /* calculate buffere space remaining */
    remaining = MAX_VALUE_SIZE - (vptr - document);

    /* check remaining space */
    if (remaining > 1) {
        /* close json body */
        *vptr++ = '}';
        /* terminate pointer */
        *vptr = '\0';
    } else {
        /* this isn't good ... no space left to close the body */
        radlog(L_ERR, "rlm_couchbase: Could not close JSON body, insufficient buffer space!");
        /* terminate document */
        document[MAX_VALUE_SIZE-1] = '\0';
        /* return */
        return RLM_MODULE_FAIL;
    }

    /* build final key */
    snprintf(key, sizeof(key), "%s-%s", keya, keyb);

    /* debugging */
    RDEBUG("setting '%s' => '%s'", key, document);

    /* setup store callback */
    lcb_set_store_callback(p->couchbase, couchbase_store_callback);

    /* run the event loop */
    lcb_wait(p->couchbase);

    /* setup command set for storage operation */
    commands[0] = &cmd;
    memset(&cmd, 0, sizeof(cmd));

    /* set commands */
    cmd.v.v0.operation = LCB_SET;
    cmd.v.v0.key = key;
    cmd.v.v0.nkey = strlen(key);
    cmd.v.v0.bytes = document;
    cmd.v.v0.nbytes = strlen(document);

    /* store document */
    cb_error = lcb_store(p->couchbase, NULL, 1, commands);

    /* check return */
    if (cb_error != LCB_SUCCESS) {
        radlog(L_ERR, "rlm_couchbase: Failed to store document: %s", lcb_strerror(NULL, cb_error));
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

    /* free radius instance struct */
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
