#include "common.h"

static const char *local;

bool convert_ctor(void)
{
#ifdef BSD
    {
# include <sys/types.h>
# include <pwd.h>
# include <login_cap.h>

        login_cap_t *lc;
        const char *tmp;
        struct passwd *pwd;

        if (NULL != (pwd = getpwuid(getuid()))) {
            if (NULL != (lc = login_getuserclass(pwd))) {
                if (NULL != (tmp = login_getcapstr(lc, "charset", NULL, NULL))) {
                    local = tmp;
                }
                login_close(lc);
            } else {
                if (NULL != (lc = login_getpwclass(pwd))) {
                    if (NULL != (tmp = login_getcapstr(lc, "charset", NULL, NULL))) {
                        local = tmp;
                    }
                    login_close(lc);
                }
            }
        }
        if (NULL != (tmp = getenv("MM_CHARSET"))) {
            local = tmp;
        }
    }
#endif /* BSD */
    {
#include <langinfo.h>

        // TODO: linux specific?
        local = nl_langinfo(CODESET);
    }

    return TRUE;
}

static int convert_local_to_utf8(const char *src, size_t src_len, char **dst, size_t *dst_len)
{
    if (0 == strcmp(local, "UTF-8")) {
        // nothing to do, just copy
        *dst = src;
        if (NULL != dst_len) {
            dst_len = src_len;
        }
    } else {
        //
    }
}

static int convert_utf8_to_local(const char *src, size_t src_len, char **dst, size_t *dst_len)
{
    if (0 == strcmp(local, "UTF-8")) {
        // nothing to do, just copy
        *dst = src;
        if (NULL != dst_len) {
            dst_len = src_len;
        }
    } else {
        //
    }
}

DECLARE_MODULE(conv) = {
    "conv",
    convert_ctor,
    NULL,
    NULL
};
