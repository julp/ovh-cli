#include <inttypes.h>
#include <string.h>

#include "common.h"
#include "command.h"
#include "date.h"
#include "modules/api.h"
#include "modules/table.h"
#include "struct/xtring.h"
#include "struct/hashtable.h"

// arguments
typedef struct {
    char *application;
} me_argument_t;

static const char *credential_status[] = {
    N_("expired"),
    N_("pendingValidation"),
    N_("refused"),
    N_("validated"),
    NULL
};

static const char *application_status[] = {
    N_("active"),
    N_("blocked"),
    N_("inactive"),
    N_("trusted"),
    NULL
};

typedef struct {
    modelized_t data;
    int status;
    char *name;
    int applicationId;
    char *description;
    char *applicationKey;
} application_t;

static model_t application_model = {
    sizeof(application_t), "applications", NULL, NULL,
    (const model_field_t []) {
        { "status",         MODEL_TYPE_ENUM,   offsetof(application_t, status),         0, application_status, 0 },
        { "name",           MODEL_TYPE_STRING, offsetof(application_t, name),           0, NULL,               0 },
        { "applicationId",  MODEL_TYPE_INT,    offsetof(application_t, applicationId),  0, NULL,               MODEL_FLAG_PRIMARY | MODEL_FLAG_INTERNAL },
        { "description",    MODEL_TYPE_STRING, offsetof(application_t, description),    0, NULL,               0 },
        { "applicationKey", MODEL_TYPE_STRING, offsetof(application_t, applicationKey), 0, NULL,               0 },
        MODEL_FIELD_SENTINEL
    }
};

static HashTable *applications;

static bool me_ctor(error_t **UNUSED(error))
{
    applications = hashtable_ascii_cs_new(NULL, NULL, (DtorFunc) modelized_destroy);

    return TRUE;
}

static void me_dtor(void)
{
    hashtable_destroy(applications);
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
    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/me");
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
        table_sort(t, 0, TABLE_SORT_ASC);
        table_display(t, TABLE_FLAG_NONE);
        table_destroy(t);
        json_document_destroy(doc);
    }

    return COMMAND_SUCCESS;
}

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

static command_status_t credentials_list(COMMAND_ARGS)
{
    bool success;
    request_t *req;
    json_document_t *doc;

    USED(arg);
    USED(mainopts);
    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/me/api/credential");
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
            req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/me/api/credential/%" PRIu32, credentialId);
            success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
            request_destroy(req);
            if (success) {
                json_value_t root;
                application_t *app;
                int64_t applicationId;
                const char *stringified_rules;
                time_t lastUse, expiration, creation;

                app = NULL;
                stringified_rules = "-";
                root = json_document_get_root(doc);
                lastUse = expiration = creation = 0;
                json_object_get_property(root, "status", &v);
                credentialStatus = json_get_enum(v, credential_status, 0);
                JSON_GET_PROP_INT(root, "applicationId", applicationId);
                json_object_get_property(root, "lastUse", &v);
                if (json_null != v) {
                    date_parse_to_timestamp(json_get_string(v), "%FT%T%z", &lastUse);
                }
                json_object_get_property(root, "creation", &v);
                date_parse_to_timestamp(json_get_string(v), "%FT%T%z", &creation);
                json_object_get_property(root, "expiration", &v);
                date_parse_to_timestamp(json_get_string(v), "%FT%T%z", &expiration);
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
                    req = request_new(REQUEST_FLAG_SIGN | REQUEST_FLAG_IGNORE_404, HTTP_GET, NULL, error, API_BASE_URL "/me/api/credential/%" PRIu32 "/application", credentialId);
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

static command_status_t credentials_flush(COMMAND_ARGS)
{
    bool success;
    request_t *req;
    json_document_t *doc;

    USED(arg);
    USED(mainopts);
    // TODO: find a way to not invalidate ourselves
    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/me/api/credential");
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
            req = request_new(REQUEST_FLAG_SIGN, HTTP_DELETE, NULL, error, API_BASE_URL "/me/api/credential/%" PRIu32, credentialId);
            success = request_execute(req, RESPONSE_IGNORE, NULL, error);
            request_destroy(req);
        }
        iterator_close(&it);
        json_document_destroy(doc);
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static bool fetch_applications(error_t **error)
{
    bool success;

    success = TRUE;
    if (0 == hashtable_size(applications)) { // if you haven't any applications, you couldn't use ovh-cli
        request_t *req;
        json_document_t *doc;

        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/me/api/application");
        success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (success) {
            Iterator it;

            json_array_to_iterator(&it, json_document_get_root(doc));
            for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
                json_value_t v;
                json_document_t *doc;
                int64_t applicationId;

                v = (json_value_t) iterator_current(&it, NULL);
                applicationId = json_get_integer(v);
                req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/me/api/application/%" PRIu32, applicationId);
                success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
                request_destroy(req);
                if (success) {
                    application_t *application;

                    application = (application_t *) modelized_new(&application_model);
                    json_object_to_modelized(json_document_get_root(doc), (modelized_t *) application, TRUE, NULL);
                    hashtable_put(applications, 0, application->name, application, NULL);
                    json_document_destroy(doc);
                }
            }
            iterator_close(&it);
            json_document_destroy(doc);
        }
    }

    return success;
}

static command_status_t application_list(COMMAND_ARGS)
{
    bool success;

    USED(arg);
    USED(mainopts);
    success = fetch_applications(error);
    if (success) {
        table_t *t;
        Iterator it;

        t = table_new_from_model(&application_model, 0);
        hashtable_to_iterator(&it, applications);
        for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
            modelized_t *application;

            application = /*(modelized_t *)*/iterator_current(&it, NULL);
            table_store_modelized(t, application);
        }
        iterator_close(&it);
        table_display(t, TABLE_FLAG_NONE);
        table_destroy(t);
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t application_delete(COMMAND_ARGS)
{
    bool success;
    request_t *req;
    me_argument_t *args;

    USED(mainopts);
    args = (me_argument_t *) arg;
    success = fetch_applications(error);
    if (success) {
        ht_hash_t h;
        application_t *application;

        h = hashtable_hash(applications, args->application);
        if (hashtable_quick_get(applications, h, args->application, &application)) {
            req = request_new(REQUEST_FLAG_SIGN, HTTP_DELETE, NULL, error, API_BASE_URL "/me/api/application/%" PRIu32, application->applicationId);
            success = request_execute(req, RESPONSE_IGNORE, NULL, error);
            request_destroy(req);
            if (success) {
                hashtable_quick_delete(applications, h, args->application, TRUE);
            }
        } else {
            error_set(error, NOTICE, _("no such application named %s"), args->application);
        }
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static bool complete_application_name(void *parsed_arguments, const char *current_argument, size_t current_argument_len, completer_t *possibilities, void *UNUSED(data))
{
    if (!fetch_applications(NULL)) {
        return FALSE;
    }

    return complete_from_hashtable_keys(parsed_arguments, current_argument, current_argument_len, possibilities, applications);
}

static void me_regcomm(graph_t *g)
{
    argument_t *lit_me;

    lit_me = argument_create_literal("me", me, _("display your personal informations"));

    graph_create_full_path(g, lit_me, NULL);
    // credentials ...
    {
        argument_t *lit_crendentials, *lit_list, *lit_flush;

        lit_crendentials = argument_create_literal("credentials", NULL, NULL);
        lit_list = argument_create_literal("list", credentials_list, _("list all credentials"));
        lit_flush = argument_create_literal("flush", credentials_flush, _("revoke all credentials"));

        graph_create_full_path(g, lit_crendentials, lit_list, NULL);
        graph_create_full_path(g, lit_crendentials, lit_flush, NULL);
    }
    // me application ...
    {
        argument_t *arg_application;
        argument_t *lit_application, *lit_list, *lit_delete;

        lit_application = argument_create_literal("application", NULL, NULL);
        lit_list = argument_create_literal("list", application_list, _("list applications you have created"));
        lit_delete = argument_create_literal("delete", application_delete, _("delete one of your own applications"));

        arg_application = argument_create_string(offsetof(me_argument_t, application), "<application>", complete_application_name, NULL);

        graph_create_full_path(g, lit_me, lit_application, lit_list, NULL);
        graph_create_full_path(g, lit_me, lit_application, arg_application, lit_delete, NULL);
    }
}

static void me_register_rules(json_value_t rules, bool ro)
{
    JSON_ADD_RULE(rules, "GET", "/me");
    JSON_ADD_RULE(rules, "GET", "/me/*");
    if (!ro) {
        JSON_ADD_RULE(rules, "PUT", "/me/*");
        JSON_ADD_RULE(rules, "POST", "/me/*");
        JSON_ADD_RULE(rules, "DELETE", "/me/*");
    }
}

DECLARE_MODULE(me) = {
    "me",
    me_regcomm,
    me_register_rules,
    me_ctor,
    NULL,
    me_dtor
};
