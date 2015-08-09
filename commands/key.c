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

#define MODULE_NAME "key"

#define FETCH_ACCOUNT_KEYS(/*key_set_t **/ ks) \
    do { \
        ks = NULL; \
        account_current_get_data(MODULE_NAME, (void **) &ks); \
        assert(NULL != ks); \
    } while (0);

typedef struct {
    bool uptodate;
    HashTable *keys;
} key_set_t;

typedef struct {
    DECL_MEMBER_BOOL(isDefault);
    DECL_MEMBER_STRING(keyName);
    DECL_MEMBER_STRING(key);
} ssh_key_t; /* key_t is already used by ftok */

typedef struct {
    bool on_off;
    bool nocache;
    const char *name;
    const char *value;
} key_argument_t;

#undef DECL_FIELD_STRUCT_NAME
#define DECL_FIELD_STRUCT_NAME ssh_key_t
static model_field_t key_fields[] = {
//     DECL_FIELD_BOOL(N_("default"), default, 0),
    DECL_FIELD_SIMPLE(N_("default"), "default", isDefault, MODEL_TYPE_BOOL, 0),
    DECL_FIELD_STRING(N_("keyName"), keyName, MODEL_FLAG_PRIMARY),
    DECL_FIELD_STRING(N_("key"), key, 0),
    MODEL_FIELD_SENTINEL
};

static model_t *key_model;

typedef struct {
    const char *module_name;
    size_t hashtable_offset;
//     size_t uptodate_offset;
} hashtable_backend_data_t;

static void *hashtable_backend_init(const model_t *UNUSED(model), error_t **UNUSED(error))
{
    hashtable_backend_data_t *hbd;

    hbd = mem_new(*hbd);
    hbd->module_name = MODULE_NAME;
    hbd->hashtable_offset = offsetof(key_set_t, keys);
//     hbd->uptodate_offset = offsetof(key_set_t, uptodate);

    return hbd;
}

static void hashtable_backend_free(void *data)
{
    free(data);
}

static bool hashtable_backend_save(modelized_t *obj, void *data, error_t **UNUSED(error))
{
    void *p;
    bool put;
    hashtable_backend_data_t *hbd;

    p = NULL;
    put = FALSE;
    hbd = (hashtable_backend_data_t *) data;
    account_current_get_data(hbd->module_name, (void **) &p);
    if (NULL != p) {
        char name[MAX_MODELIZED_NAME_LENGTH];

        modelized_name_to_s(obj, name, ARRAY_SIZE(name));
        put = hashtable_put(VOIDP_TO_X(p, hbd->hashtable_offset, HashTable *), 0, name, obj, NULL);
//         if (!put) {
//             error_set(); // ?
//         }
    }

    return put;
}

static bool hashtable_backend_delete(modelized_t *obj, void *data, error_t **UNUSED(error))
{
    void *p;
    hashtable_backend_data_t *hbd;

    p = NULL;
    hbd = (hashtable_backend_data_t *) data;
    account_current_get_data(hbd->module_name, (void **) &p);
    if (NULL != p) {
        char name[MAX_MODELIZED_NAME_LENGTH];

        modelized_name_to_s(obj, name, ARRAY_SIZE(name));
        return hashtable_delete(VOIDP_TO_X(p, hbd->hashtable_offset, HashTable *), name, TRUE);
    }

    return FALSE;
}

static bool hashtable_backend_all(Iterator *it, const model_t *UNUSED(model), void *data, error_t **UNUSED(error))
{
    void *p;
    hashtable_backend_data_t *hbd;

    p = NULL;
    hbd = (hashtable_backend_data_t *) data;
    account_current_get_data(hbd->module_name, (void **) &p);
    if (NULL != p) {
        hashtable_to_iterator(it, VOIDP_TO_X(p, hbd->hashtable_offset, HashTable *));
    }

    return NULL != p;
}

model_backend_t hashtable_backend = {
    hashtable_backend_init,
    hashtable_backend_free,
    hashtable_backend_all,
    hashtable_backend_save,
    hashtable_backend_delete,
};

static void key_set_destroy(void *data)
{
    key_set_t *ks;

    assert(NULL != data);
    ks = (key_set_t *) data;
    if (NULL != ks->keys) {
        hashtable_destroy(ks->keys);
    }
    free(ks);
}

static void key_on_set_account(void **data)
{
    if (NULL == *data) {
        key_set_t *ks;

        ks = mem_new(*ks);
        ks->uptodate = FALSE;
        ks->keys = hashtable_ascii_cs_new(NULL, NULL, (DtorFunc) modelized_destroy);
        *data = ks;
    }
}

static bool key_ctor(error_t **error)
{
    key_model = model_new("keys", sizeof(ssh_key_t), key_fields, ARRAY_SIZE(key_fields) - 1, "keyName", &hashtable_backend, error);

    account_register_module_callbacks(MODULE_NAME, key_set_destroy, key_on_set_account);

    return TRUE;
}

static void key_dtor(void)
{
    model_destroy(key_model);
}

static bool fetch_keys(key_set_t *ks, bool force, error_t **error)
{
    bool success;

    success = TRUE;
    if (!ks->uptodate || force) {
        request_t *req;
        json_document_t *doc;

        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/me/sshKey");
        success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (success) {
            Iterator it;
            json_value_t root;

            root = json_document_get_root(doc);
            hashtable_clear(ks->keys);
            json_array_to_iterator(&it, root);
            for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
                ssh_key_t *k;
                request_t *req;
                json_value_t v;
                json_document_t *doc;

                k = (ssh_key_t *) modelized_new(key_model);
                MODELIZED_SET_STRING(k, keyName, json_get_string(v));
                v = (json_value_t) iterator_current(&it, NULL);
                hashtable_put(ks->keys, 0, k->keyName, k, NULL);
                req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/me/sshKey/%s", k->keyName);
                success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
                request_destroy(req);
                json_object_to_modelized(json_document_get_root(doc), (modelized_t *) k, FALSE);
                json_document_destroy(doc);
            }
            iterator_close(&it);
            ks->uptodate = TRUE;
            json_document_destroy(doc);
        }
    }

    return success;
}

// key list
static command_status_t key_list(COMMAND_ARGS)
{
    key_set_t *ks;
    key_argument_t *args;

    USED(mainopts);
    args = (key_argument_t *) arg;
    FETCH_ACCOUNT_KEYS(ks);
    // populate
    if (!fetch_keys(ks, args->nocache, error)) {
        return COMMAND_FAILURE;
    }
    // display
    return model_to_table(key_model, error);
}

// key <name> add <value>
static command_status_t key_add(COMMAND_ARGS)
{
    bool success;
    ssh_key_t *k;
    key_set_t *ks;
    request_t *req;
    key_argument_t *args;
    json_document_t *reqdoc;

    USED(mainopts);
    success = TRUE;
    FETCH_ACCOUNT_KEYS(ks);
    args = (key_argument_t *) arg;
    assert(NULL != args->name);
    assert(NULL != args->value);
    k = (ssh_key_t *) modelized_new(key_model);
    MODELIZED_SET_STRING(k, key, args->value);
    MODELIZED_SET_STRING(k, keyName, args->name);
    if (NULL != (reqdoc = json_object_from_modelized((modelized_t *) k))) {
        req = request_new(REQUEST_FLAG_SIGN | REQUEST_FLAG_JSON, HTTP_POST, reqdoc, error, API_BASE_URL "/me/sshKey");
        success = request_execute(req, RESPONSE_IGNORE, NULL, error);
        request_destroy(req);
        json_document_destroy(reqdoc);
        if (success) {
            success = modelized_save((modelized_t *) k, error);
        }
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

// key <name> delete
static command_status_t key_delete(COMMAND_ARGS)
{
    bool success;
    key_set_t *ks;
    request_t *req;
    key_argument_t *args;

    success = TRUE;
    FETCH_ACCOUNT_KEYS(ks);
    args = (key_argument_t *) arg;
    assert(NULL != args->name);
    if (confirm(mainopts, _("Confirm deletion of global SSH key named '%s'"), args->name)) {
        req = request_new(REQUEST_FLAG_SIGN, HTTP_DELETE, NULL, error, API_BASE_URL "/me/sshKey/%s", args->name);
        success = request_execute(req, RESPONSE_IGNORE, NULL, error);
        request_destroy(req);
        if (success) {
            hashtable_delete(ks->keys, args->name, TRUE); // TODO: modelized_delete();
        }
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

// key <name> default <on/off>
static command_status_t key_default(COMMAND_ARGS)
{
    bool success;
    key_set_t *ks;
    request_t *req;
    key_argument_t *args;
    json_document_t *reqdoc;

    USED(mainopts);
    success = TRUE;
    FETCH_ACCOUNT_KEYS(ks);
    args = (key_argument_t *) arg;
    assert(NULL != args->name);
    reqdoc = json_document_new();
    {
        json_value_t root;

        root = json_object();
        json_object_set_property(root, "default", args->on_off ? json_true : json_false);
        json_document_set_root(reqdoc, root);
    }
    req = request_new(REQUEST_FLAG_SIGN | REQUEST_FLAG_JSON, HTTP_PUT, reqdoc, error, API_BASE_URL "/me/sshKey/%s", args->name);
    success = request_execute(req, RESPONSE_IGNORE, NULL, error);
    request_destroy(req);
    json_document_destroy(reqdoc);
    if (success) {
        ssh_key_t *k;

        if (args->on_off) {
            Iterator it;

            hashtable_to_iterator(&it, ks->keys);
            for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
                k = iterator_current(&it, NULL);
                MODELIZED_SET(k, isDefault, FALSE);
            }
            iterator_close(&it);
        }
        if (hashtable_get(ks->keys, args->name, &k)) { // TODO: find_by_name
            MODELIZED_SET(k, isDefault, args->on_off);
        }
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static bool complete_key_name(void *parsed_arguments, const char *current_argument, size_t current_argument_len, completer_t *possibilities, void *UNUSED(data))
{
    key_set_t *ks;

    FETCH_ACCOUNT_KEYS(ks);
    if (COMMAND_SUCCESS != fetch_keys(ks, FALSE, NULL)) {
        return FALSE;
    }

    return complete_from_hashtable_keys(parsed_arguments, current_argument, current_argument_len, possibilities, ks->keys);
}

static void key_regcomm(graph_t *g)
{
    argument_t *arg_name, *arg_value, *arg_on_off;
    argument_t *lit_key, *lit_key_add, *lit_key_list, *lit_key_delete, *lit_key_default;

    lit_key = argument_create_literal("key", NULL, NULL);
    lit_key_add = argument_create_literal("add", key_add, _("upload a new global SSH key to OVH"));
    lit_key_list = argument_create_literal("list", key_list, _("list global SSH keys"));
    lit_key_delete = argument_create_literal("delete", key_delete, _("delete a global SSH key"));
    lit_key_default = argument_create_literal("default", NULL, _("(un)define the global SSH key used in rescue mode"));

    arg_value = argument_create_string(offsetof(key_argument_t, value), "<value>", NULL, NULL);
    arg_name = argument_create_string(offsetof(key_argument_t, name), "<name>", complete_key_name, NULL);
    arg_on_off = argument_create_choices_off_on(offsetof(key_argument_t, on_off), key_default);

    graph_create_full_path(g, lit_key, lit_key_list, NULL);
    graph_create_full_path(g, lit_key, arg_name, lit_key_delete, NULL);
    graph_create_full_path(g, lit_key, arg_name, lit_key_add, arg_value, NULL);
    graph_create_full_path(g, lit_key, arg_name, lit_key_default, arg_on_off, NULL);
}

static void key_register_rules(json_value_t rules, bool ro)
{
    JSON_ADD_RULE(rules, "GET", "/me/sshKey");
    JSON_ADD_RULE(rules, "GET", "/me/sshKey/*");
    if (!ro) {
        JSON_ADD_RULE(rules, "PUT", "/me/sshKey/*");
        JSON_ADD_RULE(rules, "POST", "/me/sshKey/*");
        JSON_ADD_RULE(rules, "DELETE", "/me/sshKey/*");
    }
}

DECLARE_MODULE(key) = {
    MODULE_NAME,
    key_regcomm,
    key_register_rules,
    key_ctor,
    NULL,
    key_dtor
};
