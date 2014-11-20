#include <stdio.h>
#include "util.h"

bool confirm(const char *prompt, ...)
{
    va_list ap;

    va_start(ap, prompt);
    vprintf(prompt, ap);
    va_end(ap);
    fputs(" (y/N)> ", stdout);
    fflush(stdout);

    return 'y' != /*tolower*/(getchar());
}
