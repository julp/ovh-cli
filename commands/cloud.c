#include <string.h>
#include <inttypes.h>

#include "common.h"
#include "command.h"
#include "date.h"
#include "util.h"
#include "modules/api.h"
#include "modules/table.h"
#include "commands/account.h"
#include "struct/hashtable.h"

#define MODULE_NAME "cloud"

#define FETCH_ACCOUNT_CLOUDS(/*cloud_set **/ cs) \
    do { \
        cs = NULL; \
        account_current_get_data(MODULE_NAME, (void **) &cs); \
        assert(NULL != cs); \
    } while (0);

typedef struct {
    bool uptodate;
    HashTable *clouds; // projects?
} cloud_set_t;

typedef struct {
    char x;
} cloud_t; // project_t?

typedef struct {
    const char *name;
} project_argument_t;

static cloud_t *cloud_new(void)
{
    cloud_t *c;

    c = mem_new(*c);

    return c;
}

static void cloud_destroy(void *data)
{
    cloud_t *c;

    assert(NULL != data);

    c = (cloud_t *) data;
    free(c);
}

static void cloud_set_destroy(void *data)
{
    cloud_set_t *cs;

    assert(NULL != data);
    cs = (cloud_set_t *) data;
    if (NULL != cs->clouds) {
        hashtable_destroy(cs->clouds);
    }
    free(cs);
}

static void cloud_on_set_account(void **data)
{
    if (NULL == *data) {
        cloud_set_t *cs;

        cs = mem_new(*cs);
        cs->uptodate = FALSE;
        cs->clouds = hashtable_ascii_cs_new((DupFunc) strdup, free, cloud_destroy);
        *data = cs;
    }
}

static bool cloud_ctor(error_t **UNUSED(error))
{
    account_register_module_callbacks(MODULE_NAME, cloud_set_destroy, cloud_on_set_account);

    return TRUE;
}

static bool fetch_project(cloud_t *c, const char *project_name, error_t **error)
{
    bool success;
    request_t *req;
    json_document_t *doc;

    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/cloud/project/%s", project_name);
    success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
    request_destroy(req);
    if (success) {
        // TODO: parse
        json_document_destroy(doc);
    }

    return success;
}

static bool fetch_projects(cloud_set_t *cs, bool force, error_t **error)
{
    Iterator it;
    bool success;
    request_t *req;
    json_document_t *doc;

    success = TRUE;
    if (!cs->uptodate || force) {
        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/cloud/project");
        success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (success) {
            json_value_t root;

            hashtable_clear(cs->clouds);
            root = json_document_get_root(doc);
            json_array_to_iterator(&it, root);
            for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
                cloud_t *c;
                json_value_t v;

                c = cloud_new();
                v = (json_value_t) iterator_current(&it, NULL);
                hashtable_put(cs->clouds, 0, json_get_string(v), c, NULL); // cs->clouds has strdup as key_duper, don't need to strdup it ourself
#if 0 /* temporary disabled to not slow down tests and make useless HTTP requests */
                success &= fetch_project(c, json_get_string(v), error);
#endif
            }
            iterator_close(&it);
            json_document_destroy(doc);
            cs->uptodate = TRUE;
        }
    }

    return success;
}

static command_status_t cloud_list(COMMAND_ARGS) // project(s)_list?
{
    cloud_set_t *cs;

    USED(arg);
    USED(mainopts);
    FETCH_ACCOUNT_CLOUDS(cs);
    // populate
    if (!fetch_projects(cs, FALSE, error)) {
        return COMMAND_FAILURE;
    }
    // display
    hashtable_puts_keys(cs->clouds);

    return COMMAND_SUCCESS;
}

static const char * const status[] = {
    "ACTIVE",
    "BUILDING",
    "DELETED",
    "ERROR",
    "HARD_REBOOT",
    "PASSWORD",
    "PAUSED",
    "REBOOT",
    "REBUILD",
    "RESCUED",
    "RESIZED",
    "REVERT_RESIZE",
    "SOFT_DELETED",
    "STOPPED",
    "SUSPENDED",
    "UNKNOWN",
    "VERIFY_RESIZE",
    "MIGRATING",
    "RESIZE",
    "BUILD",
    "SHUTOFF",
    "RESCUE",
    "SHELVED",
    "SHELVED_OFFLOADED",
    NULL
};

typedef struct {
    int status;
    char *name;
    char *region;
    char *imageId;
    time_t created;
    char *flavorId;
    char *sshKeyId;
    // monthlyBilling (json object with 2 properties ?)
    // ipAddresses (array of json object with 2 properties ?)
    char *id;
} instance_t;

static command_status_t instance_list(COMMAND_ARGS) // instances?
{
    bool success;
    project_argument_t *args;

    USED(mainopts);
    args = (project_argument_t *) arg;
    {
        table_t *t;
        request_t *req;
        json_document_t *doc;

        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/cloud/project/%s/instance", args->name);
        success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (success) {
            Iterator it;
            json_value_t root;
            instance_t instance;

            t = table_new(
                8,
                _("status"), TABLE_TYPE_ENUM, status,
                _("name"), TABLE_TYPE_STRING,
                _("region"), TABLE_TYPE_STRING,
                _("imageId"), TABLE_TYPE_STRING,
                _("created"), TABLE_TYPE_DATETIME,
                _("flavorId"), TABLE_TYPE_STRING,
                _("sshKeyId"), TABLE_TYPE_STRING,
                _("id"), TABLE_TYPE_STRING
            );
            // NOTE: GET /cloud/project/{serviceName}/instance/{instanceId} gives the same informations but include data of image and flavor instead of their ID ?
            root = json_document_get_root(doc);
            json_array_to_iterator(&it, root);
            for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
                json_value_t v, propvalue;

                v = (json_value_t) iterator_current(&it, NULL);
                json_object_get_property(v, "status", &propvalue);
                instance.status = json_get_enum(v, status, -1);
                JSON_GET_PROP_STRING_EX(v, "name", instance.name, FALSE);
                JSON_GET_PROP_STRING_EX(v, "region", instance.region, FALSE);
                JSON_GET_PROP_STRING_EX(v, "imageId", instance.imageId, FALSE);
                json_object_get_property(v, "created", &propvalue);
                date_parse_to_timestamp(json_get_string(propvalue), "%F", &instance.created);
                JSON_GET_PROP_STRING_EX(v, "flavorId", instance.flavorId, FALSE);
                JSON_GET_PROP_STRING_EX(v, "sshKeyId", instance.sshKeyId, FALSE);
                JSON_GET_PROP_STRING_EX(v, "id", instance.id, FALSE);
                table_store(t, instance.status, instance.name, instance.region, instance.imageId, instance.created, instance.flavorId, instance.sshKeyId, instance.id);
            }
            iterator_close(&it);
            table_display(t, TABLE_FLAG_NONE);
            table_destroy(t);
            json_document_destroy(doc);
        }
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

typedef struct {
    char *visibility; // enum in fact even if OVH document it as string ?
    time_t creationDate;
    char *status; // enum in fact even if OVH document it as string ?
    char *name;
    char *region; // enum in fact even if OVH document it as string ?
    char *id;
    char *type; // enum in fact even if OVH document it as string ?
    int minDisk;
} image_t;

static command_status_t image_list(COMMAND_ARGS) // images?
{
    bool success;
    project_argument_t *args;

    USED(mainopts);
    args = (project_argument_t *) arg;
    {
        table_t *t;
        request_t *req;
        json_document_t *doc;

        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/cloud/project/%s/image", args->name);
        success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (success) {
            Iterator it;
            image_t image;
            json_value_t root;

            t = table_new(
                8,
                _("visibility"), TABLE_TYPE_STRING,
                _("creationDate"), TABLE_TYPE_DATETIME,
                _("status"), TABLE_TYPE_STRING,
                _("name"), TABLE_TYPE_STRING,
                _("region"), TABLE_TYPE_STRING,
                _("id"), TABLE_TYPE_STRING,
                _("type"), TABLE_TYPE_STRING,
                _("minDisk"), TABLE_TYPE_INT
            );
            // NOTE: GET /cloud/project/{serviceName}/image/{imageId} seems to only add minRam information
            root = json_document_get_root(doc);
            json_array_to_iterator(&it, root);
            for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
                json_value_t v, propvalue;

                v = (json_value_t) iterator_current(&it, NULL);
                JSON_GET_PROP_STRING_EX(v, "visibility", image.visibility, FALSE);
                json_object_get_property(v, "creationDate", &propvalue);
                date_parse_to_timestamp(json_get_string(propvalue), "%F", &image.creationDate);
                JSON_GET_PROP_STRING_EX(v, "status", image.status, FALSE);
                JSON_GET_PROP_STRING_EX(v, "name", image.name, FALSE);
                JSON_GET_PROP_STRING_EX(v, "region", image.region, FALSE);
                JSON_GET_PROP_STRING_EX(v, "id", image.id, FALSE);
                JSON_GET_PROP_STRING_EX(v, "type", image.type, FALSE);
                JSON_GET_PROP_INT(v, "minDisk", image.minDisk);
                table_store(t, image.visibility, image.creationDate, image.status, image.name, image.region, image.id, image.type, image.minDisk);
            }
            iterator_close(&it);
            table_display(t, TABLE_FLAG_NONE);
            table_destroy(t);
            json_document_destroy(doc);
        }
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static bool complete_projects(void *parsed_arguments, const char *current_argument, size_t current_argument_len, DPtrArray *possibilities, void *UNUSED(data))
{
    cloud_set_t *cs;

    FETCH_ACCOUNT_CLOUDS(cs);
    if (!fetch_projects(cs, FALSE, NULL)) {
        return FALSE;
    }

    return complete_from_hashtable_keys(parsed_arguments, current_argument, current_argument_len, possibilities, cs->clouds);
}

static void cloud_regcomm(graph_t *g)
{
    argument_t *arg_project;
    argument_t *lit_cloud, *lit_list;

    lit_cloud = argument_create_literal("cloud", NULL);
    lit_list = argument_create_literal("list", cloud_list);

    arg_project = argument_create_string(offsetof(project_argument_t, name), "<project>", complete_projects, NULL);

    graph_create_full_path(g, lit_cloud, lit_list, NULL);

    // instances
    {
        argument_t *lit_instance, *lit_list;

        lit_instance = argument_create_literal("instance", NULL);
        lit_list = argument_create_literal("list", instance_list);

        graph_create_full_path(g, lit_cloud, arg_project, lit_instance, lit_list, NULL);
    }
    // images
    {
        argument_t *lit_image, *lit_list;

        lit_image = argument_create_literal("image", NULL);
        lit_list = argument_create_literal("list", image_list);

        graph_create_full_path(g, lit_cloud, arg_project, lit_image, lit_list, NULL);
    }
}

static void cloud_register_rules(json_value_t rules, bool ro)
{
    JSON_ADD_RULE(rules, "GET", "/cloud");
    JSON_ADD_RULE(rules, "GET", "/cloud/*");
    if (!ro) {
        JSON_ADD_RULE(rules, "PUT", "/cloud/*");
        JSON_ADD_RULE(rules, "POST", "/cloud/*");
        JSON_ADD_RULE(rules, "DELETE", "/cloud/*");
    }
}

DECLARE_MODULE(cloud) = {
    MODULE_NAME,
    cloud_regcomm,
    cloud_register_rules,
    cloud_ctor,
    NULL,
    NULL
};
