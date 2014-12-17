#include <string.h>
#include <inttypes.h>

#include "common.h"
#include "json.h"
#include "date.h"
#include "util.h"
#include "table.h"
#include "modules/api.h"
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
    char *service_name;
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
    HashTable *domains; // domain => path ?
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

static service_t *service_new(void)
{
    service_t *s;

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
    INIT(s, serviceName);
    s->domains_uptodate = FALSE;
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

static bool hosting_ctor(void)
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
    if (!force && !ss->uptodate && !hashtable_get(ss->services, service_name, (void **) &s)) {
        request_t *req;
        json_document_t *doc;
        bool request_success;

        hashtable_put(ss->services, (void *) service_name, s = service_new(), NULL);
        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/hosting/web/%s", service_name);
        request_success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (request_success) {
            json_value_t root, propvalue;

            s->serviceName = strdup(service_name);
            root = json_document_get_root(doc);
            json_object_get_property(root, "offer", &propvalue);
            s->offer = strdup(json_get_string(propvalue));
            json_object_get_property(root, "hostingIpv6", &propvalue);
            s->hostingIpv6 = json_null == propvalue ? NULL : strdup(json_get_string(propvalue));
            json_object_get_property(root, "primaryLogin", &propvalue);
            s->primaryLogin = strdup(json_get_string(propvalue));
            json_object_get_property(root, "filer", &propvalue);
            s->filer = strdup(json_get_string(propvalue));
            json_object_get_property(root, "state", &propvalue);
            s->state = strdup(json_get_string(propvalue));
            json_object_get_property(root, "hasCdn", &propvalue);
            s->hasCdn = json_true == propvalue;
            json_object_get_property(root, "operatingSystem", &propvalue);
            s->operatingSystem = strdup(json_get_string(propvalue));
            json_object_get_property(root, "home", &propvalue);
            s->home = strdup(json_get_string(propvalue));
            // TODO: quotaSize, quotaUsed, trafficQuotaSize, trafficQuotaUsed: double, size_t ?
//             json_object_get_property(root, "availableBoostOffer", &propvalue);
//             s->availableBoostOffer = strdup(json_get_string(propvalue));
            json_object_get_property(root, "cluster", &propvalue);
            s->cluster = strdup(json_get_string(propvalue));
            json_object_get_property(root, "resourceType", &propvalue);
            s->resourceType = strdup(json_get_string(propvalue));
            json_object_get_property(root, "clusterIp", &propvalue);
            s->clusterIp = json_null == propvalue ? NULL : strdup(json_get_string(propvalue));
            json_object_get_property(root, "clusterIpv6", &propvalue);
            s->clusterIpv6 = json_null == propvalue ? NULL : strdup(json_get_string(propvalue));
            json_object_get_property(root, "boostOffer", &propvalue);
            s->boostOffer = json_null == propvalue ? NULL : strdup(json_get_string(propvalue));
            json_object_get_property(root, "hasHostedSsl", &propvalue);
            s->hasHostedSsl = json_true == propvalue;
            json_object_get_property(root, "hostingIp", &propvalue);
            s->hostingIp = json_null == propvalue ? NULL : strdup(json_get_string(propvalue));
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

        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/hosting/web");
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

static command_status_t hosting_list(void *UNUSED(arg), error_t **error)
{
    table_t *t;
    Iterator it;
    service_set_t *ss;
    command_status_t ret;

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
    table_sort(t, 0);
    table_display(t, TABLE_FLAG_NONE);
    table_destroy(t);

    return COMMAND_SUCCESS;
}

static command_status_t hosting_domain_list(void *arg, error_t **error)
{
    bool success;
    service_t *s;
    service_set_t *ss;
    hosting_argument_t *args;

    success = TRUE;
    args = (hosting_argument_t *) arg;
    assert(NULL != args->service_name);
    FETCH_ACCOUNT_HOSTING(ss);
    if (!hashtable_get(ss->services, args->service_name, (void **) &s)) {
        s = fetch_single_hosting(ss, args->service_name, FALSE, error);
        success = NULL != s;
    }
    if (success && !s->domains_uptodate) {
        Iterator it;
        request_t *req;
        json_value_t root;
        json_document_t *doc;

        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/hosting/web/%s/attachedDomain", args->service_name);
        success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (success) {
            root = json_document_get_root(doc);
            json_array_to_iterator(&it, root);
            for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
                json_document_t *doc;
                json_value_t v, root;

                v = (json_value_t) iterator_current(&it, NULL);
                req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/hosting/web/%s/attachedDomain/%s", args->service_name, json_get_string(v));
                success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
                request_destroy(req);
                if (success) {
                    char *path;

                    root = json_document_get_root(doc);
                    json_object_get_property(root, "path", &v);
                    path = strdup(json_get_string(v));
                    json_object_get_property(root, "domain", &v);
                    hashtable_put(s->domains, (void *) json_get_string(v), path, NULL);
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
static command_status_t hosting_domain_create(void *arg, error_t **error)
{
    //
}

static command_status_t hosting_domain_update(void *arg, error_t **error)
{
    //
}

static command_status_t hosting_domain_delete(void *arg, error_t **error)
{
    //
}
#endif

static command_status_t hosting_cron_list(void *arg, error_t **error)
{
    bool success;
#if 0
    service_t *s;
    service_set_t *ss;
#endif
    hosting_argument_t *args;

    success = TRUE;
    args = (hosting_argument_t *) arg;
    assert(NULL != args->service_name);
#if 0
    FETCH_ACCOUNT_HOSTING(ss);
    if (!hashtable_get(ss->services, args->service_name, (void **) &s)) {
        s = fetch_single_hosting(ss, args->service_name, FALSE, error);
        success = NULL != s;
    }
#endif
    if (success) {
        Iterator it;
        request_t *req;
        json_value_t root;
        json_document_t *doc;

        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/hosting/web/%s/cron", args->service_name);
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
                _("language"), TABLE_TYPE_STRING,
                _("frequency"), TABLE_TYPE_STRING,
                _("description"), TABLE_TYPE_STRING,
                _("command"), TABLE_TYPE_STRING,
                _("email"), TABLE_TYPE_STRING
            );
            for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
                int64_t id; // cron ID
                json_document_t *doc;
                json_value_t v, root;

                v = (json_value_t) iterator_current(&it, NULL);
                id = json_get_integer(v);
                req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/hosting/web/%s/cron/%u", args->service_name, id);
                success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
                request_destroy(req);
                if (success) {
                    char *email, *frequency, *language, *description, *command;

                    root = json_document_get_root(doc);
                    json_object_get_property(root, "email", &v);
                    email = strdup(json_get_string(v));
                    json_object_get_property(root, "frequency", &v);
                    frequency = strdup(json_get_string(v));
                    json_object_get_property(root, "language", &v);
                    language = strdup(json_get_string(v));
                    json_object_get_property(root, "description", &v);
                    description = strdup(json_get_string(v));
                    json_object_get_property(root, "command", &v);
                    command = strdup(json_get_string(v));
                    // TODO: leaks on email, frequency, language, description, command
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
static command_status_t hosting_cron_create(void *arg, error_t **error)
{
    //
}

static command_status_t hosting_cron_update(void *arg, error_t **error)
{
    //
}

static command_status_t hosting_cron_delete(void *arg, error_t **error)
{
    //
}
#endif

#if 0
state: _("off"), _("rw")
iisRemoteRights, webDavRights: _("off"), _("read"), _("rw")
#endif
static command_status_t hosting_user_list(void *arg, error_t **error)
{
    bool success;
#if 0
    service_t *s;
    service_set_t *ss;
#endif
    hosting_argument_t *args;

    success = TRUE;
    args = (hosting_argument_t *) arg;
    assert(NULL != args->service_name);
#if 0
    FETCH_ACCOUNT_HOSTING(ss);
    if (!hashtable_get(ss->services, args->service_name, (void **) &s)) {
        s = fetch_single_hosting(ss, args->service_name, FALSE, error);
        success = NULL != s;
    }
#endif
    if (success) {
        Iterator it;
        request_t *req;
        json_value_t root;
        json_document_t *doc;

        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/hosting/web/%s/user", args->service_name);
        success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (success) {
            table_t *t;

            root = json_document_get_root(doc);
            json_array_to_iterator(&it, root);
            t = table_new(
                6,
                _("login"), TABLE_TYPE_STRING,
                _("home"), TABLE_TYPE_STRING,
                _("isPrimaryAccount"), TABLE_TYPE_BOOLEAN,
                _("state"), TABLE_TYPE_STRING,
                _("iisRemoteRights"), TABLE_TYPE_STRING,
                _("webDavRights"), TABLE_TYPE_STRING
            );
            for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
                char *login;
                json_document_t *doc;
                json_value_t v, root;

                v = (json_value_t) iterator_current(&it, NULL);
                login = strdup(json_get_string(v));
                req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/hosting/web/%s/user/%s", args->service_name, login);
                success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
                request_destroy(req);
                if (success) {
                    bool isPrimaryAccount;
                    char *home, *iisRemoteRights, *state, *webDavRights;

                    root = json_document_get_root(doc);
                    json_object_get_property(root, "home", &v);
                    home = strdup(json_get_string(v));
                    json_object_get_property(root, "isPrimaryAccount", &v);
                    isPrimaryAccount = json_true == v;
                    json_object_get_property(root, "state", &v);
                    state = strdup(json_get_string(v));
                    json_object_get_property(root, "iisRemoteRights", &v);
                    iisRemoteRights = json_null == v ? NULL : strdup(json_get_string(v));
                    json_object_get_property(root, "webDavRights", &v);
                    webDavRights = json_null == v ? NULL : strdup(json_get_string(v));
                    // TODO: leaks on login, home, state, iisRemoteRights (if non NULL), webDavRights (if non NULL)
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
static command_status_t hosting_user_create(void *arg, error_t **error)
{
    //
}

static command_status_t hosting_user_update(void *arg, error_t **error)
{
    //
}

static command_status_t hosting_user_delete(void *arg, error_t **error)
{
    //
}
#endif

#if 0
mode: _("besteffort"), _("classic")
state: _("close"), _("ok"), _("readonly")
type: _("mysql"), _("postgresql")
#endif
static command_status_t hosting_database_list(void *arg, error_t **error)
{
    bool success;
#if 0
    service_t *s;
    service_set_t *ss;
#endif
    hosting_argument_t *args;

    success = TRUE;
    args = (hosting_argument_t *) arg;
    assert(NULL != args->service_name);
#if 0
    FETCH_ACCOUNT_HOSTING(ss);
    if (!hashtable_get(ss->services, args->service_name, (void **) &s)) {
        s = fetch_single_hosting(ss, args->service_name, FALSE, error);
        success = NULL != s;
    }
#endif
    if (success) {
        Iterator it;
        request_t *req;
        json_value_t root;
        json_document_t *doc;

        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/hosting/web/%s/database", args->service_name);
        success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (success) {
            table_t *t;

            // TODO: quotaSize + quotaUsed
            root = json_document_get_root(doc);
            json_array_to_iterator(&it, root);
            t = table_new(
                8,
                _("mode"), TABLE_TYPE_STRING,
                _("version"), TABLE_TYPE_STRING,
                _("name"), TABLE_TYPE_STRING,
                _("port"), TABLE_TYPE_INT,
                _("state"), TABLE_TYPE_STRING,
                _("user"), TABLE_TYPE_STRING,
                _("type"), TABLE_TYPE_STRING,
                _("server"), TABLE_TYPE_STRING
            );
            for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
                char *dbname;
                json_document_t *doc;
                json_value_t v, root;

                v = (json_value_t) iterator_current(&it, NULL);
                dbname = strdup(json_get_string(v));
                req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/hosting/web/%s/database/%s", args->service_name, dbname);
                success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
                request_destroy(req);
                if (success) {
                    int64_t port;
                    char *mode, *version, *state, *user, *type, *server;

                    root = json_document_get_root(doc);
                    json_object_get_property(root, "mode", &v);
                    mode = strdup(json_get_string(v));
                    json_object_get_property(root, "version", &v);
                    version = strdup(json_get_string(v));
                    json_object_get_property(root, "port", &v);
                    port = json_get_integer(v);
                    json_object_get_property(root, "state", &v);
                    state = strdup(json_get_string(v));
                    json_object_get_property(root, "user", &v);
                    user = strdup(json_get_string(v));
                    json_object_get_property(root, "type", &v);
                    type = strdup(json_get_string(v));
                    json_object_get_property(root, "server", &v);
                    server = json_null == v ? NULL : strdup(json_get_string(v));
                    // TODO: leaks on mode, version, dbname, state, user, type, server (if non NULL)
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

#if 0
static command_status_t hosting_database_create(void *arg, error_t **error)
{
    //
}

static command_status_t hosting_database_delete(void *arg, error_t **error)
{
    //
}
#endif

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
    argument_t *arg_hosting;
    argument_t *lit_host_list;
    argument_t *lit_hosting, *lit_cron, *lit_user, *lit_db, *lit_domain;
    argument_t *lit_cron_list, *lit_user_list, *lit_db_list, *lit_domain_list;

    lit_hosting = argument_create_literal("hosting", NULL);
    lit_host_list = argument_create_literal("list", hosting_list);

    lit_domain = argument_create_literal("domain", NULL);
    lit_domain_list = argument_create_literal("list", hosting_domain_list);

    lit_db = argument_create_literal("database", NULL);
    lit_db_list = argument_create_literal("list", hosting_database_list);

    lit_user = argument_create_literal("user", NULL);
    lit_user_list = argument_create_literal("list", hosting_user_list);

    lit_cron = argument_create_literal("cron", NULL);
    lit_cron_list = argument_create_literal("list", hosting_cron_list);

    arg_hosting = argument_create_string(offsetof(hosting_argument_t, service_name), "<hosting>", complete_hosting, NULL);

    graph_create_full_path(g, lit_hosting, lit_host_list, NULL);

    graph_create_full_path(g, lit_hosting, arg_hosting, lit_domain, lit_domain_list, NULL);
    graph_create_full_path(g, lit_hosting, arg_hosting, lit_db, lit_db_list, NULL);
    graph_create_full_path(g, lit_hosting, arg_hosting, lit_user, lit_user_list, NULL);
    graph_create_full_path(g, lit_hosting, arg_hosting, lit_cron, lit_cron_list, NULL);
}

#if 0
void hosting_register_rules(json_value_t rules)
{
    JSON_ADD_RULE(rules, "GET", "/hosting/web/*");
    JSON_ADD_RULE(rules, "PUT", "/hosting/web/*");
    JSON_ADD_RULE(rules, "POST", "/hosting/web/*");
    JSON_ADD_RULE(rules, "DELETE", "/hosting/web/*");
}
#endif

DECLARE_MODULE(hosting) = {
    MODULE_NAME,
    hosting_regcomm,
    hosting_ctor,
    NULL,
    NULL
};
