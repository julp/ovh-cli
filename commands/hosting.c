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

#define MODULE_NAME "hosting"

#define FETCH_ACCOUNT_HOSTING(/*service_set **/ ss) \
    do { \
        ss = NULL; \
        account_current_get_data(MODULE_NAME, (void **) &ss); \
        assert(NULL != ss); \
    } while (0);

// crons/databases/modules/users

typedef struct {
    char *user_name;
    char *service_name;
    char *domain_path;
    char *domain_name;
    char *database_name;
    int database_date;
} hosting_argument_t;

typedef struct {
    bool uptodate;
    HashTable *services;
} service_set_t;

typedef struct {
    char *offer;
    char *hostingIpv6;
    char *primaryLogin;
    char *filer;
    char *state; // TODO: enum?
    bool hasCdn;
    char *operatingSystem; // TODO: enum?
    char *home;
    double quotaSize, quotaUsed; // TODO: double ?
    double trafficQuotaUsed, trafficQuotaSize; // TODO: double ?
//     char *availableBoostOffer; // TODO: enum[] ?
    char *cluster;
    char *resourceType; // TODO: enum ?
    char *clusterIp;
    char *clusterIpv6;
    char *boostOffer; // TODO: enum ?
    bool hasHostedSsl;
    char *hostingIp;
    char *serviceName;
    bool domains_uptodate;
    HashTable *domains; // domain (string) => path (string)
} service_t;

static void service_destroy(void *data)
{
    service_t *s;

    assert(NULL != data);

    s = (service_t *) data;
    FREE(s, offer);
    FREE(s, hostingIpv6);
    FREE(s, primaryLogin);
    FREE(s, filer);
    FREE(s, state);
    FREE(s, operatingSystem);
    FREE(s, home);
//     FREE(s, availableBoostOffer);
    FREE(s, cluster);
    FREE(s, resourceType);
    FREE(s, clusterIp);
    FREE(s, clusterIpv6);
    FREE(s, boostOffer);
    FREE(s, hostingIp);
    FREE(s, serviceName);
    if (NULL != s->domains) {
        hashtable_destroy(s->domains);
    }
    free(s);
}

static service_set_t *service_set_new(void)
{
    service_set_t *ss;

    ss = mem_new(*ss);
    ss->uptodate = FALSE;
    ss->services = hashtable_ascii_cs_new(/*(DupFunc) strdup, free*/NULL, NULL, service_destroy);

    return ss;
}

static service_t *service_new(const char *service_name)
{
    service_t *s;

    assert(NULL != service_name);

    s = mem_new(*s);
    s->hasCdn = FALSE;
    s->hasHostedSsl = FALSE;
    INIT(s, offer);
    INIT(s, hostingIpv6);
    INIT(s, primaryLogin);
    INIT(s, filer);
    INIT(s, state);
    INIT(s, operatingSystem);
    INIT(s, home);
//     INIT(s, availableBoostOffer);
    INIT(s, cluster);
    INIT(s, resourceType);
    INIT(s, clusterIp);
    INIT(s, clusterIpv6);
    INIT(s, boostOffer);
    INIT(s, hostingIp);
    s->domains_uptodate = FALSE;
    s->serviceName = strdup(service_name);
    s->domains = hashtable_ascii_cs_new((DupFunc) strdup, free, free);

    return s;
}

static void service_set_destroy(void *data)
{
    service_set_t *ss;

    assert(NULL != data);

    ss = (service_set_t *) data;
    if (NULL != ss->services) {
        hashtable_destroy(ss->services);
    }
    free(ss);
}

static void hosting_on_set_account(void **data)
{
    if (NULL == *data) {
        *data = service_set_new();
    }
}

static bool hosting_ctor(error_t **UNUSED(error))
{
    account_register_module_callbacks(MODULE_NAME, service_set_destroy, hosting_on_set_account);

    return TRUE;
}

#if 0
state: _("active"), _("bloqued"), _("maintenance")
operatingSystem: _("linux"), _("windows")
resourceType: _("bestEffort"), _("dedicated"), _("shared")
#endif
static service_t *fetch_single_hosting(service_set_t *ss, const char * const service_name, bool force, error_t **error)
{
    service_t *s;

    s = NULL;
    if (!force && !ss->uptodate && !hashtable_get(ss->services, service_name, &s)) {
        request_t *req;
        json_document_t *doc;
        bool request_success;

        s = service_new(service_name);
        hashtable_put(ss->services, 0, s->serviceName, s, NULL);
        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/hosting/web/%s", service_name);
        request_success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (request_success) {
            json_value_t root;

            root = json_document_get_root(doc);
            JSON_GET_PROP_STRING(root, "offer", s->offer);
            JSON_GET_PROP_STRING(root, "hostingIpv6", s->hostingIpv6);
            JSON_GET_PROP_STRING(root, "primaryLogin", s->primaryLogin);
            JSON_GET_PROP_STRING(root, "filer", s->filer);
            JSON_GET_PROP_STRING(root, "state", s->state);
            JSON_GET_PROP_BOOL(root, "hasCdn", s->hasCdn);
            JSON_GET_PROP_STRING(root, "operatingSystem", s->operatingSystem);
            JSON_GET_PROP_STRING(root, "home", s->home);
            // TODO: quotaSize, quotaUsed, trafficQuotaSize, trafficQuotaUsed: double, size_t ?
//             json_object_get_property(root, "availableBoostOffer", &propvalue);
//             s->availableBoostOffer = strdup(json_get_string(propvalue));
            JSON_GET_PROP_STRING(root, "cluster", s->cluster);
            JSON_GET_PROP_STRING(root, "resourceType", s->resourceType);
            JSON_GET_PROP_STRING(root, "clusterIp", s->clusterIp);
            JSON_GET_PROP_STRING(root, "clusterIpv6", s->clusterIpv6);
            JSON_GET_PROP_STRING(root, "boostOffer", s->boostOffer);
            JSON_GET_PROP_BOOL(root, "hasHostedSsl", s->hasHostedSsl);
            JSON_GET_PROP_STRING(root, "hostingIp", s->hostingIp);

            json_document_destroy(doc);
        }
    }

    return s;
}

static command_status_t fetch_all_hosting(service_set_t *ss, bool force, error_t **error)
{
    if (!ss->uptodate || force) {
        request_t *req;
        bool request_success;
        json_document_t *doc;

        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/hosting/web");
        request_success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (request_success) {
            Iterator it;
            json_value_t root;

            root = json_document_get_root(doc);
            hashtable_clear(ss->services);
            json_array_to_iterator(&it, root);
            for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
                json_value_t v;

                v = (json_value_t) iterator_current(&it, NULL);
                fetch_single_hosting(ss, json_get_string(v), force, error);
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

static command_status_t hosting_list(COMMAND_ARGS)
{
    table_t *t;
    Iterator it;
    service_set_t *ss;
    command_status_t ret;

    USED(arg);
    USED(mainopts);
    FETCH_ACCOUNT_HOSTING(ss);
    // populate
    if ((COMMAND_SUCCESS != (ret = fetch_all_hosting(ss, FALSE /*args->nocache*/, error)))) {
        return ret;
    }
    // display
    t = table_new(
        16,
        _("serviceName"), TABLE_TYPE_STRING,
        _("offer"), TABLE_TYPE_STRING,
        _("hostingIp"), TABLE_TYPE_STRING,
        _("hostingIpv6"), TABLE_TYPE_STRING,
        _("primaryLogin"), TABLE_TYPE_STRING,
        _("filer"), TABLE_TYPE_STRING,
        _("state"), TABLE_TYPE_STRING,
        _("hasCdn"), TABLE_TYPE_BOOLEAN,
        _("operatingSystem"), TABLE_TYPE_STRING,
        _("home"), TABLE_TYPE_STRING,
//         _("availableBoostOffer"), TABLE_TYPE_STRING,
        _("resourceType"), TABLE_TYPE_STRING,
        _("cluster"), TABLE_TYPE_STRING,
        _("clusterIp"), TABLE_TYPE_STRING,
        _("clusterIpv6"), TABLE_TYPE_STRING,
//         _("quotaSize"), TABLE_TYPE_STRING,
//         _("quotaUsed"), TABLE_TYPE_STRING,
//         _("trafficQuotaSize"), TABLE_TYPE_STRING,
//         _("trafficQuotaUsed"), TABLE_TYPE_STRING,
        _("boostOffer"), TABLE_TYPE_STRING,
        _("hasHostedSsl"), TABLE_TYPE_BOOLEAN
    );
    hashtable_to_iterator(&it, ss->services);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        service_t *s;

        s = iterator_current(&it, NULL);
        table_store(t, s->serviceName, s->offer, s->hostingIp, s->hostingIpv6, s->primaryLogin, s->filer, s->state, s->hasCdn, s->operatingSystem, s->home, s->resourceType, s->cluster, s->clusterIp, s->clusterIpv6, s->boostOffer ,s->hasHostedSsl);
    }
    iterator_close(&it);
    table_sort(t, 0, TABLE_SORT_ASC);
    table_display(t, TABLE_FLAG_NONE);
    table_destroy(t);

    return COMMAND_SUCCESS;
}

static command_status_t hosting_domain_list(COMMAND_ARGS)
{
    bool success;
    service_t *s;
    service_set_t *ss;
    hosting_argument_t *args;

    USED(mainopts);
    success = TRUE;
    args = (hosting_argument_t *) arg;
    assert(NULL != args->service_name);
    FETCH_ACCOUNT_HOSTING(ss);
    if (!hashtable_get(ss->services, args->service_name, &s)) {
        s = fetch_single_hosting(ss, args->service_name, FALSE, error);
        success = NULL != s;
    }
    if (success && !s->domains_uptodate) {
        Iterator it;
        request_t *req;
        json_value_t root;
        json_document_t *doc;

        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/hosting/web/%s/attachedDomain", args->service_name);
        success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (success) {
            root = json_document_get_root(doc);
            json_array_to_iterator(&it, root);
            for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
                json_document_t *doc;
                json_value_t v, root;

                v = (json_value_t) iterator_current(&it, NULL);
                req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/hosting/web/%s/attachedDomain/%s", args->service_name, json_get_string(v));
                success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
                request_destroy(req);
                if (success) {
                    char *path;

                    root = json_document_get_root(doc);
                    JSON_GET_PROP_STRING(root, "path", path);
                    json_object_get_property(root, "domain", &v);
                    hashtable_put(s->domains, 0, json_get_string(v), path, NULL);
                    json_document_destroy(doc);
                }
            }
            iterator_close(&it);
            json_document_destroy(doc);
            if (success) {
                s->domains_uptodate = TRUE;
            }
        }
    }
    // display
    {
        table_t *t;
        Iterator it;

        t = table_new(2, _("domain"), TABLE_TYPE_STRING, _("path"), TABLE_TYPE_STRING);
        hashtable_to_iterator(&it, s->domains);
        for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
            char *k, *v;

            v = iterator_current(&it, (void **) &k);
            table_store(t, k, v);
        }
        iterator_close(&it);
        table_display(t, TABLE_FLAG_NONE);
        table_destroy(t);
    }


    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

#if 0
static void parse_and_display_task(json_document_t *doc, const char *msgf, ...)
{
    va_lsit ap;
    int64_t task_id;
    json_value_t root;

    JSON_GET_PROP_INT(root, "id", task_id);
    json_document_destroy(doc);
    va_start(msgf, ap);
    vprintf(msgf, ap);
    va_end(ap);
}
#endif

static command_status_t hosting_domain_create(COMMAND_ARGS)
{
    bool success;
    request_t *req;
    service_set_t *ss;
    hosting_argument_t *args;
    json_document_t *doc, *reqdoc;

    USED(mainopts);
    FETCH_ACCOUNT_HOSTING(ss);
    args = (hosting_argument_t *) arg;
    assert(NULL != args->domain_path);
    assert(NULL != args->domain_name);
    assert(NULL != args->service_name);
    reqdoc = json_document_new();
    {
        json_value_t root;

        root = json_object();
        json_object_set_property(root, "path", json_string(args->domain_path));
        json_object_set_property(root, "domain", json_string(args->domain_name));
        json_document_set_root(reqdoc, root);
    }
    req = request_new(REQUEST_FLAG_SIGN | REQUEST_FLAG_JSON, HTTP_POST, reqdoc, error, API_BASE_URL "/hosting/web/%s/attachedDomain", args->service_name);
    success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
    request_destroy(req);
    json_document_destroy(reqdoc);
    if (success) {
        service_t *s;
        int64_t task_id;
        json_value_t root;

        root = json_document_get_root(doc);
        JSON_GET_PROP_INT(root, "id", task_id);
        json_document_destroy(doc);
        if (hashtable_get(ss->services, args->service_name, &s)) {
            hashtable_put(s->domains, 0, args->domain_name, strdup(args->domain_path), NULL); // check domain isn't already attached? (else we have a leak on previous path in hashtable?)
        }
        printf("Request to link domain '%s' to hosting '%s' was successfully registered as task #%" PRIi64 "\n", args->domain_name, args->service_name, task_id);
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

#if 0
static command_status_t hosting_domain_update(COMMAND_ARGS)
{
    //
}
#endif

static command_status_t hosting_domain_delete(COMMAND_ARGS)
{
    bool success;
    service_set_t *ss;
    hosting_argument_t *args;

    success = TRUE;
    FETCH_ACCOUNT_HOSTING(ss);
    args = (hosting_argument_t *) arg;
    assert(NULL != args->domain_name);
    assert(NULL != args->service_name);
    if (confirm(mainopts, _("Confirm unlinking domain '%s' to hosting '%s'"), args->domain_name, args->service_name)) {
        request_t *req;
        json_document_t *doc;

        req = request_new(REQUEST_FLAG_SIGN, HTTP_DELETE, NULL, error, API_BASE_URL "/hosting/web/%s/attachedDomain/%s", args->service_name, args->domain_name);
        success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (success) {
            service_t *s;
            int64_t task_id;
            json_value_t root;

            root = json_document_get_root(doc);
            JSON_GET_PROP_INT(root, "id", task_id);
            json_document_destroy(doc);
            if (hashtable_get(ss->services, args->service_name, &s)) {
                hashtable_delete(s->domains, args->domain_name, TRUE);
            }
            printf("Request to unlink domain '%s' to hosting '%s' was successfully registered as task #%" PRIi64 "\n", args->domain_name, args->service_name, task_id);
        }
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t hosting_cron_list(COMMAND_ARGS)
{
    bool success;
#if 0
    service_t *s;
    service_set_t *ss;
#endif
    hosting_argument_t *args;

    USED(mainopts);
    success = TRUE;
    args = (hosting_argument_t *) arg;
    assert(NULL != args->service_name);
#if 0
    FETCH_ACCOUNT_HOSTING(ss);
    if (!hashtable_get(ss->services, args->service_name, &s)) {
        s = fetch_single_hosting(ss, args->service_name, FALSE, error);
        success = NULL != s;
    }
#endif
    if (success) {
        Iterator it;
        request_t *req;
        json_value_t root;
        json_document_t *doc;

        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/hosting/web/%s/cron", args->service_name);
        success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (success) {
            table_t *t;

            root = json_document_get_root(doc);
            json_array_to_iterator(&it, root);
            t = table_new(
#ifdef PRINT_OVH_ID
                6,
                _("id"), TABLE_TYPE_INT,
#else
                5,
#endif /* PRINT_OVH_ID */
                _("language"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
                _("frequency"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
                _("description"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
                _("command"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
                _("email"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE
            );
            for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
                int64_t id; // cron ID
                json_document_t *doc;
                json_value_t v, root;

                v = (json_value_t) iterator_current(&it, NULL);
                id = json_get_integer(v);
                req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/hosting/web/%s/cron/%u", args->service_name, id);
                success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
                request_destroy(req);
                if (success) {
                    char *email, *frequency, *language, *description, *command;

                    root = json_document_get_root(doc);
                    JSON_GET_PROP_STRING(root, "email", email);
                    JSON_GET_PROP_STRING(root, "frequency", frequency);
                    JSON_GET_PROP_STRING(root, "language", language);
                    JSON_GET_PROP_STRING(root, "description", description);
                    JSON_GET_PROP_STRING(root, "command", command);
                    table_store(
                        t,
#ifdef PRINT_OVH_ID
                        id,
#endif /* PRINT_OVH_ID */
                        language,
                        frequency,
                        description,
                        command,
                        email
                    );
                    json_document_destroy(doc);
                }
            }
            iterator_close(&it);
            json_document_destroy(doc);
            table_display(t, TABLE_FLAG_NONE);
            table_destroy(t);
        }
    }


    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

#if 0
static command_status_t hosting_cron_create(COMMAND_ARGS)
{
    //
}

static command_status_t hosting_cron_update(COMMAND_ARGS)
{
    //
}

static command_status_t hosting_cron_delete(COMMAND_ARGS)
{
    //
}
#endif

#if 0
state: _("off"), _("rw")
iisRemoteRights, webDavRights: _("off"), _("read"), _("rw")
#endif
static command_status_t hosting_user_list(COMMAND_ARGS)
{
    bool success;
#if 0
    service_t *s;
    service_set_t *ss;
#endif
    hosting_argument_t *args;

    USED(mainopts);
    success = TRUE;
    args = (hosting_argument_t *) arg;
    assert(NULL != args->service_name);
#if 0
    FETCH_ACCOUNT_HOSTING(ss);
    if (!hashtable_get(ss->services, args->service_name, &s)) {
        s = fetch_single_hosting(ss, args->service_name, FALSE, error);
        success = NULL != s;
    }
#endif
    if (success) {
        Iterator it;
        request_t *req;
        json_value_t root;
        json_document_t *doc;

        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/hosting/web/%s/user", args->service_name);
        success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (success) {
            table_t *t;

            root = json_document_get_root(doc);
            json_array_to_iterator(&it, root);
            t = table_new(
                6,
                _("login"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
                _("home"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
                _("isPrimaryAccount"), TABLE_TYPE_BOOLEAN,
                _("state"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
                _("iisRemoteRights"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
                _("webDavRights"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE
            );
            for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
                char *login;
                json_document_t *doc;
                json_value_t v, root;

                v = (json_value_t) iterator_current(&it, NULL);
                login = strdup(json_get_string(v));
                req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/hosting/web/%s/user/%s", args->service_name, login);
                success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
                request_destroy(req);
                if (success) {
                    bool isPrimaryAccount;
                    char *home, *iisRemoteRights, *state, *webDavRights;

                    root = json_document_get_root(doc);
                    JSON_GET_PROP_STRING(root, "home", home);
                    JSON_GET_PROP_BOOL(root, "isPrimaryAccount", isPrimaryAccount);
                    JSON_GET_PROP_STRING(root, "state", state);
                    JSON_GET_PROP_STRING(root, "iisRemoteRights", iisRemoteRights);
                    JSON_GET_PROP_STRING(root, "webDavRights", webDavRights);
                    table_store(t, login, home, isPrimaryAccount, state, iisRemoteRights, webDavRights);
                    json_document_destroy(doc);
                }
            }
            iterator_close(&it);
            json_document_destroy(doc);
            table_display(t, TABLE_FLAG_NONE);
            table_destroy(t);
        }
    }


    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

#if 0
static command_status_t hosting_user_create(COMMAND_ARGS)
{
    //
}

static command_status_t hosting_user_update(COMMAND_ARGS)
{
    //
}
#endif

static command_status_t hosting_user_delete(COMMAND_ARGS)
{
    bool success;
    service_set_t *ss;
    hosting_argument_t *args;

    success = TRUE;
    FETCH_ACCOUNT_HOSTING(ss);
    args = (hosting_argument_t *) arg;
    assert(NULL != args->user_name);
    assert(NULL != args->service_name);
    if (confirm(mainopts, _("Confirm deletion of user '%s' for hosting '%s'"), args->user_name, args->service_name)) {
        request_t *req;
        json_document_t *doc;

        req = request_new(REQUEST_FLAG_SIGN, HTTP_DELETE, NULL, error, API_BASE_URL "/hosting/web/%s/user/%s", args->service_name, args->user_name);
        success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (success) {
            service_t *s;
            int64_t task_id;
            json_value_t root;

            root = json_document_get_root(doc);
            JSON_GET_PROP_INT(root, "id", task_id);
            json_document_destroy(doc);
            if (hashtable_get(ss->services, args->service_name, &s)) {
                hashtable_delete(s->domains, args->domain_name, TRUE);
            }
            printf("Request to delete user '%s' to hosting '%s' was successfully registered as task #%" PRIi64 "\n", args->user_name, args->service_name, task_id);
        }
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

#if 0
mode: _("besteffort"), _("classic")
state: _("close"), _("ok"), _("readonly")
type: _("mysql"), _("postgresql")
#endif
static command_status_t hosting_database_list(COMMAND_ARGS)
{
    bool success;
#if 0
    service_t *s;
    service_set_t *ss;
#endif
    hosting_argument_t *args;

    USED(mainopts);
    success = TRUE;
    args = (hosting_argument_t *) arg;
    assert(NULL != args->service_name);
#if 0
    FETCH_ACCOUNT_HOSTING(ss);
    if (!hashtable_get(ss->services, args->service_name, &s)) {
        s = fetch_single_hosting(ss, args->service_name, FALSE, error);
        success = NULL != s;
    }
#endif
    if (success) {
        Iterator it;
        request_t *req;
        json_value_t root;
        json_document_t *doc;

        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/hosting/web/%s/database", args->service_name);
        success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (success) {
            table_t *t;

            // TODO: quotaSize + quotaUsed
            root = json_document_get_root(doc);
            json_array_to_iterator(&it, root);
            t = table_new(
                8,
                _("mode"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
                _("version"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
                _("name"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
                _("port"), TABLE_TYPE_INT,
                _("state"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
                _("user"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
                _("type"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE,
                _("server"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE
            );
            for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
                char *dbname;
                json_document_t *doc;
                json_value_t v, root;

                v = (json_value_t) iterator_current(&it, NULL);
                dbname = strdup(json_get_string(v));
                req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/hosting/web/%s/database/%s", args->service_name, dbname);
                success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
                request_destroy(req);
                if (success) {
                    int64_t port;
                    char *mode, *version, *state, *user, *type, *server;

                    root = json_document_get_root(doc);
                    JSON_GET_PROP_STRING(root, "mode", mode);
                    JSON_GET_PROP_STRING(root, "version", version);
                    JSON_GET_PROP_INT(root, "port", port);
                    JSON_GET_PROP_STRING(root, "state", state);
                    JSON_GET_PROP_STRING(root, "user", user);
                    JSON_GET_PROP_STRING(root, "type", type);
                    JSON_GET_PROP_STRING(root, "server", server);
                    table_store(t, mode, version, dbname, port, state, user, type, server);
                    json_document_destroy(doc);
                }
            }
            iterator_close(&it);
            json_document_destroy(doc);
            table_display(t, TABLE_FLAG_NONE);
            table_destroy(t);
        }
    }


    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static const char *database_dates[] = {
    "now",
    "daily.1",
    "weekly.1",
    NULL
};

static command_status_t hosting_database_dump(COMMAND_ARGS)
{
    bool success;
    request_t *req;
    hosting_argument_t *args;
    json_document_t *doc, *reqdoc;

    USED(mainopts);
    args = (hosting_argument_t *) arg;
    assert(NULL != args->service_name);
    assert(NULL != args->database_name);
    reqdoc = json_document_new();
    {
        json_value_t root;

        root = json_object();
        json_object_set_property(root, "date", json_string(database_dates[args->database_date]));
        json_document_set_root(reqdoc, root);
    }
    req = request_new(REQUEST_FLAG_SIGN | REQUEST_FLAG_JSON, HTTP_POST, reqdoc, error, API_BASE_URL "/hosting/web/%s/database/%s/dump", args->service_name, args->database_name);
    success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
    request_destroy(req);
    json_document_destroy(reqdoc);
    if (success) {
        int64_t task_id;
        json_value_t root;

        root = json_document_get_root(doc);
        JSON_GET_PROP_INT(root, "id", task_id);
        json_document_destroy(doc);
        printf("Request to dump database '%s' for hosting '%s' was successfully registered as task #%" PRIi64 "\n", args->database_name, args->service_name, task_id);
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

#if 0
static command_status_t hosting_database_create(COMMAND_ARGS)
{
    //
}
#endif

static command_status_t hosting_database_delete(COMMAND_ARGS)
{
    bool success;
    request_t *req;
    json_document_t *doc;
    hosting_argument_t *args;

    success = TRUE;
    args = (hosting_argument_t *) arg;
    assert(NULL != args->service_name);
    assert(NULL != args->database_name);
    if (confirm(mainopts, _("Confirm drop database '%s' for hosting '%s'"), args->database_name, args->service_name)) {
        req = request_new(REQUEST_FLAG_SIGN, HTTP_DELETE, NULL, error, API_BASE_URL "/hosting/web/%s/database/%s", args->service_name, args->database_name);
        success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (success) {
            int64_t task_id;
            json_value_t root;

            root = json_document_get_root(doc);
            JSON_GET_PROP_INT(root, "id", task_id);
            json_document_destroy(doc);
            printf("Request to drop database '%s' for hosting '%s' was successfully registered as task #%" PRIi64 "\n", args->database_name, args->service_name, task_id);
        }
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static bool complete_hosting(void *parsed_arguments, const char *current_argument, size_t current_argument_len, DPtrArray *possibilities, void *UNUSED(data))
{
    service_set_t *ss;

    FETCH_ACCOUNT_HOSTING(ss);
    if (COMMAND_SUCCESS != fetch_all_hosting(ss, FALSE, NULL)) {
        return FALSE;
    }

    return complete_from_hashtable_keys(parsed_arguments, current_argument, current_argument_len, possibilities, ss->services);
}

static void hosting_regcomm(graph_t *g)
{
    argument_t *lit_host_list;
    argument_t *lit_hosting, *lit_cron, *lit_user, *lit_db, *lit_domain;
    argument_t *lit_cron_list;
    argument_t *lit_user_list, *lit_user_delete;
    argument_t *lit_db_list, *lit_db_dump, *lit_db_delete;
    argument_t *lit_domain_list, *lit_domain_add, *lit_domain_delete;
    argument_t *arg_hosting, *arg_domain_name, *arg_domain_path, *arg_database_date, *arg_database_name, *arg_user_name;

    lit_hosting = argument_create_literal("hosting", NULL);
    lit_host_list = argument_create_literal("list", hosting_list);

    lit_domain = argument_create_literal("domain", NULL);
    lit_domain_list = argument_create_literal("list", hosting_domain_list);
    lit_domain_add = argument_create_literal("add", hosting_domain_create);
    lit_domain_delete = argument_create_literal("delete", hosting_domain_delete);

    lit_db = argument_create_literal("database", NULL);
    lit_db_list = argument_create_literal("list", hosting_database_list);
    lit_db_dump = argument_create_literal("dump", hosting_database_dump);
    lit_db_delete = argument_create_literal("delete", hosting_database_delete);

    lit_user = argument_create_literal("user", NULL);
    lit_user_list = argument_create_literal("list", hosting_user_list);
    lit_user_delete = argument_create_literal("delete", hosting_user_delete);

    lit_cron = argument_create_literal("cron", NULL);
    lit_cron_list = argument_create_literal("list", hosting_cron_list);

    arg_user_name = argument_create_string(offsetof(hosting_argument_t, user_name), "<user>", NULL, NULL);
    arg_domain_path = argument_create_string(offsetof(hosting_argument_t, domain_path), "<path>", NULL, NULL);
    arg_domain_name = argument_create_string(offsetof(hosting_argument_t, domain_name), "<domain>", NULL, NULL);
    arg_hosting = argument_create_string(offsetof(hosting_argument_t, service_name), "<hosting>", complete_hosting, NULL);
    arg_database_name = argument_create_string(offsetof(hosting_argument_t, database_name), "<database>", NULL, NULL);
    arg_database_date = argument_create_choices(offsetof(hosting_argument_t, database_date), "<date>", database_dates);

    graph_create_full_path(g, lit_hosting, lit_host_list, NULL);

    // hosting ...
    graph_create_full_path(g, lit_hosting, arg_hosting, lit_domain, lit_domain_list, NULL);
    // hosting <hosting> domain ...
    graph_create_full_path(g, lit_hosting, arg_hosting, lit_domain, arg_domain_name, lit_domain_delete, NULL);
    graph_create_full_path(g, lit_hosting, arg_hosting, lit_domain, arg_domain_name, lit_domain_add, arg_domain_path, NULL);
    // hosting <hosting> database ...
    graph_create_full_path(g, lit_hosting, arg_hosting, lit_db, lit_db_list, NULL);
    graph_create_full_path(g, lit_hosting, arg_hosting, lit_db, arg_database_name, lit_db_delete, NULL);
    graph_create_full_path(g, lit_hosting, arg_hosting, lit_db, arg_database_name, lit_db_dump, arg_database_date, NULL);
    // hosting <hosting> user ...
    graph_create_full_path(g, lit_hosting, arg_hosting, lit_user, lit_user_list, NULL);
    graph_create_full_path(g, lit_hosting, arg_hosting, lit_user, arg_user_name, lit_user_delete, NULL);
    // hosting <hosting> cron ...
    graph_create_full_path(g, lit_hosting, arg_hosting, lit_cron, lit_cron_list, NULL);
}

static void hosting_register_rules(json_value_t rules, bool ro)
{
    JSON_ADD_RULE(rules, "GET", "/hosting/web");
    JSON_ADD_RULE(rules, "GET", "/hosting/web/*");
    if (!ro) {
        JSON_ADD_RULE(rules, "PUT", "/hosting/web/*");
        JSON_ADD_RULE(rules, "POST", "/hosting/web/*");
        JSON_ADD_RULE(rules, "DELETE", "/hosting/web/*");
    }
}

DECLARE_MODULE(hosting) = {
    MODULE_NAME,
    hosting_regcomm,
    hosting_register_rules,
    hosting_ctor,
    NULL,
    NULL
};
