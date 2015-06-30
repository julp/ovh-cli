#include <string.h>
#include <libxml/xpath.h>
#include <libxml/parser.h>

#include "common.h"

static bool libxml_ctor(void)
{
    xmlInitParser();
    xmlXPathInit();

    return TRUE;
}

static void libxml_dtor(void)
{
    xmlCleanupParser();
}

DECLARE_MODULE(libxml) = {
    "libxml",
    NULL,
    libxml_ctor,
    NULL,
    libxml_dtor
};

char *xmlGetPropAsString(xmlNodePtr node, const char *name)
{
    char *ret;
    xmlChar *value;

    ret = NULL;
    if (NULL != (value = xmlGetProp(node, BAD_CAST name))) {
        /**
        * prefer to return a new "standard" copy to:
        * 1) avoid mixing xmlFree and free
        * 2) stay independant of libxml2/xmlFree internal implementation
        **/
        ret = strdup((char *) value);
        xmlFree(value);
    }

    return ret;
}

uint32_t xmlGetPropAsInt(xmlNodePtr node, const char *name)
{
    xmlChar *value;
    unsigned long long int ret;

    ret = 0;
    if (NULL != (value = xmlGetProp(node, BAD_CAST name))) {
        ret = strtoull((char *) value, NULL, 10);
        xmlFree(value);
    }

    return (uint32_t) ret;
}

int xmlGetPropAsCollectionIndex(xmlNodePtr node, const char *name, const char * const *values, int fallback)
{
    int ret;
    xmlChar *value;
    const char * const *v;

    ret = fallback;
    if (NULL != (value = xmlGetProp(node, BAD_CAST name))) {
        for (v = values; NULL != *v; v++) {
            if (0 == strcmp((const char *) value, *v)) {
                ret = v - values;
                break;
            }
        }
        xmlFree(value);
    }

    return ret;
}
