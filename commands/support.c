#include <inttypes.h>
#include <string.h>

#include "common.h"
#include "command.h"
#include "date.h"
#include "util.h"
#include "table.h"
#include "modules/api.h"

#define MODULE_NAME "support"

static bool support_ctor(error_t **UNUSED(error))
{
    // NOP (for now)

    return TRUE;
}

static void support_dtor(void)
{
    // NOP (for now)
}

typedef struct {
    int id;
    char *subject;
} support_argument_t;

// TODO: filtering
static command_status_t support_tickets_list(COMMAND_ARGS)
{
    table_t *t;
    bool success;
    request_t *req;
    json_document_t *doc;

    USED(arg);
    USED(mainopts);
    // TODO: no model available for now
//     t = table_new(
//         X
//     );
    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/support/tickets");
    success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
    request_destroy(req);
    if (success) {
        Iterator it;

        json_array_to_iterator(&it, json_document_get_root(doc));
        for (iterator_first(&it); success && iterator_is_valid(&it); iterator_next(&it)) {
            json_value_t v;
            int64_t ticketId;
            json_document_t *doc;

            v = (json_value_t) iterator_current(&it, NULL);
            ticketId = json_get_integer(v);
            req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/support/tickets/%" PRIu32, ticketId);
            success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
            request_destroy(req);
            if (success) {
//                 table_store(t, ...);
                json_document_destroy(doc);
            }
        }
        iterator_close(&it);
        json_document_destroy(doc);
    }
//     table_display(t, TABLE_FLAG_NONE);
//     table_destroy(t);

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t support_tickets_close(COMMAND_ARGS)
{
    bool success;
    request_t *req;
    support_argument_t *args;

    USED(mainopts);
    args = (support_argument_t *) arg;
    req = request_new(REQUEST_FLAG_SIGN, HTTP_POST, NULL, API_BASE_URL "/support/tickets/%" PRIu32 "/close", args->id);
    success = request_execute(req, RESPONSE_IGNORE, NULL, error);
    request_destroy(req);

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t support_tickets_create(COMMAND_ARGS)
{
    char *body;
    bool success;
    support_argument_t *args;

    USED(mainopts);
    body = NULL;
    success = FALSE;
    args = (support_argument_t *) arg;
    assert(NULL != args->subject);
    if (-1 != launch_editor(&body, _("--- write your message here, after having removed this line ---"), error)) {
#ifndef TEST_WITHOUT_SENDING_MSG
        request_t *req;
        json_document_t *doc, *reqdoc;

        {
            json_value_t root;

            reqdoc = json_document_new();
            root = json_object();
            json_object_set_property(root, "body", json_string(body));
//             json_object_set_property(root, "product", json_string(X)); // TODO: resolve product family from serviceName?
//             json_object_set_property(root, "serviceName", json_string(X));
            json_object_set_property(root, "subject", json_string(args->subject));
            json_object_set_property(root, "type", json_string("genericRequest")); // TODO: "criticalIntervention" if VIP, see "/me/vipStatus"
            json_document_set_root(reqdoc, root);
        }
        req = request_new(REQUEST_FLAG_SIGN | REQUEST_FLAG_JSON, HTTP_POST, reqdoc, API_BASE_URL "/support/tickets/create");
        success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
        request_destroy(req);
        json_document_destroy(reqdoc);
        if (success) {
            json_value_t root;
            int64_t ticket_id, message_id, ticket_number;

            root = json_document_get_root(doc);
            JSON_GET_PROP_INT(root, "ticketId", ticket_id);
            JSON_GET_PROP_INT(root, "messageId", message_id);
            JSON_GET_PROP_INT(root, "ticketNumber", ticket_number);
            json_document_destroy(doc);
        }
#else
        success = TRUE;
        printf("Message: >%s<\n", body);
#endif /* TEST_WITHOUT_SENDING_MSG */
        free(body);
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t support_tickets_reopen_or_reply(COMMAND_ARGS, const char *urlfmt)
{
    char *body;
    bool success;
    support_argument_t *args;

    USED(mainopts);
    body = NULL;
    success = FALSE;
    args = (support_argument_t *) arg;
    if (-1 != launch_editor(&body, _("--- write your message here, after having removed this line ---"), error)) {
#ifndef TEST_WITHOUT_SENDING_MSG
        request_t *req;
        json_document_t *reqdoc;

        {
            json_value_t root;

            reqdoc = json_document_new();
            root = json_object();
            json_object_set_property(root, "body", json_string(body));
            json_document_set_root(reqdoc, root);
        }
        req = request_new(REQUEST_FLAG_SIGN | REQUEST_FLAG_JSON, HTTP_POST, reqdoc, urlfmt, args->id);
        success = request_execute(req, RESPONSE_IGNORE, NULL, error);
        request_destroy(req);
        json_document_destroy(reqdoc);
#else
        success = TRUE;
        printf("Message: >%s<\n", body);
#endif /* TEST_WITHOUT_SENDING_MSG */
        free(body);
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static command_status_t support_tickets_reply(COMMAND_ARGS)
{
    return support_tickets_reopen_or_reply(RELAY_COMMAND_ARGS, API_BASE_URL "/support/tickets/%" PRIu32 "/reply");
}

static command_status_t support_tickets_reopen(COMMAND_ARGS)
{
    return support_tickets_reopen_or_reply(RELAY_COMMAND_ARGS, API_BASE_URL "/support/tickets/%" PRIu32 "/reopen");
}

static const char * const authors[] = {
    "customer",
    "support",
    NULL
};

static command_status_t support_tickets_read(COMMAND_ARGS)
{
    bool success;
    request_t *req;
    json_document_t *doc;
    support_argument_t *args;

    USED(mainopts);
    args = (support_argument_t *) arg;
    req = request_new(REQUEST_FLAG_SIGN, HTTP_GET, NULL, API_BASE_URL "/support/tickets/%" PRIu32 "/messages", args->id);
    success = request_execute(req, RESPONSE_JSON, (void **) &doc, error);
    request_destroy(req);
    if (success) {
        table_t *t;
        Iterator it;
        json_value_t root;

        t = table_new(
            4,
            _("author"), TABLE_TYPE_ENUM, authors,
            _("created_at"), TABLE_TYPE_DATETIME,
            _("updated_at"), TABLE_TYPE_DATETIME,
            _("body"), TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE
        );
        root = json_document_get_root(doc);
        json_array_to_iterator(&it, root);
        for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
            char *body;
            int from;
            json_value_t v, f;
            int64_t created_at, update_at;

            v = (json_value_t) iterator_current(&it, NULL);
            JSON_GET_PROP_STRING(v, "body", body);
            JSON_GET_PROP_INT(v, "creationDate", created_at);
            JSON_GET_PROP_INT(v, "updateDate", update_at);
            json_object_get_property(v, "from", &f);
            from = json_get_enum(f, authors, -1);
            assert(-1 != from);
            table_store(t, from, created_at, update_at, body);
        }
        iterator_close(&it);
        json_document_destroy(doc);
        table_display(t, TABLE_FLAG_NONE);
        table_destroy(t);
    }

    return success ? COMMAND_SUCCESS : COMMAND_FAILURE;
}

static void support_regcomm(graph_t *g)
{
    argument_t *arg_id, *arg_subject;
    argument_t *lit_tickets, *lit_list, *lit_create, *lit_close, *lit_read, *lit_reply, *lit_reopen;

    lit_tickets = argument_create_literal("tickets", NULL);
    lit_read = argument_create_literal("read", support_tickets_read);
    lit_list = argument_create_literal("list", support_tickets_list);
    lit_close = argument_create_literal("close", support_tickets_close);
    lit_reply = argument_create_literal("reply", support_tickets_reply);
    lit_reopen = argument_create_literal("reopen", support_tickets_reopen);
    lit_create = argument_create_literal("create", support_tickets_create);

    arg_id = argument_create_string(offsetof(support_argument_t, id), "<ticket id>", NULL, NULL);
    arg_subject = argument_create_string(offsetof(support_argument_t, subject), "<subject>", NULL, NULL);

    graph_create_full_path(g, lit_tickets, lit_list, NULL);
    graph_create_full_path(g, lit_tickets, arg_id, lit_read, NULL);
    graph_create_full_path(g, lit_tickets, arg_id, lit_close, NULL);
    graph_create_full_path(g, lit_tickets, arg_id, lit_reply, NULL);
    graph_create_full_path(g, lit_tickets, arg_id, lit_reopen, NULL);
    graph_create_full_path(g, lit_tickets, lit_create, arg_subject, NULL);
}

static void support_register_rules(json_value_t rules, bool ro)
{
    JSON_ADD_RULE(rules, "GET", "/support/tickets");
    JSON_ADD_RULE(rules, "GET", "/support/tickets/*");
    if (!ro) {
        JSON_ADD_RULE(rules, "POST", "/support/tickets/*");
    }
}

DECLARE_MODULE(support) = {
    MODULE_NAME,
    support_regcomm,
    support_register_rules,
    support_ctor,
    NULL,
    support_dtor
};
