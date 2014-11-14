#include <libxml/parser.h>

#include "common.h"
#include "modules/api.h"

static bool dedicated_ctor(void)
{
    // NOP (for now)

    return TRUE;
}

static void dedicated_dtor(void)
{
    // NOP (for now)
}

static command_status_t dedicated_list(void *UNUSED(arg), error_t **error)
{
    xmlDocPtr doc;
    request_t *req;
    xmlNodePtr root, n;

    req = request_get(REQUEST_FLAG_SIGN, API_BASE_URL "/dedicated/server");
    request_add_header(req, "Accept: text/xml");
    request_execute(req, RESPONSE_XML, (void **) &doc, error);
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

static void dedicated_regcomm(graph_t *g)
{
    argument_t *lit_dedicated, *lit_list;

    lit_dedicated = argument_create_literal("dedicated", NULL);
    lit_list = argument_create_literal("list", dedicated_list);

    graph_create_full_path(g, lit_dedicated, lit_list, NULL);
}

DECLARE_MODULE(dedicated) = {
    "dedicated",
    dedicated_regcomm,
    dedicated_ctor,
    NULL,
    dedicated_dtor
};
