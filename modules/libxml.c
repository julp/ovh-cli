#include <libxml/parser.h>
#include "common.h"

static bool libxml_ctor(void)
{
    xmlInitParser();

    return TRUE;
}

static void libxml_dtor(void)
{
    xmlCleanupParser();
}

DECLARE_MODULE(libxml) = {
    "libxml",
    libxml_ctor,
    libxml_dtor,
    NULL
};
