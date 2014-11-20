#include <libxml/parser.h>

#include "common.h"
#include "modules/api.h"

/**
 * an account may have 0, 1 or more servers
 * each server can have several:
 * - boots (at least 2 - rescue/hdd)
 *      + ... each boot can have 0, 1 or more options
 * - tasks
 * - ...
 **/

static bool dedicated_ctor(void)
{
    // NOP (for now)

    return TRUE;
}

static void dedicated_dtor(void)
{
    // NOP (for now)
}

typedef struct {
    char *server_name;
} dedicated_argument_t;

static command_status_t dedicated_list(void *UNUSED(arg), error_t **error)
{
    xmlDocPtr doc;
    request_t *req;
    xmlNodePtr root, n;
    bool request_success;

    req = request_get(REQUEST_FLAG_SIGN, API_BASE_URL "/dedicated/server");
    request_add_header(req, "Accept: text/xml");
    request_success = request_execute(req, RESPONSE_XML, (void **) &doc, error);
    request_dtor(req);
    if (request_success) {
        if (NULL == (root = xmlDocGetRootElement(doc))) {
            return 0;
        }
        for (n = root->children; n != NULL; n = n->next) {
            xmlChar *content;

            content = xmlNodeGetContent(n);
            puts((const char *) content);
            xmlFree(content);
        }
    }

    return request_success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t dedicated_reboot(void *arg, error_t **error)
{
    xmlDocPtr doc;
    request_t *req;
    bool request_success;
    dedicated_argument_t *args;

    request_success = TRUE;
    args = (dedicated_argument_t *) arg;
    assert(NULL != args->server_name);
    // TODO: check server exists?
    if (confirm("Confirm hard reboot of %s", args->server_name)) {
        req = request_post(REQUEST_FLAG_SIGN, NULL, API_BASE_URL "/dedicated/server/%s/reboot", args->server_name);
        request_add_header(req, "Accept: text/xml");
        request_success = request_execute(req, RESPONSE_XML, (void **) &doc, error);
        request_dtor(req);
        if (request_success) {
            // parse response and display it?
        }
    }

    return request_success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

// <opt bootId="%d" bootType="%s" description="%s" kernel="%s" />
static command_status_t dedicated_boot_list(void *arg, error_t **error)
{
    bool request_success;
    dedicated_argument_t *args;

    request_success = TRUE;
    args = (dedicated_argument_t *) arg;
    assert(NULL != args->server_name);
    // fill cache if needed
    //if (!hashtable_get(domains, (void *) server_name, (void **) s) || !(*s)->uptodate) {
    if (TRUE) {
        xmlDocPtr doc;
        request_t *req;
        xmlNodePtr root, n;

        req = request_get(REQUEST_FLAG_SIGN, API_BASE_URL "/dedicated/server/%s/boot", args->server_name);
        request_add_header(req, "Accept: text/xml");
        request_success = request_execute(req, RESPONSE_XML, (void **) &doc, error);
        request_dtor(req);
        // result
        if (request_success) {
            if (NULL == (root = xmlDocGetRootElement(doc))) {
                return 0;
            }
            for (n = root->children; request_success && n != NULL; n = n->next) {
                xmlDocPtr doc;
                xmlChar *content;

                content = xmlNodeGetContent(n);
                req = request_get(REQUEST_FLAG_SIGN, API_BASE_URL "/dedicated/server/%s/boot/%s", args->server_name, (const char *) content);
                request_add_header(req, "Accept: text/xml");
                request_success &= request_execute(req, RESPONSE_XML, (void **) &doc, error); // request_success is assumed to be TRUE before the first iteration
                request_dtor(req);
                // result
//                 parse_boot((*d)->records, doc);
                xmlFree(content);
            }
            xmlFreeDoc(doc);
//             (*d)->uptodate = TRUE;
        }
    }
    // display
    // ...

    return request_success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static void dedicated_regcomm(graph_t *g)
{
    argument_t *arg_server;
    argument_t *lit_dedicated, *lit_dedi_reboot, *lit_dedi_list;
    argument_t *lit_boot, *lit_boot_list;

    // dedicated ...
    lit_dedicated = argument_create_literal("dedicated", NULL);
    lit_dedi_list = argument_create_literal("list", dedicated_list);
    lit_dedi_reboot = argument_create_literal("reboot", dedicated_reboot);
    // dedicated <server> boot ...
    lit_boot = argument_create_literal("boot", NULL);
    lit_boot_list = argument_create_literal("list", dedicated_boot_list);

    arg_server = argument_create_string(offsetof(dedicated_argument_t, server_name), "<server>", NULL, NULL);

    // dedicated ...
    graph_create_full_path(g, lit_dedicated, lit_dedi_list, NULL);
    graph_create_full_path(g, lit_dedicated, arg_server, lit_dedi_reboot, NULL);
    // dedicated <server> boot ...
    graph_create_full_path(g, lit_dedicated, arg_server, lit_boot, lit_boot_list, NULL);
}

DECLARE_MODULE(dedicated) = {
    "dedicated",
    dedicated_regcomm,
    dedicated_ctor,
    NULL,
    dedicated_dtor
};
