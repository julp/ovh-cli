#include <string.h>
#include <inttypes.h>

#include "common.h"
#include "util.h"
#include "modules/api.h"
#include "modules/libxml.h"
#include "struct/hashtable.h"

#define MODULE_NAME "dedicated"

#define FETCH_ACCOUNT_SERVERS(/*server_set **/ ss) \
    do { \
        ss = NULL; \
        account_current_get_data(MODULE_NAME, &ss); \
        assert(NULL != ss); \
    } while (0);

/**
 * an account may have 0, 1 or more servers
 * each server can have several:
 * - boots (at least 2 - rescue/hdd)
 *      + ... each boot can have 0, 1 or more options
 * - tasks
 * - ...
 **/

// describe all dedicated servers owned by a given account
typedef struct {
    bool uptodate;
    HashTable *servers;
} server_set_t;

// describe a dedicated server
typedef struct {
    bool boots_uptodate;
    HashTable *boots;
} server_t;

static const char * const boot_types[] = {
    "harddisk",
    "ipxeCustomerScript",
    "network",
    "rescue",
    NULL
};

// describe a boot
typedef struct {
    int type;
    uint32_t id;
    const char *kernel;
    const char *description;
} boot_t;

static void server_destroy(void *data)
{
    server_t *s;

    assert(NULL != data);

    s = (server_t *) data;
    hashtable_destroy(s->boots);
    free(s);
}

static void boot_destroy(void *data)
{
    boot_t *b;

    assert(NULL != data);

    b = (boot_t *) data;
    if (NULL != b->kernel) {
        free((void *) b->kernel);
    }
    if (NULL != b->description) {
        free((void *) b->description);
    }
    free(b);
}

static server_t *server_new(void)
{
    server_t *s;

    s = mem_new(*s);
    s->boots_uptodate = FALSE;
    s->boots = hashtable_new(NULL, value_equal, NULL, NULL, boot_destroy);

    return s;
}

static void server_set_destroy(void *data)
{
    server_set_t *ss;

    assert(NULL != data);
    ss = (server_set_t *) data;
    if (NULL != ss->servers) {
        hashtable_destroy(ss->servers);
    }
    free(ss);
}

static void dedicated_on_set_account(void **data)
{
    if (NULL == *data) {
        server_set_t *ss;

        ss = mem_new(*ss);
        ss->uptodate = FALSE;
        ss->servers = hashtable_ascii_cs_new((DupFunc) strdup, free, server_destroy);
        *data = ss;
    }
}

static bool dedicated_ctor(void)
{
    account_register_module_callbacks(MODULE_NAME, server_set_destroy, dedicated_on_set_account);

    return TRUE;
}

typedef struct {
    char *server_name;
} dedicated_argument_t;

static command_status_t fetch_servers(server_set_t *ss, bool force, error_t **error)
{
    if (!ss->uptodate || force) {
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
                error_set(error, WARN, "Failed to parse XML document");
                return COMMAND_FAILURE;
            }
            hashtable_clear(ss->servers);
            for (n = root->children; n != NULL; n = n->next) {
                xmlChar *content;

                content = xmlNodeGetContent(n);
                hashtable_put(ss->servers, content, server_new(), NULL);
                xmlFree(content);
            }
            ss->uptodate = TRUE;
        } else {
            return COMMAND_FAILURE;
        }
    }

    return COMMAND_SUCCESS;
}

static command_status_t dedicated_list(void *UNUSED(arg), error_t **error)
{
    server_set_t *ss;
    command_status_t ret;

    FETCH_ACCOUNT_SERVERS(ss);
    // populate
    if (COMMAND_SUCCESS != (ret = fetch_servers(ss, FALSE /*args->nocache*/, error))) {
        return ret;
    }
    // display
    hashtable_puts_keys(ss->servers);

    return COMMAND_SUCCESS;
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
    if (confirm(_("Confirm hard reboot of %s"), args->server_name)) {
        req = request_post(REQUEST_FLAG_SIGN, NULL, API_BASE_URL "/dedicated/server/%s/reboot", args->server_name);
        request_add_header(req, "Accept: text/xml");
        request_success = request_execute(req, RESPONSE_XML, (void **) &doc, error);
        request_dtor(req);
        if (request_success) {
            // parse response to register the task. Display it? ("This request to hard reboot %s is registered as task #%d (see dedicated %s task %d to see its status)")
        }
    }

    return request_success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static int parse_boot(HashTable *boots, xmlDocPtr doc)
{
    boot_t *b;
    xmlNodePtr root;

    if (NULL == (root = xmlDocGetRootElement(doc))) {
        return 0;
    }
    b = mem_new(*b);
    b->id = xmlGetPropAsInt(root, "bootId");
    b->kernel = xmlGetPropAsString(root, "kernel");
    b->description = xmlGetPropAsString(root, "description");
    b->type = xmlGetPropAsCollectionIndex(root, "bootType", boot_types, -1);
    hashtable_quick_put_ex(boots, 0, b->id, NULL, b, NULL);
    xmlFreeDoc(doc);

    return TRUE;
}

static int fetch_server_boots(const char *server_name, server_t **s, error_t **error)
{
    server_set_t *ss;
    bool request_success;

    *s = NULL;
    FETCH_ACCOUNT_SERVERS(ss);
    request_success = TRUE;
    if (!hashtable_get(ss->servers, (void *) server_name, (void **) s) || !(*s)->boots_uptodate) {
        xmlDocPtr doc;
        request_t *req;
        xmlNodePtr root, n;

        if (NULL == *s) {
            hashtable_put(ss->servers, (void *) server_name, *s = server_new(), NULL);
        }
        req = request_get(REQUEST_FLAG_SIGN, API_BASE_URL "/dedicated/server/%s/boot", server_name);
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
                req = request_get(REQUEST_FLAG_SIGN, API_BASE_URL "/dedicated/server/%s/boot/%s", server_name, (const char *) content);
                request_add_header(req, "Accept: text/xml");
                request_success &= request_execute(req, RESPONSE_XML, (void **) &doc, error); // request_success is assumed to be TRUE before the first iteration
                request_dtor(req);
                // result
                parse_boot((*s)->boots, doc);
                xmlFree(content);
            }
            xmlFreeDoc(doc);
            (*s)->boots_uptodate = TRUE;
        }
    }

    return request_success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

// <opt bootId="%d" bootType="%s" description="%s" kernel="%s" />
static command_status_t dedicated_boot_list(void *arg, error_t **error)
{
    server_t *s;
    Iterator it;
    command_status_t ret;
    dedicated_argument_t *args;

    args = (dedicated_argument_t *) arg;
    assert(NULL != args->server_name);
    if (COMMAND_SUCCESS == (ret = fetch_server_boots(args->server_name, &s, error))) {
        // display
        hashtable_to_iterator(&it, s->boots);
        printf("Available boots for '%s':\n", args->server_name);
        for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
            boot_t *b;

            b = iterator_current(&it, NULL);
            printf(
#if 0
                "  - %s (%s) (id: %" PRIu32 "): %s\n",
#else
                "  - %s (%s): %s\n",
#endif
                b->kernel,
                boot_types[b->type],
#if 0
                b->id,
#endif
                b->description
            );
        }
        iterator_close(&it);
    }

    return ret;
}

static bool complete_servers(void *parsed_arguments, const char *current_argument, size_t current_argument_len, DPtrArray *possibilities, void *data)
{
    server_set_t *ss;

    FETCH_ACCOUNT_SERVERS(ss);
    if (COMMAND_SUCCESS != fetch_servers(ss, FALSE, NULL)) {
        return FALSE;
    }

    return complete_from_hashtable_keys(parsed_arguments, current_argument, current_argument_len, possibilities, ss->servers);
}

static bool complete_boots(void *parsed_arguments, const char *current_argument, size_t current_argument_len, DPtrArray *possibilities, void *data)
{
    server_t *s;
    bool request_success;
    dedicated_argument_t *args;

    args = (dedicated_argument_t *) parsed_arguments;
    assert(NULL != args->server_name);
    if (request_success = (COMMAND_SUCCESS == fetch_server_boots(args->server_name, &s, NULL))) {
        Iterator it;

        hashtable_to_iterator(&it, s->boots);
        for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
            boot_t *b;

            b = iterator_current(&it, NULL);
            if (0 == strncmp(b->kernel, current_argument, current_argument_len)) {
                dptrarray_push(possibilities, (void *) b->kernel);
            }
        }
        iterator_close(&it);
    }

    return request_success;
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

    arg_server = argument_create_string(offsetof(dedicated_argument_t, server_name), "<server>", complete_servers, NULL);

    // dedicated ...
    graph_create_full_path(g, lit_dedicated, lit_dedi_list, NULL);
    graph_create_full_path(g, lit_dedicated, arg_server, lit_dedi_reboot, NULL);
    // dedicated <server> boot ...
    graph_create_full_path(g, lit_dedicated, arg_server, lit_boot, lit_boot_list, NULL);
}

#if 0
void dedicated_register_rules(json_value_t rules)
{
    JSON_ADD_RULE(rules, "GET", "/dedicated/server/*");
    JSON_ADD_RULE(rules, "PUT", "/dedicated/server/*");
    JSON_ADD_RULE(rules, "POST", "/dedicated/server/*");
    JSON_ADD_RULE(rules, "DELETE", "/dedicated/server/*");
}
#endif

DECLARE_MODULE(dedicated) = {
    MODULE_NAME,
    dedicated_regcomm,
    dedicated_ctor,
    NULL,
    NULL
};
