#include <stdio.h>
#include "common.h"

static int leave(int UNUSED(argc), const char **UNUSED(argv), error_t **UNUSED(error))
{
    exit(EXIT_SUCCESS);
}

extern const size_t modules_count;
extern const module_t */*builtin_*/modules[];

static int help(int UNUSED(argc), const char **UNUSED(argv), error_t **UNUSED(error))
{
    size_t i;
    const command_t *c;

    puts("Available commands:");
    for (i = 0; i < modules_count; i++) {
        if (NULL != modules[i]->commands) {
            for (c = modules[i]->commands; NULL != c->handle; c++) {
                if (NULL != c->args) {
                    const char *a, **v;

                    printf("  - ");
                    for (v = c->args; NULL != *v; v++) {
                        if (ARG_MODULE_NAME == *v) {
                            a = modules[i]->name;
                        } else if (ARG_ANY_VALUE == *v) {
                            a = "<value>";
                        } else {
                            a = *v;
                        }
                        printf("%s ", a);
                    }
                    printf("\n");
                }
            }
        }
    }

    return TRUE;
}

#ifdef DEBUG
static int test(int argc, const char **argv, error_t **UNUSED(error))
{
    int i;

    for (i = 0; i < argc; i++) {
        printf("%d. >%s<\n", i + 1, argv[i]);
    }

    return TRUE;
}
#endif /* DEBUG */

static const command_t base_commands[] = {
    { help, 0, (const char * const []) { "help", NULL } },
    { leave, 0, (const char * const []) { "quit", NULL } },
#ifdef DEBUG
    { test, -1, (const char * const []) { "test", ARG_ANY_VALUE, NULL } },
#endif /* DEBUG */
    { NULL }
};

DECLARE_MODULE(base) = {
    NULL,
    NULL,
    NULL,
    base_commands
};
