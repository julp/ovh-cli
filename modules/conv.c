#include <errno.h>
#include <iconv.h>

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

typedef enum {
    ICONV_ERR_SUCCESS       = 0,
    ICONV_ERR_CONVERTER     = 1,
    ICONV_ERR_WRONG_CHARSET = 2,
    ICONV_ERR_ILLEGAL_CHAR  = 3,
    ICONV_ERR_ILLEGAL_SEQ   = 4,
    ICONV_ERR_TOO_BIG       = 5,
    ICONV_ERR_NOMEM         = 6,
    ICONV_ERR_UNKNOWN       = 7
} iconv_err_t;

static iconv_err_t iconv_string(
    const char *from, const char *to,
    const char *in, size_t in_len,
    char **out, size_t *out_len
) {
    iconv_t cd;
    int retval;
    char *in_p, *out_p;
    size_t in_left, out_left, out_size, result;

    *out = NULL;
    *out_len = 0;
    result = (size_t) -1;
    retval = ICONV_ERR_SUCCESS;

#if 1
    if ('\0' == *in) {
        *out_len = 0;
        *out = malloc(sizeof(**out) * (*out_len + 1));
        if (NULL == *out) {
            return ICONV_ERR_NOMEM;
        }
        **out = '\0';
        return retval;
    }
#else
    if (0 == strcmp(from, to) || '\0' == *in) {
        *out = (char *) in;
        *out_len = in_len;
        return retval;
    }
#endif

    errno = 0;
    if (((iconv_t) -1) == (cd = iconv_open(to, from))) {
        if (EINVAL == errno) {
            return ICONV_ERR_WRONG_CHARSET;
        } else {
            return ICONV_ERR_CONVERTER;
        }
    }

    out_size = 1; /* out_size: capacity allocated to *out */
    *out = malloc(sizeof(**out) * (out_size + 1)); /* + 1 for \0 */
    if (NULL == *out) {
        return ICONV_ERR_NOMEM;
    }
    in_p = (char *) in;
    out_p = *out;
    in_left = in_len;
    out_left = out_size; /* out_left: free space left in *out */
    while (in_left > 0) {
// printf("iconv(%d, %d)\n", out_size, out_left);
        result = iconv(cd, (const char **) &in_p, &in_left, &out_p, &out_left);
        *out_len = out_p - *out;
        out_left = out_size - *out_len;
        if (((size_t) -1) == result) {
            if (E2BIG == errno && in_left > 0) {
                char *tmp;

                tmp = realloc(*out, sizeof(**out) * (++out_size + 1)); /* *out is no longer valid */
                if (NULL == tmp) {
                    free(*out);
                    return ICONV_ERR_NOMEM;
                }
                *out = tmp;
                out_p = *out + *out_len;
                out_left = out_size - *out_len;
                continue;
            }
        }
        break;
    }

    if (((size_t) -1) != result) {
        while (1) {
            result = iconv(cd, NULL, NULL, &out_p, &out_left);
            *out_len = out_p - *out;
            out_left = out_size - *out_len;
            if (((size_t) -1) != result) {
                break;
            }
            if (E2BIG == errno) {
                char *tmp;

                tmp = realloc(*out, sizeof(**out) * (++out_size + 1)); /* *out is no longer valid */
                if (NULL == tmp) {
                    free(*out);
                    return ICONV_ERR_NOMEM;
                }
                *out = tmp;
                out_p = *out + *out_len;
                out_left = out_size - *out_len;
            } else {
                break;
            }
        }
    }

    iconv_close(cd);

    if (((size_t) -1) == result) {
        switch (errno) {
            case EINVAL:
                retval = ICONV_ERR_ILLEGAL_CHAR;
                break;
            case EILSEQ:
                retval = ICONV_ERR_ILLEGAL_SEQ;
                break;
            case E2BIG:
                retval = ICONV_ERR_TOO_BIG;
                break;
            default:
                retval = ICONV_ERR_UNKNOWN;
        }
    }

    *out_p = '\0';
    *out_len = out_p - *out;

    return retval;
}

int convert_local_to_utf8(const char *src, size_t src_len, char **dst, size_t *dst_len)
{
    return iconv_string(local, "UTF-8", src, src_len, dst, dst_len);
}

int convert_utf8_to_local(const char *src, size_t src_len, char **dst, size_t *dst_len)
{
    return iconv_string("UTF-8", local, src, src_len, dst, dst_len);
}

DECLARE_MODULE(conv) = {
    "conv",
    convert_ctor,
    NULL,
    NULL
};
