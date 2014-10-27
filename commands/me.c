#include <libxml/parser.h>

#include "common.h"
#include "modules/api.h"

static bool me_ctor(void)
{
    return TRUE;
}

static void me_dtor(void)
{
    // NOP (for now)
}

static int me(int UNUSED(argc), const char **UNUSED(argv))
{
    xmlAttrPtr a;
    xmlDocPtr doc;
    request_t *req;
    xmlNodePtr root;

    req = request_get(API_BASE_URL "/me");
    request_add_header(req, "Accept: text/xml");
    request_sign(req);
    request_execute(req, RESPONSE_XML, (void **) &doc);
    request_dtor(req);
    if (NULL == (root = xmlDocGetRootElement(doc))) {
        return 0;
    }
    for (a = root->properties; a != NULL; a = a->next) {
        printf("%s: %s\n", a->name, xmlGetProp(root, a->name));
    }

    return 1;
}

static const command_t me_commands[] = {
    { "me", 0, me, (const char * const []) { "me", NULL } },
    { NULL }
};

DECLARE_MODULE(me) = {
    "me",
    me_ctor,
    me_dtor,
    me_commands
};
