#include <locale.h>
#include "common.h"
#include "command.h"

#define PACKAGE "ovh-cli"

bool nls_ctor(void)
{
    setlocale(LC_ALL, "");
#ifndef HAVE_STRTOD_L
    setlocale(LC_NUMERIC, "C"); // for JSON parsing
#endif /* !HAVE_STRTOD_L */
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);

    return TRUE;
}

DECLARE_MODULE(nls) = {
    "nls",
    NULL,
    NULL,
    nls_ctor,
    NULL,
    NULL
};
