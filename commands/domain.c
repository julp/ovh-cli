#include <string.h>
#include <inttypes.h>

#include "common.h"
#include "json.h"
#include "modules/api.h"
#include "modules/libxml.h"
#include "struct/hashtable.h"

typedef struct {
    bool uptodate;
    HashTable *records;
} domain_t;

typedef enum {
    RECORD_TYPE_ANY, /* special value to target any type */
    RECORD_TYPE_A,
    RECORD_TYPE_AAAA,
    RECORD_TYPE_CNAME,
    RECORD_TYPE_DKIM,
    RECORD_TYPE_LOC,
    RECORD_TYPE_MX,
    RECORD_TYPE_NAPTR,
    RECORD_TYPE_NS,
    RECORD_TYPE_PTR,
    RECORD_TYPE_SPF,
    RECORD_TYPE_SRV,
    RECORD_TYPE_SSHFP,
    RECORD_TYPE_TXT
} record_type_t;

static struct {
    const char *short_name;
    // ...
} record_type_map[] = {
    [ RECORD_TYPE_ANY ] = { "ANY" },
    [ RECORD_TYPE_A ] = { "A" },
    [ RECORD_TYPE_AAAA ] = { "AAAA" },
    [ RECORD_TYPE_CNAME ] = { "CNAME" },
    [ RECORD_TYPE_DKIM ] = { "DKIM" },
    [ RECORD_TYPE_LOC ] = { "LOC" },
    [ RECORD_TYPE_MX ] = { "MX" },
    [ RECORD_TYPE_NAPTR ] = { "NAPTR" },
    [ RECORD_TYPE_NS ] = { "NS" },
    [ RECORD_TYPE_PTR ] = { "PTR" },
    [ RECORD_TYPE_SPF ] = { "SPF" },
    [ RECORD_TYPE_SRV ] = { "SRV" },
    [ RECORD_TYPE_SSHFP ] = { "SSHFP" },
    [ RECORD_TYPE_TXT ] = { "TXT" }
};

typedef struct {
    uint32_t id;
    uint32_t ttl;
    const char *name;
    record_type_t type;
    const char *target;
} record_t;

// TODO: domains/records cache is not account specific
static HashTable *domains = NULL;

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
    if (NULL != r->name) {
        free((void *) r->name);
    }
    if (NULL != r->target) {
        free((void *) r->target);
    }
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

static bool domain_ctor(void)
{
    domains = hashtable_ascii_cs_new((DupFunc) strdup, free, domain_destroy);

    return TRUE;
}

static void domain_dtor(void)
{
    if (NULL != domains) {
        hashtable_destroy(domains);
    }
}

static record_type_t xmlGetPropAsRecordType(xmlNodePtr node, const char *name)
{
    size_t i;
    xmlChar *value;
    record_type_t ret;

    ret = 0;
    if (NULL != (value = xmlGetProp(node, BAD_CAST name))) {
        for (i = 0; i < ARRAY_SIZE(record_type_map); i++) {
            if (0 == strcmp((const char *) value, record_type_map[i].short_name)) {
                ret = i;
                break;
            }
        }
        xmlFree(value);
    }

    return ret;
}

static int parse_record(HashTable *records, xmlDocPtr doc)
{
    record_t *r;
    xmlNodePtr root;

    if (NULL == (root = xmlDocGetRootElement(doc))) {
        return 0;
    }
    r = mem_new(*r);
    r->id = xmlGetPropAsInt(root, "id");
    r->ttl = xmlGetPropAsInt(root, "ttl");
    r->name = xmlGetPropAsString(root, "subDomain");
    r->target = xmlGetPropAsString(root, "target");
    r->type = xmlGetPropAsRecordType(root, "fieldType");
    hashtable_quick_put_ex(records, 0, r->id, NULL, r, NULL);
    xmlFreeDoc(doc);

    return TRUE;
}

/*
<opt>
  <anon>domain1.ext</anon>
  <anon>domain2.ext</anon>
</opt>
*/
static int domain_list(int argc, const char **argv)
{
    // populate
    // TODO: hashtable_size(domains) < 1 is not sufficient to known if we have the full list of domains in this hashtable
    if (hashtable_size(domains) < 1/* || (1 == argc && 0 == strcmp(argv[0], "nocache"))*/) {
        xmlDocPtr doc;
        request_t *req;
        xmlNodePtr root, n;

        req = request_get(API_BASE_URL "/domain");
        request_add_header(req, "Accept: text/xml");
        request_sign(req);
        request_execute(req, RESPONSE_XML, (void **) &doc);
        request_dtor(req);

        if (NULL == (root = xmlDocGetRootElement(doc))) {
            return FALSE;
        }
        hashtable_clear(domains);
        for (n = root->children; n != NULL; n = n->next) {
            xmlChar *content;

            content = xmlNodeGetContent(n);
            hashtable_put(domains, content, domain_new(), NULL);
            xmlFree(content);
        }
    }
    // display
    {
        Iterator it;

        hashtable_to_iterator(&it, domains);
        for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
            const char *domain;

            iterator_current(&it, (void **) &domain);
            puts(domain);
        }
        iterator_close(&it);
    }

    return TRUE;
}

static int get_domain_records(const char *domain, domain_t **d)
{
    *d = NULL;
    if (!hashtable_get(domains, (void *) domain, (void **) d) || !(*d)->uptodate) {
        xmlDocPtr doc;
        request_t *req;
        xmlNodePtr root, n;

        if (NULL == *d) {
            hashtable_put(domains, (void *) domain, *d = domain_new(), NULL);
        }
        req = request_get(API_BASE_URL "/domain/zone/%s/record", domain);
        request_add_header(req, "Accept: text/xml");
        request_sign(req);
        request_execute(req, RESPONSE_XML, (void **) &doc);
        request_dtor(req);
        // result
        if (NULL == (root = xmlDocGetRootElement(doc))) {
            return 0;
        }
        for (n = root->children; n != NULL; n = n->next) {
            xmlDocPtr doc;
            xmlChar *content;

            content = xmlNodeGetContent(n);
            puts((const char *) content);
            req = request_get(API_BASE_URL "/domain/zone/%s/record/%s", domain, (const char *) content);
            request_add_header(req, "Accept: text/xml");
            request_sign(req);
            request_execute(req, RESPONSE_XML, (void **) &doc);
            request_dtor(req);
            // result
            parse_record((*d)->records, doc);
            xmlFree(content);
        }
        xmlFreeDoc(doc);
        (*d)->uptodate = TRUE;
    }

    return 1;
}

// TODO: optionnal arguments fieldType and subDomain in query string
static int record_list(int argc, const char **argv)
{
    domain_t *d;
    Iterator it;

    assert(3 == argc);

    get_domain_records(argv[0], &d);
    // display
    hashtable_to_iterator(&it, d->records);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        record_t *r;

        r = iterator_current(&it, NULL);
        printf(
            "%s %s%s%s => %s (ttl: %" PRIu32 ", id: %" PRIu32 ")\n",
            record_type_map[r->type].short_name,
            r->name,
            NULL == r->name || '\0' == *r->name ? "" : ".",
            argv[0],
            r->target,
            r->ttl,
            r->id
        );
    }
    iterator_close(&it);

    return 1;
}

static bool str_include(const char *search, ...)
{
    char *str;
    bool found;
    va_list ap;

    found = FALSE;
    va_start(ap, search);
    while (!found && NULL != (str = va_arg(ap, char *))) {
        if (0 == strcmp(search, str)) {
            found = TRUE;
        }
    }
    va_end(ap);

    return found;
}

// ./ovh domain domain.ext record add toto type CNAME www
// NOTE: it seems OVH permits to create multiple times a DNS record with same name and type
static int record_add(int argc, const char **argv)
{
    xmlDocPtr doc;
    const char *target, *type, *subdomain;

    assert(7 == argc);
    subdomain = argv[3];
    type = argv[5];
    target = argv[6];
    if (!str_include(type, "A", "AAAA", "CNAME", "DKIM", "LOC", "MX", "NAPTR", "NS", "PTR", "SPF", "SRV", "SSHFP", "TXT", NULL)) {
        fprintf(stderr, "unknown DNS record type '%s'\n", type);
        return 0;
    }
    {
        String *buffer;

        // data
        {
            json_value_t root;
            json_document_t *doc;

            buffer = string_new();
            doc = json_document_new();
            root = json_object();
            json_object_set_property(root, "target", json_string(target));
            json_object_set_property(root, "fieldType", json_string(type));
//             if ('\0' != *subdomain)
                json_object_set_property(root, "subDomain", json_string(subdomain));
            json_document_set_root(doc, root);
            json_document_serialize(doc, buffer);
            json_document_destroy(doc);
        }
        // request
        {
            request_t *req;

            req = request_post(buffer->ptr, STRING_NOCOPY, API_BASE_URL "/domain/zone/%s/record", argv[0]);
            request_add_header(req, "Accept: text/xml");
            request_add_header(req, "Content-type: application/json");
            request_sign(req);
            request_execute(req, RESPONSE_XML, (void **) &doc);
            request_dtor(req);
            string_destroy(buffer);
        }
    }
    // result
    {
        domain_t *d;

        d = NULL;
        if (!hashtable_get(domains, (void *) argv[0], (void **) &d)) {
            hashtable_put(domains, (void *) argv[0], d = domain_new(), NULL);
        }
        parse_record(d->records, doc);
    }

    return TRUE;
}

static int record_delete(int argc, const char **argv)
{
    domain_t *d;
    record_t *match;
    const char *record;

    record = argv[3];
    get_domain_records(argv[0], &d);
#if 0
    // what was the goal? If true, it is more the domain which does not exist!
    if (!hashtable_get(domains, (void *) argv[0], (void **) &d)) {
        fprintf(stderr, "Domain '%s' doesn't have any record named '%s'\n", argv[0], argv[3]);
        return 0;
    }
#endif
    {
        Iterator it;
        size_t matches;

        matches = 0;
        hashtable_to_iterator(&it, d->records);
        for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
            record_t *r;

            r = iterator_current(&it, NULL);
            if (0 == strcmp(r->name, record)) { // TODO: strcmp_l? type?
                ++matches;
                match = r;
            }
        }
        iterator_close(&it);
        switch (matches) {
            case 1:
            {
                char c;

                do {
                    printf("Confirm deletion of '%s.%s' (y/N)> ", match->name, argv[0]);
                    fflush(stdout);
                    c = getchar();
                } while ('y' != /*tolower*/(c) && 'n' != /*tolower*/(c));
                if ('n' == /*tolower*/(c)) {
                    return 1;
                }
                break;
            }
            case 0:
                fprintf(stderr, "Abort, no record match '%s'\n", argv[3]);
                return 0;
            default:
                fprintf(stderr, "Abort, more than one record match '%s'\n", argv[3]);
                return 0;
        }
    }
    {
        request_t *req;

        // request
#if 0
        req = request_delete(API_BASE_URL "/domain/zone/%s/%" PRIu32, argv[0], match->id);
        request_add_header(req, "Accept: text/xml");
        request_sign(req);
        request_execute(req, RESPONSE_IGNORE, NULL);
        request_dtor(req);
#else
        printf("deletion of '%s.%s' done\n", match->name, argv[0]);
#endif
        // result
        // ... if successful (TODO)
        hashtable_quick_delete(d->records, match->id, NULL, TRUE);
    }
}

static const command_t domain_commands[] = {
    { domain_list, 2, (const char * const []) { ARG_MODULE_NAME, "list", NULL } },
    { record_list, 4, (const char * const []) { ARG_MODULE_NAME, ARG_ANY_VALUE, "record", "list", NULL } },
    { record_add, 8, (const char * const []) { ARG_MODULE_NAME, ARG_ANY_VALUE, "record", "add", ARG_ANY_VALUE, "type", ARG_ANY_VALUE, ARG_ANY_VALUE, NULL } },
    { record_delete, 5, (const char * const []) { ARG_MODULE_NAME, ARG_ANY_VALUE, "record", "delete", ARG_ANY_VALUE, /*"type", ARG_ANY_VALUE,*/ NULL } },
    { NULL }
};

DECLARE_MODULE(domain) = {
    "domain",
    domain_ctor,
    domain_dtor,
    domain_commands
};
