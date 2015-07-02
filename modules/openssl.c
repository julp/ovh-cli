#include <openssl/ssl.h>
#include <openssl/evp.h>
#include "common.h"
#include "command.h"

static bool openssl_ctor(void)
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
    NULL,
    NULL,
    openssl_ctor,
    NULL,
    openssl_dtor
};
