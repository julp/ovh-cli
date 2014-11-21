#include <libxml/parser.h>

#include "common.h"
#include "modules/api.h"

static bool me_ctor(void)
{
    // NOP (for now)

    return TRUE;
}

static void me_dtor(void)
{
    // NOP (for now)
}

static command_status_t me(void *UNUSED(arg), error_t **error)
{
    xmlAttrPtr a;
    xmlDocPtr doc;
    request_t *req;
    xmlNodePtr root;

    req = request_get(REQUEST_FLAG_SIGN, API_BASE_URL "/me");
    request_add_header(req, "Accept: text/xml");
    request_execute(req, RESPONSE_XML, (void **) &doc, error);
    request_dtor(req);
    if (NULL == (root = xmlDocGetRootElement(doc))) {
        return 0;
    }
    for (a = root->properties; a != NULL; a = a->next) {
        xmlChar *value;

        printf("%s: %s\n", a->name, value = xmlGetProp(root, a->name));
        xmlFree(value);
    }

    return 1;
}

static void me_regcomm(graph_t *g)
{
    argument_t *lit_me;

    lit_me = argument_create_literal("me", me);

    graph_create_full_path(g, lit_me, NULL);
}

#if 0
void me_register_rules(json_value_t rules)
{
    JSON_ADD_RULE(rules, "GET", "/me/*");
    JSON_ADD_RULE(rules, "PUT", "/me/*");
    JSON_ADD_RULE(rules, "POST", "/me/*");
    JSON_ADD_RULE(rules, "DELETE", "/me/*");
}
#endif

DECLARE_MODULE(me) = {
    "me",
    me_regcomm,
    me_ctor,
    NULL,
    me_dtor
};
