#include <unistd.h>
#include <errno.h>
#include <iconv.h>
#include <locale.h>
#include <string.h>

#include "common.h"

static const char *input_encoding, *output_encoding;

static bool convert_ctor(void)
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
                    input_encoding = tmp;
                }
                login_close(lc);
            } else {
                if (NULL != (lc = login_getpwclass(pwd))) {
                    if (NULL != (tmp = login_getcapstr(lc, "charset", NULL, NULL))) {
                        input_encoding = tmp;
                    }
                    login_close(lc);
                }
            }
        }
        if (NULL != (tmp = getenv("MM_CHARSET"))) {
            input_encoding = tmp;
        }
    }
#endif /* BSD */
    {
#include <langinfo.h>

        setlocale(LC_ALL, "");
        // TODO: linux specific?
        input_encoding = nl_langinfo(CODESET);
    }
    if (!isatty(STDOUT_FILENO)) {
        output_encoding = "UTF-8";
    } else {
        output_encoding = input_encoding;
    }

    return TRUE;
}

#define INVALID_SIZE_T ((size_t) -1)
#define INVALID_ICONV_T ((iconv_t) -1)

#define ICONV_SET_ERROR(error, msg, ...) \
    error_set(error, FATAL, msg, ## __VA_ARGS__)

#define ICONV_ERR_CONVERTER(error) \
    ICONV_SET_ERROR(error, _("Cannot open converter"))
#define ICONV_ERR_WRONG_CHARSET(error, to, from) \
    ICONV_SET_ERROR(error, _("Wrong charset, conversion from '%s' to '%s' is not allowed"), to, from)
#define ICONV_ERR_ILLEGAL_CHAR(error) \
    ICONV_SET_ERROR(error, _("Incomplete multibyte character detected in input string"))
#define ICONV_ERR_ILLEGAL_SEQ(error) \
    ICONV_SET_ERROR(error, _("Illegal character detected in input string"))
#define ICONV_ERR_TOO_BIG(error) \
    ICONV_SET_ERROR(error, _("Buffer length exceeded"))
#define ICONV_ERR_UNKNOWN(error, errno) \
    ICONV_SET_ERROR(error, _("Unknown error (%d)"), errno)

static bool iconv_string(const char *from, const char *to, const char *in, size_t in_len, char **out, size_t *out_len_arg_p, error_t **error)
{
    iconv_t cd;
    bool retval;
    char *in_p, *out_p;
    size_t in_left, out_left, out_size, result, out_len, *out_len_p;

    *out = NULL;
    if (NULL == out_len_arg_p) {
        out_len_p = &out_len;
    } else {
        out_len_p = out_len_arg_p;
    }
    *out_len_p = 0;
    retval = TRUE;
    result = INVALID_SIZE_T;
#if 0
    if ('\0' == *in) {
        *out_len_p = 0;
        *out = mem_new_n(**out, *out_len_p + 1);
        **out = '\0';
        return retval;
    }
#else
    if (0 == strcmp(from, to) || '\0' == *in) {
        *out = (char *) in;
        *out_len_p = in_len;
        return retval;
    }
#endif

    errno = 0;
    if (INVALID_ICONV_T == (cd = iconv_open(to, from))) {
        if (EINVAL == errno) {
            ICONV_ERR_WRONG_CHARSET(error, to, from);
            return FALSE;
        } else {
            ICONV_ERR_CONVERTER(error);
            return FALSE;
        }
    }
#ifdef HAVE_ICONVCTL
    {
        int one;

        one = 1;
        iconvctl(cd, ICONV_SET_ILSEQ_INVALID, &one);
    }
#endif /* HAVE_ICONVCTL */
    out_size = 1; /* out_size: capacity allocated to *out */
    *out = mem_new_n(**out, out_size + 1); /* + 1 for \0 */
    in_p = (char *) in;
    out_p = *out;
    in_left = in_len;
    out_left = out_size; /* out_left: free space left in *out */
    while (in_left > 0) {
// printf("iconv(%d, %d)\n", out_size, out_left);
        result = iconv(cd, (ICONV_CONST char **) &in_p, &in_left, &out_p, &out_left);
        *out_len_p = out_p - *out;
        out_left = out_size - *out_len_p;
        if (INVALID_SIZE_T == result) {
            if (E2BIG == errno && in_left > 0) {
                char *tmp;

                tmp = mem_renew(*out, **out, ++out_size + 1); /* *out is no longer valid */
                *out = tmp;
                out_p = *out + *out_len_p;
                out_left = out_size - *out_len_p;
                continue;
            }
        }
        break;
    }
    if (INVALID_SIZE_T != result) {
        while (1) {
            result = iconv(cd, NULL, NULL, &out_p, &out_left);
            *out_len_p = out_p - *out;
            out_left = out_size - *out_len_p;
            if (INVALID_SIZE_T != result) {
                break;
            }
            if (E2BIG == errno) {
                char *tmp;

                tmp = mem_renew(*out, **out, ++out_size + 1); /* *out is no longer valid */
                *out = tmp;
                out_p = *out + *out_len_p;
                out_left = out_size - *out_len_p;
            } else {
                break;
            }
        }
    }
    iconv_close(cd);
    if (INVALID_SIZE_T == result) {
        retval = FALSE;
        free(*out);
        *out = NULL;
        switch (errno) {
            case EINVAL:
                ICONV_ERR_ILLEGAL_CHAR(error);
                break;
            case EILSEQ:
                ICONV_ERR_ILLEGAL_SEQ(error);
                break;
            case E2BIG:
                ICONV_ERR_TOO_BIG(error);
                break;
            default:
                ICONV_ERR_UNKNOWN(error, errno);
        }
    } else {
        *out_p = '\0';
        *out_len_p = out_p - *out;
    }

    return retval;
}

void convert_string_free(const char *original_utf8_string, char **maybe_converted_string)
{
    assert(NULL != maybe_converted_string);

    if (NULL != *maybe_converted_string && original_utf8_string != *maybe_converted_string) {
        free((void *) *maybe_converted_string);
        *maybe_converted_string = NULL;
    }
}

void convert_array_free(int argc, char **original_argv, char **maybe_converted_argv)
{
    int i;

    if (maybe_converted_argv != original_argv) {
        for (i = 0; i < argc; i++) {
            if (NULL != maybe_converted_argv[i] && original_argv[i] != maybe_converted_argv[i]) {
                free(maybe_converted_argv[i]);
            }
        }
        free(maybe_converted_argv);
    }
}

bool convert_string_local_to_utf8(const char *src, size_t src_len, char **dst, size_t *dst_len, error_t **error)
{
    return iconv_string(input_encoding, "UTF-8", src, src_len, dst, dst_len, error);
}

bool convert_array_local_to_utf8(int argc, char **in_argv, char ***out_argv, error_t **error)
{
    int i;
    bool ok;

    ok = TRUE;
    if (0 == strcmp(input_encoding, "UTF-8")) {
        *out_argv = in_argv;
    } else {
        *out_argv = mem_new_n(**out_argv, argc);
        for (i = 0; i < argc; i++) {
            (*out_argv)[i] = NULL;
        }
        for (i = 0; ok && i < argc; i++) {
            ok &= iconv_string(input_encoding, "UTF-8", in_argv[i], strlen(in_argv[i]), &(*out_argv)[i], NULL, error);
        }
    }

    return ok;
}

bool convert_string_utf8_to_local(const char *src, size_t src_len, char **dst, size_t *dst_len, error_t **error)
{
    return iconv_string("UTF-8", output_encoding, src, src_len, dst, dst_len, error);
}

DECLARE_MODULE(conv) = {
    "conv",
    NULL,
    convert_ctor,
    NULL,
    NULL
};
