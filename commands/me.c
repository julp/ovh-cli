#include <inttypes.h>
#include <string.h>

#include "common.h"
#include "json.h"
#include "table.h"
#include "modules/api.h"
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
static command_status_t me(void *UNUSED(arg), error_t **error)
{
    bool success;
    request_t *req;
    json_document_t *doc;

    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/me");
    success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
    request_destroy(req);
    if (success) {
        table_t *t;
        Iterator it;
        json_value_t root;

        t = table_new(2, "Attribute", TABLE_TYPE_STRING, "Value", TABLE_TYPE_STRING);
        root = json_document_get_root(doc);
        json_object_to_iterator(&it, root);
        for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
            const char *key;
            json_value_t v;

            v = (json_value_t) iterator_current(&it, (void **) &key);
            table_store(t, key, v == json_null ? NULL : json_get_string(v));
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

#if 0
    // credential status
    _("expired")
    _("pendingValidation")
    _("refused")
    _("validated")
    // application status
    _("active")
    _("blocked")
    _("inactive")
    _("trusted")
#endif
static command_status_t me_credential_list(void *UNUSED(arg), error_t **error)
{
    bool success;
    request_t *req;
    json_document_t *doc;

    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/me/api/credential");
    success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
    request_destroy(req);
    if (success) {
        Iterator it;
        HashTable *applications;

        applications = hashtable_new(NULL, value_equal, NULL, NULL, application_destroy);
        json_array_to_iterator(&it, json_document_get_root(doc));
        for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
            json_value_t v;
            json_document_t *doc;
            int64_t credentialId;

            v = (json_value_t) iterator_current(&it, NULL);
            credentialId = json_get_integer(v);
            req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/me/api/credential/%" PRIu32, credentialId);
            success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
            request_destroy(req);
            if (success) {
                json_value_t root;
                application_t *app;
                int64_t applicationId;

                root = json_document_get_root(doc);
/*
{
    ovhSupport: false
    status: "validated"
    applicationId: 168
    credentialId: 1414498
    rules: [
        {
            method: "GET"
            path: "/*"
        }
        {
            method: "POST"
            path: "/*"
        }
        {
            method: "PUT"
            path: "/*"
        }
        {
            method: "DELETE"
            path: "/*"
        }
    ]
    lastUse: "2014-10-27T15:44:19+01:00"
    expiration: "2014-10-28T15:35:58+01:00"
    creation: "2014-10-27T15:35:37+01:00"
}
*/
                app = NULL;
                json_object_get_property(root, "applicationId", &v);
                applicationId = json_get_integer(v);
                json_document_destroy(doc);
                if (!hashtable_quick_get(applications, (hash_t) applicationId, applicationId, (void **) &app)) {
                    req = request_new(REQUEST_FLAG_SIGN | REQUEST_FLAG_IGNORE_404, HTTP_GET, NULL, API_BASE_URL "/me/api/credential/%" PRIu32 "/application", credentialId);
                    success = request_execute(req, RESPONSE_JSON, (void **) &doc, error); // si l'application n'existe plus, on obtient une 404
                    request_destroy(req);
                    if (success) {
                        if (200L == request_response_status(req)) {
                            app = mem_new(*app);
                            root = json_document_get_root(doc);
                            json_object_get_property(root, "name", &v);
                            app->name = strdup(json_get_string(v));
                            json_object_get_property(root, "description", &v);
                            app->description = strdup(json_get_string(v));
/*
{
    status: one of ["active", "blocked", "inactive", "trusted"]
    name: "Ovh_Console"
    applicationId: 168
    description: "OVH API console"
    applicationKey: "***"

}
*/
                            json_document_destroy(doc);
                        }
                    }
                    hashtable_quick_put_ex(applications, 0, (hash_t) applicationId, applicationId, app, NULL);
                }
                printf(
                    "credentialId = %" PRIu32 " | applicationId = %" PRIi32 " | name = %s | description %s\n",
                    credentialId,
                    applicationId,
                    NULL == app ? "-" : app->name,
                    NULL == app ? "-" : app->description
                );
            }
        }
        iterator_close(&it);
        json_document_destroy(doc);
        hashtable_destroy(applications);
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t me_credential_flush(void *UNUSED(arg), error_t **error)
{
    bool success;
    request_t *req;
    json_document_t *doc;

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
