#include <string.h>
#include <inttypes.h>

#include "common.h"
#include "command.h"
#include "date.h"
#include "util.h"
#include "table.h"
#include "modules/api.h"
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
    bool default_key;
    const char *key;
} ssh_key_t; /* key_t is already used by ftok */

typedef struct {
    bool on_off;
    bool nocache;
    const char *name;
    const char *value;
} key_argument_t;

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

static void key_destroy(void *data)
{
    ssh_key_t *k;

    assert(NULL != data);

    k = (ssh_key_t *) data;
    FREE(k, key);
    free(k);
}

static ssh_key_t *key_new(void)
{
    ssh_key_t *k;

    k = mem_new(*k);
    INIT(k, key);
    k->default_key = FALSE;

    return k;
}

static void key_on_set_account(void **data)
{
    if (NULL == *data) {
        key_set_t *ks;

        ks = mem_new(*ks);
        ks->uptodate = FALSE;
        ks->keys = hashtable_ascii_cs_new((DupFunc) strdup, free, key_destroy);
        *data = ks;
    }
}

static bool key_ctor(error_t **UNUSED(error))
{
    account_register_module_callbacks(MODULE_NAME, key_set_destroy, key_on_set_account);

    return TRUE;
}

static command_status_t fetch_keys(key_set_t *ks, bool force, error_t **error)
{
    if (!ks->uptodate || force) {
        request_t *req;
        bool request_success;
        json_document_t *doc;

        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/me/sshKey");
        request_success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (request_success) {
            Iterator it;
            json_value_t root;

            root = json_document_get_root(doc);
            hashtable_clear(ks->keys);
            json_array_to_iterator(&it, root);
            for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
                ssh_key_t *k;
                request_t *req;
                json_value_t v;
                json_value_t root;
                json_document_t *doc;

                k = key_new();
                v = (json_value_t) iterator_current(&it, NULL);
                hashtable_put(ks->keys, 0, json_get_string(v), k, NULL); // ks->keys has strdup as key_duper, don't need to strdup it ourself
                req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/me/sshKey/%s", json_get_string(v));
                request_execute(req, RESPONSE_JSON, (void **) &doc, error);
                request_destroy(req);
                root = json_document_get_root(doc);
                JSON_GET_PROP_BOOL(root, "default", k->default_key);
                JSON_GET_PROP_STRING(root, "key", k->key);
                json_document_destroy(doc);
            }
            iterator_close(&it);
            ks->uptodate = TRUE;
            json_document_destroy(doc);
        } else {
            return COMMAND_FAILURE;
        }
    }

    return COMMAND_SUCCESS;
}

// key list
static command_status_t key_list(COMMAND_ARGS)
{
    table_t *t;
    Iterator it;
    key_set_t *ks;
    command_status_t ret;
    key_argument_t *args;

    USED(mainopts);
    args = (key_argument_t *) arg;
    FETCH_ACCOUNT_KEYS(ks);
    // populate
    if (COMMAND_SUCCESS != (ret = fetch_keys(ks, args->nocache, error))) {
        return ret;
    }
    // display
    t = table_new(
        3,
        _("name"), TABLE_TYPE_STRING,
        _("default"), TABLE_TYPE_BOOLEAN,
        _("key"), TABLE_TYPE_STRING
    );
    hashtable_to_iterator(&it, ks->keys);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        ssh_key_t *k;
        const char *name;

        k = iterator_current(&it, (void **) &name);
        table_store(t, name, k->default_key, k->key);
    }
    iterator_close(&it);
    table_display(t, TABLE_FLAG_NONE);
    table_destroy(t);

    return COMMAND_SUCCESS;
}

// key <name> add <value>
static command_status_t key_add(COMMAND_ARGS)
{
    bool success;
    key_set_t *ks;
    request_t *req;
    key_argument_t *args;
    json_document_t *reqdoc;

    USED(mainopts);
    FETCH_ACCOUNT_KEYS(ks);
    args = (key_argument_t *) arg;
    assert(NULL != args->name);
    assert(NULL != args->value);
    reqdoc = json_document_new();
    {
        json_value_t root;

        root = json_object();
        json_object_set_property(root, "key", json_string(args->value));
        json_object_set_property(root, "keyName", json_string(args->name));
        json_document_set_root(reqdoc, root);
    }
    req = request_new(REQUEST_FLAG_SIGN | REQUEST_FLAG_JSON, HTTP_POST, reqdoc, API_BASE_URL "/me/sshKey");
    success = request_execute(req, RESPONSE_IGNORE, NULL, error);
    request_destroy(req);
    json_document_destroy(reqdoc);
    if (success) {
        ssh_key_t *k;

        k = key_new();
        k->key = strdup(args->value);
        hashtable_put(ks->keys, 0, args->name, k, NULL);
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
        req = request_new(REQUEST_FLAG_SIGN, HTTP_DELETE, NULL, API_BASE_URL "/me/sshKey/%s", args->name);
        success = request_execute(req, RESPONSE_IGNORE, NULL, error);
        request_destroy(req);
        if (success) {
            hashtable_delete(ks->keys, args->name, TRUE);
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
    req = request_new(REQUEST_FLAG_SIGN | REQUEST_FLAG_JSON, HTTP_PUT, reqdoc, API_BASE_URL "/me/sshKey/%s", args->name);
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
                k->default_key = FALSE;
            }
            iterator_close(&it);
        }
        if (hashtable_get(ks->keys, args->name, &k)) {
            k->default_key = args->on_off;
        }
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static bool complete_key_name(void *parsed_arguments, const char *current_argument, size_t current_argument_len, DPtrArray *possibilities, void *UNUSED(data))
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

    lit_key = argument_create_literal("key", NULL);
    lit_key_add = argument_create_literal("add", key_add);
    lit_key_list = argument_create_literal("list", key_list);
    lit_key_delete = argument_create_literal("delete", key_delete);
    lit_key_default = argument_create_literal("default", NULL);

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
    NULL
};
