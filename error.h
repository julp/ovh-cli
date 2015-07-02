#ifndef ERROR_H

# define ERROR_H

enum {
    INFO,
    NOTICE,
    WARN,
    FATAL
};

# include "config.h"

#ifdef DEBUG
const char *ubasename(const char *);

# define debug(format, ...) \
    do { \
        error_t *e; \
 \
        e = NULL; \
        error_set(&e, INFO, format, ## __VA_ARGS__); \
        print_error(e); \
    } while (0);

# define UGREP_FILE_LINE_FUNC_D \
    const char *__ugrep_file, const unsigned int __ugrep_line, const char *__ugrep_func

# define UGREP_FILE_LINE_FUNC_DC \
    UGREP_FILE_LINE_FUNC_D,

# define UGREP_FILE_LINE_FUNC_C \
    __FILE__, __LINE__, __func__

# define UGREP_FILE_LINE_FUNC_CC \
    UGREP_FILE_LINE_FUNC_C,

# define UGREP_FILE_LINE_FUNC_RELAY_C \
    __ugrep_file, __ugrep_line, __ugrep_func

# define UGREP_FILE_LINE_FUNC_RELAY_CC \
    UGREP_FILE_LINE_FUNC_RELAY_C,

#else

# define msg(type, format, ...) \
    report(type, format "\n", ## __VA_ARGS__)

# define debug(format, ...)            /* NOP */
# define UGREP_FILE_LINE_FUNC_D        /* NOP */
# define UGREP_FILE_LINE_FUNC_DC       /* NOP */
# define UGREP_FILE_LINE_FUNC_C        /* NOP */
# define UGREP_FILE_LINE_FUNC_CC       /* NOP */
# define UGREP_FILE_LINE_FUNC_RELAY_C  /* NOP */
# define UGREP_FILE_LINE_FUNC_RELAY_CC /* NOP */
#endif /* DEBUG */

typedef struct {
    int type;
    char *message;
} error_t;

#define error_set(/*error_t ** */ error, /*int*/ type, /*const char * */ format, ...) \
    _error_set(UGREP_FILE_LINE_FUNC_CC error, type, format, ## __VA_ARGS__)

void error_destroy(error_t *);
void error_propagate(error_t **, error_t *);

error_t *error_new(UGREP_FILE_LINE_FUNC_DC int, const char *, ...) WARN_UNUSED_RESULT;
error_t *error_vnew(UGREP_FILE_LINE_FUNC_DC int, const char *, va_list) WARN_UNUSED_RESULT;
void _error_set(UGREP_FILE_LINE_FUNC_DC error_t **, int, const char *, ...);

void print_error(error_t *);

#endif /* !ERROR_H */
