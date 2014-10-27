#include <string.h>
#include <libxml/parser.h>

#include "common.h"
#include "json.h"
#include "modules/api.h"

static bool domain_ctor(void)
{
    return TRUE;
}

static void domain_dtor(void)
{
    // NOP (for now)
}

/*
<opt>
  <anon>domain.ext</anon>
</opt>
*/
static int domain_list(int UNUSED(argc), const char **UNUSED(argv))
{
    xmlDocPtr doc;
    request_t *req;
    xmlNodePtr root, n;

    req = request_get(API_BASE_URL "/domain");
    request_add_header(req, "Accept: text/xml");
    request_sign(req);
    request_execute(req, RESPONSE_XML, (void **) &doc);
    request_dtor(req);

    if (NULL == (root = xmlDocGetRootElement(doc))) {
        return 0;
    }
    for (n = root->children; n != NULL; n = n->next) {
        xmlChar *content;

        content = xmlNodeGetContent(n);
        puts((const char *) content);
        xmlFree(content);
    }

    return 1;
}

// TODO: optionnal arguments fieldType and subDomain in query string
static int record_list(int argc, const char **argv)
{
    xmlDocPtr doc;
    request_t *req;
    xmlNodePtr root, n;
    char *p, url[1024];

    assert(3 == argc);
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
        xmlAttrPtr a;
        xmlDocPtr doc;
        xmlNodePtr root;
        xmlChar *content;

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
        for (a = root->properties; a != NULL; a = a->next) {
            // TODO: build a cache name (+ type ?) => id
            printf("%s: %s\n", a->name, xmlGetProp(root, a->name));
        }
        xmlFree(content);
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

static const command_t domain_commands[] = {
    { "list", 0, domain_list, (const char * const []) { ARG_MODULE_NAME, "list", NULL } },
    { "record list", 0, record_list, (const char * const []) { ARG_MODULE_NAME, ARG_ANY_VALUE, "record", "list", NULL } },
    { "record add", 0, record_add, (const char * const []) { ARG_MODULE_NAME, ARG_ANY_VALUE, "record", "add", ARG_ANY_VALUE, "type", ARG_ANY_VALUE, ARG_ANY_VALUE, NULL } },
    { NULL }
};

DECLARE_MODULE(domain) = {
    "domain",
    domain_ctor,
    domain_dtor,
    domain_commands
};
