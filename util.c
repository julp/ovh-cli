#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#ifdef BSD
# include <libgen.h>
#endif /* BSD */
#include "util.h"

#include <limits.h>
#if !defined(MAXPATHLEN) && defined(PATH_MAX)
# define MAXPATHLEN PATH_MAX
#endif /* !MAXPATHLEN && PATH_MAX */

#define DEFAULT_WIDTH  80
#define DEFAULT_HEIGHT 24
#define DEFAULT_EDITOR "vi"

bool confirm(const main_options_t *mainopts, const char *prompt, ...)
{
    va_list ap;

    if (mainopts->noconfirm) {
        return mainopts->yes;
    } else {
        va_start(ap, prompt);
        vprintf(prompt, ap);
        va_end(ap);
        fputs(" (y/N)> ", stdout);
        if (mainopts->yes) {
            fputs("y\n", stdout);
            return TRUE;
        }
        fflush(stdout);

        return 'y' == /*tolower*/(getchar());
    }
}

int console_width(void)
{
    int columns;

    if (isatty(STDOUT_FILENO)) {
        const char *v;
        struct winsize w;

        if (NULL != (v = getenv("COLUMNS")) && 0 != (columns = atoi(v))) {
            // OK
        } else if (-1 != ioctl(STDOUT_FILENO, TIOCGWINSZ, &w)) {
            columns = w.ws_col;
        } else {
            columns = DEFAULT_WIDTH;
        }
    } else {
        columns = -1; // unlimited
    }

    return columns;
}

int console_height(void)
{
    int lines;

    if (isatty(STDOUT_FILENO)) {
        const char *v;
        struct winsize w;

        if (NULL != (v = getenv("LINES")) && 0 != (lines = atoi(v))) {
            // OK
        } else if (-1 != ioctl(STDOUT_FILENO, TIOCGWINSZ, &w)) {
            lines = w.ws_row;
        } else {
            lines = DEFAULT_HEIGHT;
        }
    } else {
        lines = -1; // unlimited
    }

    return lines;
}

int launch_editor(char **dest, const char *hint, error_t **error)
{
    pid_t pid;
    ssize_t count;
    int fd, status;
    struct stat st;
    size_t dest_len;
    const char *editor;
    char tempname[MAXPATHLEN];
    sigset_t oldsigset, nsigset;
    struct sigaction sa, sa_int, sa_quit;

    assert(NULL != dest);
    if (snprintf(tempname, ARRAY_SIZE(tempname), "%s.%d.XXXXXX", "ovh-cli", getpid()) >= (int) STR_SIZE(tempname)) {
        error_set(error, NOTICE, _("error or buffer overflow"));
        return -1;
    }
    if (-1 == (fd = mkostemp(tempname, O_SYNC))) {
        error_set(error, NOTICE, _("can't open temporary file %s"), tempname);
        return -1;
    }
    if (NULL != hint && '\0' != *hint) {
        if (-1 == write(fd, hint, strlen(hint))) {
            error_set(error, NOTICE, _("can't write to temporary file %s"), tempname);
            return -1;
        }
        lseek(fd, 0, SEEK_SET);
    }
    if (NULL == (editor = getenv("EDITOR"))) {
        editor = DEFAULT_EDITOR;
    }
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, &sa_int);
    sigaction(SIGQUIT, &sa, &sa_quit);
    sigemptyset(&nsigset);
    sigaddset(&nsigset, SIGCHLD);
    sigprocmask(SIG_BLOCK, &nsigset, &oldsigset);
    pid = fork();
    if (-1 == pid) {
        error_set(error, NOTICE, _("system call %s failed: %s"), "fork", strerror(errno));
        return -1;
    } else if (0 == pid) {
        sigaction(SIGINT, &sa_int, NULL);
        sigaction(SIGQUIT, &sa_quit, NULL);
        sigprocmask(SIG_SETMASK, &oldsigset, NULL);
        execlp(editor, basename(editor), tempname, NULL);
        exit(EXIT_FAILURE);
    }
    while (1) {
        if (-1 == waitpid(pid, &status, WUNTRACED)) {
            if (EINTR == errno) {
                continue;
            }
            unlink(tempname);
            break;
        } else if (WIFSTOPPED(status)) {
            raise(WSTOPSIG(status));
        } else if (WIFEXITED(status) && EXIT_SUCCESS == WEXITSTATUS(status)) {
            break;
        } else {
            unlink(tempname);
            break;
        }
    }
    sigaction(SIGINT, &sa_int, NULL);
    sigaction(SIGQUIT, &sa_quit, NULL);
    sigprocmask(SIG_SETMASK, &oldsigset, NULL);
    if (-1 == stat(tempname, &st)) {
        error_set(error, NOTICE, _("system call %s failed: %s"), "stat", strerror(errno));
        return -1;
    }
    dest_len = (size_t) st.st_size;
    *dest = mem_new_n(*dest, dest_len + 1);
    if (-1 == (count = read(fd, *dest, dest_len)) || ((size_t) count) != dest_len) {
        free(*dest);
        error_set(error, NOTICE, _("can't read temporary file %s"), tempname);
        return -1;
    }
    close(fd);
    unlink(tempname);
    (*dest)[dest_len] = '\0';
    if (dest_len > 0 && '\n' == (*dest)[dest_len - 1]) {
        (*dest)[--dest_len] = '\0';
        if (dest_len > 0 && '\r' == (*dest)[dest_len - 1]) {
            (*dest)[--dest_len] = '\0';
        }
    }
    if (0 == count || 0 == strcmp(*dest, hint)) {
        free(*dest);
        error_set(error, NOTICE, _("abort, message is empty"));
        return -1;
    }

    return dest_len;
}
