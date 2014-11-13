#include <curl/curl.h>
#include "common.h"

static bool curl_ctor(graph_t *UNUSED(g))
{
    return CURLE_OK == curl_global_init(CURL_GLOBAL_ALL);
}

static void curl_dtor(void)
{
    curl_global_cleanup();
}

DECLARE_MODULE(curl) = {
    "curl",
    curl_ctor,
    NULL,
    curl_dtor
};
