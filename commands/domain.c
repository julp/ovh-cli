#include <libxml/parser.h>

#include "common.h"
#include "modules/api.h"

/*
<opt>
  <anon>julp.fr</anon>
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

static const command_t domain_commands[] = {
    { "list", 0, domain_list },
    { NULL }
};

DECLARE_MODULE(domain) = {
    "domain",
    NULL, // domain_ctor,
    NULL, // domain_dtor,
    domain_commands
};
