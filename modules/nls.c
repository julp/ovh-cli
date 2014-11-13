#include <locale.h>
#include "common.h"

#define PACKAGE "ovh-cli"

bool nls_ctor(graph_t *UNUSED(g))
{
    setlocale(LC_ALL, "");
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);

    return TRUE;
}

DECLARE_MODULE(nls) = {
    "nls",
    nls_ctor,
    NULL,
    NULL
};
