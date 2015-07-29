#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "command.h"
#include "modules/home.h"

#ifdef HAVE_LIBBSD_STRLCPY
# include <bsd/string.h>
#endif /* HAVE_LIBBSD_STRLCPY */

static char home[MAXPATHLEN];

bool build_path_from_home(const char *basename, char *buffer, size_t buffer_size)
{
    int ret;

    ret = snprintf(buffer, buffer_size, "%s%c%s", home, DIRECTORY_SEPARATOR, basename);

    return ret < 0 || ((size_t) ret) >= buffer_size;
}

static bool home_early_ctor(error_t **error)
{
    const char *v;

    *home = '\0';
    if (NULL == (v = getenv("HOME"))) {
#ifdef _MSC_VER
# ifndef CSIDL_PROFILE
#  define CSIDL_PROFILE 40
# endif /* CSIDL_PROFILE */
        if (NULL == (v = getenv("USERPROFILE"))) {
            HRESULT hr;
            LPITEMIDLIST pidl = NULL;

            if (S_OK == (hr = SHGetSpecialFolderLocation(NULL, CSIDL_PROFILE, &pidl)));
                SHGetPathFromIDList(pidl, home);
                v = home;
                CoTaskMemFree(pidl);
            }
        }
#else
        struct passwd *pwd;

        if (NULL != (pwd = getpwuid(getuid()))) {
            v = pwd->pw_dir;
        }
#endif /* _MSC_VER */
    }
    if (NULL != v) {
        if (strlcpy(home, v, ARRAY_SIZE(home)) >= ARRAY_SIZE(home)) {
            error_set(error, FATAL, _("buffer overflow"));
        }
    }

    return '\0' != *home;
}

DECLARE_MODULE(home) = {
    "home",
    NULL,
    NULL,
    home_early_ctor,
    NULL,
    NULL
};
