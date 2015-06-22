#include <stdio.h>
#include <string.h>
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

// source < ./ovh complete
static command_status_t complete(void *UNUSED(arg), error_t **error)
{
    char *shell;

    shell = getenv("SHELL");
    if (NULL == shell) {
        error_set(error, WARN, "undefined shell");
        return COMMAND_FAILURE;
    }
    if (str_endswith(shell, "/bash")) {
        puts("_ovh()\n\
{\n\
    local cur=${COMP_WORDS[COMP_CWORD]}\n\
    if [ ${COMP_CWORD} -eq 1 ]; then\n\
        # roots\n\
        COMPREPLY=( $(compgen -W \"help log quit complete account me credentials vps domain hosting dedicated\" -- $cur) )\n\
    else\n\
        # TODO\n\
        COMPREPLY=( $(compgen -W \"fooOption barOption\" -- $cur) )\n\
    fi\n\
}\n\
\n\
complete -F _ovh ovh");
//     } else if (str_endswith(shell, "/tcsh")) {
        // TODO
    } else {
        error_set(error, WARN, "unsupported shell %s", shell);
        return COMMAND_FAILURE;
    }
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
    NULL
};
