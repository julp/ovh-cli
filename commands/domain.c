#include <string.h>
#include <inttypes.h>
#include <libxml/parser.h>

#include "common.h"
#include "json.h"
#include "modules/api.h"
#include "struct/hashtable.h"

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

static HashTable *domains = NULL;

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

static bool domain_ctor(void)
{
    domains = hashtable_ascii_cs_new((DupFunc) strdup, free, NULL);

    return TRUE;
}

static void domain_dtor(void)
{
    if (NULL != domains) {
        hashtable_destroy(domains);
    }
}

static char *xmlGetPropAsString(xmlNodePtr node, const char *name)
{
    char *ret;
    xmlChar *value;

    value = xmlGetProp(node, BAD_CAST name);
    /**
     * prefer to return a new "standard" copy to:
     * 1) avoid mixing xmlFree and free
     * 2) stay independant of libxml2/xmlFree internal implemtation
     **/
    ret = strdup((char *) value);
    xmlFree(value);

    return ret;
}

static uint32_t xmlGetPropAsInt(xmlNodePtr node, const char *name)
{
    xmlChar *value;
    unsigned long long int ret;

    value = xmlGetProp(node, BAD_CAST name);
    ret = strtoull(value, NULL, 10);
    xmlFree(value);

    return (uint32_t) ret;
}

static record_type_t xmlGetPropAsRecordType(xmlNodePtr node, const char *name)
{
    size_t i;
    xmlChar *value;
    record_type_t ret;

    value = xmlGetProp(node, BAD_CAST name);
    for (i = 0; i < ARRAY_SIZE(record_type_map); i++) {
        if (0 == strcmp((const char *) value, record_type_map[i].short_name)) {
            ret = i;
            break;
        }
    }
    xmlFree(value);

    return ret;
}

/*
<opt>
  <anon>domain1.ext</anon>
  <anon>domain2.ext</anon>
</opt>
*/
static int domain_list(int argc, const char **argv)
{
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
            hashtable_put(domains, content, hashtable_new(NULL, value_equal, NULL, NULL, record_destroy), NULL);
            puts((const char *) content);
            xmlFree(content);
        }
    } else {
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

// TODO: optionnal arguments fieldType and subDomain in query string
static int record_list(int argc, const char **argv)
{
    HashTable *records;

    assert(3 == argc);
    if (!hashtable_get(domains, (void *) argv[0], (void **) &records) || hashtable_size(records) < 1) {
        xmlDocPtr doc;
        request_t *req;
        xmlNodePtr root, n;
        char *p, url[1024];

        hashtable_put(domains, (void *) argv[0], records = hashtable_new(NULL, value_equal, NULL, NULL, record_destroy), NULL);
        // forge url
        *url = '\0';
        p = stpcpy(url, API_BASE_URL "/domain/zone/");
        p = stpcpy(p, argv[0]);
        p = stpcpy(p, "/record");
        // request
        req = request_get(url);
        request_add_header(req, "Accept: text/xml");
        request_sign(req);
        request_execute(req, RESPONSE_XML, (void **) &doc);
        request_dtor(req);
        // result
        if (NULL == (root = xmlDocGetRootElement(doc))) {
            return 0;
        }
        for (n = root->children; n != NULL; n = n->next) {
#if 0
            xmlAttrPtr a;
#endif
            xmlDocPtr doc;
            xmlNodePtr root;
            xmlChar *content;
            record_t *r;

            content = xmlNodeGetContent(n);
            puts((const char *) content);
            // forge url
            *url = '\0';
            p = stpcpy(url, API_BASE_URL "/domain/zone/");
            p = stpcpy(p, argv[0]);
            p = stpcpy(p, "/record/");
            p = stpcpy(p, (const char *) content);
            // request
            req = request_get(url);
            request_add_header(req, "Accept: text/xml");
            request_sign(req);
            request_execute(req, RESPONSE_XML, (void **) &doc);
            request_dtor(req);
            // result
            if (NULL == (root = xmlDocGetRootElement(doc))) {
                return 0;
            }
            r = mem_new(*r);
            r->id = xmlGetPropAsInt(root, "id");
            r->ttl = xmlGetPropAsInt(root, "ttl");
            r->name = xmlGetPropAsString(root, BAD_CAST "subDomain");
            r->target = xmlGetPropAsString(root, BAD_CAST "target");
            r->type = xmlGetPropAsRecordType(root, "fieldType");
            hashtable_quick_put_ex(records, 0, r->id, NULL, r, NULL);
#if 0
            for (a = root->properties; a != NULL; a = a->next) {
                // TODO: build a cache name (+ type ?) => id
                printf("%s: %s\n", a->name, xmlGetProp(root, a->name)); // TODO: leak on xmlGetProp
            }
#endif
            xmlFree(content);
        }
    }
    {
        Iterator it;

        hashtable_to_iterator(&it, records);
        for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
            record_t *r;

            r = iterator_current(&it, NULL);
            printf("%s %s (id: %" PRIu32 ")\n", r->name, record_type_map[r->type].short_name, r->id);
            printf("    ttl: %" PRIu32 "\n", r->ttl);
            printf("    target: %s\n", r->target);
        }
        iterator_close(&it);
    }

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
    request_t *req;
    String *buffer;
    char *p, url[1024];
    const char *target, *type, *subdomain;

    assert(7 == argc);
    subdomain = argv[3];
    type = argv[5];
    target = argv[6];
    if (!str_include(type, "A", "AAAA", "CNAME", "DKIM", "LOC", "MX", "NAPTR", "NS", "PTR", "SPF", "SRV", "SSHFP", "TXT", NULL)) {
        fprintf(stderr, "unknown DNS record type '%s'\n", type);
        return 0;
    }
    // forge url
    *url = '\0';
    p = stpcpy(url, API_BASE_URL "/domain/zone/");
    p = stpcpy(p, argv[0]);
    p = stpcpy(p, "/record");
    // data
    {
        json_value_t root;
        json_document_t *doc;

        buffer = string_new();
        doc = json_document_new();
        root = json_object();
        json_object_set_property(root, "target", json_string(target));
        json_object_set_property(root, "fieldType", json_string(type));
//         if ('\0' != *subdomain)
            json_object_set_property(root, "subDomain", json_string(subdomain));
        json_document_set_root(doc, root);
        json_document_serialize(doc, buffer);
        json_document_destroy(doc);
    }
    // request
    req = request_post(url, buffer->ptr, STRING_NOCOPY);
    request_add_header(req, "Accept: text/xml");
    request_add_header(req, "Content-type: application/json");
    request_sign(req);
    request_execute(req, RESPONSE_XML, (void **) &doc);
    request_dtor(req);
    string_destroy(buffer);
    // result
    // <opt id="\d+" fieldType="CNAME" subDomain="toto" target="www" ttl="0" zone="domain.ext" />
    // TODO: update cache with response data
}

static int record_delete(int argc, const char **argv)
{
    //
}

static const command_t domain_commands[] = {
    { "list", 0, domain_list, (const char * const []) { ARG_MODULE_NAME, "list", NULL } },
    { "record list", 0, record_list, (const char * const []) { ARG_MODULE_NAME, ARG_ANY_VALUE, "record", "list", NULL } },
    { "record add", 0, record_add, (const char * const []) { ARG_MODULE_NAME, ARG_ANY_VALUE, "record", "add", ARG_ANY_VALUE, "type", ARG_ANY_VALUE, ARG_ANY_VALUE, NULL } },
    { "record delete", 0, record_delete, (const char * const []) { ARG_MODULE_NAME, ARG_ANY_VALUE, "record", "delete", ARG_ANY_VALUE, /*"type", ARG_ANY_VALUE,*/ NULL } },
    { NULL }
};

DECLARE_MODULE(domain) = {
    "domain",
    domain_ctor,
    domain_dtor,
    domain_commands
};
