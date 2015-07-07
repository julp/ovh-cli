#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "common.h"
#include "command.h"
#include "date.h"
#include "endpoints.h"
#include "table.h"
#include "sqlite.h"
#include "modules/api.h"
#include "struct/hashtable.h"
#include "account_api.h"

#include <limits.h>
#if !defined(MAXPATHLEN) && defined(PATH_MAX)
# define MAXPATHLEN PATH_MAX
#endif /* !MAXPATHLEN && PATH_MAX */

enum {
    DTOR_NOCALL,
    DTOR_CALL
};

typedef struct {
    char path[MAXPATHLEN];
    HashTable *accounts;
    HashTable *applications; // int (index in endpoints - see endpoints.h) => application_t *
    account_t *autosel;
    account_t *current_account;
    HashTable *modules_callbacks;
} account_command_data_t;

typedef struct {
    int endpoint;
    char *account;
    char *password;
    int expires_in_at;
    char *expiration;
    char *consumer_key;
    bool endpoint_present;
} account_argument_t;

typedef struct {
    int endpoint;
    char *key;
    char *secret;
} application_argument_t;

#define UNEXISTANT_ACCOUNT \
    do { \
        error_set(error, WARN, _("no account named '%s'"), args->account); \
    } while (0);

typedef struct {
    DtorFunc dtor;
    void (*on_set_account)(void **);
} module_callbacks_t;

static account_command_data_t *acd = NULL;

account_t * const *current_account;
const application_t *current_application = NULL;

/**
 * TODO:
 * - sqlite3_prepare_v2 only once each query (on demand? in ctor?)
 * - sqlite3_finalize only at the end (on dtor?)
 */

enum {
    EXPIRES_IN,
    EXPIRES_AT
};

static const char * const expires_in_at[] = {
    [ EXPIRES_IN ] = "in",
    [ EXPIRES_AT ] = "at",
    NULL
};

// TODO: for compatibility
static void account_set_current(account_t *account, bool autosel)
{
    if (acd->current_account != account) {
        bool exists;
        Iterator it;

        acd->current_account = account;
        // change global variables
        current_application = NULL;
        hashtable_direct_get(acd->applications, account->endpoint_id, &current_application);
        // notify modules
        hashtable_to_iterator(&it, acd->modules_callbacks);
        for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
            void *key, *data;
            module_callbacks_t *mc;

            mc = iterator_current(&it, &key);
            assert(NULL != mc);
            if (NULL != mc->on_set_account) {
                data = NULL;
                exists = hashtable_get(acd->current_account->modules_data, key, &data);
                mc->on_set_account(&data);
                if (!exists) {
                    hashtable_put(acd->current_account->modules_data, 0, key, data, NULL);
                }
            }
        }
        iterator_close(&it);
    }
    if (autosel) {
        acd->autosel = account;
    }
}

bool check_current_application_and_account(bool skip_CK_check, error_t **error)
{
    if (NULL == current_account || NULL == *current_account) {
        error_set(error, WARN, _("no current account"));
        return FALSE;
    }
//     if (NULL == acd->current_account->endpoint_id) {
//         error_set(error, WARN, _("no endpoint associated to account '%s'"), acd->current_account->account);
//         return FALSE;
//     }
    if (NULL == current_application) {
        error_set(error, WARN, _("no application registered for endpoint '%s'"), endpoint_names[acd->current_account->endpoint_id]);
        return FALSE;
    }
    if (skip_CK_check) {
        return TRUE;
    }
    // if no CK is defined or it is expired, get one
    if (NULL == acd->current_account->consumer_key || (0 != acd->current_account->expires_at && acd->current_account->expires_at < time(NULL))) {
        // if we successfully had a CK, save it
        if (NULL != (acd->current_account->consumer_key = request_consumer_key(&acd->current_account->expires_at, error))) {
            sqlite3_stmt *stmt;

            sqlite3_prepare_v2(db, "UPDATE accounts SET consumer_key = ?, expires_at = ? WHERE name = ?", -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, acd->current_account->consumer_key, -1, SQLITE_STATIC/*SQLITE_TRANSIENT*/);
            sqlite3_bind_int(stmt, 2, acd->current_account->expires_at);
            sqlite3_bind_text(stmt, 3, acd->current_account->account, -1, SQLITE_STATIC/*SQLITE_TRANSIENT*/);
            assert(SQLITE_DONE == sqlite3_step(stmt));
            sqlite3_reset(stmt);
            sqlite3_finalize(stmt);
        }
    }

    return NULL != acd->current_account->consumer_key;
}

const char *account_current(void)
{
    if (NULL == acd->current_account) {
        return "(no current account)";
    } else {
        return acd->current_account->account;
    }
}

// TODO: for compatibility
void account_current_set_data(const char *name, void *data)
{
    assert(NULL != acd->current_account);

    hashtable_put(acd->current_account->modules_data, 0, name, data, NULL);
}

// TODO: for compatibility
bool account_current_get_data(const char *name, void **data)
{
    assert(NULL != acd->current_account);

    return hashtable_get(acd->current_account->modules_data, name, data);
}

// TODO: for compatibility
void account_register_module_callbacks(const char *name, DtorFunc dtor, void (*on_set_account)(void **))
{
    module_callbacks_t *mc;

    assert(NULL != acd);
    assert(NULL != name);

    if (NULL != dtor && NULL != on_set_account) { // nothing to do? Skip it!
        bool exists;
        ht_hash_t h;

        h = hashtable_hash(acd->modules_callbacks, name);
        if (!(exists = hashtable_quick_get(acd->modules_callbacks, h, name, &mc))) {
            mc = mem_new(*mc);
        }
        mc->dtor = dtor;
        mc->on_set_account = on_set_account;
        if (!exists) {
            hashtable_quick_put(acd->modules_callbacks, 0, h, name, mc, NULL);
        }
    }
}

// TODO: for compatibility
static void application_dtor(void *data)
{
    application_t *app;

    app = (application_t *) data;
    FREE(app, key);
    FREE(app, secret);
    free(app);
}

// TODO: for compatibility
static bool account_late_init(error_t **error)
{
    int id;
    Iterator it;
    bool isdefault;
    sqlite3_stmt *stmt;
    account_t account = { 0 };
    application_t application = { 0 };

    sqlite3_prepare_v2(db, "SELECT * FROM accounts", -1, &stmt, NULL);
    statement_to_iterator(&it, stmt, "isssiii", &id, &account.account, &account.password, &account.consumer_key, &account.endpoint_id, &isdefault, &account.expires_at);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        account_t *a;

        iterator_current(&it, NULL);
        a = mem_new(*a);
        *a = account;
        a->modules_data = hashtable_ascii_cs_new(NULL, NULL, NULL); // no dup/dtor for keys as they are "static" strings ; no dtor for values, we need to do it ourselves
        hashtable_put(acd->accounts, 0, a->account, a, NULL);
        if (isdefault) {
            account_set_current(a, TRUE);
        }
    }
    iterator_close(&it);
    sqlite3_finalize(stmt);

    sqlite3_prepare_v2(db, "SELECT * FROM applications", -1, &stmt, NULL);
    statement_to_iterator(&it, stmt, "ssi", &application.key, &application.secret, &application.endpoint_id);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        application_t *app;

        iterator_current(&it, NULL);
        app = mem_new(*app);
        *app = application;
        hashtable_direct_put(acd->applications, 0, app->endpoint_id, app, NULL);
    }
    iterator_close(&it);
    sqlite3_finalize(stmt);

    if (hashtable_size(acd->accounts) > 0) {
        if (NULL == acd->current_account) {
            account_set_current((account_t *) hashtable_first(acd->accounts), FALSE);
        }
    }

    return TRUE;
}

// TODO: for compatibility
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
                if (hashtable_get(acd->modules_callbacks, key, &mc)) {
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

    int ret;
    char *errmsg;

    // TODO: faire des requêtes de création des tables un champ du module pour que main se charge de les exécuter directement ?
    ret = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS accounts(\n\
        id INTEGER PRIMARY KEY,\n\
        name TEXT NOT NULL UNIQUE,\n\
        password TEXT,\n\
        consumer_key TEXT,\n\
        endpoint_id INTEGER NOT NULL,\n\
        is_default INTEGER NOT NULL,\n\
        expires_at INTEGER NOT NULL\n\
    );\n\
    \n\
    CREATE TABLE IF NOT EXISTS applications(\n\
        app_key TEXT NOT NULL,\n\
        secret TEXT NOT NULL,\n\
        endpoint_id INTEGER NOT NULL UNIQUE\n\
    )", NULL, NULL, &errmsg);
    if (SQLITE_OK != ret) {
//         error_set(error, FATAL, _("can't create tables for module %s: %s"), "account", errmsg);
        debug("%s", errmsg);
        sqlite3_free(errmsg);
    }

    // TODO: for compatibility
    acd = mem_new(*acd);
    current_account = &acd->current_account;
    acd->autosel = acd->current_account = NULL;
    acd->applications = hashtable_new(value_hash, value_equal, NULL, NULL, application_dtor);
    acd->accounts = hashtable_ascii_cs_new(NULL, NULL /* no dtor for key as it is also part of the value */, account_account_dtor);
    acd->modules_callbacks = hashtable_ascii_cs_new(NULL, NULL /* no dtor for key as it is also part of the value */, free);

    // <TODO: move to main.c>
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
    // </TODO: move to main.c>

    return TRUE;
}

// TODO: for compatibility
static void account_dtor(void)
{
    if (NULL != acd) {
        if (NULL != acd->accounts) {
            hashtable_destroy(acd->accounts);
        }
        if (NULL != acd->applications) {
            hashtable_destroy(acd->applications);
        }
        if (NULL != acd->modules_callbacks) {
            hashtable_destroy(acd->modules_callbacks);
        }
        free(acd);
    }
    acd = NULL;
}

static command_status_t account_list(COMMAND_ARGS)
{
    int id;
    bool isdefault;
    table_t *t;
    Iterator it;
    sqlite3_stmt *stmt;
    account_t account = { 0 };

    USED(arg);
    USED(error);
    USED(mainopts);
    t = table_new(6,
        _("account"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
        _("consumer key"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
        _("key expiration"), TABLE_TYPE_DATETIME,
        _("password"), TABLE_TYPE_BOOLEAN,
        _("endpoint"), TABLE_TYPE_ENUM, endpoint_names,
        _("current"), TABLE_TYPE_BOOLEAN,
        _("default"), TABLE_TYPE_BOOLEAN
    );
    sqlite3_prepare_v2(db, "SELECT * FROM accounts", -1, &stmt, NULL);
    statement_to_iterator(&it, stmt, "isssiii", &id, &account.account, &account.password, &account.consumer_key, &account.endpoint_id, &isdefault, &account.expires_at);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        struct tm expiration, *tm;

        iterator_current(&it, NULL);
        if (0 == account.expires_at) {
            bzero(&expiration, sizeof(expiration));
        } else {
            tm = localtime(&account.expires_at);
            expiration = *tm;
        }
        table_store(t, account.account, account.consumer_key, expiration, NULL != account.password, account.endpoint_id, FALSE /* TODO */, isdefault);
        free(account.password);
    }
    iterator_close(&it);
    sqlite3_finalize(stmt);
    table_display(t, TABLE_FLAG_NONE);
    table_destroy(t);

    return COMMAND_SUCCESS;
}

static command_status_t account_add_or_update(COMMAND_ARGS, bool update)
{
    ht_hash_t h;
    account_t *a;
    time_t expires_at;
    account_argument_t *args;

    USED(mainopts);
    expires_at = (time_t) 0;
    args = (account_argument_t *) arg;

    assert(NULL != args->account);
    if (!update) {
        assert(NULL != args->password); // password argument is mandatory with actual account add command (may be not the case in the future if account add and update share the same logic)
    }

    if (NULL != args->expiration) {
        switch (args->expires_in_at) {
            case EXPIRES_IN:
                if (!parse_duration(args->expiration, &expires_at)) {
                    error_set(error, WARN, _("command aborted: unable to parse duration '%s'"), args->expiration);
                    return COMMAND_USAGE;
                }
                expires_at += time(NULL);
                break;
            case EXPIRES_AT:
            {
                char *endptr;
                struct tm ltm = { 0 };

                if (NULL == (endptr = strptime(args->expiration, "%c", &ltm))) {
                    error_set(error, WARN, _("command aborted: unable to parse expiration date '%s'"), args->expiration);
                    return COMMAND_USAGE;
                }
                expires_at = mktime(&ltm);
                break;
            }
            default:
                assert(FALSE);
        }
    }
    h = hashtable_hash(acd->accounts, args->account);
    if (hashtable_quick_get(acd->accounts, h, args->account, &a)) {
        if (!update) {
            error_set(error, WARN, _("an account named '%s' already exists"), args->account);
            return COMMAND_FAILURE;
        }
        if (NULL != args->password) { // keep current password if a new one is not given
            FREE(a, password);
        }
        if (NULL != args->consumer_key) { // keep current consumer_key if a new one is not given
            FREE(a, consumer_key);
        }
    } else {
        if (update) {
            UNEXISTANT_ACCOUNT;
            return COMMAND_FAILURE;
        }
        if (!args->endpoint_present) {
            error_set(error, WARN, _("no endpoint specified"));
            return COMMAND_USAGE;
        }
        a = mem_new(*a);
        a->password = NULL;
        a->endpoint_id = 0; // TODO: temporary
        a->consumer_key = NULL;
        a->account = strdup(args->account);
        a->modules_data = hashtable_ascii_cs_new(NULL, NULL, NULL);
    }
    if (args->endpoint_present) {
        a->endpoint_id = args->endpoint;
    }
    if (!update || NULL != args->password) { // for update, only if a new password is set
        a->password = strdup(args->password);
    }
    if (NULL != args->consumer_key) { // only if a new CK is set
        a->consumer_key = strdup(args->consumer_key);
        a->expires_at = expires_at;
    }
    if (!update) {
        sqlite3_stmt *stmt;
        // if this is the first account, set it as current
        if (0 == hashtable_size(acd->accounts)) {
            account_set_current(a, TRUE);
        }
        hashtable_quick_put(acd->accounts, 0, h, a->account, a, NULL);

        sqlite3_prepare_v2(db, "INSERT INTO accounts(name, password, consumer_key, endpoint_id, is_default, expires_at) VALUES(?, ?, ?, ?, ?, ?)", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, a->account, -1, SQLITE_STATIC/*SQLITE_TRANSIENT*/);
        sqlite3_bind_text(stmt, 2, a->password, -1, SQLITE_STATIC/*SQLITE_TRANSIENT*/);
        sqlite3_bind_text(stmt, 3, a->consumer_key, -1, SQLITE_STATIC/*SQLITE_TRANSIENT*/);
        sqlite3_bind_int(stmt, 4, a->endpoint_id);
        sqlite3_bind_int(stmt, 5, 0);
        sqlite3_bind_int(stmt, 6, a->expires_at);
        assert(SQLITE_DONE == sqlite3_step(stmt));
        sqlite3_reset(stmt);
        sqlite3_finalize(stmt);
    } else {
        sqlite3_stmt *stmt;

        sqlite3_prepare_v2(db, "UPDATE accounts SET password = IFNULL(?, password), consumer_key = IFNULL(?, consumer_key), expires_at = IFNULL(?, expires_at), endpoint_id = IFNULL(?, endpoint_id) WHERE name = ?", -1, &stmt, NULL);
        if (NULL != args->password) {
            sqlite3_bind_text(stmt, 1, a->password, -1, SQLITE_STATIC/*SQLITE_TRANSIENT*/);
        } else {
            sqlite3_bind_null(stmt, 1);
        }
        if (NULL != args->consumer_key) {
            sqlite3_bind_text(stmt, 2, a->consumer_key, -1, SQLITE_STATIC/*SQLITE_TRANSIENT*/);
            sqlite3_bind_int(stmt, 3, a->expires_at);
        } else {
            sqlite3_bind_null(stmt, 2);
            sqlite3_bind_null(stmt, 3);
        }
        if (args->endpoint_present) {
            sqlite3_bind_int(stmt, 4, a->endpoint_id);
        } else {
            sqlite3_bind_null(stmt, 4);
        }
        sqlite3_bind_text(stmt, 5, a->account, -1, SQLITE_STATIC/*SQLITE_TRANSIENT*/);
        assert(SQLITE_DONE == sqlite3_step(stmt));
        sqlite3_reset(stmt);
        sqlite3_finalize(stmt);
    }

    return COMMAND_SUCCESS;
}

/**
 * account [nic-handle] add (password [password]) (key [consumer key] expires in|at [date]) (endpoint [endpoint])
 *
 * NOTE: in order to not record password, use an empty string (with "")
 **/
static command_status_t account_add(COMMAND_ARGS)
{
    return account_add_or_update(RELAY_COMMAND_ARGS, FALSE);
}

static command_status_t account_update(COMMAND_ARGS)
{
    return account_add_or_update(RELAY_COMMAND_ARGS, TRUE);
}

static command_status_t account_default_set(COMMAND_ARGS)
{
    int ret;
    void *ptr;
    account_argument_t *args;

    USED(mainopts);
    args = (account_argument_t *) arg;
    assert(NULL != args->account);
    if ((ret = hashtable_get(acd->accounts, args->account, &ptr))) {
        acd->autosel = ptr;
        sqlite3_stmt *stmt;

        sqlite3_prepare_v2(db, "UPDATE accounts SET is_default = (name = ?)", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, args->account, -1, SQLITE_STATIC/*SQLITE_TRANSIENT*/);
        assert(SQLITE_DONE == sqlite3_step(stmt));
        sqlite3_reset(stmt);
        sqlite3_finalize(stmt);
    } else {
        UNEXISTANT_ACCOUNT;
    }

    return ret ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t account_delete(COMMAND_ARGS)
{
    int ret;
    account_argument_t *args;

    USED(mainopts);
    args = (account_argument_t *) arg;
    assert(NULL != args->account);
    if (NULL != acd->current_account && 0 == strcmp(args->account, acd->current_account->account)) {
        if (acd->current_account == acd->autosel) {
            acd->autosel = NULL;
        }
        acd->current_account = NULL;
    }
    if ((ret = hashtable_delete(acd->accounts, args->account, DTOR_CALL))) {
        sqlite3_stmt *stmt;

        sqlite3_prepare_v2(db, "DELETE FROM accounts WHERE name = ?", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, args->account, -1, SQLITE_STATIC/*SQLITE_TRANSIENT*/);
        assert(SQLITE_DONE == sqlite3_step(stmt));
        sqlite3_reset(stmt);
        sqlite3_finalize(stmt);
    } else {
        UNEXISTANT_ACCOUNT;
    }

    return ret ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t account_switch(COMMAND_ARGS)
{
    int ret;
    account_t *ptr;
    account_argument_t *args;

    USED(mainopts);
    args = (account_argument_t *) arg;
    assert(NULL != args->account);
    if ((ret = hashtable_get(acd->accounts, args->account, &ptr))) {
        account_set_current(ptr, FALSE);
    } else {
        UNEXISTANT_ACCOUNT;
    }

    return ret ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t application_list(COMMAND_ARGS)
{
    table_t *t;
    Iterator it;
    sqlite3_stmt *stmt;
    application_t application = { 0 };

    USED(arg);
    USED(error);
    USED(mainopts);
    t = table_new(
        3,
        _("endpoint"), TABLE_TYPE_ENUM, endpoint_names,
        _("key"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
        _("secret"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE
    );
    sqlite3_prepare_v2(db, "SELECT * FROM applications", -1, &stmt, NULL);
    statement_to_iterator(&it, stmt, "ssi", &application.key, &application.secret, &application.endpoint_id);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        iterator_current(&it, NULL);
        table_store(t, application.endpoint_id, application.key, application.secret);
    }
    iterator_close(&it);
    sqlite3_finalize(stmt);
    table_display(t, TABLE_FLAG_NONE);
    table_destroy(t);

    return COMMAND_SUCCESS;
}

static command_status_t application_add(COMMAND_ARGS)
{
    sqlite3_stmt *stmt;
    application_t *app;
    application_argument_t *args;

    USED(error);
    USED(mainopts);
    args = (application_argument_t *) arg;
    assert(NULL != args->key);
    assert(NULL != args->secret);

    app = mem_new(*app);
    app->key = strdup(args->key);
    app->secret = strdup(args->secret);
    app->endpoint_id = args->endpoint;
    hashtable_direct_put(acd->applications, 0, app->endpoint_id, app, NULL);

    sqlite3_prepare_v2(db, "INSERT INTO applications(app_key, secret, endpoint_id) VALUES(?, ?, ?)", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, app->key, -1, SQLITE_STATIC/*SQLITE_TRANSIENT*/);
    sqlite3_bind_text(stmt, 2, app->secret, -1, SQLITE_STATIC/*SQLITE_TRANSIENT*/);
    sqlite3_bind_int(stmt, 3, app->endpoint_id);
    assert(SQLITE_DONE == sqlite3_step(stmt));
    sqlite3_reset(stmt);
    sqlite3_finalize(stmt);

    return COMMAND_SUCCESS;
}

static command_status_t application_delete(COMMAND_ARGS)
{
    application_argument_t *args;

    USED(error);
    USED(mainopts);
    args = (application_argument_t *) arg;
    if (hashtable_direct_delete(acd->applications, args->endpoint, TRUE)) {
        sqlite3_stmt *stmt;

        sqlite3_prepare_v2(db, "DELETE FROM applications WHERE endpoint_id = ?", -1, &stmt, NULL);
        sqlite3_bind_int(stmt, 1, args->endpoint);
        assert(SQLITE_DONE == sqlite3_step(stmt));
        sqlite3_reset(stmt);
        sqlite3_finalize(stmt);
    } else {
        error_set(error, NOTICE, _("no application associated to endpoint %s"), endpoint_names[args->endpoint]);
    }

    return COMMAND_SUCCESS;
}

static void account_regcomm(graph_t *g)
{
    // account ...
    {
        argument_t *arg_expires_in_at;
        argument_t *arg_account, *arg_password, *arg_consumer_key, *arg_expiration, *arg_endpoint;
        argument_t *lit_account, *lit_list, *lit_delete, *lit_add, *lit_update, *lit_switch, *lit_default, *lit_expires, *lit_password, *lit_key, *lit_endpoint;

        lit_account = argument_create_literal("account", NULL);
        lit_list = argument_create_literal("list", account_list);
        lit_add = argument_create_literal("add", account_add);
        lit_delete = argument_create_literal("delete", account_delete);
        lit_default = argument_create_literal("default", account_default_set);
        lit_switch = argument_create_literal("switch", account_switch);
        lit_expires = argument_create_literal("expires", NULL);
        lit_update = argument_create_literal("update", account_update);
        lit_key = argument_create_literal("key", NULL);
        lit_password = argument_create_literal("password", NULL);
        lit_endpoint = argument_create_relevant_literal(offsetof(account_argument_t, endpoint_present), "endpoint", NULL);

        arg_password = argument_create_string(offsetof(account_argument_t, password), "<password>", NULL, NULL);
        arg_expires_in_at = argument_create_choices(offsetof(account_argument_t, expires_in_at), "<in/at>",  expires_in_at);
        arg_expiration = argument_create_string(offsetof(account_argument_t, expiration), "<expiration>", NULL, NULL);
        arg_consumer_key = argument_create_string(offsetof(account_argument_t, consumer_key), "<consumer key>", NULL, NULL);
        arg_endpoint = argument_create_choices(offsetof(account_argument_t, endpoint), "<endpoint>", endpoint_names);
        arg_account = argument_create_string(offsetof(account_argument_t, account), "<account>", complete_from_hashtable_keys, acd->accounts);

        graph_create_full_path(g, lit_account, lit_list, NULL);
        graph_create_path(g, lit_account, lit_add, arg_account, NULL);
        graph_create_all_path(g, lit_add, NULL, 2, lit_password, arg_password, 5, lit_key, arg_consumer_key, lit_expires, arg_expires_in_at, arg_expiration, 2, lit_endpoint, arg_endpoint, 0);
        graph_create_full_path(g, lit_account, arg_account, lit_delete, NULL);
        graph_create_full_path(g, lit_account, arg_account, lit_default, NULL);
        graph_create_full_path(g, lit_account, arg_account, lit_switch, NULL);

        graph_create_path(g, lit_account, lit_update, arg_account, NULL);
        graph_create_all_path(g, lit_update, NULL, 2, lit_password, arg_password, 5, lit_key, arg_consumer_key, lit_expires, arg_expires_in_at, arg_expiration, 2, lit_endpoint, arg_endpoint, 0);
    }
    // application ...
    {
        argument_t *arg_app_key, *arg_app_secret, *arg_endpoint;
        argument_t *lit_application, *lit_add, *lit_list, *lit_delete;

        lit_application = argument_create_literal("application", NULL);
        lit_add = argument_create_literal("add", application_add);
        lit_list = argument_create_literal("list", application_list);
        lit_delete = argument_create_literal("delete", application_delete);

        arg_endpoint = argument_create_choices(offsetof(application_argument_t, endpoint), "<endpoint>", endpoint_names);
        arg_app_key = argument_create_string(offsetof(application_argument_t, key), "<key>", NULL, NULL);
        arg_app_secret = argument_create_string(offsetof(application_argument_t, secret), "<secret>", NULL, NULL);

        graph_create_full_path(g, lit_application, lit_list, NULL);
        graph_create_full_path(g, lit_application, arg_endpoint, lit_add, arg_app_key, arg_app_secret, NULL);
        graph_create_full_path(g, lit_application, arg_endpoint, lit_delete, NULL);
    }
}

DECLARE_MODULE(account) = {
    "account",
    account_regcomm,
    NULL,
    account_early_init,
    account_late_init,
    account_dtor
};
