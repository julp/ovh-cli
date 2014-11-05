#include <libxml/parser.h>

#include "common.h"
#include "modules/api.h"

static bool dedicated_ctor(void)
{
    return TRUE;
}

static void dedicated_dtor(void)
{
    // NOP (for now)
}

static int dedicated_list(int UNUSED(argc), const char **UNUSED(argv), error_t **error)
{
    xmlDocPtr doc;
    request_t *req;
    xmlNodePtr root, n;

    req = request_get(API_BASE_URL "/dedicated/server");
    request_add_header(req, "Accept: text/xml");
    request_sign(req);
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

static const command_t dedicated_commands[] = {
    { dedicated_list, 2, (const char * const []) { ARG_MODULE_NAME, "list", NULL } },
    { NULL }
};

DECLARE_MODULE(dedicated) = {
    "dedicated",
    dedicated_ctor,
    NULL,
    dedicated_dtor,
    dedicated_commands
};
