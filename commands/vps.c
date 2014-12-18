#include <string.h>
#include <inttypes.h>

#include "common.h"
#include "json.h"
#include "date.h"
#include "util.h"
#include "table.h"
#include "modules/api.h"
#include "commands/account.h"
#include "struct/hashtable.h"

#define MODULE_NAME "vps"

static command_status_t vps_list(void *UNUSED(arg), error_t **error)
{
    request_t *req;
    bool success;
    json_document_t *doc;

    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/vps");
    success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
    request_destroy(req);
    if (success) {
        Iterator it;
        json_value_t root;

        root = json_document_get_root(doc);
        json_array_to_iterator(&it, root);
        for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
            json_value_t v;

            v = (json_value_t) iterator_current(&it, NULL);
            puts(json_get_string(v));
        }
        iterator_close(&it);
        json_document_destroy(doc);
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static void vps_regcomm(graph_t *g)
{
    argument_t *lit_vps, *lit_vps_list;

    lit_vps = argument_create_literal("vps", NULL);
    lit_vps_list = argument_create_literal("list", vps_list);

    graph_create_full_path(g, lit_vps, lit_vps_list, NULL);
}

DECLARE_MODULE(vps) = {
    MODULE_NAME,
    vps_regcomm,
    NULL,
    NULL,
    NULL
};
