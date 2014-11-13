#include <openssl/evp.h>
#include "common.h"

static bool openssl_ctor(graph_t *UNUSED(g))
{
    SSL_library_init();
    OpenSSL_add_all_digests();

    return TRUE;
}

static void openssl_dtor(void)
{
    EVP_cleanup();
}

DECLARE_MODULE(openssl) = {
    "openssl",
    openssl_ctor,
    NULL,
    openssl_dtor
};
