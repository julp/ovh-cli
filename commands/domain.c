#include <string.h>
#include <unistd.h>
#include <inttypes.h>

#include "common.h"
#include "command.h"
#include "date.h"
#include "util.h"
#include "modules/api.h"
#include "modules/table.h"
#include "modules/sqlite.h"
#include "commands/account.h"
#include "struct/dptrarray.h"

#define MODULE_NAME "domain"

#define FETCH_DOMAINS_IF_NEEDED \
    do { \
        time_t updated_at; \
 \
        if (!fetch_domains(args->nocache || (!account_get_last_fetch_for(MODULE_NAME, &updated_at, error) && NULL == *error), error)) { \
            return COMMAND_FAILURE; \
        } \
    } while (0);

// account 0,N <=> 1,1 domain 0,N <=> 1,1 record

// #define TEST_WITHOUT_SENDING_HTTP_REQUEST

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
    STMT_RECORD_FIND_BY_NAME,
    STMT_COUNT
};

static sqlite_statement_t statements[STMT_COUNT] = {
    [ STMT_DOMAIN_LIST ]         = DECL_STMT("SELECT * FROM domains WHERE accountId = ?", "i", ""),
    [ STMT_DOMAIN_UPSERT ]       = DECL_STMT("INSERT OR REPLACE INTO domains(accountId, name, hasDnsAnycast, dnssecSupported, owoSupported, transferLockStatus, offer, nameServerType, engagedUpTo, contactBilling, expiration, contactTech, contactAdmin, creation) VALUES(:accountId, :name, :hasDnsAnycast, :dnssecSupported, :owoSupported, :transferLockStatus, :offer, :nameServerType, :engagedUpTo, :contactBilling, :expiration, :contactTech, :contactAdmin, :creation)", "is" "bb" "biii" "isissi", ""),
    [ STMT_DOMAIN_COMPLETION ]   = DECL_STMT("SELECT name FROM domains WHERE accountId = ? AND name LIKE ? || '%'", "is", "s"),
    [ STMT_RECORD_LIST ]         = DECL_STMT("SELECT records.* FROM records JOIN domains ON records.zone = domains.name WHERE accountId = ? AND domains.name = ?", "is", ""),
    [ STMT_RECORD_UPSERT ]       = DECL_STMT("INSERT OR REPLACE INTO records(target, ttl, zone, fieldType, id, subDomain) VALUES(:target, :ttl, :zone, :fieldType, :id, :subDomain)", "sisiis", ""),
    [ STMT_RECORD_COMPLETION ]   = DECL_STMT("SELECT records.* FROM records JOIN domains ON records.zone = domains.name WHERE accountId = ? AND domains.name = ? AND startswith(subDomain, ?)", "iss", ""),
    [ STMT_RECORD_FIND_BY_NAME ] = DECL_STMT("SELECT records.* FROM records JOIN domains ON records.zone = domains.name WHERE accountId = ? AND domains.name = ? AND subDomain = ?", "iss", ""),
};

// describe a domain
typedef struct {
    modelized_t data;
    DECL_MEMBER_STRING(name);
    DECL_MEMBER_BOOL(hasDnsAnycast);
    DECL_MEMBER_BOOL(dnssecSupported);
    DECL_MEMBER_BOOL(owoSupported);
    DECL_MEMBER_ENUM(transferLockStatus);
    DECL_MEMBER_ENUM(offer);
    DECL_MEMBER_ENUM(nameServerType);
    DECL_MEMBER_DATE(engagedUpTo);
    DECL_MEMBER_STRING(contactBilling);
    DECL_MEMBER_DATE(expiration);
    DECL_MEMBER_STRING(contactTech);
    DECL_MEMBER_STRING(contactAdmin);
    DECL_MEMBER_DATE(creation);
} domain_t;

// describe a DNS record of a given domain
typedef struct {
    modelized_t data;
    DECL_MEMBER_INT(id);
    DECL_MEMBER_INT(ttl); // in minutes
    DECL_MEMBER_STRING(subDomain);
    DECL_MEMBER_ENUM(fieldType);
    DECL_MEMBER_STRING(target);
    DECL_MEMBER_STRING(zone);
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
    N_("external"),
    N_("hosted"),
    NULL
};

static model_t *domain_model, *record_model;

#undef DECL_FIELD_STRUCT_NAME
#define DECL_FIELD_STRUCT_NAME domain_t
static model_field_t domain_fields[] = {
    DECL_FIELD_STRING(N_("name"), name, MODEL_FLAG_PRIMARY | MODEL_FLAG_RO),
    DECL_FIELD_BOOL(N_("hasDnsAnycast"), hasDnsAnycast, MODEL_FLAG_RO),
    DECL_FIELD_BOOL(N_("dnssecSupported"), dnssecSupported, MODEL_FLAG_RO),
    DECL_FIELD_BOOL(N_("owoSupported"), owoSupported, MODEL_FLAG_RO),
    DECL_FIELD_ENUM(N_("transferLockStatus"), transferLockStatus, 0, transfer_lock_status),
    DECL_FIELD_ENUM(N_("offer"), offer, MODEL_FLAG_RO, offers),
    DECL_FIELD_ENUM(N_("nameServerType"), nameServerType, 0, name_server_types),
    DECL_FIELD_DATE(N_("engagedUpTo"), engagedUpTo, MODEL_FLAG_NULLABLE | MODEL_FLAG_RO),
    DECL_FIELD_STRING(N_("contactBilling"), contactBilling, MODEL_FLAG_RO),
    DECL_FIELD_DATE(N_("expiration"), expiration, MODEL_FLAG_RO),
    DECL_FIELD_STRING(N_("contactTech"), contactTech, MODEL_FLAG_RO),
    DECL_FIELD_STRING(N_("contactAdmin"), contactAdmin, MODEL_FLAG_RO),
    DECL_FIELD_DATE(N_("creation"), creation, MODEL_FLAG_RO),
    MODEL_FIELD_SENTINEL
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
    size_t target_len;

    r = (record_t *) ptr;
    target_len = strlen(r->target);
    snprintf(buffer, ARRAY_SIZE(buffer), "type %s - %.*s%s", domain_record_types[r->fieldType], 40, r->target, target_len > 40 ? "..." : "");

    return strdup(buffer);
}

#undef DECL_FIELD_STRUCT_NAME
#define DECL_FIELD_STRUCT_NAME record_t
static model_field_t record_fields[] = {
    DECL_FIELD_INT(N_("id"), id, MODEL_FLAG_PRIMARY | MODEL_FLAG_INTERNAL | MODEL_FLAG_RO),
    DECL_FIELD_STRING(N_("zone"), zone, MODEL_FLAG_INTERNAL | MODEL_FLAG_RO),
    DECL_FIELD_INT(N_("TTL"), ttl, 0), // in minutes
    DECL_FIELD_STRING(N_("subdomain"), subDomain, 0), // record name
    DECL_FIELD_ENUM(N_("type"), fieldType, MODEL_FLAG_RO, domain_record_types),
    DECL_FIELD_STRING(N_("target"), target, 0), // record value
    MODEL_FIELD_SENTINEL
};

static bool domain_ctor(error_t **error)
{
    domain_model = model_new("domains", sizeof(domain_t), domain_fields, ARRAY_SIZE(domain_fields) - 1);
    record_model = model_new("records", sizeof(record_t), record_fields, ARRAY_SIZE(record_fields) - 1);
    record_model->to_s = record_to_s;
    record_model->to_name = record_to_name;

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
    model_destroy(domain_model);
    model_destroy(record_model);
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

static bool parse_record(domain_t *UNUSED(domain), json_document_t *doc, error_t **error)
{
    record_t record;

    modelized_init(record_model, (modelized_t *) &record);
    json_object_to_modelized(json_document_get_root(doc), (modelized_t *) &record, FALSE, NULL);
#if 1
    statement_bind_from_model(&statements[STMT_RECORD_UPSERT], NULL, (modelized_t *) &record);
    statement_fetch(&statements[STMT_RECORD_UPSERT], error);
#else
    modelized_save((modelized_t *) &record, error);
#endif
    json_document_destroy(doc);

    return NULL == *error;
}

static bool get_domain_records(domain_t *domain, bool force, error_t **error)
{
    bool success;

    success = TRUE;
    if (force) {
        request_t *req;
        json_document_t *doc;

        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/domain/zone/%s/record", domain->name);
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
                req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/domain/zone/%s/record/%u", domain->name, json_get_integer(v));
                success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
                request_destroy(req);
                // result
                if (success) {
                    success = parse_record(domain, doc, error);
                }
            }
            iterator_close(&it);
            json_document_destroy(doc);
        }
    }

    return success;
}

static bool fetch_domain(const char * const domain_name, bool force, error_t **error)
{
    bool success;

    success = TRUE;
    if (force) {
        size_t i;
        domain_t domain;
        const char * const urls[] = { API_BASE_URL "/domain/zone/%s", API_BASE_URL "/domain/%s", API_BASE_URL "/domain/%s/serviceInfos" };
        json_document_t *docs[ARRAY_SIZE(urls)];

        modelized_init(domain_model, (modelized_t *) &domain);
        domain.name = (char *) domain_name; // TODO: rename name to zone to have nothing to do?
        for (i = 0; success && i < ARRAY_SIZE(urls); i++) {
            request_t *req;
            req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, urls[i], domain_name);
            success = request_execute(req, RESPONSE_JSON, (void **) &docs[i], error);
            request_destroy(req);
            if (success) {
                json_object_to_modelized(json_document_get_root(docs[i]), (modelized_t *) &domain, FALSE, NULL);
            }
        }
        if (success) {
            statement_bind( /* e = enum (int), d = date (int), |n = or NULL */
                &statements[STMT_DOMAIN_UPSERT], NULL /*"is" "bb" "biii" "isissi"*/,
                // accountId (i), name (s)
                current_account->id, domain.name,
                // hasDnsAnycast (b), dnssecSupported (b)
                domain.hasDnsAnycast, domain.dnssecSupported,
                // owoSupported (b), transferLockStatus (e), offer (e), nameServerType (e)
                domain.owoSupported, domain.transferLockStatus, domain.offer, domain.nameServerType,
                // engaged_up_to (d|n), contact_billing (s), expiration (d), contact_tech (s), contact_admin (s), creation (d)
                domain.engagedUpTo, domain.contactBilling, domain.expiration, domain.contactTech, domain.contactAdmin, domain.creation
            );
            statement_fetch(&statements[STMT_DOMAIN_UPSERT], error);
        }
        do {
            json_document_destroy(docs[--i]);
        } while (0 != i);
        if (success) {
            success = get_domain_records(&domain, force, error);
        }
    }

    return success;
}

static bool fetch_domains(bool force, error_t **error)
{
    bool success;

    success = TRUE;
    if (force) {
        request_t *req;
        json_document_t *doc;

        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/domain");
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
                success = fetch_domain(json_get_string(v), force, error);
            }
            iterator_close(&it);
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
    domain_record_argument_t *args;

    USED(mainopts);
    args = (domain_record_argument_t *) arg;
    // populate
    FETCH_DOMAINS_IF_NEEDED;
    // display
    statement_bind(&statements[STMT_DOMAIN_LIST], NULL, current_account->id);

    return statement_to_table(domain_model, &statements[STMT_DOMAIN_LIST]);
}

static command_status_t domain_check(COMMAND_ARGS)
{
//     bool success;
    domain_record_argument_t *args;

    USED(mainopts);
    args = (domain_record_argument_t *) arg;
    FETCH_DOMAINS_IF_NEEDED;
    // TODO

    return COMMAND_SUCCESS;
}

#include <libxml/parser.h>
static command_status_t domain_export(COMMAND_ARGS)
{
    bool success;
    xmlDocPtr doc;
    request_t *req;
    xmlNodePtr root;
    domain_record_argument_t *args;

    USED(mainopts);
    args = (domain_record_argument_t *) arg;
    assert(NULL != args->domain);

    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/domain/zone/%s/export", args->domain);
    REQUEST_XML_RESPONSE_WANTED(req); // we ask XML instead of JSON else we have to parse invalid json document or to unescape characters
    if ((success = request_execute(req, RESPONSE_XML, (void **) &doc, error))) {
        if (NULL != (root = xmlDocGetRootElement(doc))) {
            xmlChar *content;

            content = xmlNodeGetContent(root);
            puts((const char *) content);
            xmlFree(content);
        }
        success = NULL != root;
        xmlFreeDoc(doc);
    }
    request_destroy(req);

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t domain_refresh(COMMAND_ARGS)
{
    bool success;
    request_t *req;
    domain_record_argument_t *args;

    USED(mainopts);
    args = (domain_record_argument_t *) arg;
    assert(NULL != args->domain);

    req = request_new(REQUEST_FLAG_SIGN, HTTP_POST, NULL, error, API_BASE_URL "/domain/zone/%s/refresh", args->domain);
    success = request_execute(req, RESPONSE_IGNORE, NULL, error); // Response is void
    request_destroy(req);

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

// TODO: optionnal arguments fieldType and subDomain in query string
static command_status_t record_list(COMMAND_ARGS)
{
    domain_record_argument_t *args;

    USED(mainopts);
    args = (domain_record_argument_t *) arg;
    assert(NULL != args->domain);
    FETCH_DOMAINS_IF_NEEDED; // just the one we want instead of all of them?
    // display
    statement_bind(&statements[STMT_RECORD_LIST], NULL, current_account->id, args->domain);

    return statement_to_table(record_model, &statements[STMT_RECORD_LIST]);
}

// ./ovh domain domain.ext record toto add www type CNAME
// TODO: ttl (optionnal)
static command_status_t record_add(COMMAND_ARGS)
{
    bool success;
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
#ifndef TEST_WITHOUT_SENDING_HTTP_REQUEST
        // request
        {
            request_t *req;

            req = request_new(REQUEST_FLAG_SIGN | REQUEST_FLAG_JSON, HTTP_POST, reqdoc, error, API_BASE_URL "/domain/zone/%s/record", args->domain);
            success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
            request_destroy(req);
            json_document_destroy(reqdoc);
        }
#endif /* !TEST_WITHOUT_SENDING_HTTP_REQUEST */
    }
    // result
    if (success) {
        parse_record(NULL/* since unused */, doc, error);
        ask_for_refresh(RELAY_COMMAND_ARGS);
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static bool handle_conflict_on_record_name(const char *domain_name, const char *record_name, modelized_t **recordp)
{
    bool ret;
    size_t l;
    Iterator it;
    DPtrArray *ary;

    assert(NULL != domain_name);
    assert(NULL != record_name);
    assert(NULL != recordp);

    ret = TRUE;
    *recordp = NULL;
    ary = dptrarray_new(NULL, (DtorFunc) modelized_destroy, NULL);
    statement_bind(&statements[STMT_RECORD_FIND_BY_NAME], NULL, current_account->id, domain_name, record_name);
    statement_model_to_iterator(&it, &statements[STMT_RECORD_FIND_BY_NAME], record_model, TRUE/*unused*/);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        modelized_t *record;

        record = iterator_current(&it, NULL);
        dptrarray_push(ary, record);
    }
    iterator_close(&it);
    l = dptrarray_length(ary);
    switch (l) {
        case 0:
            ret = FALSE;
            break;
        case 1:
            *recordp = dptrarray_remove_at(ary, 0, FALSE);
            break;
        default:
            if (isatty(STDOUT_FILENO) && isatty(STDIN_FILENO)) {
                while (NULL == *recordp) {
                    size_t i;
                    int userno;
                    char line[64];

                    fputs(_("Targetted record is ambiguous.\n"), stdout);
                    for (i = 0; i < l; i++) {
                        modelized_t *match;
                        const char *name, *desc;

                        match = dptrarray_at_unsafe(ary, i, modelized_t);
                        name = match->model->to_name(match);
                        desc = match->model->to_s(match);
                        printf("    %2zu. %s - %s\n", i + 1, name, desc);
                        free((void *) name);
                        free((void *) desc);
                    }
                    printf("    %2d. %s\n", 0, "Cancel");
                    fputs(_("Please enter your choice: "), stdout);
                    fflush(stdout);
                    if (NULL != fgets(line, STR_SIZE(line), stdin)) {
                        userno = atoi(line);
                        if (0 == userno) {
                            break;
                        } else if (userno > 0 && ((size_t) userno) <= l) {
                            *recordp = dptrarray_remove_at(ary, userno - 1, FALSE);
                        }
                    }
                }
            } else {
                ret = FALSE;
            }
    }
    dptrarray_destroy(ary);

    return ret;
}

static command_status_t record_delete(COMMAND_ARGS)
{
    bool success;
    record_t *record;
    domain_record_argument_t *args;

    args = (domain_record_argument_t *) arg;
    assert(NULL != args->domain);
    assert(NULL != args->record);
    FETCH_DOMAINS_IF_NEEDED; // just the one we want instead of all of them?
    success = handle_conflict_on_record_name(args->domain, args->record, (modelized_t **) &record);
    if (success && NULL != record) {
        request_t *req;

        // request
#ifndef TEST_WITHOUT_SENDING_HTTP_REQUEST
        req = request_new(REQUEST_FLAG_SIGN, HTTP_DELETE, NULL, error, API_BASE_URL "/domain/zone/%s/record/%d", args->domain, record->id);
        success = request_execute(req, RESPONSE_IGNORE, NULL, error);
        request_destroy(req);
#else
        debug("deleting: %s %s %s", record->subDomain, record->target, domain_record_types[record->fieldType]);
#endif /* !TEST_WITHOUT_SENDING_HTTP_REQUEST */
        // result
        if (success) {
            success = modelized_delete((modelized_t *) record, error);
            if (success) {
                ask_for_refresh(RELAY_COMMAND_ARGS);
            }
        }
        modelized_destroy((modelized_t *) record);
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

// arguments: [ttl <int>] [name <string>] [target <string>]
// (in any order)
// if we change record's name (subDomain), we need to update records cache
static command_status_t record_update(COMMAND_ARGS)
{
    bool success;
    record_t *record;
    domain_record_argument_t *args;

    USED(mainopts);
    args = (domain_record_argument_t *) arg;
    assert(NULL != args->domain);
    assert(NULL != args->record);
    FETCH_DOMAINS_IF_NEEDED; // just the one we want instead of all of them?
    success = handle_conflict_on_record_name(args->domain, args->record, (modelized_t **) &record);
    if (success && NULL != record) {
        json_document_t *reqdoc;
        // data
        {
            json_value_t root;

            reqdoc = json_document_new();
            root = json_object();
            if (NULL != args->value) {
                MODELIZED_SET_STRING(record->target, args->value);
                json_object_set_property(root, "target", json_string(args->value));
            }
            if (NULL != args->subDomain) {
                MODELIZED_SET_STRING(record->subDomain, args->subDomain);
                json_object_set_property(root, "subDomain", json_string(args->subDomain));
            }
            record->ttl = args->ttl;
            json_object_set_property(root, "ttl", json_integer(args->ttl));
            json_document_set_root(reqdoc, root);
        }
#ifndef TEST_WITHOUT_SENDING_HTTP_REQUEST
        // request
        {
            request_t *req;

            req = request_new(REQUEST_FLAG_SIGN | REQUEST_FLAG_JSON, HTTP_PUT, reqdoc, error, API_BASE_URL "/domain/zone/%s/record/%d", args->domain, record->id);
            success = request_execute(req, RESPONSE_IGNORE, NULL, error);
            request_destroy(req);
            json_document_destroy(reqdoc);
        }
#else
        debug("updating: %s %s %s", record->subDomain, record->target, domain_record_types[record->fieldType]);
        json_document_destroy(reqdoc);
#endif /* !TEST_WITHOUT_SENDING_HTTP_REQUEST */
        if (success) {
            success = modelized_save((modelized_t *) record, error);
            if (success) {
                ask_for_refresh(RELAY_COMMAND_ARGS);
            }
        }
        modelized_destroy((modelized_t *) record);
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t dnssec_status(COMMAND_ARGS)
{
    bool success;
    request_t *req;
    json_document_t *doc;
    domain_record_argument_t *args;

    USED(mainopts);
    args = (domain_record_argument_t *) arg;
    assert(NULL != args->domain);
    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, error, API_BASE_URL "/domain/zone/%s/dnssec", args->domain);
    success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
    request_destroy(req);
    if (success) {
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

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t dnssec_enable_disable(COMMAND_ARGS)
{
    bool success;
    request_t *req;
    domain_record_argument_t *args;

    USED(mainopts);
    args = (domain_record_argument_t *) arg;
    assert(NULL != args->domain);
    if (args->on_off) {
        req = request_new(REQUEST_FLAG_SIGN, HTTP_POST, NULL, error, API_BASE_URL "/domain/zone/%s/dnssec", args->domain);
    } else {
        req = request_new(REQUEST_FLAG_SIGN, HTTP_DELETE, NULL, error, API_BASE_URL "/domain/zone/%s/dnssec", args->domain);
    }
    success = request_execute(req, RESPONSE_IGNORE, NULL, error);
    request_destroy(req);

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
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
    domain_record_argument_t *args;

    args = (domain_record_argument_t *) parsed_arguments;
    assert(NULL != args->domain);
    statement_bind(&statements[STMT_RECORD_COMPLETION], NULL, current_account->id, args->domain, current_argument);

    return complete_from_modelized(record_model, &statements[STMT_RECORD_COMPLETION], possibilities);
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
#ifndef TEST
        graph_create_path(g, arg_record, lit_update, NULL);
        graph_create_all_path(g, lit_update, NULL, 2, lit_name, arg_name, 2, lit_target, arg_value, 2, lit_ttl, arg_ttl, 0);
#else
        plug_update_subcommands(g, arg_record, record_model, record_update, _("XXX"));
#endif /* !TEST */
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
