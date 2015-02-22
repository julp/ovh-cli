#include <stdio.h>
#include "common.h"

static command_status_t quit(void *UNUSED(arg), error_t **UNUSED(error))
{
    exit(EXIT_SUCCESS);
}

static command_status_t help(void *UNUSED(arg), error_t **UNUSED(error))
{
    extern graph_t *g;

    puts("Available commands:");
    graph_display(g);

    return COMMAND_SUCCESS;
}

static void base_regcomm(graph_t *g)
{
    argument_t *lit_help, *lit_quit;

    lit_help = argument_create_literal("help", help);
    lit_quit = argument_create_literal("quit", quit);

    graph_create_full_path(g, lit_help, NULL);
    graph_create_full_path(g, lit_quit, NULL);
}

DECLARE_MODULE(base) = {
    "base",
    base_regcomm,
    NULL,
    NULL,
    NULL
};
