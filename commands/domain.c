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

#define MODULE_NAME "domain"

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

// describe all domains owned by a given account
typedef struct {
    bool uptodate;
    HashTable *domains;
} domain_set_t;

// describe a domain
typedef struct {
    bool uptodate;
    HashTable *records;
} domain_t;

// describe a DNS record of a given domain
typedef struct {
    uint32_t id;
    uint32_t ttl; // in minutes
    const char *name;
    record_type_t type;
    const char *target;
} record_t;

typedef struct {
    bool on_off;
    bool nocache;
    uint32_t ttl; // in minutes
    char *domain;
    char *record; // also called subdomain (current value)
    char *name; // new subdomain name
    char *value; // also called target
    record_type_t type;
} domain_record_argument_t;

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
    FREE(r, name);
    FREE(r, target);
    free(r);
}

static domain_t *domain_new(void)
{
    domain_t *d;

    d = mem_new(*d);
    d->uptodate = FALSE;
    d->records = hashtable_new(NULL, value_equal, NULL, NULL, record_destroy);

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

static bool domain_ctor(void)
{
    account_register_module_callbacks(MODULE_NAME, domain_set_destroy, domain_on_set_account);

    return TRUE;
}

// TODO: should be run after any change on a domain?
static command_status_t domain_refresh(COMMAND_ARGS);

static void ask_for_refresh(COMMAND_ARGS)
{
    domain_record_argument_t *args;

    args = (domain_record_argument_t *) arg;
    if (confirm(mainopts, _("Do you want to refresh '%s' now?"), args->domain)) {
        domain_refresh(RELAY_COMMAND_ARGS);
    }
}

static int parse_record(HashTable *records, json_document_t *doc)
{
    record_t *r;
    json_value_t root, v;

    root = json_document_get_root(doc);
    r = mem_new(*r);
    JSON_GET_PROP_INT(root, "id", r->id);
    JSON_GET_PROP_INT(root, "ttl", r->ttl);
    JSON_GET_PROP_STRING(root, "target", r->target);
    JSON_GET_PROP_STRING(root, "subDomain", r->name);
    json_object_get_property(root, "fieldType", &v);
    r->type = json_get_enum(v, domain_record_types, RECORD_TYPE_ANY);
    hashtable_quick_put(records, 0, r->id, NULL, r, NULL);
    json_document_destroy(doc);

    return TRUE;
}

static command_status_t fetch_domains(domain_set_t *ds, bool force, error_t **error)
{
    if (!ds->uptodate || force) {
        request_t *req;
        bool request_success;
        json_document_t *doc;

        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, /*error, */API_BASE_URL "/domain");
        request_success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        if (request_success) {
            Iterator it;
            json_value_t root;

            root = json_document_get_root(doc);
            hashtable_clear(ds->domains);
            json_array_to_iterator(&it, root);
            for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
                json_value_t v;

                v = (json_value_t) iterator_current(&it, NULL);
                hashtable_put(ds->domains, 0, json_get_string(v), domain_new(), NULL); // ds->domains has strdup as key_duper, don't need to strdup it ourself
            }
            iterator_close(&it);
            ds->uptodate = TRUE;
            json_document_destroy(doc);
        } else {
            return COMMAND_FAILURE;
        }
    }

    return COMMAND_SUCCESS;
}

static command_status_t domain_list(COMMAND_ARGS)
{
    domain_set_t *ds;
    command_status_t ret;
    domain_record_argument_t *args;

    USED(mainopts);
    args = (domain_record_argument_t *) arg;
    FETCH_ACCOUNT_DOMAINS(ds);
    // populate
    if (COMMAND_SUCCESS != (ret = fetch_domains(ds, args->nocache, error))) {
        return ret;
    }
    // display
    hashtable_puts_keys(ds->domains);

    return COMMAND_SUCCESS;
}

static command_status_t domain_check(COMMAND_ARGS)
{
    bool success;
    domain_set_t *ds;

    USED(arg);
    USED(mainopts);
    FETCH_ACCOUNT_DOMAINS(ds);
    // populate
    if ((success = (COMMAND_SUCCESS == fetch_domains(ds, FALSE, error)))) {
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
            req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/domain/%s/serviceInfos", domain_name);
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

    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/domain/zone/%s/export", args->domain);
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

    req = request_new(REQUEST_FLAG_SIGN, HTTP_POST, NULL, API_BASE_URL "/domain/zone/%s/refresh", args->domain);
    request_success = request_execute(req, RESPONSE_IGNORE, NULL, error); // Response is void
    request_destroy(req);

    return request_success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static bool get_domain_records(const char *domain, domain_t **d, bool force, error_t **error)
{
    domain_set_t *ds;
    bool request_success;

    *d = NULL;
    FETCH_ACCOUNT_DOMAINS(ds);
    request_success = TRUE;
    // TODO: hashtable_clear((*d)->records) if force
    if (!hashtable_get(ds->domains, domain, d) || !(*d)->uptodate || force) {
        request_t *req;
        json_document_t *doc;

        if (NULL == *d) {
            *d = domain_new();
            hashtable_put(ds->domains, 0, domain, *d, NULL);
        }
        req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/domain/zone/%s/record", domain);
        request_success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        // result
        if (request_success) {
            Iterator it;
            json_value_t root;

            root = json_document_get_root(doc);
            json_array_to_iterator(&it, root);
            for (iterator_first(&it); request_success && iterator_is_valid(&it); iterator_next(&it)) {
                json_value_t v;
                json_document_t *doc;

                v = (json_value_t) iterator_current(&it, NULL);
                req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/domain/zone/%s/record/%u", domain, json_get_integer(v));
                request_success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
                request_destroy(req);
                // result
                parse_record((*d)->records, doc);
            }
            iterator_close(&it);
            json_document_destroy(doc);
            (*d)->uptodate = TRUE;
        }
    }

    return request_success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

// TODO: optionnal arguments fieldType and subDomain in query string
static command_status_t record_list(COMMAND_ARGS)
{
    domain_t *d;
    Iterator it;
    command_status_t ret;
    domain_record_argument_t *args;

    USED(mainopts);
    args = (domain_record_argument_t *) arg;
    assert(NULL != args->domain);
    if (COMMAND_SUCCESS == (ret = get_domain_records(args->domain, &d, args->nocache, error))) {
        // display
        table_t *t;

        t = table_new(
#ifdef PRINT_OVH_ID
            5, _("id"), TABLE_TYPE_INT,
#else
            4,
#endif /* PRINT_OVH_ID */
            _("subdomain"), TABLE_TYPE_STRING,
            _("type"), TABLE_TYPE_ENUM, domain_record_types,
            _("TTL"), TABLE_TYPE_INT,
            _("target"), TABLE_TYPE_STRING
        );
        hashtable_to_iterator(&it, d->records);
        for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
            record_t *r;

            r = iterator_current(&it, NULL);
            if (0 == args->type || r->type == args->type) {
                table_store(t,
#ifdef PRINT_OVH_ID
                    r->id,
#endif /* PRINT_OVH_ID */
                    r->name, r->type, r->ttl, r->target
                );
            }
        }
        iterator_close(&it);
        table_display(t, TABLE_FLAG_NONE);
        table_destroy(t);
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
            json_object_set_property(root, "fieldType", json_string(domain_record_types[args->type]));
//             if ('\0' != *subdomain)
                json_object_set_property(root, "subDomain", json_string(args->record));
            json_document_set_root(reqdoc, root);
        }
        // request
        {
            request_t *req;

            req = request_new(REQUEST_FLAG_SIGN | REQUEST_FLAG_JSON, HTTP_POST, reqdoc, API_BASE_URL "/domain/zone/%s/record", args->domain);
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
        parse_record(d->records, doc);
    }

    return request_success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static size_t find_record(HashTable *records, const char *name, record_t **match)
{
    Iterator it;
    size_t matches;

    matches = 0;
    hashtable_to_iterator(&it, records);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        record_t *r;

        r = iterator_current(&it, NULL);
        if (r->name[0] == name[0] && 0 == strcmp(r->name, name)) { // TODO: strcmp_l? type?
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
                    if (!confirm(mainopts, _("Confirm deletion of '%s.%s'"), match->name, args->domain)) {
                        return COMMAND_SUCCESS; // yeah, success because *we* canceled it
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
            req = request_new(REQUEST_FLAG_SIGN, HTTP_DELETE, NULL, API_BASE_URL "/domain/zone/%s/record/%" PRIu32, args->domain, match->id);
            request_success = request_execute(req, RESPONSE_IGNORE, NULL, error);
            request_destroy(req);
            // result
            if (request_success) {
                hashtable_quick_delete(d->records, match->id, NULL, TRUE);
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
            if (NULL != args->name) {
                json_object_set_property(root, "subDomain", json_string(args->name));
            }
            json_object_set_property(root, "ttl", json_integer(args->ttl));
            json_document_set_root(reqdoc, root);
        }
        // request
        {
            request_t *req;

            req = request_new(REQUEST_FLAG_SIGN | REQUEST_FLAG_JSON, HTTP_PUT, reqdoc, API_BASE_URL "/domain/zone/%s/record/%" PRIu32, args->domain, r->id);
            success = request_execute(req, RESPONSE_IGNORE, NULL, error);
            request_destroy(req);
            json_document_destroy(reqdoc);
        }
        if (success) {
            if (NULL != args->value) {
                FREE(r, target);
                r->target = strdup(args->value);
            }
            if (NULL != args->name) {
                FREE(r, name);
                r->name = strdup(args->name);
            }
            r->ttl = args->ttl;
        }
    }
//     debug("request update of %s.%s to %s.%s with TTL = %" PRIu32 " and value = '%s'", args->record, args->domain, args->name, args->domain, args->ttl, args->value);

    return COMMAND_SUCCESS;
}

static bool complete_domains(void *parsed_arguments, const char *current_argument, size_t current_argument_len, DPtrArray *possibilities, void *UNUSED(data))
{
    domain_set_t *ds;

    FETCH_ACCOUNT_DOMAINS(ds);
    if (COMMAND_SUCCESS != fetch_domains(ds, FALSE, NULL)) {
        return FALSE;
    }

    return complete_from_hashtable_keys(parsed_arguments, current_argument, current_argument_len, possibilities, ds->domains);
}

static bool complete_records(void *parsed_arguments, const char *current_argument, size_t current_argument_len, DPtrArray *possibilities, void *UNUSED(data))
{
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
            if (0 == strncmp(r->name, current_argument, current_argument_len)) {
                dptrarray_push(possibilities, (void *) r->name);
            }
        }
        iterator_close(&it);
    }

    return request_success;
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
    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/domain/zone/%s/dnssec", args->domain);
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
        req = request_new(REQUEST_FLAG_SIGN, HTTP_POST, NULL, API_BASE_URL "/domain/zone/%s/dnssec", args->domain);
    } else {
        req = request_new(REQUEST_FLAG_SIGN, HTTP_DELETE, NULL, API_BASE_URL "/domain/zone/%s/dnssec", args->domain);
    }
    request_success = request_execute(req, RESPONSE_IGNORE, NULL, error);
    request_destroy(req);

    return request_success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static void domain_regcomm(graph_t *g)
{
    argument_t
        *arg_ttl,
        *arg_name,
        *arg_domain,
        *arg_record, // called subdomain by OVH
        *arg_type,
        *arg_value, // called target by OVH
        *arg_dnssec_on_off
    ;
    argument_t *lit_ttl, *lit_name, *lit_target;
    argument_t *lit_dnssec, *lit_dnssec_status;
    argument_t *lit_domain, *lit_domain_list, *lit_domain_check, *lit_domain_refresh, *lit_domain_export, *lit_domain_nocache;
    argument_t *lit_record, *lit_record_list, *lit_record_add, *lit_record_delete, *lit_record_update, /**lit_record_type, */*lit_record_nocache;
    argument_t *lit_record_list_type, *lit_record_add_type;

    lit_ttl = argument_create_literal("ttl", NULL);
    lit_name = argument_create_literal("name", NULL);
    lit_target = argument_create_literal("target", NULL);
    // domain ...
    lit_domain = argument_create_literal("domain", NULL);
    lit_domain_list = argument_create_literal("list", domain_list);
    lit_domain_check = argument_create_literal("check", domain_check);
    lit_domain_export = argument_create_literal("export", domain_export);
    lit_domain_refresh = argument_create_literal("refresh", domain_refresh);
    lit_domain_nocache = argument_create_relevant_literal(offsetof(domain_record_argument_t, nocache), "nocache", NULL);
    // domain X dnssec ...
    lit_dnssec = argument_create_literal("dnssec", NULL);
    lit_dnssec_status = argument_create_literal("status", dnssec_status);
    // domain X record ...
    lit_record = argument_create_literal("record", NULL);
    lit_record_list = argument_create_literal("list", record_list);
    lit_record_add = argument_create_literal("add", record_add);
    lit_record_delete = argument_create_literal("delete", record_delete);
    lit_record_update = argument_create_literal("update", record_update);
//     lit_record_type = argument_create_literal("type", NULL);
    lit_record_add_type = argument_create_literal("type", NULL);
    lit_record_list_type = argument_create_literal("type", NULL);
    lit_record_nocache = argument_create_relevant_literal(offsetof(domain_record_argument_t, nocache), "nocache", NULL);

    arg_ttl = argument_create_uint(offsetof(domain_record_argument_t, ttl), "<ttl>");
    arg_domain = argument_create_string(offsetof(domain_record_argument_t, domain), "<domain>", complete_domains, NULL);
    arg_record = argument_create_string(offsetof(domain_record_argument_t, record), "<record>", complete_records, NULL);
    arg_type = argument_create_choices(offsetof(domain_record_argument_t, type), "<type>",  domain_record_types);
    arg_name = argument_create_string(offsetof(domain_record_argument_t, name), "<name>", NULL, NULL);
    arg_value = argument_create_string(offsetof(domain_record_argument_t, value), "<value>", NULL, NULL);
    arg_dnssec_on_off = argument_create_choices_disable_enable(offsetof(domain_record_argument_t, on_off), dnssec_enable_disable);

    // domain ...
    graph_create_full_path(g, lit_domain, lit_domain_list, NULL);
    graph_create_full_path(g, lit_domain, lit_domain_check, NULL);
    graph_create_path(g, lit_domain_list, NULL, lit_domain_nocache, NULL);
    graph_create_full_path(g, lit_domain, arg_domain, lit_domain_export, NULL);
    graph_create_full_path(g, lit_domain, arg_domain, lit_domain_refresh, NULL);
    // domain X dnssec ...
    graph_create_full_path(g, lit_domain, arg_domain, lit_dnssec, lit_dnssec_status, NULL);
    graph_create_full_path(g, lit_domain, arg_domain, lit_dnssec, arg_dnssec_on_off, NULL);
    // domain X record ...
    graph_create_full_path(g, lit_domain, arg_domain, lit_record, lit_record_list, NULL);
    graph_create_all_path(g, lit_record_list, NULL, 1, lit_record_nocache, 2, /*lit_record_type*/lit_record_list_type, arg_type, 0);
    // needs 2 distinct arg_type: 1 for record add command and 1 for record list?
    graph_create_full_path(g, lit_domain, arg_domain, lit_record, arg_record, lit_record_add, arg_value, /*lit_record_type*/lit_record_add_type, arg_type, NULL);
    graph_create_full_path(g, lit_domain, arg_domain, lit_record, arg_record, lit_record_delete, NULL);

//     graph_create_path(g, /*lit_domain*/arg_domain, lit_record_update, /*arg_domain, */lit_record, arg_record, NULL);
    graph_create_path(g, arg_record, lit_record_update, NULL);
    graph_create_all_path(g, lit_record_update, NULL, 2, lit_name, arg_name, 2, lit_target, arg_value, 2, lit_ttl, arg_ttl, 0);
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
    NULL
};
