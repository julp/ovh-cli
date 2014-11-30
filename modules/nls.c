#include <locale.h>
#include "common.h"

#define PACKAGE "ovh-cli"

bool nls_ctor(void)
{
    setlocale(LC_ALL, "");
    setlocale(LC_NUMERIC, "C"); // for JSON parsing
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);

    return TRUE;
}

DECLARE_MODULE(nls) = {
    "nls",
    NULL,
    nls_ctor,
    NULL,
    NULL
};
