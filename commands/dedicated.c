#include <string.h>
#include <inttypes.h>

#include "common.h"
#include "command.h"
#include "date.h"
#include "util.h"
#include "graphic.h"
#include "modules/api.h"
#include "modules/table.h"
#include "modules/sqlite.h"
#include "commands/account.h"

#define MODULE_NAME "dedicated"

/**
 * an account may have 0, 1 or more servers
 * each server can have several:
 * - boots (at least 2 - rescue/hdd)
 *      + ... each boot can have 0, 1 or more options
 * - tasks
 * - ...
 **/

/**
 * TODO:
 * - cleanup: DELETE expired servers? DELETE them all when running fetch_servers?
 */

enum {
    // dedicated
    STMT_DEDICATED_LIST,
    STMT_DEDICATED_UPSERT,
    // specialized read (select)
    STMT_DEDICATED_GET_IP,
    STMT_DEDICATED_NEAR_EXPIRATION,
    STMT_DEDICATED_COMPLETION,
    STMT_DEDICATED_CURRENT_BOOT,
    // specialized update
    STMT_DEDICATED_SET_REVERSE,
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

#define BOOT_OUTPUT_BINDS "iss"

static sqlite_statement_t statements[STMT_COUNT] = {
    [ STMT_DEDICATED_LIST ]            = DECL_STMT("SELECT * FROM dedicated WHERE accountId = ?", "i", ""),
    [ STMT_DEDICATED_UPSERT ]          = DECL_STMT("INSERT OR REPLACE INTO dedicated(serverId, name, datacenter, professionalUse, supportLevel, commercialRange, ip, os, state, reverse, monitoring, rack, rootDevice, linkSpeed, bootId, engagedUpTo, contactBilling, expiration, contactTech, contactAdmin, creation, accountId) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", "isibisssisbssii" "isissi" "i", ""),
    [ STMT_DEDICATED_GET_IP ]          = DECL_STMT("SELECT ip FROM dedicated WHERE accountId = ? AND name = ?", "is", "s"),
    [ STMT_DEDICATED_NEAR_EXPIRATION ] = DECL_STMT("SELECT julianday(datetime(expiration, 'unixepoch', 'localtime')) - julianday('now') AS days, name FROM dedicated WHERE accountId = ? AND days < 120", "i", "is"),
    [ STMT_DEDICATED_SET_REVERSE ]     = DECL_STMT("UPDATE dedicated SET reverse = ? WHERE accountId = ? AND name = ?", "sis", ""),
    [ STMT_DEDICATED_COMPLETION ]      = DECL_STMT("SELECT name FROM dedicated WHERE accountId = ? AND name LIKE ? || '%'", "is", "s"),
    [ STMT_DEDICATED_CURRENT_BOOT ]    = DECL_STMT("SELECT boots.* FROM boots JOIN dedicated ON boots.bootId = dedicated.bootId WHERE accountId = ? AND name = ?", "is", BOOT_OUTPUT_BINDS),
    [ STMT_BOOT_LIST ]                 = DECL_STMT("SELECT boots.* FROM boots JOIN boots_dedicated ON boots_dedicated.bootId = boots.bootId JOIN dedicated ON boots_dedicated.serverId = dedicated.serverId WHERE accountId = ? AND name = ?", "is", BOOT_OUTPUT_BINDS),
    [ STMT_BOOT_UPSERT ]               = DECL_STMT("INSERT OR REPLACE INTO boots(bootId, bootType, kernel, description) VALUES(:bootId, :bootType, :kernel, :description)", "iiss", ""),
    [ STMT_BOOT_COMPLETION ]           = DECL_STMT("SELECT boots.*/*kernel*/ FROM boots JOIN boots_dedicated ON boots_dedicated.bootId = boots.bootId JOIN dedicated ON boots_dedicated.serverId = dedicated.serverId WHERE accountId = ? AND name = ? AND kernel LIKE ? || '%'", "iss", "s"),
    [ STMT_B_D_LINK ]                  = DECL_STMT("INSERT OR IGNORE INTO boots_dedicated(bootId, serverId) VALUES(?, ?)", "ii", ""),
    [ STMT_B_D_FIND_BY_NAME ]          = DECL_STMT("SELECT boots.bootId FROM boots JOIN boots_dedicated ON boots_dedicated.bootId = boots.bootId JOIN dedicated ON boots_dedicated.serverId = dedicated.serverId WHERE accountId = ? AND name = ? AND kernel = ?", "iss", "i"),
};

#define FETCH_SERVERS_IF_NEEDED \
    do { \
        time_t updated_at; \
 \
        if (!fetch_servers(args->nocache || (!account_get_last_fetch_for(MODULE_NAME, &updated_at, error) && NULL == *error), error)) { \
            return COMMAND_FAILURE; \
        } \
    } while (0);

// describe a dedicated server
typedef struct {
    modelized_t data;
    int accountId;
    int datacenter;
    bool professionalUse;
    int supportLevel;
    char *ip;
    char *name;
    char *commercialRange;
    char *os;
    int state;
    char *reverse;
    int serverId;
    bool monitoring;
    char *rack;
    char *rootDevice;
    int linkSpeed;
    int bootId;
    time_t engagedUpTo;
    char *contactBilling;
    time_t expiration;
    char *contactTech;
    char *contactAdmin;
    time_t creation;
} server_t;

// describe a boot
typedef struct {
    modelized_t data;
    int bootType;
    int bootId;
    const char *kernel;
    const char *description;
} boot_t;

// arguments
typedef struct {
    bool nocache;
//     int boot_type;
    char *boot_name;
    char *server_name;
    char *reverse;
    int mrtg_period;
    int mrtg_type;
} dedicated_argument_t;

// enums
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

static const char *mrtg_periods[] = {
    "daily",
    "hourly",
    "monthly",
    "weekly",
    "yearly",
    NULL
};

static const char *mrtg_types[] = {
    "errors:download",
    "errors:upload",
    "packets:download",
    "packets:upload",
    "traffic:download",
    "traffic:upload",
    NULL
};

static model_t *server_model, *boot_model, *task_model;

#undef DECL_FIELD_STRUCT_NAME
#define DECL_FIELD_STRUCT_NAME server_t
static model_field_t server_fields[] = {
    DECL_FIELD_INT(N_("accountId"), accountId, MODEL_FLAG_INTERNAL),
    DECL_FIELD_INT(N_("serverId"), serverId, MODEL_FLAG_PRIMARY | MODEL_FLAG_INTERNAL),
    DECL_FIELD_STRING(N_("name"), name, MODEL_FLAG_UNIQUE),
    DECL_FIELD_ENUM(N_("datacenter"), datacenter, 0, datacenters),
    DECL_FIELD_BOOL(N_("professionalUse"), professionalUse, 0),
    DECL_FIELD_ENUM(N_("supportLevel"), supportLevel, 0, support_levels),
    DECL_FIELD_STRING(N_("commercialRange"), commercialRange, MODEL_FLAG_NULLABLE),
    DECL_FIELD_STRING(N_("ip"), ip, 0),
    DECL_FIELD_STRING(N_("os"), os, 0),
    DECL_FIELD_ENUM(N_("state"), state, 0, states),
    DECL_FIELD_STRING(N_("reverse"), reverse, MODEL_FLAG_NULLABLE),
    DECL_FIELD_BOOL(N_("monitoring"), monitoring, 0),
    DECL_FIELD_STRING(N_("rack"), rack, 0),
    DECL_FIELD_STRING(N_("rootDevice"), rootDevice, MODEL_FLAG_NULLABLE),
    DECL_FIELD_INT(N_("linkSpeed"), linkSpeed, MODEL_FLAG_NULLABLE),
    DECL_FIELD_INT(N_("bootId"), bootId, MODEL_FLAG_NULLABLE),
    DECL_FIELD_DATE(N_("engagedUpTo"), engagedUpTo, MODEL_FLAG_NULLABLE),
    DECL_FIELD_STRING(N_("contactBilling"), contactBilling, 0),
    DECL_FIELD_DATE(N_("expiration"), expiration, 0),
    DECL_FIELD_STRING(N_("contactTech"), contactTech, 0),
    DECL_FIELD_STRING(N_("contactAdmin"), contactAdmin, 0),
    DECL_FIELD_DATE(N_("creation"), creation, 0),
    MODEL_FIELD_SENTINEL
};

static const char *boot_to_name(void *ptr)
{
    return strdup(((boot_t *) ptr)->kernel);
}

static const char *boot_to_s(void *ptr)
{
    return strdup(((boot_t *) ptr)->description); // TODO: we create a second copy of the string (the first was in sqlite module)
}

#undef DECL_FIELD_STRUCT_NAME
#define DECL_FIELD_STRUCT_NAME boot_t
static model_field_t boot_fields[] = {
    DECL_FIELD_INT(N_("bootId"), bootId, MODEL_FLAG_PRIMARY | MODEL_FLAG_INTERNAL),
    DECL_FIELD_ENUM(N_("bootType"), bootType, 0, boot_types),
    DECL_FIELD_STRING(N_("kernel"), kernel, 0),
    DECL_FIELD_STRING(N_("description"), description, 0),
    MODEL_FIELD_SENTINEL
};

static const char * const task_functions[] = {
    "addVirtualMac",
    "addWindowSplaFromExistingSerial",
    "applyBackupFtpAcls",
    "applyBackupFtpQuota",
    "changePasswordBackupFTP",
    "checkAndReleaseIp",
    "createBackupFTP",
    "createOrUpdateRipeOrg",
    "createPrivateNetwork",
    "disableFirewall",
    "enableFirewall",
    "genericMoveFloatingIp",
    "hardReboot",
    "migrateBackupFTP",
    "moveFloatingIp",
    "moveVirtualMac",
    "reinstallServer",
    "releaseIp",
    "removeBackupFTP",
    "removeVirtualMac",
    "requestAccessIPMI",
    "resetIPMI",
    "resetIPMISession",
    "testIPMIhttp",
    "testIPMIpassword",
    "testIPMIping",
    "virtualMacAdd",
    "virtualMacDelete",
    NULL
};

static const char * const task_status[] = {
    "cancelled",
    "customerError",
    "doing",
    "done",
    "init",
    "ovhError",
    "todo",
    NULL
};

typedef struct {
    modelized_t data;
    int taskId;
    int function;
    time_t lastUpdate;
    char *comment;
    int status;
    time_t startDate;
    time_t doneDate;
} task_t;

#undef DECL_FIELD_STRUCT_NAME
#define DECL_FIELD_STRUCT_NAME task_t
static model_field_t task_fields[] = {
    DECL_FIELD_INT(N_("taskId"), taskId, MODEL_FLAG_PRIMARY | MODEL_FLAG_INTERNAL),
    DECL_FIELD_ENUM(N_("function"), function, 0, task_functions),
    DECL_FIELD_DATETIME(N_("lastUpdate"), lastUpdate, MODEL_FLAG_NULLABLE),
    DECL_FIELD_STRING(N_("comment"), comment, MODEL_FLAG_NULLABLE),
    DECL_FIELD_ENUM(N_("status"), status, 0, task_status),
    DECL_FIELD_DATETIME(N_("startDate"), startDate, 0),
    DECL_FIELD_DATETIME(N_("doneDate"), doneDate, MODEL_FLAG_NULLABLE),
    MODEL_FIELD_SENTINEL
};

sqlite_migration_t dedicated_migrations[] = {
    {
        1,
        "PRAGMA foreign_keys = off;\
        BEGIN TRANSACTION;\
        CREATE TABLE dedicated_tmp(\n\
            accountId INT NOT NULL REFERENCES accounts(id) ON UPDATE CASCADE ON DELETE CASCADE,\n\
            -- From GET /dedicated/server/{serviceName}\n\
            serverId INTEGER NOT NULL PRIMARY KEY, -- OVH ID\n\
            name TEXT NOT NULL UNIQUE,\n\
            datacenter INT NOT NULL, -- enum\n\
            professionalUse INT NOT NULL, -- bool\n\
            supportLevel INT NOT NULL, -- enum\n\
            commercialRange TEXT, -- nullable\n\
            ip TEXT NOT NULL, -- unique ?\n\
            os TEXT NOT NULL,\n\
            state INT NOT NULL, -- enum\n\
            reverse TEXT, -- nullable\n\
            monitoring INT NOT NULL, -- bool\n\
            rack TEXT NOT NULL,\n\
            rootDevice TEXT, -- nullable\n\
            linkSpeed INT, -- nullable\n\
            bootId INT REFERENCES boots(bootId) ON UPDATE CASCADE ON DELETE CASCADE,\n\
            -- From GET /dedicated/server/{serviceName}/serviceInfos\n\
            -- status INT NOT NULL, -- enum\n\
            engagedUpTo INT, -- data, nullable\n\
            -- possibleRenewPeriod: array of int (JSON response)\n\
            contactBilling TEXT NOT NULL,\n\
            -- renew: subobkect (JSON response)\n\
            -- domain TEXT NOT NULL, -- same as name?\n\
            expiration INT NOT NULL, -- date\n\
            contactTech TEXT NOT NULL,\n\
            contactAdmin TEXT NOT NULL,\n\
            creation INT NOT NULL -- date\n\
        );\
        INSERT INTO dedicated_tmp SELECT * FROM dedicated;\
        DROP TABLE dedicated;\
        ALTER TABLE dedicated_tmp RENAME TO dedicated;\
        PRAGMA foreign_key_check;\
        COMMIT TRANSACTION;\
        PRAGMA foreign_keys = on;"
    }
};

static bool dedicated_ctor(error_t **error)
{
    boot_model = model_new("boots", sizeof(boot_t), boot_fields, ARRAY_SIZE(boot_fields) - 1);
    boot_model->to_s = boot_to_s;
    boot_model->to_name = boot_to_name;
    task_model = model_new("tasks", sizeof(task_t), task_fields, ARRAY_SIZE(task_fields) - 1);
    server_model = model_new("servers", sizeof(server_t), server_fields, ARRAY_SIZE(server_fields) - 1);

    if (!create_or_migrate("boots", "CREATE TABLE boots(\n\
        bootId INTEGER NOT NULL PRIMARY KEY, -- OVH ID (bootId)\n\
        bootType INT NOT NULL, -- enum\n\
        kernel TEXT NOT NULL,\n\
        description TEXT NOT NULL\n\
    )", NULL, 0, error)) {
        return FALSE;
    }
    if (!create_or_migrate("dedicated", "CREATE TABLE dedicated(\n\
        accountId INT NOT NULL REFERENCES accounts(id) ON UPDATE CASCADE ON DELETE CASCADE,\n\
        -- From GET /dedicated/server/{serviceName}\n\
        serverId INTEGER NOT NULL PRIMARY KEY, -- OVH ID\n\
        name TEXT NOT NULL UNIQUE,\n\
        datacenter INT NOT NULL, -- enum\n\
        professionalUse INT NOT NULL, -- bool\n\
        supportLevel INT NOT NULL, -- enum\n\
        commercialRange TEXT, -- nullable\n\
        ip TEXT NOT NULL, -- unique ?\n\
        os TEXT NOT NULL,\n\
        state INT NOT NULL, -- enum\n\
        reverse TEXT, -- nullable\n\
        monitoring INT NOT NULL, -- bool\n\
        rack TEXT NOT NULL,\n\
        rootDevice TEXT, -- nullable\n\
        linkSpeed INT, -- nullable\n\
        bootId INT REFERENCES boots(bootId) ON UPDATE CASCADE ON DELETE CASCADE,\n\
        -- From GET /dedicated/server/{serviceName}/serviceInfos\n\
        -- status INT NOT NULL, -- enum\n\
        engagedUpTo INT, -- data, nullable\n\
        -- possibleRenewPeriod: array of int (JSON response)\n\
        contactBilling TEXT NOT NULL,\n\
        -- renew: subobkect (JSON response)\n\
        -- domain TEXT NOT NULL, -- same as name?\n\
        expiration INT NOT NULL, -- date\n\
        contactTech TEXT NOT NULL,\n\
        contactAdmin TEXT NOT NULL,\n\
        creation INT NOT NULL -- date\n\
    )", dedicated_migrations, ARRAY_SIZE(dedicated_migrations), error)) {
        return FALSE;
    }
    if (!create_or_migrate("boots_dedicated", "CREATE TABLE boots_dedicated(\n\
        serverId INT NOT NULL REFERENCES dedicated(serverId) ON UPDATE CASCADE ON DELETE CASCADE,\n\
        bootId INT NOT NULL REFERENCES boots(bootId) ON UPDATE CASCADE ON DELETE CASCADE,\n\
        PRIMARY KEY (serverId, bootId)\n\
    )", NULL, 0, error)) {
        return FALSE;
    }

    if (!statement_batched_prepare(statements, STMT_COUNT, error)) {
        return FALSE;
    }

    return TRUE;
}

static void dedicated_dtor(void)
{
    statement_batched_finalize(statements, STMT_COUNT);
    model_destroy(boot_model);
    model_destroy(task_model);
    model_destroy(server_model);
}

static bool parse_boot(server_t *s, json_document_t *doc, error_t **error)
{
    boot_t boot;

    modelized_init(boot_model, (modelized_t *) &boot);
    json_object_to_modelized(json_document_get_root(doc), (modelized_t *) &boot, FALSE, NULL);
    statement_bind_from_model(&statements[STMT_BOOT_UPSERT], NULL, (modelized_t *) &boot);
    statement_fetch(&statements[STMT_BOOT_UPSERT], error);
    if (statement_fetch(&statements[STMT_BOOT_UPSERT], error) || NULL == *error) {
        statement_bind(&statements[STMT_B_D_LINK], NULL, boot.bootId, s->serverId);
        statement_fetch(&statements[STMT_B_D_LINK], error);
    }
    json_document_destroy(doc);

    return NULL == *error;
}

static bool fetch_server_boots(server_t *s, error_t **error)
{
    bool success;
    request_t *req;
    json_document_t *doc;

    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/dedicated/server/%s/boot", s->name);
    success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
    request_destroy(req);
    // result
    if (success) {
        Iterator it;
        json_value_t root;

        root = json_document_get_root(doc);
        json_array_to_iterator(&it, root);
        for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
            json_value_t v;
            json_document_t *doc;

            v = (json_value_t) iterator_current(&it, NULL);
            req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/dedicated/server/%s/boot/%u", s->name, json_get_integer(v));
            success = request_execute(req, RESPONSE_JSON, (void **) &doc, error); // success is assumed to be TRUE before the first iteration
            request_destroy(req);
            // result
            success = parse_boot(s, doc, error);
        }
        iterator_close(&it);
        json_document_destroy(doc);
    }

    return success;
}

static bool fetch_server(const char * const server_name, bool force, error_t **error)
{
    bool success;

    success = TRUE;
    if (force) {
        server_t s;
        request_t *req;
        json_document_t *doc;

        modelized_init(server_model, (modelized_t *) &s);
        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/dedicated/server/%s", server_name);
        success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (success) {
            bool nulls[22] = { FALSE };

            CHECK_NULLS_LENGTH(nulls, statements[STMT_DEDICATED_UPSERT]);
            json_object_to_modelized(json_document_get_root(doc), (modelized_t *) &s, FALSE, nulls);
            {
                request_t *req;
                json_document_t *doc;

                req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/dedicated/server/%s/serviceInfos", server_name);
                success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
                request_destroy(req);
                if (success) {
                    json_object_to_modelized(json_document_get_root(doc), (modelized_t *) &s, FALSE, nulls);
                    statement_bind( /* e = enum (int), d = date (int), |n = or NULL */
                        &statements[STMT_DEDICATED_UPSERT], nulls /* "isibisssisbssii" "isissi" "i" */,
                        // id (i), name (s), datacenter (e), professionalUse (b), supportLevel (e), commercialRange (s|n), ip (s), os (s), state (e), reverse (s|n), monitoring (b), rack (s), rootDevice (s|n), linkSpeed (i|n), bootId (i|n)
                        s.serverId, s.name, s.datacenter, s.professionalUse, s.supportLevel, s.commercialRange, s.ip, s.os, s.state, s.reverse, s.monitoring, s.rack, s.rootDevice, s.linkSpeed, s.bootId,
                        // engagedUpTo (d|n), contactBilling (s), expiration (d), contactTech (s), contactAdmin (s), creation (d)
                        s.engagedUpTo, s.contactBilling, s.expiration, s.contactTech, s.contactAdmin, s.creation,
                        // accountId (i)
                        current_account->id
                    );
                    statement_fetch(&statements[STMT_DEDICATED_UPSERT], error);
                    json_document_destroy(doc);
                }
            }
            json_document_destroy(doc);
            success = fetch_server_boots(&s, error);
        }
    }

    return success;
}

static bool fetch_servers(bool force, error_t **error)
{
    bool success;

    success = TRUE;
    if (force) {
        request_t *req;
        json_document_t *doc;

        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/dedicated/server");
        success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (success) {
            Iterator it;
            json_value_t root;

            root = json_document_get_root(doc);
            json_array_to_iterator(&it, root);
            for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
                json_value_t v;

                v = (json_value_t) iterator_current(&it, NULL);
                success = fetch_server(json_get_string(v), force, error);
            }
            iterator_close(&it);
            json_document_destroy(doc);
        }
        if (success) {
            account_set_last_fetch_for(MODULE_NAME, error);
        }
    }

    return success;
}

static command_status_t dedicated_list(COMMAND_ARGS)
{
    dedicated_argument_t *args;

    USED(arg);
    USED(mainopts);
    args = (dedicated_argument_t *) arg;
    // populate
    FETCH_SERVERS_IF_NEEDED;
    // display
    statement_bind(&statements[STMT_DEDICATED_LIST], NULL, current_account->id);

    return statement_to_table(server_model, &statements[STMT_DEDICATED_LIST]);
}

static command_status_t dedicated_check(COMMAND_ARGS)
{
    Iterator it;
    int64_t days;
    char *server_name;
    dedicated_argument_t *args;

    USED(mainopts);
    args = (dedicated_argument_t *) arg;
    FETCH_SERVERS_IF_NEEDED;
    statement_bind(&statements[STMT_DEDICATED_NEAR_EXPIRATION], NULL, current_account->id);
    statement_to_iterator(&it, &statements[STMT_DEDICATED_NEAR_EXPIRATION], &days, &server_name);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        iterator_current(&it, NULL);
        printf("%s expires in %" PRIi64 " days\n", server_name, days);
        free(server_name);
    }
    iterator_close(&it);

    return COMMAND_SUCCESS;
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
        req = request_new(REQUEST_FLAG_SIGN, HTTP_POST, NULL, error, API_BASE_URL "/dedicated/server/%s/reboot", args->server_name);
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
    dedicated_argument_t *args;

    USED(mainopts);
    args = (dedicated_argument_t *) arg;
    assert(NULL != args->server_name);
    FETCH_SERVERS_IF_NEEDED;
    printf(_("Available boots for '%s':\n"), args->server_name);
    statement_bind(&statements[STMT_BOOT_LIST], NULL, current_account->id, args->server_name);

    return statement_to_table(boot_model, &statements[STMT_BOOT_LIST]);
}

static command_status_t dedicated_boot_get(COMMAND_ARGS)
{
    boot_t boot;
    bool success;
    dedicated_argument_t *args;

    USED(mainopts);
    args = (dedicated_argument_t *) arg;
    assert(NULL != args->server_name);
    modelized_init(boot_model, (modelized_t *) &boot);
    statement_bind(&statements[STMT_DEDICATED_CURRENT_BOOT], NULL, current_account->id, args->server_name);
    success = statement_fetch_to_model(&statements[STMT_DEDICATED_CURRENT_BOOT], (modelized_t *) &boot, FALSE, error);
    if (success) {
        printf(_("Current boot for %s is:  %s (%s): %s\n"), args->server_name, boot.kernel, boot_types[boot.bootType], boot.description);
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t dedicated_boot_set(COMMAND_ARGS)
{
    int bootId;
    bool success;
    dedicated_argument_t *args;

    USED(mainopts);
    args = (dedicated_argument_t *) arg;
    assert(NULL != args->server_name);
    assert(NULL != args->boot_name);
    statement_bind(&statements[STMT_B_D_FIND_BY_NAME], NULL, current_account->id, args->server_name, args->boot_name);
    success = statement_fetch(&statements[STMT_B_D_FIND_BY_NAME], error, &bootId);
    if (success) {
        request_t *req;
        json_document_t *reqdoc;

        // data
        {
            json_value_t root;

            reqdoc = json_document_new();
            root = json_object();
            json_object_set_property(root, "bootId", json_integer(bootId));
            json_document_set_root(reqdoc, root);
        }
        // request
        req = request_new(REQUEST_FLAG_SIGN | REQUEST_FLAG_JSON, HTTP_PUT, reqdoc, error, API_BASE_URL "/dedicated/server/%s", args->server_name);
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

    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/ip?ip=%s", server_ip);
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
    bool success;
    char *ip, *ipblock;
    dedicated_argument_t *args;

    USED(mainopts);
    args = (dedicated_argument_t *) arg;
    assert(NULL != args->server_name);
    if (set) {
        assert(NULL != args->reverse);
    }
    statement_bind(&statements[STMT_DEDICATED_GET_IP], NULL, current_account->id, args->server_name);
    success = statement_fetch(&statements[STMT_DEDICATED_GET_IP], error, &ip);
    if (success) {
        if ((success = fetch_ip_block(ip, &ipblock, error))) {
            request_t *req;
            json_document_t *doc;

            if (set) {
                json_value_t root;

                doc = json_document_new();
                root = json_object();
                json_object_set_property(root, "ipReverse", json_string(ip));
                json_object_set_property(root, "reverse", json_string(args->reverse));
                json_document_set_root(doc, root);
                req = request_new(REQUEST_FLAG_SIGN | REQUEST_FLAG_JSON, HTTP_POST, doc, error, API_BASE_URL "/ip/%s/reverse", ipblock);
            } else {
                req = request_new(REQUEST_FLAG_SIGN, HTTP_DELETE, NULL, error, API_BASE_URL "/ip/%s/reverse/%s", ipblock, ip);
            }
            success = request_execute(req, RESPONSE_IGNORE, NULL, error);
            request_destroy(req);
            if (success) {
                if (set) {
                    json_document_destroy(doc);
                }
                statement_bind(&statements[STMT_DEDICATED_SET_REVERSE], NULL, args->reverse, current_account->id, args->server_name);
                statement_fetch(&statements[STMT_DEDICATED_SET_REVERSE], error);
                success = 1 == sqlite_affected_rows();
            }
            free(ipblock);
        }
        free(ip);
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
    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/dedicated/server/%s/mrtg?period=%s&type=%s", args->server_name, mrtg_periods[args->mrtg_period], mrtg_types[args->mrtg_type]);
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

static bool fetch_task(const char *server_name, task_t *task, error_t **error)
{
    bool success;
    request_t *req;
    json_document_t *doc;

    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/dedicated/server/%s/task/%d", server_name, task->taskId);
    success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
    request_destroy(req);
    if (success) {
        json_object_to_modelized(json_document_get_root(doc), (modelized_t *) task, TRUE, NULL);
        json_document_destroy(doc);
    }

    return success;
}

static command_status_t dedicated_task_list(COMMAND_ARGS)
{
    table_t *t;
    bool success;
    request_t *req;
    json_document_t *doc;
    dedicated_argument_t *args;

    USED(mainopts);
    args = (dedicated_argument_t *) arg;
    assert(NULL != args->server_name);
    t = table_new_from_model(task_model, TABLE_FLAG_DELEGATE);
    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/dedicated/server/%s/task", args->server_name);
    success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
    request_destroy(req);
    if (success) {
        task_t task;
        Iterator it;
        json_value_t root;

        modelized_init(task_model, (modelized_t *) &task);
        root = json_document_get_root(doc);
        json_array_to_iterator(&it, root);
        for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
            json_value_t v;

            v = (json_value_t) iterator_current(&it, NULL);
            task.taskId = json_get_integer(v);
            success = fetch_task(args->server_name, &task, error);
            table_store_modelized(t, (modelized_t *) &task);
        }
        iterator_close(&it);
        json_document_destroy(doc);
    }
    table_display(t, TABLE_FLAG_NONE);
    table_destroy(t);

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

#if 0
static command_status_t dedicated_task_wait(COMMAND_ARGS)
{
    task_t task;
    bool success;
//     request_t *req;
//     json_document_t *doc;

    USED(mainopts);
    modelized_init(task_model, (modelized_t *) &task);
#if 0
    while (1) {
        // GET /dedicated/server/{serviceName}/task/{taskId}
        if (task->doneDate) {
            break;
        }
        sleep(5);
    }
#else
    USED(arg);
    USED(error);
    success = TRUE;
#endif

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t dedicated_task_cancel(COMMAND_ARGS)
{
    bool success;
//     request_t *req;

    USED(mainopts);
#if 0
    // POST /dedicated/server/{serviceName}/task/{taskId}/cancel
#else
    USED(arg);
    USED(error);
    success = TRUE;
#endif

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}
#endif

static bool complete_servers(void *UNUSED(parsed_arguments), const char *current_argument, size_t current_argument_len, completer_t *possibilities, void *UNUSED(data))
{
    char *v;
    Iterator it;

    statement_bind(&statements[STMT_DEDICATED_COMPLETION], NULL, current_account->id, current_argument);
    statement_to_iterator(&it, &statements[STMT_DEDICATED_COMPLETION], &v); // TODO: bind only current_argument_len first characters of current_argument?
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        iterator_current(&it, NULL);
        completer_push(possibilities, v, TRUE);
    }
    iterator_close(&it);

    return TRUE;
}

static bool complete_boots(void *parsed_arguments, const char *current_argument, size_t current_argument_len, completer_t *possibilities, void *UNUSED(data))
{
    dedicated_argument_t *args;

    args = (dedicated_argument_t *) parsed_arguments;
    assert(NULL != args->server_name);
    statement_bind(&statements[STMT_BOOT_COMPLETION], NULL, current_account->id, args->server_name, current_argument);

    return complete_from_modelized(boot_model, &statements[STMT_BOOT_COMPLETION], possibilities);
}

static void dedicated_regcomm(graph_t *g)
{
    argument_t *arg_server;
    argument_t *lit_dedicated;

    lit_dedicated = argument_create_literal("dedicated", NULL, NULL);
    arg_server = argument_create_string(offsetof(dedicated_argument_t, server_name), "<server>", complete_servers, NULL);

    // dedicated ...
    {
        argument_t *arg_period, *arg_type;
        argument_t *lit_reboot, *lit_list, *lit_check, *lit_mrtg, *lit_nocache;

        lit_mrtg = argument_create_literal("mrtg", dedicated_mrtg, NULL);
        lit_list = argument_create_literal("list", dedicated_list, _("list your dedicated servers"));
        lit_check = argument_create_literal("check", dedicated_check, _("display servers about to expire"));
        lit_reboot = argument_create_literal("reboot", dedicated_reboot, _("hard reboot"));
        lit_nocache = argument_create_relevant_literal(offsetof(dedicated_argument_t, nocache), "nocache", NULL);

        arg_type = argument_create_choices(offsetof(dedicated_argument_t, mrtg_type), "<types>",  mrtg_types);
        arg_period = argument_create_choices(offsetof(dedicated_argument_t, mrtg_period), "<period>",  mrtg_periods);

        graph_create_full_path(g, lit_dedicated, lit_list, NULL);
        graph_create_path(g, lit_list, NULL, lit_nocache, NULL);
        graph_create_full_path(g, lit_dedicated, lit_check, NULL);
        graph_create_full_path(g, lit_dedicated, arg_server, lit_reboot, NULL);
        graph_create_full_path(g, lit_dedicated, arg_server, lit_mrtg, arg_type, arg_period, NULL);
    }
    // dedicated <server> boot ...
    {
        argument_t *arg_boot;
        argument_t *lit_boot, *lit_list, *lit_show;

        lit_boot = argument_create_literal("boot", /*NULL*/dedicated_boot_set, _("set active boot"));
        lit_show = argument_create_literal("show", dedicated_boot_get, _("display active boot"));
        lit_list = argument_create_literal("list", dedicated_boot_list, _("list available boots"));

        arg_boot = argument_create_string(offsetof(dedicated_argument_t, boot_name), "<boot>", complete_boots, NULL);

        graph_create_full_path(g, lit_dedicated, arg_server, lit_boot, lit_list, NULL);
        graph_create_full_path(g, lit_dedicated, arg_server, lit_boot, lit_show, NULL);
        graph_create_full_path(g, lit_dedicated, arg_server, lit_boot, arg_boot, NULL);
    }
    // dedicated <server> reverse ...
    {
        argument_t *arg_reverse;
        argument_t *lit_reverse, *lit_set, *lit_delete;

        lit_reverse = argument_create_literal("reverse", NULL, NULL);
        lit_set = argument_create_literal("set", dedicated_reverse_set, _("define reverse DNS"));
        lit_delete = argument_create_literal("delete", dedicated_reverse_delete, _("remove reverse DNS"));

        arg_reverse = argument_create_string(offsetof(dedicated_argument_t, reverse), "<reverse>", NULL, NULL);

        graph_create_full_path(g, lit_dedicated, arg_server, lit_reverse, lit_delete, NULL);
        graph_create_full_path(g, lit_dedicated, arg_server, lit_reverse, lit_set, arg_reverse, NULL);
    }
    // dedicated <server> task ...
    {
        argument_t *lit_task, *lit_list;

        lit_task = argument_create_literal("task", NULL, NULL);
        lit_list = argument_create_literal("list", dedicated_task_list, _("list tasks"));

        graph_create_full_path(g, lit_dedicated, arg_server, lit_task, lit_list, NULL);
    }
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
