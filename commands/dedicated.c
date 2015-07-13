#include <string.h>
#include <inttypes.h>

#include "common.h"
#include "command.h"
#include "date.h"
#include "util.h"
#include "table.h"
#include "graphic.h"
#include "modules/api.h"
#include "modules/sqlite.h"
#include "commands/account.h"
#include "struct/hashtable.h"

#define MODULE_NAME "dedicated"

#define FETCH_ACCOUNT_SERVERS(/*server_set **/ ss) \
    do { \
        ss = NULL; \
        account_current_get_data(MODULE_NAME, (void **) &ss); \
        assert(NULL != ss); \
    } while (0);

/**
 * an account may have 0, 1 or more servers
 * each server can have several:
 * - boots (at least 2 - rescue/hdd)
 *      + ... each boot can have 0, 1 or more options
 * - tasks
 * - ...
 **/

enum {
    // dedicated
    STMT_DEDICATED_LIST,
    STMT_DEDICATED_UPSERT,
    // specialized read (select)
    STMT_DEDICATED_COMPLETION,
    STMT_DEDICATED_CURRENT_BOOT,
    // boot
    STMT_BOOT_LIST,
    STMT_BOOT_UPSERT,
    STMT_BOOT_COMPLETION,
    // dedicated <=> boot
    STMT_B_D_LINK,
    STMT_B_D_FIND_BY_NAME,
    // count
    STMT_COUNT
};

static const char *statements[STMT_COUNT] = {
    [ STMT_DEDICATED_LIST ]         = "SELECT name, ip, os, reverse, kernel, datacenter, professional_use, support_level, commercial_range, state, monitoring, rack, root_device, link_speed, engaged_up_to, contact_billing, expiration, contact_tech, contact_admin, creation FROM dedicated JOIN boots ON dedicated.boot_id = boots.id WHERE account_id = ?",
    [ STMT_DEDICATED_UPSERT ]       = "INSERT OR REPLACE INTO dedicated(account_id, id, name, datacenter, professional_use, support_level, commercial_range, ip, os, state, reverse, monitoring, rack, root_device, link_speed, boot_id, engaged_up_to, contact_billing, expiration, contact_tech, contact_admin, creation) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
    [ STMT_DEDICATED_COMPLETION ]   = "SELECT name FROM dedicated WHERE account_id = ? AND name LIKE ? || '%'",
    [ STMT_DEDICATED_CURRENT_BOOT ] = "SELECT boot_type, kernel, description FROM boots JOIN dedicated ON boots.id = dedicated.boot_id WHERE account_id = ? AND name = ?",
    [ STMT_BOOT_LIST ]              = "SELECT boot_type, kernel, description FROM boots JOIN boots_dedicated ON boots_dedicated.boot_id = boots.id JOIN dedicated ON boots_dedicated.dedicated_id = dedicated.id WHERE account_id = ? AND name = ?",
    [ STMT_BOOT_UPSERT ]            =  "INSERT OR REPLACE INTO boots(id, boot_type, kernel, description) VALUES(?, ?, ?, ?)",
    [ STMT_BOOT_COMPLETION ]        = "SELECT kernel FROM boots JOIN boots_dedicated ON boots_dedicated.boot_id = boots.id JOIN dedicated ON boots_dedicated.dedicated_id = dedicated.id WHERE account_id = ? AND name = ? AND kernel LIKE ? || '%'",
    [ STMT_B_D_LINK ]               = "INSERT OR IGNORE INTO boots_dedicated(boot_id, dedicated_id) VALUES(?, ?)",
    [ STMT_B_D_FIND_BY_NAME ]       = "SELECT boots.id FROM boots JOIN boots_dedicated ON boots_dedicated.boot_id = boots.id JOIN dedicated ON boots_dedicated.dedicated_id = dedicated.id WHERE account_id = ? AND name = ? AND kernel = ?",
};

static sqlite3_stmt *prepared[STMT_COUNT];

// describe all dedicated servers owned by a given account
typedef struct {
    bool uptodate;
    HashTable *servers;
} server_set_t;

// describe a dedicated server
typedef struct {
    bool boots_uptodate;
    HashTable *boots;
    int datacenter;
    bool professionalUse;
    int supportLevel;
    char *ip;
    char *name; // TODO: doublon avec les clés de la hashtable servers ci-dessus
    char *commercialRange;
    char *os;
    int state;
    char *reverse;
    int64_t serverId;
    bool monitoring;
    char *rack;
    char *rootDevice;
    int64_t linkSpeed;
    int64_t bootId;
    time_t engagedUpTo;
    char *contactBilling;
    time_t expiration;
    char *contactTech;
    char *contactAdmin;
    time_t creation;
} server_t;

typedef struct {
    bool nocache;
    int boot_type;
    char *boot_name;
    char *server_name;
    char *reverse;
    int64_t mrtg_period;
    int64_t mrtg_type;
} dedicated_argument_t;

static const char * const datacenters[] = {
    "bhs1",
    "bhs2",
    "bhs3",
    "bhs4",
    "dc1",
    "gra1",
    "gsw",
    "p19",
    "rbx-hz",
    "rbx1",
    "rbx2",
    "rbx3",
    "rbx4",
    "rbx5",
    "rbx6",
    "sbg1",
    "sbg2",
    "sbg3",
    "sbg4",
    NULL
};

static const char * const boot_types[] = {
    "harddisk",
    "ipxeCustomerScript",
    "network",
    "rescue",
    NULL
};

static const char * const support_levels[] = {
    "critical",
    "fastpath",
    "gs",
    "pro",
    NULL
};

static const char * const states[] = {
    "error",
    "hacked",
    "hackedBlocked",
    "ok",
    NULL
};

// describe a boot
typedef struct {
    int type;
    int64_t id;
    const char *kernel;
    const char *description;
} boot_t;

static const char *mrtg_periods[] = {
    "daily",
    "hourly",
    "monthly",
    "weekly",
    "yearly"
};

static const char *mrtg_types[] = {
    "errors:download",
    "errors:upload",
    "packets:download",
    "packets:upload",
    "traffic:download",
    "traffic:upload",
};

static void server_destroy(void *data)
{
    server_t *s;

    assert(NULL != data);

    s = (server_t *) data;
    hashtable_destroy(s->boots);
    FREE(s, ip);
    FREE(s, name);
    FREE(s, commercialRange);
    FREE(s, os);
    FREE(s, reverse);
    FREE(s, rack);
    FREE(s, rootDevice);
    FREE(s, contactBilling);
    FREE(s, contactAdmin);
    FREE(s, contactTech);
    free(s);
}

static void boot_destroy(void *data)
{
    boot_t *b;

    assert(NULL != data);

    b = (boot_t *) data;
    FREE(b, kernel);
    FREE(b, description);
    free(b);
}

static server_t *server_new(void)
{
    server_t *s;

    s = mem_new(*s);
    INIT(s, ip);
    INIT(s, name);
    INIT(s, commercialRange);
    INIT(s, os);
    INIT(s, reverse);
    INIT(s, rack);
    INIT(s, rootDevice);
    INIT(s, contactBilling);
    INIT(s, contactAdmin);
    INIT(s, contactTech);
    s->boots_uptodate = FALSE;
    s->engagedUpTo = (time_t) 0;
    s->bootId = s->linkSpeed = 0;
    s->state = s->supportLevel = s->datacenter = 0;
    s->boots = hashtable_ascii_cs_new((DupFunc) strdup, free, boot_destroy);

    return s;
}

static void server_set_destroy(void *data)
{
    server_set_t *ss;

    assert(NULL != data);
    ss = (server_set_t *) data;
    if (NULL != ss->servers) {
        hashtable_destroy(ss->servers);
    }
    free(ss);
}

static void dedicated_on_set_account(void **data)
{
    if (NULL == *data) {
        server_set_t *ss;

        ss = mem_new(*ss);
        // TODO: temporary, fetch servers only on "dedicated list nocache"
#if 0
        ss->uptodate = FALSE;
#else
        ss->uptodate = TRUE;
#endif
        ss->servers = hashtable_ascii_cs_new((DupFunc) strdup, free, server_destroy);
        *data = ss;
    }
}

static bool dedicated_ctor(error_t **error)
{
    if (!create_or_migrate("boots", "CREATE TABLE boots(\n\
        id INTEGER NOT NULL PRIMARY KEY, -- OVH ID (bootId)\n\
        boot_type INT NOT NULL, -- enum\n\
        kernel TEXT NOT NULL,\n\
        description TEXT NOT NULL\n\
    )", NULL, 0, error)) {
        return FALSE;
    }
    if (!create_or_migrate("dedicated", "CREATE TABLE dedicated(\n\
        account_id INT NOT NULL REFERENCES accounts(id) ON UPDATE CASCADE ON DELETE CASCADE,\n\
        -- From GET /dedicated/server/{serviceName}\n\
        id INTEGER NOT NULL PRIMARY KEY, -- OVH ID (serverId)\n\
        name TEXT NOT NULL UNIQUE,\n\
        datacenter INT NOT NULL, -- enum\n\
        professional_use INT NOT NULL, -- bool\n\
        support_level INT NOT NULL, -- enum\n\
        commercial_range TEXT, -- nullable\n\
        ip TEXT NOT NULL, -- unique ?\n\
        os TEXT NOT NULL,\n\
        state INT NOT NULL, -- enum\n\
        reverse TEXT, -- nullable\n\
        monitoring INT NOT NULL, -- bool\n\
        rack TEXT NOT NULL,\n\
        root_device TEXT, -- nullable\n\
        link_speed INT, -- nullable\n\
        boot_id INT REFERENCES boots(id) ON UPDATE CASCADE ON DELETE CASCADE,\n\
        -- From GET /dedicated/server/{serviceName}/serviceInfos\n\
        -- status INT NOT NULL, -- enum\n\
        engaged_up_to INT, -- data, nullable\n\
        -- possibleRenewPeriod: array of int (JSON response)\n\
        contact_billing TEXT NOT NULL,\n\
        -- renew: subobkect (JSON response)\n\
        -- domain TEXT NOT NULL, -- same as name?\n\
        expiration INT NOT NULL, -- date\n\
        contact_tech TEXT NOT NULL,\n\
        contact_admin TEXT NOT NULL,\n\
        creation INT NOT NULL -- date\n\
    )", NULL, 0, error)) {
        return FALSE;
    }
    if (!create_or_migrate("boots_dedicated", "CREATE TABLE boots_dedicated(\n\
        dedicated_id INT NOT NULL REFERENCES dedicated(id) ON UPDATE CASCADE ON DELETE CASCADE,\n\
        boot_id INT NOT NULL REFERENCES boots(id) ON UPDATE CASCADE ON DELETE CASCADE,\n\
        PRIMARY KEY (dedicated_id, boot_id)\n\
    )", NULL, 0, error)) {
        return FALSE;
    }

    statement_batched_prepare(statements, prepared, STMT_COUNT, error);

    account_register_module_callbacks(MODULE_NAME, server_set_destroy, dedicated_on_set_account);

    return TRUE;
}

static void dedicated_dtor(void)
{
    statement_batched_finalize(prepared, STMT_COUNT);
}

static int parse_boot(server_t *s, json_document_t *doc, error_t **error)
{
    boot_t *b;
    json_value_t root, v;

    root = json_document_get_root(doc);
    b = mem_new(*b);
    JSON_GET_PROP_INT(root, "bootId", b->id);
    JSON_GET_PROP_STRING(root, "kernel", b->kernel);
    JSON_GET_PROP_STRING(root, "description", b->description);
    json_object_get_property(root, "bootType", &v);
    b->type = json_get_enum(v, boot_types, -1);
    hashtable_put(s->boots, 0, b->kernel, b, NULL);
    json_document_destroy(doc);

    statement_bind(prepared[STMT_BOOT_UPSERT], "iiss", b->id, b->type, b->kernel, b->description);
    statement_fetch(prepared[STMT_BOOT_UPSERT], error, "");
    assert(1 == sqlite_affected_rows());
    statement_bind(prepared[STMT_B_D_LINK], "ii", b->id, s->serverId);
    statement_fetch(prepared[STMT_B_D_LINK], error, "");

    return TRUE;
}

static command_status_t fetch_server_boots(const char *server_name, server_t **s, error_t **error)
{
    server_set_t *ss;
    bool request_success;

    *s = NULL;
    FETCH_ACCOUNT_SERVERS(ss);
    request_success = TRUE;
    if (!hashtable_get(ss->servers, server_name, s) || !(*s)->boots_uptodate) {
        request_t *req;
        json_document_t *doc;

        if (NULL == *s) {
            *s = server_new();
            hashtable_put(ss->servers, 0, server_name, *s, NULL);
        }
        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/dedicated/server/%s/boot", server_name);
        request_success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        // result
        if (request_success) {
            Iterator it;
            json_value_t root;

            root = json_document_get_root(doc);
            json_array_to_iterator(&it, root);
            for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
                json_value_t v;
                json_document_t *doc;

                v = (json_value_t) iterator_current(&it, NULL);
                req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/dedicated/server/%s/boot/%u", server_name, json_get_integer(v));
                request_success = request_execute(req, RESPONSE_JSON, (void **) &doc, error); // request_success is assumed to be TRUE before the first iteration
                request_destroy(req);
                // result
                parse_boot(*s, doc, error);
            }
            iterator_close(&it);
            json_document_destroy(doc);
            (*s)->boots_uptodate = TRUE;
        }
    }

    return request_success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static server_t *fetch_server(server_set_t *ss, const char * const server_name, bool force, error_t **error)
{
    server_t *s;

    s = NULL;
    if (!force || !ss->uptodate || !hashtable_get(ss->servers, server_name, &s)) {
        request_t *req;
        json_document_t *doc;
        bool request_success;

        s = server_new();
        hashtable_put(ss->servers, 0, server_name, s, NULL);
        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/dedicated/server/%s", server_name);
        request_success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (request_success) {
            json_value_t root, propvalue;
            char binds[] = "iisibisssisbssiiisissi";

            root = json_document_get_root(doc);
            json_object_get_property(root, "datacenter", &propvalue);
            s->datacenter = json_get_enum(propvalue, datacenters, -1);
            JSON_GET_PROP_BOOL(root, "professionalUse", s->professionalUse);
            json_object_get_property(root, "supportLevel", &propvalue);
            s->supportLevel = json_get_enum(propvalue, support_levels, -1);
            JSON_GET_PROP_STRING(root, "ip", s->ip);
            JSON_GET_PROP_STRING(root, "name", s->name);
            JSON_GET_PROP_STRING(root, "commercialRange", s->commercialRange);
            if (NULL == s->commercialRange) {
                binds[6] = 'n';
            }
            JSON_GET_PROP_STRING(root, "os", s->os);
            json_object_get_property(root, "state", &propvalue);
            s->state = json_get_enum(propvalue, states, -1);
            JSON_GET_PROP_STRING(root, "reverse", s->reverse);
            if (NULL == s->reverse) {
                binds[10] = 'n';
            }
            JSON_GET_PROP_INT(root, "serverId", s->serverId);
            JSON_GET_PROP_BOOL(root, "monitoring", s->monitoring);
            JSON_GET_PROP_STRING(root, "rack", s->rack);
            JSON_GET_PROP_STRING(root, "rootDevice", s->rootDevice);
            if (NULL == s->rootDevice) {
                binds[13] = 'n';
            }
            json_object_get_property(root, "linkSpeed", &propvalue);
//             s->linkSpeed = json_null == propvalue ? 0 : json_get_integer(propvalue);
            if (json_null == propvalue) {
                binds[14] = 'n';
            } else {
                s->linkSpeed = json_get_integer(propvalue);
            }
            json_object_get_property(root, "bootId", &propvalue);
//             s->bootId = json_null == propvalue ? 0 : json_get_integer(propvalue);
            if (json_null == propvalue) {
                binds[15] = 'n';
            } else {
                s->bootId = json_get_integer(propvalue);
            }
            json_document_destroy(doc);
            {
                request_t *req;
                json_document_t *doc;

                req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/dedicated/server/%s/serviceInfos", server_name);
                request_success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
                request_destroy(req);
                if (request_success) {
                    json_value_t root, propvalue;

                    root = json_document_get_root(doc);
                    JSON_GET_PROP_STRING(root, "contactAdmin", s->contactAdmin);
                    JSON_GET_PROP_STRING(root, "contactBilling", s->contactBilling);
                    JSON_GET_PROP_STRING(root, "contactTech", s->contactTech);
                    json_object_get_property(root, "engagedUpTo", &propvalue);
                    if (json_null != propvalue) {
                        date_parse_to_timestamp(json_get_string(propvalue), "%F", &s->engagedUpTo);
                    }
                    json_object_get_property(root, "expiration", &propvalue);
                    date_parse_to_timestamp(json_get_string(propvalue), "%F", &s->expiration);
                    json_object_get_property(root, "creation", &propvalue);
                    date_parse_to_timestamp(json_get_string(propvalue), "%F", &s->creation);
                    statement_bind( /* e = enum (int), d = date (int), |n = or NULL */
                        prepared[STMT_DEDICATED_UPSERT], binds/*"i" "isibisssisbssii" "isissi"*/,
                        // account_id (i)
                        current_account->id,
                        // id (i), name (s), datacenter (e), professional_use (b), support_level (e), commercial_range (s|n), ip (s), os (s), state (e), reverse (s|n), monitoring (b), rack (s), root_device (s|n), link_speed (i|n), boot_id (i|n)
                        s->serverId, s->name, s->datacenter, s->professionalUse, s->supportLevel, s->commercialRange, s->ip, s->os, s->state, s->reverse, s->monitoring, s->rack, s->rootDevice, s->linkSpeed, s->bootId,
                        // engaged_up_to (d|n), contact_billing (s), expiration (d), contact_tech (s), contact_admin (s), creation (d)
                        s->engagedUpTo, s->contactBilling, s->expiration, s->contactTech, s->contactAdmin, s->creation
                    );
                    statement_fetch(prepared[STMT_DEDICATED_UPSERT], error, "");
                    json_document_destroy(doc);
                }
            }
            fetch_server_boots(server_name, &s, error);
        }
    }

    return s;
}

static command_status_t fetch_servers(server_set_t *ss, bool force, error_t **error)
{
    if (!ss->uptodate || force) {
        request_t *req;
        bool request_success;
        json_document_t *doc;

        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/dedicated/server");
        request_success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (request_success) {
            Iterator it;
            json_value_t root;

            root = json_document_get_root(doc);
            hashtable_clear(ss->servers);
            json_array_to_iterator(&it, root);
            for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
                json_value_t v;

                v = (json_value_t) iterator_current(&it, NULL);
                fetch_server(ss, json_get_string(v), force, error);
            }
            iterator_close(&it);
            json_document_destroy(doc);
            ss->uptodate = TRUE;
        } else {
            return COMMAND_FAILURE;
        }
    }

    return COMMAND_SUCCESS;
}

static command_status_t dedicated_list(COMMAND_ARGS)
{
    table_t *t;
    Iterator it;
    server_t server;
    const char *boot;
    server_set_t *ss;
    command_status_t ret;
    dedicated_argument_t *args;

    USED(arg);
    USED(mainopts);
    FETCH_ACCOUNT_SERVERS(ss);
    args = (dedicated_argument_t *) arg;
    // populate
    // TODO: si on a aucun compte ou si nocache?
    if ((COMMAND_SUCCESS != (ret = fetch_servers(ss, args->nocache, error)))) {
        return ret;
    }
    // display
    t = table_new(
        20,
        _("name"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
        _("ip"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
        _("os"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
        _("reverse"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
        _("boot"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
        _("datacenter"), TABLE_TYPE_ENUM, datacenters,
        _("professionalUse"), TABLE_TYPE_BOOLEAN,
        _("supportLevel"), TABLE_TYPE_ENUM, support_levels,
        _("commercialRange"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
        _("state"), TABLE_TYPE_ENUM, states,
        _("monitoring"), TABLE_TYPE_BOOLEAN,
        _("rack"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
        _("rootDevice"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
        _("linkSpeed"), TABLE_TYPE_INT,
        _("engagedUpTo"), TABLE_TYPE_DATE,
        _("contactBilling"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
        _("expiration"), TABLE_TYPE_DATE,
        _("contactTech"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
        _("contactAdmin"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
        _("creation"), TABLE_TYPE_DATE
    );
    statement_bind(prepared[STMT_DEDICATED_LIST], "i", current_account->id);
    statement_to_iterator(&it, prepared[STMT_DEDICATED_LIST],
        "sssssibisibssi" "isissi",
        &server.name, &server.ip, &server.os, &server.reverse, &boot, &server.datacenter, &server.professionalUse, &server.supportLevel, &server.commercialRange, &server.state, &server.monitoring, &server.rack, &server.rootDevice, &server.linkSpeed,
        &server.engagedUpTo, &server.contactBilling, &server.expiration, &server.contactTech, &server.contactAdmin, &server.creation
    );
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        iterator_current(&it, NULL);
        table_store(t,
            server.name, server.ip, server.os, server.reverse, boot, server.datacenter, server.professionalUse, server.supportLevel, server.commercialRange, server.state, server.monitoring, server.rack, server.rootDevice, server.linkSpeed,
            server.engagedUpTo, server.contactBilling, server.expiration, server.contactTech, server.contactAdmin, server.creation
        );
    }
    iterator_close(&it);
    table_display(t, TABLE_FLAG_NONE);
    table_destroy(t);

    return COMMAND_SUCCESS;
}

static command_status_t dedicated_check(COMMAND_ARGS)
{
    bool success;
    server_set_t *ss;

    USED(arg);
    USED(mainopts);
    FETCH_ACCOUNT_SERVERS(ss);
    // populate
    if ((success = (COMMAND_SUCCESS == fetch_servers(ss, FALSE, error)))) {
        time_t now;
        Iterator it;

        now = time(NULL);
        hashtable_to_iterator(&it, ss->servers);
        for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
            request_t *req;
            json_document_t *doc;
            const char *server_name;

            iterator_current(&it, (void **) &server_name);
            // request
            req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/dedicated/server/%s/serviceInfos", server_name);
            success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
            request_destroy(req);
            // response
            if (success) {
                json_value_t root, expiration;
                time_t server_expiration;

                root = json_document_get_root(doc);
                if (json_object_get_property(root, "expiration", &expiration)) {
                    if (date_parse_to_timestamp(json_get_string(expiration), NULL, &server_expiration)) {
                        int diff_days;

                        diff_days = date_diff_in_days(server_expiration, now);
                        if (diff_days > 0 && diff_days < 3000) {
                            printf("%s expires in %d days\n", server_name, diff_days);
                        }
                    }
                }
                json_document_destroy(doc);
            }
        }
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t dedicated_reboot(COMMAND_ARGS)
{
    request_t *req;
    bool request_success;
    json_document_t *doc;
    dedicated_argument_t *args;

    USED(mainopts);
    request_success = TRUE;
    args = (dedicated_argument_t *) arg;
    assert(NULL != args->server_name);
    // TODO: check server exists?
    if (confirm(mainopts, _("Confirm hard reboot of %s"), args->server_name)) {
        req = request_new(REQUEST_FLAG_SIGN, HTTP_POST, NULL, API_BASE_URL "/dedicated/server/%s/reboot", args->server_name);
        request_success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (request_success) {
            // parse response to register the task. Display it? ("This request to hard reboot %s is registered as task #%d (see dedicated %s task %d to see its status)")
            json_document_destroy(doc);
        }
    }

    return request_success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t dedicated_boot_list(COMMAND_ARGS)
{
    table_t *t;
    boot_t boot;
    Iterator it;
    dedicated_argument_t *args;

    USED(error);
    USED(mainopts);
    // TODO: fetch servers here if needed?
    args = (dedicated_argument_t *) arg;
    assert(NULL != args->server_name);
    printf(_("Available boots for '%s':\n"), args->server_name);
    t = table_new(
        3,
        _("bootType"), TABLE_TYPE_ENUM, boot_types,
        _("kernel"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
        _("description"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE
    );
    statement_bind(prepared[STMT_BOOT_LIST], "is", current_account->id, args->server_name);
    statement_to_iterator(&it, prepared[STMT_BOOT_LIST], "iss", &boot.type, &boot.kernel, &boot.description);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        iterator_current(&it, NULL);
        table_store(t, boot.type, boot.kernel, boot.description);
    }
    iterator_close(&it);
    table_display(t, TABLE_FLAG_NONE);
    table_destroy(t);

    return COMMAND_SUCCESS;
}

static command_status_t dedicated_boot_get(COMMAND_ARGS)
{
    boot_t boot;
    bool success;
    dedicated_argument_t *args;

    USED(mainopts);
    args = (dedicated_argument_t *) arg;
    assert(NULL != args->server_name);
    statement_bind(prepared[STMT_DEDICATED_CURRENT_BOOT], "is", current_account->id, args->server_name);
    success = statement_fetch(prepared[STMT_DEDICATED_CURRENT_BOOT], error, "iss", &boot.type, &boot.kernel, &boot.description);
    if (success) {
        printf(_("Current boot for %s is:  %s (%s): %s\n"), args->server_name, boot.kernel, boot_types[boot.type], boot.description);
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t dedicated_boot_set(COMMAND_ARGS)
{
    int boot_id;
    bool success;
    dedicated_argument_t *args;

    USED(mainopts);
    args = (dedicated_argument_t *) arg;
    assert(NULL != args->server_name);
    assert(NULL != args->boot_name);
    statement_bind(prepared[STMT_B_D_FIND_BY_NAME], "iss", current_account->id, args->server_name, args->boot_name);
    success = statement_fetch(prepared[STMT_B_D_FIND_BY_NAME], error, "i", &boot_id);
    if (success) {
        request_t *req;
        json_document_t *reqdoc;

        // data
        {
            json_value_t root;

            reqdoc = json_document_new();
            root = json_object();
            json_object_set_property(root, "bootId", json_integer(boot_id));
            json_document_set_root(reqdoc, root);
        }
        // request
        req = request_new(REQUEST_FLAG_SIGN | REQUEST_FLAG_JSON, HTTP_PUT, reqdoc, API_BASE_URL "/dedicated/server/%s", args->server_name);
        success = request_execute(req, RESPONSE_IGNORE, NULL, error);
        request_destroy(req);
        json_document_destroy(reqdoc);
        error_set(error, INFO, _("This new boot will only be effective on the next (re)boot of '%s'"), args->server_name);
    } else {
        error_set(error, NOTICE, _("Any boot named '%s' was found for server '%s'"), args->boot_name, args->server_name);
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static bool fetch_ip_block(const char * const server_ip, char **ip, error_t **error)
{
    bool success;
    request_t *req;
    json_document_t *doc;

    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/ip?ip=%s", server_ip);
    success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
    request_destroy(req);
    if (success) {
        json_value_t root, ipBlock;

        root = json_document_get_root(doc);
        ipBlock = json_array_get_at(root, 0);
        *ip = strdup(json_get_string(ipBlock));
        json_document_destroy(doc);
    }

    return success;
}

static command_status_t dedicated_reverse_set_delete(COMMAND_ARGS, bool set)
{
    char *ip;
    server_t *s;
    bool success;
    server_set_t *ss;
    dedicated_argument_t *args;

    USED(mainopts);
    args = (dedicated_argument_t *) arg;
    assert(NULL != args->server_name);
    if (set) {
        assert(NULL != args->reverse);
    }
    FETCH_ACCOUNT_SERVERS(ss);
    s = fetch_server(ss, args->server_name, FALSE, error);
    if ((success = (s != NULL))) {
        if ((success = fetch_ip_block(s->ip, &ip, error))) {
            request_t *req;
            json_document_t *doc;

            if (set) {
                json_value_t root;

                doc = json_document_new();
                root = json_object();
                json_object_set_property(root, "ipReverse", json_string(s->ip));
                json_object_set_property(root, "reverse", json_string(args->reverse));
                json_document_set_root(doc, root);
                req = request_new(REQUEST_FLAG_SIGN | REQUEST_FLAG_JSON, HTTP_POST, doc, API_BASE_URL "/ip/%s/reverse", ip);
            } else {
                req = request_new(REQUEST_FLAG_SIGN, HTTP_DELETE, NULL, API_BASE_URL "/ip/%s/reverse/%s", ip, s->ip);
            }
            success = request_execute(req, RESPONSE_IGNORE, NULL, error);
            request_destroy(req);
            if (success) {
                FREE(s, reverse);
                if (set) {
                    json_document_destroy(doc);
                    s->reverse = strdup(args->reverse);
                } else {
                    INIT(s, reverse);
                }
            }
            free(ip);
        }
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t dedicated_reverse_delete(COMMAND_ARGS)
{
    return dedicated_reverse_set_delete(RELAY_COMMAND_ARGS, FALSE);
}

static command_status_t dedicated_reverse_set(COMMAND_ARGS)
{
    return dedicated_reverse_set_delete(RELAY_COMMAND_ARGS, TRUE);
}

static command_status_t dedicated_mrtg(COMMAND_ARGS)
{
    bool success;
    request_t *req;
    json_document_t *doc;
    dedicated_argument_t *args;

    USED(mainopts);
    args = (dedicated_argument_t *) arg;
    assert(NULL != args->server_name);
#if 1
    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/dedicated/server/%s/mrtg?period=%s&type=%s", args->server_name, mrtg_periods[args->mrtg_period], mrtg_types[args->mrtg_type]);
    success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
    request_destroy(req);
#else
    success = TRUE;
    doc = json_document_parse("[{\"timestamp\":1435140540,\"value\":{\"unit\":\"bps\",\"value\":2281.533}},{\"timestamp\":1435140600,\"value\":{\"unit\":\"bps\",\"value\":1981.95}},{\"timestamp\":1435140660,\"value\":{\"unit\":\"bps\",\"value\":1922.11}},{\"timestamp\":1435140720,\"value\":{\"unit\":\"bps\",\"value\":2215.201}},{\"timestamp\":1435140780,\"value\":{\"unit\":\"bps\",\"value\":1824.761}},{\"timestamp\":1435140840,\"value\":{\"unit\":\"bps\",\"value\":1856.88}},{\"timestamp\":1435140900,\"value\":{\"unit\":\"bps\",\"value\":1962.336}},{\"timestamp\":1435140960,\"value\":{\"unit\":\"bps\",\"value\":1702.629}},\
    {\"timestamp\":1435141020,\"value\":{\"unit\":\"bps\",\"value\":2194.6}},{\"timestamp\":1435141080,\"value\":{\"unit\":\"bps\",\"value\":2003.756}},{\"timestamp\":1435141140,\"value\":{\"unit\":\"bps\",\"value\":1941.444}},{\"timestamp\":1435141200,\"value\":{\"unit\":\"bps\",\"value\":1888.647}},{\"timestamp\":1435141260,\"value\":{\"unit\":\"bps\",\"value\":1877.333}},{\"timestamp\":1435141320,\"value\":{\"unit\":\"bps\",\"value\":1980.607}},{\"timestamp\":1435141380,\"value\":{\"unit\":\"bps\",\"value\":3732.604}}]", NULL);
#endif
    if (success) {
        Iterator it;
        graphic_t *g;
        json_value_t root;

        g = graphic_new();
        root = json_document_get_root(doc);
        json_array_to_iterator(&it, root);
        for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
            time_t sse;
            double value;
            json_value_t v, sv;

            v = (json_value_t) iterator_current(&it, NULL);
// debug("json_get_type(v) == JSON_TYPE_OBJECT : %d", json_get_type(v));
            JSON_GET_PROP_INT(v, "timestamp", sse);
            json_object_get_property(v, "value", &sv);
            JSON_GET_PROP_DOUBLE(sv, "value", value);
            graphic_store(g, sse, value);
        }
        iterator_close(&it);
        json_document_destroy(doc);
        graphic_display(g);
        graphic_destroy(g);
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static bool complete_servers(void *UNUSED(parsed_arguments), const char *current_argument, size_t current_argument_len, DPtrArray *possibilities, void *UNUSED(data))
{
    char *v;
    Iterator it;

    statement_bind(prepared[STMT_DEDICATED_COMPLETION], "is", current_account->id, current_argument);
    statement_to_iterator(&it, prepared[STMT_DEDICATED_COMPLETION], "s", &v); // TODO: bind only current_argument_len first characters of current_argument?
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        iterator_current(&it, NULL);
        dptrarray_push(possibilities, v); // TODO: values need to be freed
    }
    iterator_close(&it);

    return TRUE;
}

static bool complete_boots(void *parsed_arguments, const char *current_argument, size_t current_argument_len, DPtrArray *possibilities, void *UNUSED(data))
{
    char *v;
    Iterator it;
    dedicated_argument_t *args;

    args = (dedicated_argument_t *) parsed_arguments;
    assert(NULL != args->server_name);
    statement_bind(prepared[STMT_BOOT_COMPLETION], "iss", current_account->id, args->server_name, current_argument);
    statement_to_iterator(&it, prepared[STMT_BOOT_COMPLETION], "s", &v); // TODO: bind only current_argument_len first characters of current_argument?
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        iterator_current(&it, NULL);
        dptrarray_push(possibilities, v); // TODO: values need to be freed
    }
    iterator_close(&it);

    return TRUE;
}

static void dedicated_regcomm(graph_t *g)
{
    argument_t *arg_period, *arg_type;
    argument_t *arg_server, *arg_boot, *arg_reverse;
    argument_t *lit_boot, *lit_boot_list, *lit_boot_show;
    argument_t *lit_reverse, *lit_rev_set, *lit_rev_delete;
    argument_t *lit_dedicated, *lit_dedi_reboot, *lit_dedi_list, *lit_dedi_check, *lit_dedi_mrtg, *lit_dedi_nocache;

    // dedicated ...
    lit_dedicated = argument_create_literal("dedicated", NULL);
    lit_dedi_mrtg = argument_create_literal("mrtg", dedicated_mrtg);
    lit_dedi_list = argument_create_literal("list", dedicated_list);
    lit_dedi_check = argument_create_literal("check", dedicated_check);
    lit_dedi_reboot = argument_create_literal("reboot", dedicated_reboot);
    // dedicated <server> boot ...
    lit_boot = argument_create_literal("boot", /*NULL*/dedicated_boot_set);
    lit_boot_show = argument_create_literal("show", dedicated_boot_get);
    lit_boot_list = argument_create_literal("list", dedicated_boot_list);
    // dedicated <server> reverse ...
    lit_reverse = argument_create_literal("reverse", NULL);
    lit_rev_set = argument_create_literal("set", dedicated_reverse_set);
    lit_rev_delete = argument_create_literal("delete", dedicated_reverse_delete);

    lit_dedi_nocache = argument_create_relevant_literal(offsetof(dedicated_argument_t, nocache), "nocache", NULL);

    arg_server = argument_create_string(offsetof(dedicated_argument_t, server_name), "<server>", complete_servers, NULL);
    arg_boot = argument_create_string(offsetof(dedicated_argument_t, boot_name), "<boot>", complete_boots, NULL);
    arg_reverse = argument_create_string(offsetof(dedicated_argument_t, reverse), "<reverse>", NULL, NULL);
    arg_type = argument_create_choices(offsetof(dedicated_argument_t, mrtg_type), "<types>",  mrtg_types);
    arg_period = argument_create_choices(offsetof(dedicated_argument_t, mrtg_period), "<period>",  mrtg_periods);

    // dedicated ...
    graph_create_full_path(g, lit_dedicated, lit_dedi_list, NULL);
    graph_create_path(g, lit_dedi_list, NULL, lit_dedi_nocache, NULL);
    graph_create_full_path(g, lit_dedicated, lit_dedi_check, NULL);
    graph_create_full_path(g, lit_dedicated, arg_server, lit_dedi_reboot, NULL);
    graph_create_full_path(g, lit_dedicated, arg_server, lit_dedi_mrtg, arg_type, arg_period, NULL);
    // dedicated <server> boot ...
    graph_create_full_path(g, lit_dedicated, arg_server, lit_boot, lit_boot_list, NULL);
    graph_create_full_path(g, lit_dedicated, arg_server, lit_boot, lit_boot_show, NULL);
    graph_create_full_path(g, lit_dedicated, arg_server, lit_boot, arg_boot, NULL);
    // dedicated <server> reverse ...
    graph_create_full_path(g, lit_dedicated, arg_server, lit_reverse, lit_rev_delete, NULL);
    graph_create_full_path(g, lit_dedicated, arg_server, lit_reverse, lit_rev_set, arg_reverse, NULL);
}

static void dedicated_register_rules(json_value_t rules, bool ro)
{
    JSON_ADD_RULE(rules, "GET", "/ip");
    JSON_ADD_RULE(rules, "GET", "/dedicated/server");
    JSON_ADD_RULE(rules, "GET", "/dedicated/server/*");
    if (!ro) {
        JSON_ADD_RULE(rules, "POST", "/ip/*");
        JSON_ADD_RULE(rules, "DELETE", "/ip/*");
        JSON_ADD_RULE(rules, "PUT", "/dedicated/server/*");
        JSON_ADD_RULE(rules, "POST", "/dedicated/server/*");
        JSON_ADD_RULE(rules, "DELETE", "/dedicated/server/*");
    }
}

DECLARE_MODULE(dedicated) = {
    MODULE_NAME,
    dedicated_regcomm,
    dedicated_register_rules,
    dedicated_ctor,
    NULL,
    dedicated_dtor
};
