#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "common.h"
#include "date.h"
#include "table.h"
#include "modules/api.h"
#include "modules/libxml.h"
#include "struct/hashtable.h"

#include <limits.h>
#if !defined(MAXPATHLEN) && defined(PATH_MAX)
# define MAXPATHLEN PATH_MAX
#endif /* !MAXPATHLEN && PATH_MAX */

enum {
    DTOR_NOCALL,
    DTOR_CALL
};

typedef struct {
    char *account;
    char *password;
    time_t expires_at;
    const char *consumer_key;
    HashTable *modules_data;
    // TODO: application_key + application_secret?
} account_t;

typedef struct {
    char path[MAXPATHLEN];
    HashTable *accounts;
    account_t *autosel;
    account_t *current;
    HashTable *modules_callbacks;
} account_command_data_t;

typedef struct {
    DtorFunc dtor;
    void (*on_set_account)(void **);
} module_callbacks_t;

static account_command_data_t *acd = NULL;

#define duration_test(string, expected_result, expected_value) \
    do { \
        bool r; \
        time_t v; \
         \
        if (expected_result == (r = parse_duration(string, &v))) { \
            if (r && v != expected_value) { \
                printf("parse_duration('%s') failed (expected = %lld ; got = %lld)\n", string, (long long) expected_value, (long long) v); \
            } \
        } else { \
            printf("parse_duration('%s') failed (expected = %d ; got = %d)\n", string, expected_result, r); \
        } \
    } while (0);

static void account_notify_change(void)
{
    bool exists;
    Iterator it;

    hashtable_to_iterator(&it, acd->modules_callbacks);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        void *key, *data;
        module_callbacks_t *mc;

        mc = iterator_current(&it, &key);
        assert(NULL != mc);
        if (NULL != mc->on_set_account) {
            data = NULL;
            exists = hashtable_get(acd->current->modules_data, key, &data);
            mc->on_set_account(&data);
            if (!exists) {
                hashtable_put(acd->current->modules_data, key, data, NULL);
            }
        }
    }
    iterator_close(&it);
}

const char *account_current(void)
{
    if (NULL == acd->current) {
        return "(no current account)";
    } else {
        return acd->current->account;
    }
}

void account_current_set_data(const char *name, void *data)
{
    assert(NULL != acd->current);

    hashtable_put(acd->current->modules_data, (void *) name, data, NULL);
}

bool account_current_get_data(const char *name, void **data)
{
    assert(NULL != acd->current);

    return hashtable_get(acd->current->modules_data, name, data);
}

void account_register_module_callbacks(const char *name, DtorFunc dtor, void (*on_set_account)(void **))
{
    module_callbacks_t *mc;

    assert(NULL != acd);
    assert(NULL != name);

    if (NULL != dtor && NULL != on_set_account) { // nothing to do? Skip it!
        bool exists;

        if (!(exists = hashtable_get(acd->modules_callbacks, name, (void **) &mc))) {
            mc = mem_new(*mc);
        }
        mc->dtor = dtor;
        mc->on_set_account = on_set_account;
        if (!exists) {
            hashtable_put(acd->modules_callbacks, (void *) name, mc, NULL);
        }
    }
}

static bool account_save(error_t **error)
{
    int ret;
    Iterator it;
    xmlDocPtr doc;
    xmlNodePtr root;
    mode_t old_umask;

#define SET_PROP(node, name, value) \
    do { \
        if (NULL == xmlSetProp(node, BAD_CAST name, BAD_CAST value)) { \
            xmlFreeNode(node); \
            xmlFreeDoc(doc); \
            error_set(error, WARN, "Unable to set attribute '%s' to value '%s'", name, value); \
            return FALSE; \
        } \
    } while (0);

    doc = xmlNewDoc(BAD_CAST "1.0");
    if (NULL == (root = xmlNewNode(NULL, BAD_CAST "ovh"))) {
        error_set(error, WARN, "Unable to create XML node 'ovh'");
        return FALSE;
    }
    xmlDocSetRootElement(doc, root);
    hashtable_to_iterator(&it, acd->accounts);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        xmlNodePtr node;
        account_t *account;

        account = (account_t *) iterator_current(&it, NULL);
        if (NULL == (node = xmlNewNode(NULL, BAD_CAST "account"))) {
            xmlFreeDoc(doc);
            error_set(error, WARN, "Unable to create XML node 'account'");
            return FALSE;
        }
        SET_PROP(node, "account", account->account);
        SET_PROP(node, "password", account->password);
        if (account == acd->autosel) {
            SET_PROP(node, "default", "1");
        }
        if (NULL != account->consumer_key) {
            char buffer[512];

            SET_PROP(node, "consumer_key", account->consumer_key);
            ret = snprintf(buffer, ARRAY_SIZE(buffer), "%lld", (long long) account->expires_at);
            if (ret < 0 || ((size_t) ret) >= ARRAY_SIZE(buffer)) {
                error_set(error, WARN, "error or buffer overflow");
                return FALSE;
            }
            SET_PROP(node, "expires_at", buffer);
        }
        if (NULL == xmlAddChild(root, node)) {
            xmlFreeNode(node);
            xmlFreeDoc(doc);
            error_set(error, WARN, "Unable to add 'account' to document root");
            return FALSE;
        }
    }
    iterator_close(&it);
    old_umask = umask(077);
    ret = xmlSaveFormatFile(acd->path, doc, 1);
    umask(old_umask);
    if (-1 == ret) {
        xmlFreeDoc(doc);
        error_set(error, WARN, "Could not save file into '%s'", acd->path);
        return FALSE;
    }
    xmlFreeDoc(doc);

#undef SET_PROP

    return TRUE;
}

const char *account_key(error_t **error)
{
    assert(NULL != acd->current);

    if (NULL == acd->current) {
        error_set(error, WARN, "There is no current account");
        return NULL;
    }
    if (NULL == acd->current->consumer_key || (0 != acd->current->expires_at && acd->current->expires_at < time(NULL))) {
        if (NULL != (acd->current->consumer_key = request_consumer_key(acd->current->account, acd->current->password, &acd->current->expires_at, error))) {
            account_save(error);
        }
    }

    return acd->current->consumer_key;
}

static int account_load(error_t **error)
{
    struct stat st;

    if ((-1 != (stat(acd->path, &st)))/* && S_ISREG(st.st_mode)*/) {
        xmlDocPtr doc;
        xmlNodePtr root, n;

        xmlKeepBlanksDefault(0);
        if (NULL == (doc = xmlParseFile(acd->path))) {
            // TODO
            error_set(error, WARN, "");
            return COMMAND_FAILURE;
        }
        if (NULL == (root = xmlDocGetRootElement(doc))) {
            // TODO
            error_set(error, WARN, "");
            return COMMAND_FAILURE;
        }
        for (n = root->children; n != NULL; n = n->next) {
            account_t *a;

            if (0 != xmlStrcmp(n->name, BAD_CAST "account")) {
                continue;
            }
            a = mem_new(*a);
            a->account = xmlGetPropAsString(n, "account");
            a->password = xmlGetPropAsString(n, "password");
            a->consumer_key = NULL;
            a->modules_data = hashtable_ascii_cs_new(NULL, NULL, NULL); // no dup/dtor for keys as they are "static" strings ; no dtor for values, we need to do it ourselves
            if (NULL != xmlHasProp(n, BAD_CAST "consumer_key")) {
                char *expires_at;

                a->consumer_key = xmlGetPropAsString(n, "consumer_key");
                if ('\0' == *a->consumer_key) {
                    FREE(a, consumer_key);
                } else {
                    expires_at = xmlGetPropAsString(n, "expires_at");
                    a->expires_at = (time_t) atol(expires_at);
                    free(expires_at);
                }
            }
            hashtable_put(acd->accounts, a->account, a, NULL);
            if (xmlHasProp(n, BAD_CAST "default")) {
                acd->current = acd->autosel = a;
                account_notify_change();
            }
        }
        if (hashtable_size(acd->accounts) > 0) {
            if (NULL == acd->current) {
                acd->current = (account_t *) hashtable_first(acd->accounts);
                account_notify_change();
            }
            acd->current->consumer_key = account_key(error); // TODO: error checking?
        }
        xmlFreeDoc(doc);
    }

    return COMMAND_SUCCESS;
}

static void account_account_dtor(void *data)
{
    account_t *account;

    assert(NULL != data);

    account = (account_t *) data;
    FREE(account, account);
    FREE(account, password);
    FREE(account, consumer_key);
    if (NULL != account->modules_data) {
        if (NULL != acd->modules_callbacks) {
            Iterator it;

            hashtable_to_iterator(&it, account->modules_data);
            for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
                void *key, *value;
                module_callbacks_t *mc;

                value = iterator_current(&it, &key);
                if (hashtable_get(acd->modules_callbacks, key, (void **) &mc)) {
                    mc->dtor(value);
                }
            }
            iterator_close(&it);
        }
        hashtable_destroy(account->modules_data);
    }
    free(account);
}

static bool account_early_init(void)
{
    char *home;

    acd = mem_new(*acd);
    acd->autosel = acd->current = NULL;
    acd->accounts = hashtable_ascii_cs_new(NULL, NULL /* no dtor for key as it is also part of the value */, account_account_dtor);
    acd->modules_callbacks = hashtable_ascii_cs_new(NULL, NULL /* no dtor for key as it is also part of the value */, free);
    if (NULL == (home = getenv("HOME"))) {
# ifdef _MSC_VER
#  ifndef CSIDL_PROFILE
#   define CSIDL_PROFILE 40
#  endif /* CSIDL_PROFILE */
        if (NULL == (home = getenv("USERPROFILE"))) {
            HRESULT hr;
            LPITEMIDLIST pidl = NULL;

            hr = SHGetSpecialFolderLocation(NULL, CSIDL_PROFILE, &pidl);
            if (S_OK == hr) {
                SHGetPathFromIDList(pidl, buffer);
                home = buffer;
                CoTaskMemFree(pidl);
            }
        }
# else
        struct passwd *pwd;

        if (NULL != (pwd = getpwuid(getuid()))) {
            home = pwd->pw_dir;
        }
# endif /* _MSC_VER */
    }

    if (NULL != home) {
        if (snprintf(acd->path, ARRAY_SIZE(acd->path), "%s%c%s", home, DIRECTORY_SEPARATOR, OVH_SHELL_CONFIG_FILE) >= (int) ARRAY_SIZE(acd->path)) {
            return FALSE;
        }
    }
#if 0
    duration_test("3 day 1 days", FALSE, 0);
    duration_test("3 seconds 1 hour", FALSE, 0);
    duration_test("12 11 hours", FALSE, 0);
    duration_test("3 days 1", FALSE, 0);
    duration_test("3 days 1 second", TRUE, 3 * DAY + 1 SECOND);
#endif

    return TRUE;
}

static bool account_late_init(void)
{
    error_t *error;

    error = NULL;
    account_load(&error);
    print_error(error);

    return TRUE;
}

typedef struct {
    char *account;
    char *password;
    bool expires_in;
    bool expires_at;
    char *expiration;
    char *consumer_key;
} account_argument_t;

static void account_dtor(void)
{
    if (NULL != acd) {
        if (NULL != acd->accounts) {
            hashtable_destroy(acd->accounts);
        }
        if (NULL != acd->modules_callbacks) {
            hashtable_destroy(acd->modules_callbacks);
        }
        free(acd);
    }
    acd = NULL;
}

static command_status_t account_list(void *UNUSED(arg), error_t **UNUSED(error))
{
    table_t *t;
    Iterator it;

    t = table_new(4, "account", TABLE_TYPE_STRING, "expiration", TABLE_TYPE_DATETIME, "current", TABLE_TYPE_BOOLEAN, "default", TABLE_TYPE_BOOLEAN);
    hashtable_to_iterator(&it, acd->accounts);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        account_t *account;
        struct tm expiration, *tm;

        account = (account_t *) iterator_current(&it, NULL);
        if (0 == account->expires_at) {
            bzero(&expiration, sizeof(expiration));
        } else {
            tm = localtime(&account->expires_at);
            expiration = *tm;
        }
        table_store(t, account->account, expiration, account == acd->current, account == acd->autosel);
    }
    iterator_close(&it);
    table_display(t, TABLE_FLAG_NONE);
    table_destroy(t);

    return COMMAND_SUCCESS;
}

/**
 * account add [nickname] [password] ([consumer key] expires in|at [date])
 *
 * NOTE:
 * - in order to not record password, use an empty string (with "")
 * - default expiration of consumer key is 0 (unlimited)
 **/
static command_status_t account_add(void *arg, error_t **error)
{
    hash_t h;
    time_t expires_at;
    account_argument_t *args;

    expires_at = (time_t) 0;
    args = (account_argument_t *) arg;

    assert(NULL != args->account);
    assert(NULL != args->password);

    if (NULL != args->expiration) {
        if (args->expires_in) {
            if (!parse_duration(args->expiration, &expires_at)) {
                error_set(error, WARN, _("Command aborted: unable to parse duration '%s'"), args->expiration);
                return COMMAND_USAGE;
            }
            expires_at += time(NULL);
        } else if (args->expires_at) {
            char *endptr;
            struct tm ltm = { 0 };

            if (NULL == (endptr = strptime(args->expiration, "%c", &ltm))) {
                error_set(error, WARN, _("Command aborted: unable to parse expiration date '%s'"), args->expiration);
                return COMMAND_USAGE;
            }
            expires_at = mktime(&ltm);
        } else {
            assert(FALSE); // we should not reach this point if "graph" have done its job
        }
    }
    h = hashtable_hash(acd->accounts, args->account);
    // TODO: how to handle account overwrites? Don't allow it, add an update command? Keep password (if any for both) and non-expired CK (if any for both)?
#if 0
    if (hashtable_quick_contains(acd->accounts, h, account)) {
        fprintf(stderr, "An account named '%s' already exists", account);
    } else {
#else
    {
#endif
        account_t *a;

        a = mem_new(*a);
        a->account = strdup(args->account);
        a->password = strdup(args->password);
        a->consumer_key = strdup(args->consumer_key);
        a->expires_at = expires_at;
        a->modules_data = hashtable_ascii_cs_new(NULL, NULL, NULL);
//         hashtable_quick_put_ex(acd->accounts, HT_PUT_ON_DUP_KEY_PRESERVE, h, (void *) account, a, NULL);
        hashtable_quick_put_ex(acd->accounts, 0, h, (void *) a->account, a, NULL); // TODO: old value (overwrite) is not freed!
        // TODO: if this is the first account, set it as current?
        account_save(error);
    }

    return COMMAND_SUCCESS;
}

static command_status_t account_default_set(void *arg, error_t **error)
{
    int ret;
    void *ptr;
    account_argument_t *args;

    args = (account_argument_t *) arg;
    assert(NULL != args->account);
    if ((ret = hashtable_get(acd->accounts, args->account, &ptr))) {
        acd->autosel = ptr;
        account_save(error);
    } else {
        error_set(error, NOTICE, "Any account named '%s' was found", args->account);
    }

    return ret ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t account_delete(void *arg, error_t **error)
{
    int ret;
    account_argument_t *args;

    args = (account_argument_t *) arg;
    assert(NULL != args->account);
    if ((ret = hashtable_delete(acd->accounts, args->account, DTOR_CALL))) {
        account_save(error);
    } else {
        error_set(error, NOTICE, "Any account named '%s' was found", args->account);
    }

    return ret ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t account_switch(void *arg, error_t **error)
{
    int ret;
    account_t *ptr;
    account_argument_t *args;

    args = (account_argument_t *) arg;
    assert(NULL != args->account);
    if ((ret = hashtable_get(acd->accounts, args->account, (void **) &ptr))) {
        const char *consumer_key;

        acd->current = ptr;
        if (NULL != (consumer_key = account_key(error))) {
            acd->current->consumer_key = consumer_key;
        }
        account_notify_change();
    } else {
        error_set(error, NOTICE, "Any account named '%s' was found", args->account);
    }

    return ret ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

#if TODO
static command_status_t account_update(void *arg, error_t **error)
{
    return COMMAND_SUCCESS;
}
#endif

static void account_regcomm(graph_t *g)
{
    argument_t *arg_account, *arg_password, *arg_consumer_key, *arg_expiration;
    argument_t *lit_account, *lit_list, *lit_delete, *lit_add, *lit_switch, *lit_default, *lit_expires, *lit_in, *lit_at;

    lit_account = argument_create_literal("account", NULL);
    lit_list = argument_create_literal("list", account_list);
    lit_add = argument_create_literal("add", account_add);
    lit_delete = argument_create_literal("delete", account_delete);
    lit_default = argument_create_literal("default", account_default_set);
    lit_switch = argument_create_literal("switch", account_switch);
    lit_expires = argument_create_literal("expires", NULL);
    lit_in = argument_create_relevant_literal(offsetof(account_argument_t, expires_in), "in", NULL);
    lit_at = argument_create_relevant_literal(offsetof(account_argument_t, expires_at), "at", NULL);

    arg_password = argument_create_string(offsetof(account_argument_t, password), "<password>", NULL, NULL);
    arg_expiration = argument_create_string(offsetof(account_argument_t, expiration), "<expiration>", NULL, NULL);
    arg_consumer_key = argument_create_string(offsetof(account_argument_t, consumer_key), "<consumer key>", NULL, NULL);
    arg_account = argument_create_string(offsetof(account_argument_t, account), "<account>", complete_from_hashtable_keys, acd->accounts);

    graph_create_full_path(g, lit_account, lit_list, NULL);
    graph_create_full_path(g, lit_account, arg_account, lit_add, arg_password, NULL);
    graph_create_full_path(g, lit_account, arg_account, lit_add, arg_password, arg_consumer_key, NULL);
    graph_create_full_path(g, lit_account, arg_account, lit_add, arg_password, arg_consumer_key, lit_expires, lit_at, arg_expiration, NULL);
    graph_create_full_path(g, lit_account, arg_account, lit_add, arg_password, arg_consumer_key, lit_expires, lit_in, arg_expiration, NULL);
    graph_create_full_path(g, lit_account, arg_account, lit_delete, NULL);
    graph_create_full_path(g, lit_account, arg_account, lit_default, NULL);
    graph_create_full_path(g, lit_account, arg_account, lit_switch, NULL);
}

DECLARE_MODULE(account) = {
    "account",
    account_regcomm,
    account_early_init,
    account_late_init,
    account_dtor
};
