#include <inttypes.h>
#include <string.h>

#include "common.h"
#include "json.h"
#include "date.h"
#include "table.h"
#include "modules/api.h"
#include "struct/xtring.h"
#include "struct/hashtable.h"

static bool me_ctor(void)
{
    // NOP (for now)

    return TRUE;
}

static void me_dtor(void)
{
    // NOP (for now)
}

#if 0
_("firstname")
_("vat")
_("ovhSubsidiary")
_("area")
_("birthDay")
_("nationalIdentificationNumber")
_("spareEmail")
_("ovhCompany")
_("state")
_("email")
_("city")
_("fax")
_("nichandle")
_("address")
_("companyNationalIdentificationNumber")
_("birthCity")
_("country")
_("language")
_("organisation")
_("name")
_("phone")
_("sex")
_("zip")
_("corporationType")
_("legalform")
_("male")
_("female")
#endif
static command_status_t me(COMMAND_ARGS)
{
    bool success;
    request_t *req;
    json_document_t *doc;

    USED(arg);
    USED(mainopts);
    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/me");
    success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
    request_destroy(req);
    if (success) {
        table_t *t;
        Iterator it;
        json_value_t root;

        t = table_new(2,
            _("Attribute"), TABLE_TYPE_STRING,
            _("Value"), TABLE_TYPE_STRING
        );
        root = json_document_get_root(doc);
        json_object_to_iterator(&it, root);
        for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
            const char *key;
            json_value_t v;

            v = (json_value_t) iterator_current(&it, (void **) &key);
            table_store(t, gettext(key), v == json_null ? NULL : json_get_string(v));
        }
        iterator_close(&it);
        table_sort(t, 0);
        table_display(t, TABLE_FLAG_NONE);
        table_destroy(t);
        json_document_destroy(doc);
    }

    return COMMAND_SUCCESS;
}

typedef struct {
    char *name;
    size_t status;
    char *description;
} application_t;

void application_destroy(void *data)
{
    if (NULL != data) {
        application_t *app;

        app = (application_t *) data;
        free(app->name);
        free(app->description);
        free(app);
    }
}

static struct {
    const char *name;
    size_t name_len;
} rule_methods[] = {
    { "GET", STR_LEN("GET") },
    { "POST", STR_LEN("POST") },
    { "PUT", STR_LEN("PUT") },
    { "DELETE", STR_LEN("DELETE") }
};

static const char *credential_status[] = {
    N_("expired"),
    N_("pendingValidation"),
    N_("refused"),
    N_("validated")
};

static const char *application_status[] = {
    N_("active"),
    N_("blocked"),
    N_("inactive"),
    N_("trusted")
};

static command_status_t me_credential_list(COMMAND_ARGS)
{
    bool success;
    request_t *req;
    json_document_t *doc;

    USED(arg);
    USED(mainopts);
    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/me/api/credential");
    success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
    request_destroy(req);
    if (success) {
        table_t *t;
        Iterator it;
        HashTable *applications;

        t = table_new(
#ifdef PRINT_OVH_ID
            10,
            _("credentialId"), TABLE_TYPE_INTEGER,
#else
            9,
#endif /* PRINT_OVH_ID */
            _("creation"), TABLE_TYPE_DATETIME,
            _("expiration"), TABLE_TYPE_DATETIME,
            _("last used"), TABLE_TYPE_DATETIME,
            _("ovhSupport"), TABLE_TYPE_BOOLEAN,
            _("credential status"), TABLE_TYPE_STRING,
            _("rules"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
            _("application status"), TABLE_TYPE_STRING,
            _("application name"), TABLE_TYPE_STRING,
            _("application description"), TABLE_TYPE_STRING
        );
        applications = hashtable_new(NULL, value_equal, NULL, NULL, application_destroy);
        json_array_to_iterator(&it, json_document_get_root(doc));
        for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
            json_value_t v;
            json_document_t *doc;
            int64_t credentialId;
            size_t credentialStatus;

            v = (json_value_t) iterator_current(&it, NULL);
            credentialId = json_get_integer(v);
            req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/me/api/credential/%" PRIu32, credentialId);
            success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
            request_destroy(req);
            if (success) {
                json_value_t root;
                application_t *app;
                int64_t applicationId;
                const char *stringified_rules;
                struct tm lastUse, expiration, creation;

                app = NULL;
                stringified_rules = "-";
                root = json_document_get_root(doc);
                json_object_get_property(root, "status", &v);
                credentialStatus = json_get_enum(v, credential_status, 0);
                JSON_GET_PROP_INT(root, "applicationId", applicationId);
                json_object_get_property(root, "lastUse", &v);
                bzero(&lastUse, sizeof(lastUse));
                if (json_null != v) {
                    date_parse(json_get_string(v), "%FT%T%z", &lastUse, NULL);
                }
                json_object_get_property(root, "creation", &v);
                date_parse(json_get_string(v), "%FT%T%z", &creation, NULL);
                json_object_get_property(root, "expiration", &v);
                date_parse(json_get_string(v), "%FT%T%z", &expiration, NULL);
                json_object_get_property(root, "rules", &v);
                {
                    Iterator it;
                    String *buffer;
                    HashTable *rules;

                    rules = hashtable_ascii_cs_new(NULL, NULL, free);
                    json_array_to_iterator(&it, v);
                    for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
                        size_t i;
                        uintptr_t *methods;
                        char *path, *method;
                        json_value_t jrules;

                        jrules = (json_value_t) iterator_current(&it, NULL);
                        JSON_GET_PROP_STRING_EX(jrules, "path", path, FALSE);
                        JSON_GET_PROP_STRING_EX(jrules, "method", method, FALSE);
                        if (!hashtable_get(rules, path, &methods)) {
                            methods = mem_new(*methods);
                            *methods = 0;
                            hashtable_put(rules, 0, path, methods, NULL);
                        }
                        for (i = 0; i < ARRAY_SIZE(rule_methods); i++) {
                            if (0 == strcmp(rule_methods[i].name, method)) {
                                SET_FLAG(*methods, 1 << (i + 1));
                                break;
                            }
                        }
                    }
                    iterator_close(&it);
                    buffer = string_new();
                    hashtable_to_iterator(&it, rules);
                    for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
                        size_t i, j;
                        const char *path;
                        uintptr_t *methods;

                        j = 0;
                        methods = (uintptr_t *) iterator_current(&it, (void *) &path);
                        if (!string_empty(buffer)) {
                            string_append_char(buffer, '\n');
                        }
                        for (i = 0; i < ARRAY_SIZE(rule_methods); i++) {
                            if (HAS_FLAG(*methods, (1 << (i + 1)))) {
                                if (j++ > 0) {
                                    string_append_char(buffer, ',');
                                }
                                string_append_string_len(buffer, rule_methods[i].name, rule_methods[i].name_len);
                            }
                        }
                        string_append_char(buffer, ' ');
                        string_append_string(buffer, path);
                    }
                    iterator_close(&it);
                    hashtable_destroy(rules);
                    stringified_rules = string_orphan(buffer);
                }
                json_document_destroy(doc);
                if (!hashtable_direct_get(applications, applicationId, &app)) {
                    req = request_new(REQUEST_FLAG_SIGN | REQUEST_FLAG_IGNORE_404, HTTP_GET, NULL, API_BASE_URL "/me/api/credential/%" PRIu32 "/application", credentialId);
                    success = request_execute(req, RESPONSE_JSON, (void **) &doc, error); // if the application doesn't exist anymore, we get a 404
                    request_destroy(req);
                    if (success) {
                        if (200L == request_response_status(req)) {
                            app = mem_new(*app);
                            root = json_document_get_root(doc);
                            JSON_GET_PROP_STRING(root, "name", app->name);
                            JSON_GET_PROP_STRING(root, "description", app->description);
                            json_object_get_property(root, "status", &v);
                            app->status = json_get_enum(v, application_status, 0);
                            json_document_destroy(doc);
                        }
                    }
                    hashtable_direct_put(applications, 0, applicationId, app, NULL);
                }
                table_store(
                    t,
#ifdef PRINT_OVH_ID
                    credentialId,
#endif /* PRINT_OVH_ID */
                    creation,
                    expiration,
                    lastUse,
                    FALSE,
                    _(credential_status[credentialStatus]),
                    stringified_rules,
                    NULL == app ? NULL : _(application_status[app->status]),
                    NULL == app ? NULL : app->name,
                    NULL == app ? NULL : app->description
                );
            }
        }
        iterator_close(&it);
        table_display(t, TABLE_FLAG_NONE);
        table_destroy(t);
        json_document_destroy(doc);
        hashtable_destroy(applications);
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t me_credential_flush(COMMAND_ARGS)
{
    bool success;
    request_t *req;
    json_document_t *doc;

    USED(arg);
    USED(mainopts);
    // TODO: find a way to not invalidate ourselves
    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/me/api/credential");
    success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
    request_destroy(req);
    if (success) {
        Iterator it;

        json_array_to_iterator(&it, json_document_get_root(doc));
        for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
            json_value_t v;
            int64_t credentialId;

            v = (json_value_t) iterator_current(&it, NULL);
            credentialId = json_get_integer(v);
            req = request_new(REQUEST_FLAG_SIGN, HTTP_DELETE, NULL, API_BASE_URL "/me/api/credential/%" PRIu32, credentialId);
            success = request_execute(req, RESPONSE_IGNORE, NULL, error);
            request_destroy(req);
        }
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static void me_regcomm(graph_t *g)
{
    argument_t *lit_me;
    argument_t *lit_crendentials, *lit_cred_list, *lit_cred_flush;

    lit_me = argument_create_literal("me", me);
    lit_crendentials = argument_create_literal("credentials", NULL);
    lit_cred_list = argument_create_literal("list", me_credential_list);
    lit_cred_flush = argument_create_literal("flush", me_credential_flush);

    graph_create_full_path(g, lit_me, NULL);
    graph_create_full_path(g, lit_crendentials, lit_cred_list, NULL);
    graph_create_full_path(g, lit_crendentials, lit_cred_flush, NULL);
}

#if 0
void me_register_rules(json_value_t rules)
{
    JSON_ADD_RULE(rules, "GET", "/me/*");
    JSON_ADD_RULE(rules, "PUT", "/me/*");
    JSON_ADD_RULE(rules, "POST", "/me/*");
    JSON_ADD_RULE(rules, "DELETE", "/me/*");
}
#endif

DECLARE_MODULE(me) = {
    "me",
    me_regcomm,
    me_ctor,
    NULL,
    me_dtor
};
