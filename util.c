#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include "util.h"

bool confirm(const main_options_t *mainopts, const char *prompt, ...)
{
    va_list ap;

    if (mainopts->noconfirm) {
        return mainopts->yes;
    } else {
        va_start(ap, prompt);
        vprintf(prompt, ap);
        va_end(ap);
        fputs(" (y/N)> ", stdout);
        if (mainopts->yes) {
            fputs("y\n", stdout);
            return TRUE;
        }
        fflush(stdout);

        return 'y' == /*tolower*/(getchar());
    }
}

#define DEFAULT_WIDTH 80

int console_width(void)
{
    int columns;

    if (isatty(STDOUT_FILENO)) {
        struct winsize w;

        if (NULL != getenv("COLUMNS") && 0 != atoi(getenv("COLUMNS"))) {
            columns = atoi(getenv("COLUMNS"));
        } else if (-1 != ioctl(STDOUT_FILENO, TIOCGWINSZ, &w)) {
            columns = w.ws_col;
        } else {
            columns = DEFAULT_WIDTH;
        }
    } else {
        columns = -1; // unlimited
    }

    return columns;
}
