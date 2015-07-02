#include <stdio.h>
#include <string.h>
#include "common.h"
#include "command.h"

extern graph_t *g;

static command_status_t quit(COMMAND_ARGS)
{
    USED(arg);
    USED(error);
    USED(mainopts);
    exit(EXIT_SUCCESS);
}

static command_status_t help(COMMAND_ARGS)
{
    USED(arg);
    USED(error);
    USED(mainopts);
    puts("Available commands:");
    graph_display(g);

    return COMMAND_SUCCESS;
}

static bool str_endswith(const char *string, const char *suffix)
{
    size_t string_len, suffix_len;

    string_len = strlen(string);
    suffix_len = strlen(suffix);
    if (suffix_len > string_len) {
        return FALSE;
    }

    return 0 == strcmp(string + string_len - suffix_len, suffix);
}

// source <(./ovh complete)
static command_status_t complete(COMMAND_ARGS)
{
    char *shell, *content;

    USED(arg);
    USED(mainopts);
    shell = getenv("SHELL");
    if (NULL == shell) {
        error_set(error, WARN, "undefined shell");
        return COMMAND_FAILURE;
    }
    if (str_endswith(shell, "/bash")) {
        content = graph_bash(g);
        puts(content);
        free(content);
    } else if (str_endswith(shell, "/tcsh")) {
        puts(
            "complete ovh \
                'p/1/(help log quit complete account me credentials key vps domain hosting dedicated)/' \
        ");
    } else {
        error_set(error, WARN, "unsupported shell %s", shell);
        return COMMAND_FAILURE;
    }

    return COMMAND_SUCCESS;
}

static void base_regcomm(graph_t *g)
{
    argument_t *lit_help, *lit_quit, *lit_complete;

    lit_help = argument_create_literal("help", help);
    lit_quit = argument_create_literal("quit", quit);
    lit_complete = argument_create_literal("complete", complete);

    graph_create_full_path(g, lit_help, NULL);
    graph_create_full_path(g, lit_quit, NULL);
    graph_create_full_path(g, lit_complete, NULL);
}

DECLARE_MODULE(base) = {
    "base",
    base_regcomm,
    NULL,
    NULL,
    NULL,
    NULL
};
