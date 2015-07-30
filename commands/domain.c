#include <string.h>
#include <inttypes.h>

#include "common.h"
#include "command.h"
#include "date.h"
#include "util.h"
#include "modules/api.h"
#include "modules/table.h"
#include "modules/sqlite.h"
#include "commands/account.h"
#include "struct/hashtable.h"

#define MODULE_NAME "domain"

#define FETCH_DOMAINS_IF_NEEDED

#define FETCH_ACCOUNT_DOMAINS(/*domain_set **/ ds) \
    do { \
        ds = NULL; \
        account_current_get_data(MODULE_NAME, (void **) &ds); \
        assert(NULL != ds); \
    } while (0);

// account 0,N <=> 1,1 domain 0,N <=> 1,1 record

typedef enum {
#define DECLARE_RECORD_TYPE(name) \
    RECORD_TYPE_ ## name,
#include "record.h"
#undef DECLARE_RECORD_TYPE
} record_type_t;

#if 0 /* UNUSED */
static struct {
    const char *short_name;
    // ...
} record_type_map[] = {
#define DECLARE_RECORD_TYPE(name) \
    [ RECORD_TYPE_ ## name ] = { #name },
#include "record.h"
#undef DECLARE_RECORD_TYPE
};
#endif

static const char *domain_record_types[] = {
#define DECLARE_RECORD_TYPE(name) \
    #name,
#include "record.h"
#undef DECLARE_RECORD_TYPE
    NULL
};

enum {
    STMT_DOMAIN_LIST,
    STMT_DOMAIN_UPSERT,
    STMT_DOMAIN_COMPLETION,
    STMT_RECORD_LIST,
    STMT_RECORD_UPSERT,
    STMT_RECORD_COMPLETION,
    STMT_COUNT
};

static sqlite_statement_t statements[STMT_COUNT] = {
    [ STMT_DOMAIN_LIST ]       = DECL_STMT("SELECT * FROM domains WHERE accountId = ?", "i", ""),
    [ STMT_DOMAIN_UPSERT ]     = DECL_STMT("INSERT OR REPLACE INTO domains(accountId, name, hasDnsAnycast, dnssecSupported, owoSupported, transferLockStatus, offer, nameServerType, engagedUpTo, contactBilling, expiration, contactTech, contactAdmin, creation) VALUES(:accountId, :name, :hasDnsAnycast, :dnssecSupported, :owoSupported, :transferLockStatus, :offer, :nameServerType, :engagedUpTo, :contactBilling, :expiration, :contactTech, :contactAdmin, :creation)", "is" "bb" "biii" "isissi", ""),
    [ STMT_DOMAIN_COMPLETION ] = DECL_STMT("SELECT name FROM domains WHERE accountId = ? AND name LIKE ? || '%'", "is", "s"),
    [ STMT_RECORD_LIST ]       = DECL_STMT("SELECT records.* FROM records JOIN domains ON records.zone = domains.name WHERE accountId = ? AND domains.name = ?", "is", ""),
    [ STMT_RECORD_UPSERT ]     = DECL_STMT("INSERT OR REPLACE INTO records(target, ttl, zone, fieldType, id, subDomain) VALUES(:target, :ttl, :zone, :fieldType, :id, :subDomain)", "sisiis", ""),
    [ STMT_RECORD_COMPLETION ] = DECL_STMT("SELECT records.* FROM records JOIN domains ON records.zone = domains.name WHERE accountId = ? AND domains.name = ? AND startswith(subDomain, ?)", "iss", ""),
};

// describe all domains owned by a given account
typedef struct {
    bool uptodate;
    HashTable *domains;
} domain_set_t;

// describe a domain
typedef struct {
    modelized_t data;
    bool uptodate;
    HashTable *records;
    char *name;
    bool hasDnsAnycast;
    bool dnssecSupported;
    bool owoSupported;
    int transferLockStatus;
    int offer;
    int nameServerType;
    time_t engagedUpTo;
    char *contactBilling;
    time_t expiration;
    char *contactTech;
    char *contactAdmin;
    time_t creation;
} domain_t;

// describe a DNS record of a given domain
typedef struct {
    modelized_t data;
    uint32_t id;
    uint32_t ttl; // in minutes
    const char *subDomain;
    record_type_t fieldType;
    const char *target;
    const char *zone;
} record_t;

typedef struct {
    bool on_off;
    bool nocache;
    uint32_t ttl; // in minutes
    char *domain;
    char *record; // also called subdomain (current value)
    char *subDomain; // new subdomain name
    char *value; // also called target
    record_type_t fieldType;
} domain_record_argument_t;

static const char * const transfer_lock_status[] = {
    "locked",
    "locking",
    "unavailable",
    "unlocked",
    "unlocking",
    NULL
};

static const char * const offers[] = {
    "diamond",
    "gold",
    "platinum",
    NULL
};

static const char * const name_server_types[] = {
    "external",
    "hosted",
    NULL
};

static model_t domain_model = {
    sizeof(domain_t), "domains", NULL, NULL,
    (const model_field_t []) {
        { "name",               MODEL_TYPE_STRING, offsetof(domain_t, name),               0, NULL,                 MODEL_FLAG_PRIMARY },
        { "hasDnsAnycast",      MODEL_TYPE_BOOL,   offsetof(domain_t, hasDnsAnycast),      0, NULL,                 0 },
        { "dnssecSupported",    MODEL_TYPE_BOOL,   offsetof(domain_t, dnssecSupported),    0, NULL,                 0 },
        { "owoSupported",       MODEL_TYPE_BOOL,   offsetof(domain_t, owoSupported),       0, NULL,                 0 },
        { "transferLockStatus", MODEL_TYPE_ENUM,   offsetof(domain_t, transferLockStatus), 0, transfer_lock_status, 0 },
        { "offer",              MODEL_TYPE_ENUM,   offsetof(domain_t, offer),              0, offers,               0 },
        { "nameServerType",     MODEL_TYPE_ENUM,   offsetof(domain_t, nameServerType),     0, name_server_types,    0 },
        { "engagedUpTo",        MODEL_TYPE_DATE,   offsetof(domain_t, engagedUpTo),        0, NULL,                 MODEL_FLAG_NULLABLE },
        { "contactBilling",     MODEL_TYPE_STRING, offsetof(domain_t, contactBilling),     0, NULL,                 0 },
        { "expiration",         MODEL_TYPE_DATE,   offsetof(domain_t, expiration),         0, NULL,                 0 },
        { "contactTech",        MODEL_TYPE_STRING, offsetof(domain_t, contactTech),        0, NULL,                 0 },
        { "contactAdmin",       MODEL_TYPE_STRING, offsetof(domain_t, contactAdmin),       0, NULL,                 0 },
        { "creation",           MODEL_TYPE_DATE,   offsetof(domain_t, creation),           0, NULL,                 0 },
        MODEL_FIELD_SENTINEL
    }
};

static const char *record_to_name(void *ptr)
{
    record_t *r;

    r = (record_t *) ptr;
    return strdup('\0' == *r->subDomain ? "\"\"" : r->subDomain);
}

static const char *record_to_s(void *ptr)
{
    record_t *r;
    char buffer[512];

    r = (record_t *) ptr;
    if ('\0' == *r->subDomain) {
        snprintf(buffer, ARRAY_SIZE(buffer), "type %s - ID %d", domain_record_types[r->fieldType], r->id);
    } else {
        snprintf(buffer, ARRAY_SIZE(buffer), "type %s", domain_record_types[r->fieldType]);
    }

    return strdup(buffer);
}

static model_t record_model = {
    sizeof(record_t), "records", record_to_name, record_to_s,
    (const model_field_t []) {
        { "id",        MODEL_TYPE_INT,    offsetof(record_t, id),        0, NULL,                MODEL_FLAG_PRIMARY | MODEL_FLAG_INTERNAL },
        { "zone",      MODEL_TYPE_STRING, offsetof(record_t, zone),      0, NULL,                MODEL_FLAG_INTERNAL },
        { "ttl",       MODEL_TYPE_INT,    offsetof(record_t, ttl),       0, NULL,                0 }, // in minutes
        { "subDomain", MODEL_TYPE_STRING, offsetof(record_t, subDomain), 0, NULL,                0 }, // record name
        { "fieldType", MODEL_TYPE_ENUM,   offsetof(record_t, fieldType), 0, domain_record_types, 0 },
        { "target",    MODEL_TYPE_STRING, offsetof(record_t, target),    0, NULL,                0 }, // record value
        MODEL_FIELD_SENTINEL
    }
};

static void domain_destroy(void *data)
{
    domain_t *d;

    assert(NULL != data);

    d = (domain_t *) data;
    hashtable_destroy(d->records);
    free(d);
}

static void record_destroy(void *data)
{
    record_t *r;

    assert(NULL != data);

    r = (record_t *) data;
    FREE(r, subDomain);
    FREE(r, target);
    free(r);
}

static domain_t *domain_new(void)
{
    domain_t *d;

    d = mem_new(*d);
    d->uptodate = FALSE;
    d->records = hashtable_new(NULL, value_equal, NULL, NULL, record_destroy);
    d->name = d->contactBilling = d->contactTech = d->contactAdmin = NULL;
    d->hasDnsAnycast = d->dnssecSupported = d->owoSupported = FALSE;
    d->transferLockStatus = d->offer = d->nameServerType = 0;
    d->engagedUpTo = d->expiration = d->creation = (time_t) 0;

    return d;
}

static void domain_set_destroy(void *data)
{
    domain_set_t *ds;

    assert(NULL != data);
    ds = (domain_set_t *) data;
    if (NULL != ds->domains) {
        hashtable_destroy(ds->domains);
    }
    free(ds);
}

static void domain_on_set_account(void **data)
{
    if (NULL == *data) {
        domain_set_t *ds;

        ds = mem_new(*ds);
        ds->uptodate = FALSE;
        ds->domains = hashtable_ascii_cs_new((DupFunc) strdup, free, domain_destroy);
        *data = ds;
    }
}

static bool domain_ctor(error_t **error)
{
    account_register_module_callbacks(MODULE_NAME, domain_set_destroy, domain_on_set_account);

    if (!create_or_migrate("domains", "CREATE TABLE domains(\n\
        accountId INT NOT NULL REFERENCES accounts(id) ON UPDATE CASCADE ON DELETE CASCADE,\n\
        name TEXT NOT NULL,\n\
        -- From GET /domain/zone/{serviceName}\n\
        --lastUpdate INT NOT NULL, -- datetime\n\
        hasDnsAnycast INT NOT NULL, -- boolean\n\
        -- name_servers -- string[] in JSON response: 1 to many\n\
        dnssecSupported INT NOT NULL, -- boolean\n\
        -- From GET /domain/{serviceName}\n\
        owoSupported INT NOT NULL, -- boolean\n\
        -- domain TEXT NOT NULL, -- same as name?\n\
        --lastUpdate INT NOT NULL, -- datetime\n\
        transferLockStatus INT NOT NULL, -- enum\n\
        offer INT NOT NULL, -- enum\n\
        nameServerType INT NOT NULL, -- enum\n\
        -- From GET /domain/{serviceName}/serviceInfos\n\
        -- status INT NOT NULL, -- enum\n\
        engagedUpTo INT, -- date, nullable\n\
        -- possibleRenewPeriod: array of int (JSON response)\n\
        contactBilling TEXT NOT NULL,\n\
        -- renew: subobject (JSON response)\n\
        -- domain TEXT NOT NULL, -- same as name?\n\
        expiration INT NOT NULL, -- date\n\
        contactTech TEXT NOT NULL,\n\
        contactAdmin TEXT NOT NULL,\n\
        creation INT NOT NULL, -- date\n\
        PRIMARY KEY (name)\n\
    )", NULL, 0, error)) {
        return FALSE;
    }
    if (!create_or_migrate("records", "CREATE TABLE records(\n\
        target TEXT NOT NULL,\n\
        ttl INT, -- nullable\n\
        zone TEXT NOT NULL REFERENCES domains(name) ON UPDATE CASCADE ON DELETE CASCADE,\n\
        fieldType INT NOT NULL, -- enum\n\
        id INT NOT NULL, -- OVH ID (why don't they call it recordId?)\n\
        subDomain TEXT, -- nullable\n\
        PRIMARY KEY (id)\n\
    )", NULL, 0, error)) {
        return FALSE;
    }

    if (!statement_batched_prepare(statements, STMT_COUNT, error)) {
        return FALSE;
    }

    return TRUE;
}

static void domain_dtor(void)
{
    statement_batched_finalize(statements, STMT_COUNT);
}

static command_status_t domain_refresh(COMMAND_ARGS);

static void ask_for_refresh(COMMAND_ARGS)
{
    domain_record_argument_t *args;

    args = (domain_record_argument_t *) arg;
    if (confirm(mainopts, _("Do you want to refresh '%s' now?"), args->domain)) {
        domain_refresh(RELAY_COMMAND_ARGS);
    }
}

static bool parse_record(HashTable *records, json_document_t *doc, error_t **error)
{
    record_t *r;
#if 0
    json_value_t root, v;

    root = json_document_get_root(doc);
    r = mem_new(*r);
    JSON_GET_PROP_INT(root, "id", r->id);
    JSON_GET_PROP_INT(root, "ttl", r->ttl);
    JSON_GET_PROP_STRING(root, "target", r->target);
    JSON_GET_PROP_STRING(root, "subDomain", r->subDomain);
    json_object_get_property(root, "fieldType", &v);
    r->fieldType = json_get_enum(v, domain_record_types, RECORD_TYPE_ANY);
#else
    r = (record_t *) modelized_new(&record_model);
    json_object_to_modelized(json_document_get_root(doc), (modelized_t *) r, FALSE, NULL);
#endif
    statement_bind_from_model(&statements[STMT_RECORD_UPSERT], NULL, (modelized_t *) r);
    statement_fetch(&statements[STMT_RECORD_UPSERT], error);
    hashtable_quick_put(records, 0, r->id, NULL, r, NULL);
    json_document_destroy(doc);

    return NULL == *error;
}

static bool fetch_domain(domain_t *d, const char * const domain_name, bool force, error_t **error)
{
    bool success;

    success = TRUE;
    if (force) {
        size_t i;
        const char * const urls[] = { API_BASE_URL "/domain/zone/%s", API_BASE_URL "/domain/%s", API_BASE_URL "/domain/%s/serviceInfos" };
        json_document_t *docs[ARRAY_SIZE(urls)];

        d->name = domain_name;
        for (i = 0; success && i < ARRAY_SIZE(urls); i++) {
            request_t *req;
            req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, urls[i], domain_name);
            success &= request_execute(req, RESPONSE_JSON, (void **) &docs[i], error);
            request_destroy(req);
            if (success) {
                json_object_to_modelized(json_document_get_root(docs[i]), (modelized_t *) d, FALSE, NULL);
            }
        }
        if (success) {
            statement_bind( /* e = enum (int), d = date (int), |n = or NULL */
                &statements[STMT_DOMAIN_UPSERT], NULL /*"is" "bb" "biii" "isissi"*/,
                // accountId (i), name (s)
                current_account->id, d->name,
                // hasDnsAnycast (b), dnssecSupported (b)
                d->hasDnsAnycast, d->dnssecSupported,
                // owoSupported (b), transferLockStatus (e), offer (e), nameServerType (e)
                d->owoSupported, d->transferLockStatus, d->offer, d->nameServerType,
                // engaged_up_to (d|n), contact_billing (s), expiration (d), contact_tech (s), contact_admin (s), creation (d)
                d->engagedUpTo, d->contactBilling, d->expiration, d->contactTech, d->contactAdmin, d->creation
            );
            statement_fetch(&statements[STMT_DOMAIN_UPSERT], error);
        }
        while (0 != --i) {
            json_document_destroy(docs[i]);
        }
    }

    return success;
}

static bool fetch_domains(domain_set_t *ds, bool force, error_t **error)
{
    bool success;

    success = TRUE;
    if (!ds->uptodate || force) {
        request_t *req;
        json_document_t *doc;

        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/domain");
        success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (success) {
            Iterator it;
            json_value_t root;

            root = json_document_get_root(doc);
            hashtable_clear(ds->domains);
            json_array_to_iterator(&it, root);
            for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
                domain_t *d;
                json_value_t v;

                d = (domain_t *) modelized_new(&domain_model);
                d->records = hashtable_new(NULL, value_equal, NULL, NULL, record_destroy); // TODO: COMPAT
                v = (json_value_t) iterator_current(&it, NULL);
                hashtable_put(ds->domains, 0, json_get_string(v), d, NULL); // ds->domains has strdup as key_duper, don't need to strdup it ourself
                success &= fetch_domain(d, json_get_string(v), force, error);
            }
            iterator_close(&it);
            ds->uptodate = TRUE;
            json_document_destroy(doc);
            if (success) {
                account_set_last_fetch_for(MODULE_NAME, error);
            }
        }
    }

    return success;
}

static command_status_t domain_list(COMMAND_ARGS)
{
    domain_set_t *ds;
    domain_record_argument_t *args;

    USED(mainopts);
    args = (domain_record_argument_t *) arg;
    FETCH_ACCOUNT_DOMAINS(ds);
    // populate
    FETCH_DOMAINS_IF_NEEDED;
    if (!fetch_domains(ds, args->nocache, error)) {
        return COMMAND_FAILURE;
    }
    // display
    statement_bind(&statements[STMT_DOMAIN_LIST], NULL, current_account->id);

    return statement_to_table(&domain_model, &statements[STMT_DOMAIN_LIST]);
}

static command_status_t domain_check(COMMAND_ARGS)
{
    bool success;
    domain_set_t *ds;

    USED(arg);
    USED(mainopts);
    FETCH_ACCOUNT_DOMAINS(ds);
    // populate
    if ((success = fetch_domains(ds, FALSE, error))) {
        time_t now;
        Iterator it;

        now = time(NULL);
        hashtable_to_iterator(&it, ds->domains);
        for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
            request_t *req;
            json_document_t *doc;
            const char *domain_name;

            iterator_current(&it, (void **) &domain_name);
            // request
            req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/domain/%s/serviceInfos", domain_name);
            success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
            request_destroy(req);
            // response
            if (success) {
                time_t domain_expiration;
                json_value_t root, expiration;

                root = json_document_get_root(doc);
                if (json_object_get_property(root, "expiration", &expiration)) {
                    if (date_parse_to_timestamp(json_get_string(expiration), NULL, &domain_expiration)) {
                        int diff_days;

                        diff_days = date_diff_in_days(domain_expiration, now);
                        if (diff_days > 0 && diff_days < 3000) {
                            printf("%s expires in %d days\n", domain_name, diff_days);
                        }
                    }
                }
                json_document_destroy(doc);
            }
        }
        iterator_close(&it);
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

#include <libxml/parser.h>
static command_status_t domain_export(COMMAND_ARGS)
{
    xmlDocPtr doc;
    request_t *req;
    xmlNodePtr root;
    bool request_success;
    domain_record_argument_t *args;

    USED(mainopts);
    args = (domain_record_argument_t *) arg;
    assert(NULL != args->domain);

    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/domain/zone/%s/export", args->domain);
    REQUEST_XML_RESPONSE_WANTED(req); // we ask XML instead of JSON else we have to parse invalid json document or to unescape characters
    if ((request_success = request_execute(req, RESPONSE_XML, (void **) &doc, error))) {
        if (NULL != (root = xmlDocGetRootElement(doc))) {
            xmlChar *content;

            content = xmlNodeGetContent(root);
            puts((const char *) content);
            xmlFree(content);
        }
        request_success = NULL != root;
        xmlFreeDoc(doc);
    }
    request_destroy(req);

    return request_success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t domain_refresh(COMMAND_ARGS)
{
    request_t *req;
    bool request_success;
    domain_record_argument_t *args;

    USED(mainopts);
    args = (domain_record_argument_t *) arg;
    assert(NULL != args->domain);

    req = request_new(REQUEST_FLAG_SIGN, HTTP_POST, NULL, error, API_BASE_URL "/domain/zone/%s/refresh", args->domain);
    request_success = request_execute(req, RESPONSE_IGNORE, NULL, error); // Response is void
    request_destroy(req);

    return request_success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static bool get_domain_records(const char *domain, domain_t **d, bool force, error_t **error)
{
    bool success;
    domain_set_t *ds;

    *d = NULL;
    FETCH_ACCOUNT_DOMAINS(ds);
    success = TRUE;
    // TODO: hashtable_clear((*d)->records) if force
    if (!hashtable_get(ds->domains, domain, d) || !(*d)->uptodate || force) {
        request_t *req;
        json_document_t *doc;

        if (NULL == *d) {
            *d = domain_new();
            hashtable_put(ds->domains, 0, domain, *d, NULL);
        }
        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/domain/zone/%s/record", domain);
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
                req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/domain/zone/%s/record/%u", domain, json_get_integer(v));
                success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
                request_destroy(req);
                // result
                if (success) {
                    success = parse_record((*d)->records, doc, error);
                }
            }
            iterator_close(&it);
            json_document_destroy(doc);
            (*d)->uptodate = TRUE;
        }
    }

    return success;
}

// TODO: optionnal arguments fieldType and subDomain in query string
static command_status_t record_list(COMMAND_ARGS)
{
    domain_t *d;
    command_status_t ret;
    domain_record_argument_t *args;

    USED(mainopts);
    args = (domain_record_argument_t *) arg;
    assert(NULL != args->domain);
    if (COMMAND_SUCCESS == (ret = get_domain_records(args->domain, &d, args->nocache, error))) {
        // display
        statement_bind(&statements[STMT_RECORD_LIST], NULL, current_account->id, args->domain);

        return statement_to_table(&record_model, &statements[STMT_RECORD_LIST]);
    }

    return ret;
}

// ./ovh domain domain.ext record toto add www type CNAME
// TODO: ttl (optionnal)
static command_status_t record_add(COMMAND_ARGS)
{
    bool request_success;
    json_document_t *doc;
    domain_record_argument_t *args;

    USED(mainopts);
    args = (domain_record_argument_t *) arg;
    assert(NULL != args->domain);
    assert(NULL != args->record);
    {
        json_document_t *reqdoc;

        // data
        {
            json_value_t root;

            reqdoc = json_document_new();
            root = json_object();
            json_object_set_property(root, "target", json_string(args->value));
            json_object_set_property(root, "fieldType", json_string(domain_record_types[args->fieldType]));
//             if ('\0' != *subdomain)
                json_object_set_property(root, "subDomain", json_string(args->record));
            json_document_set_root(reqdoc, root);
        }
        // request
        {
            request_t *req;

            req = request_new(REQUEST_FLAG_SIGN | REQUEST_FLAG_JSON, HTTP_POST, reqdoc, error, API_BASE_URL "/domain/zone/%s/record", args->domain);
            request_success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
            request_destroy(req);
            json_document_destroy(reqdoc);
        }
    }
    // result
    if (request_success) {
        domain_t *d;
        ht_hash_t h;
        domain_set_t *ds;

        d = NULL;
        ds = NULL;
        account_current_get_data(MODULE_NAME, (void **) &ds);
        assert(NULL != ds);
        h = hashtable_hash(ds->domains, args->domain);
        if (!hashtable_quick_get(ds->domains, h, args->domain, &d)) {
            d = domain_new();
            hashtable_quick_put(ds->domains, 0, h, args->domain, d, NULL);
        }
        parse_record(d->records, doc, error);
        ask_for_refresh(RELAY_COMMAND_ARGS);
    }

    return request_success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static size_t find_record(HashTable *records, const char *subDomain, record_t **match)
{
    Iterator it;
    size_t matches;

    matches = 0;
    hashtable_to_iterator(&it, records);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        record_t *r;

        r = iterator_current(&it, NULL);
        if (r->subDomain[0] == subDomain[0] && 0 == strcmp(r->subDomain, subDomain)) { // TODO: strcmp_l? type?
            ++matches;
            *match = r;
        }
    }
    iterator_close(&it);

    return matches;
}

static command_status_t record_delete(COMMAND_ARGS)
{
    domain_t *d;
    record_t *match;
    bool request_success;
    domain_record_argument_t *args;

    args = (domain_record_argument_t *) arg;
    assert(NULL != args->domain);
    assert(NULL != args->record);
    if ((request_success = (COMMAND_SUCCESS == get_domain_records(args->domain, &d, FALSE, error)))) {
#if 0
        // what was the goal? If true, it is more the domain which does not exist!
        if (!hashtable_get(domains, argv[0], &d)) {
            error_set(error, WARN, "Domain '%s' doesn't have any record named '%s'\n", argv[0], argv[3]);
            return COMMAND_FAILURE;
        }
#endif
        {
            size_t matches;

            matches = find_record(d->records, args->record, &match);
            switch (matches) {
                case 1:
                {
                    if (!confirm(mainopts, _("Confirm deletion of '%s.%s'"), match->subDomain, args->domain)) {
                        return COMMAND_SUCCESS; // yeah, success because user canceled it
                    }
                    break;
                }
                case 0:
                    error_set(error, INFO, "No record match '%s'", args->record);
                    return COMMAND_SUCCESS;
                default:
                    error_set(error, WARN, "Abort, more than one record match '%s'", args->record);
                    return COMMAND_FAILURE;
            }
        }
        {
            request_t *req;

            // request
            req = request_new(REQUEST_FLAG_SIGN, HTTP_DELETE, NULL, error, API_BASE_URL "/domain/zone/%s/record/%" PRIu32, args->domain, match->id);
            request_success = request_execute(req, RESPONSE_IGNORE, NULL, error);
            request_destroy(req);
            // result
            if (request_success) {
                hashtable_quick_delete(d->records, match->id, NULL, TRUE);
                ask_for_refresh(RELAY_COMMAND_ARGS);
            }
        }
    }

    return request_success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

// arguments: [ttl <int>] [name <string>] [target <string>]
// (in any order)
// if we change record's name (subDomain), we need to update records cache
static command_status_t record_update(COMMAND_ARGS)
{
    domain_t *d;
    record_t *r;
    bool success;
    domain_record_argument_t *args;

    USED(mainopts);
    args = (domain_record_argument_t *) arg;
    assert(NULL != args->domain);
    assert(NULL != args->record);
    if ((success = (COMMAND_SUCCESS == get_domain_records(args->domain, &d, FALSE, error)))) {
        json_document_t *reqdoc;

        size_t matches;

        matches = find_record(d->records, args->record, &r);
        switch (matches) {
            case 1:
                // NOP : OK
                break;
            case 0:
                error_set(error, WARN, "Abort, no record match '%s'", args->record);
                return COMMAND_FAILURE;
            default:
                error_set(error, WARN, "Abort, more than one record match '%s'", args->record);
                return COMMAND_FAILURE;
        }
        // data
        {
            json_value_t root;

            reqdoc = json_document_new();
            root = json_object();
            if (NULL != args->value) {
                json_object_set_property(root, "target", json_string(args->value));
            }
            if (NULL != args->subDomain) {
                json_object_set_property(root, "subDomain", json_string(args->subDomain));
            }
            json_object_set_property(root, "ttl", json_integer(args->ttl));
            json_document_set_root(reqdoc, root);
        }
        // request
        {
            request_t *req;

            req = request_new(REQUEST_FLAG_SIGN | REQUEST_FLAG_JSON, HTTP_PUT, reqdoc, error, API_BASE_URL "/domain/zone/%s/record/%" PRIu32, args->domain, r->id);
            success = request_execute(req, RESPONSE_IGNORE, NULL, error);
            request_destroy(req);
            json_document_destroy(reqdoc);
        }
        if (success) {
            if (NULL != args->value) {
                FREE(r, target);
                r->target = strdup(args->value);
            }
            if (NULL != args->subDomain) {
                FREE(r, subDomain);
                r->subDomain = strdup(args->subDomain);
            }
            r->ttl = args->ttl;
            ask_for_refresh(RELAY_COMMAND_ARGS);
        }
    }
//     debug("request update of %s.%s to %s.%s with TTL = %" PRIu32 " and value = '%s'", args->record, args->domain, args->subDomain, args->domain, args->ttl, args->value);

    return COMMAND_SUCCESS;
}

static command_status_t dnssec_status(COMMAND_ARGS)
{
    request_t *req;
    bool request_success;
    json_document_t *doc;
    domain_record_argument_t *args;

    USED(mainopts);
    args = (domain_record_argument_t *) arg;
    assert(NULL != args->domain);
    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/domain/zone/%s/dnssec", args->domain);
    request_success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
    request_destroy(req);
    if (request_success) {
        json_value_t root, v;

        root = json_document_get_root(doc);
#if 0
        // for translations, do not remove
        _("enabled")
        _("disabled")
        _("enableInProgress")
        _("disableInProgress")
#endif
        json_object_get_property(root, "status", &v);
        puts(_(json_get_string(v)));
        json_document_destroy(doc);
    }

    return request_success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t dnssec_enable_disable(COMMAND_ARGS)
{
    request_t *req;
    bool request_success;
    domain_record_argument_t *args;

    USED(mainopts);
    args = (domain_record_argument_t *) arg;
    assert(NULL != args->domain);
    if (args->on_off) {
        req = request_new(REQUEST_FLAG_SIGN, HTTP_POST, NULL, error, API_BASE_URL "/domain/zone/%s/dnssec", args->domain);
    } else {
        req = request_new(REQUEST_FLAG_SIGN, HTTP_DELETE, NULL, error, API_BASE_URL "/domain/zone/%s/dnssec", args->domain);
    }
    request_success = request_execute(req, RESPONSE_IGNORE, NULL, error);
    request_destroy(req);

    return request_success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static bool complete_domains(void *UNUSED(parsed_arguments), const char *current_argument, size_t current_argument_len, completer_t *possibilities, void *UNUSED(data))
{
    char *v;
    Iterator it;

    statement_bind(&statements[STMT_DOMAIN_COMPLETION], NULL, current_account->id, current_argument);
    statement_to_iterator(&it, &statements[STMT_DOMAIN_COMPLETION], &v); // TODO: bind only current_argument_len first characters of current_argument?
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        iterator_current(&it, NULL);
        completer_push(possibilities, v, TRUE);
    }
    iterator_close(&it);

    return TRUE;
}

static bool complete_records(void *parsed_arguments, const char *current_argument, size_t current_argument_len, completer_t *possibilities, void *UNUSED(data))
{
#if 0
    domain_t *d;
    bool request_success;
    domain_record_argument_t *args;

    args = (domain_record_argument_t *) parsed_arguments;
    assert(NULL != args->domain);
    if ((request_success = (COMMAND_SUCCESS == get_domain_records(args->domain, &d, FALSE, NULL)))) {
        Iterator it;

        hashtable_to_iterator(&it, d->records);
        for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
            record_t *r;

            r = iterator_current(&it, NULL);
            if (0 == strncmp(r->subDomain, current_argument, current_argument_len)) {
                completer_push(possibilities, r->subDomain, FALSE);
            }
        }
        iterator_close(&it);
    }

    return request_success;
#else
    domain_record_argument_t *args;

    args = (domain_record_argument_t *) parsed_arguments;
    assert(NULL != args->domain);
    statement_bind(&statements[STMT_RECORD_COMPLETION], NULL, current_account->id, args->domain, current_argument);

    return complete_from_modelized(&record_model, &statements[STMT_RECORD_COMPLETION], possibilities);
#endif
}

static void domain_regcomm(graph_t *g)
{
    argument_t *lit_domain;
    argument_t *arg_domain;

    lit_domain = argument_create_literal("domain", NULL, NULL);
    arg_domain = argument_create_string(offsetof(domain_record_argument_t, domain), "<domain>", complete_domains, NULL);

    // domain ...
    {
        argument_t *lit_list, *lit_check, *lit_refresh, *lit_export, *lit_nocache;

        lit_list = argument_create_literal("list", domain_list, _("list your domains"));
        lit_check = argument_create_literal("check", domain_check, _("display domains about to expire"));
        lit_export = argument_create_literal("export", domain_export, _("export zone in DNS format"));
        lit_refresh = argument_create_literal("refresh", domain_refresh, _("generate a new serial to reflect any change on a DNS zone"));
        lit_nocache = argument_create_relevant_literal(offsetof(domain_record_argument_t, nocache), "nocache", NULL);

        graph_create_full_path(g, lit_domain, lit_list, NULL);
        graph_create_full_path(g, lit_domain, lit_check, NULL);
        graph_create_path(g, lit_list, NULL, lit_nocache, NULL);
        graph_create_full_path(g, lit_domain, arg_domain, lit_export, NULL);
        graph_create_full_path(g, lit_domain, arg_domain, lit_refresh, NULL);
    }
    // domain X dnssec ...
    {
        argument_t *arg_dnssec_on_off;
        argument_t *lit_dnssec, *lit_status;

        lit_dnssec = argument_create_literal("dnssec", NULL, NULL);
        lit_status = argument_create_literal("status", dnssec_status, _("show DNSSEC status (enabled or disabled)"));

        // TODO: arg_dnssec_on_off would need a description
        arg_dnssec_on_off = argument_create_choices_disable_enable(offsetof(domain_record_argument_t, on_off), dnssec_enable_disable);

        graph_create_full_path(g, lit_domain, arg_domain, lit_dnssec, lit_status, NULL);
        graph_create_full_path(g, lit_domain, arg_domain, lit_dnssec, arg_dnssec_on_off, NULL);
    }
    // domain X record ...
    {
        argument_t
            *arg_ttl,
            *arg_name,
            *arg_record, // called subdomain by OVH
            *arg_type,
            *arg_value // called target by OVH
        ;
        argument_t *lit_ttl, *lit_name, *lit_target;
        argument_t *lit_record, *lit_list, *lit_add, *lit_delete, *lit_update, /**lit_type, */*lit_nocache;
        argument_t *lit_list_type, *lit_add_type;

        lit_ttl = argument_create_literal("ttl", NULL, NULL);
        lit_name = argument_create_literal("name", NULL, NULL);
        lit_target = argument_create_literal("target", NULL, NULL);
        lit_record = argument_create_literal("record", NULL, NULL);
        lit_list = argument_create_literal("list", record_list, _("display DNS records of *domain*"));
        lit_add = argument_create_literal("add", record_add, _("create a DNS record"));
        lit_delete = argument_create_literal("delete", record_delete, _("delete a DNS record"));
        lit_update = argument_create_literal("update", record_update, _("alter a DNS record"));
//         lit_type = argument_create_literal("type", NULL);
        lit_add_type = argument_create_literal("type", NULL, NULL);
        lit_list_type = argument_create_literal("type", NULL, NULL);
        lit_nocache = argument_create_relevant_literal(offsetof(domain_record_argument_t, nocache), "nocache", NULL);

        arg_ttl = argument_create_uint(offsetof(domain_record_argument_t, ttl), "<ttl>");
        arg_record = argument_create_string(offsetof(domain_record_argument_t, record), "<record>", complete_records, NULL);
        arg_type = argument_create_choices(offsetof(domain_record_argument_t, fieldType), "<type>",  domain_record_types);
        arg_name = argument_create_string(offsetof(domain_record_argument_t, subDomain), "<name>", NULL, NULL);
        arg_value = argument_create_string(offsetof(domain_record_argument_t, value), "<value>", NULL, NULL);

        graph_create_full_path(g, lit_domain, arg_domain, lit_record, lit_list, NULL);
        graph_create_all_path(g, lit_list, NULL, 1, lit_nocache, 2, /*lit_type*/lit_list_type, arg_type, 0);
        // needs 2 distinct arg_type: 1 for record add command and 1 for record list?
        graph_create_full_path(g, lit_domain, arg_domain, lit_record, arg_record, lit_add, arg_value, /*lit_type*/lit_add_type, arg_type, NULL);
        graph_create_full_path(g, lit_domain, arg_domain, lit_record, arg_record, lit_delete, NULL);

//         graph_create_path(g, /*lit_domain*/arg_domain, lit_update, /*arg_domain, */lit_record, arg_record, NULL);
        graph_create_path(g, arg_record, lit_update, NULL);
        graph_create_all_path(g, lit_update, NULL, 2, lit_name, arg_name, 2, lit_target, arg_value, 2, lit_ttl, arg_ttl, 0);
    }
}

static void domain_register_rules(json_value_t rules, bool ro)
{
    JSON_ADD_RULE(rules, "GET", "/domain");
    JSON_ADD_RULE(rules, "GET", "/domain/*");
    JSON_ADD_RULE(rules, "GET", "/domain/zone/*");
    if (!ro) {
        JSON_ADD_RULE(rules, "PUT", "/domain/zone/*");
        JSON_ADD_RULE(rules, "POST", "/domain/zone/*");
        JSON_ADD_RULE(rules, "DELETE", "/domain/zone/*");
    }
}

DECLARE_MODULE(domain) = {
    MODULE_NAME,
    domain_regcomm,
    domain_register_rules,
    domain_ctor,
    NULL,
    domain_dtor
};
