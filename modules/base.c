#include <stdio.h>
#include "common.h"

static int leave(int UNUSED(argc), const char **UNUSED(argv))
{
    exit(EXIT_SUCCESS);
}

static int help(int UNUSED(argc), const char **UNUSED(argv))
{
    printf("help\n");
}

#ifdef DEBUG
static int test(int argc, const char **argv)
{
    int i;

    for (i = 0; i < argc; i++) {
        printf("%d. >%s<\n", i + 1, argv[i]);
    }
}
#endif /* DEBUG */

static const command_t base_commands[] = {
    { "help", 0, help, (const char * const []) { "help", NULL } },
    { "quit", 0, leave, (const char * const []) { "quit", NULL } },
#ifdef DEBUG
    { "test", -1, test, (const char * const []) { "test", NULL } },
#endif /* DEBUG */
    { NULL }
};

DECLARE_MODULE(base) = {
    NULL,
    NULL,
    NULL,
    base_commands
};
