#ifndef COMMON_H

# define COMMON_H

# ifdef __GNUC__
#  define GCC_VERSION (__GNUC__ * 1000 + __GNUC_MINOR__)
# else
#  define GCC_VERSION 0
# endif /* __GNUC__ */

# ifndef __has_attribute
#  define __has_attribute(x) 0
# endif /* !__has_attribute */

# if __GNUC__ || __has_attribute(unused)
#  define UNUSED(x) UNUSED_ ## x __attribute__((unused))
# else
#  define UNUSED
# endif /* UNUSED */

# if GCC_VERSION >= 2003 || __has_attribute(format)
#  define FORMAT(archetype, string_index, first_to_check) __attribute__((format(archetype, string_index, first_to_check)))
#  define PRINTF(string_index, first_to_check) FORMAT(__printf__, string_index, first_to_check)
# else
#  define FORMAT(archetype, string_index, first_to_check)
#  define PRINTF(string_index, first_to_check)
# endif /* FORMAT,PRINTF */

# if GCC_VERSION >= 3004 || __has_attribute(warn_unused_result)
#  define WARN_UNUSED_RESULT __attribute__((warn_unused_result))
# else
#  define WARN_UNUSED_RESULT
# endif /* WARN_UNUSED_RESULT */

#ifdef TEST
# define APPLICATION_KEY "7kbG7Bk7S9Nt7ZSV"
# define URL "http://localhost:3000"
# define APPLICATION_SECRET "EXEgWIz07P0HYwtQDs7cNIqCiQaWSuHF"
# define API_BASE_URL "https://eu.api.ovh.com/1.0"
#else
# include "config.h"
#endif

# define DIRECTORY_SEPARATOR '/'
# define OVH_SHELL_CONFIG_FILE ".ovh"

# define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
# define STR_LEN(str)      (ARRAY_SIZE(str) - 1)
# define STR_SIZE(str)     (ARRAY_SIZE(str))

# define mem_new(type)           malloc((sizeof(type)))
# define mem_new_n(type, n)      malloc((sizeof(type) * (n)))
# define mem_renew(ptr, type, n) realloc((ptr), (sizeof(type) * (n)))

# include <stdio.h>
# include <stdlib.h>
# include <stdint.h>
# include <assert.h>
# include <stdarg.h>
# ifdef DEBUG
#  define debug(fmt, ...) \
    fprintf(stderr, fmt "\n", ## __VA_ARGS__)
# else
#  define debug(fmt, ...) \
    /* NOP */
# endif /* DEBUG */

# ifndef MAX
#  define MAX(a, b) ((a) > (b) ? (a) : (b))
# endif /* !MAX */

# ifndef MIN
#  define MIN(a, b) ((a) < (b) ? (a) : (b))
# endif /* !MIN */

# define HAS_FLAG(value, flag) \
    (0 != ((value) & (flag)))

# define SET_FLAG(value, flag) \
    ((value) |= (flag))

# define UNSET_FLAG(value, flag) \
    ((value) &= ~(flag))

# ifdef __bool_true_false_are_defined
#  include <stdbool.h>
#  define TRUE true
#  define FALSE false
# else
#  define FALSE 0
#  define TRUE 1
typedef int bool;
#  if 0
typedef enum {
    FALSE = 0, /* common.h:36:5: erreur: expected identifier before numeric constant ?!? */
    TRUE  = 1
} bool;
#  endif
# endif /* C99 boolean */

typedef int (*CmpFunc)(const void *, const void *);
typedef bool (*EqualFunc)(const void *, const void *);
typedef void (*DtorFunc)(void *);
typedef void *(*DupFunc)(const void *);
typedef int (*ForeachFunc)();

#define DECLARE_MODULE(foo) \
    module_t foo##_module

#define ARG_MODULE_NAME ((const char *) 1)
#define ARG_ANY_VALUE   ((const char *) 2)
#define ARG_ON_OFF      ((const char *) 3)

/*typedef enum {
    COMMAND_EXIT_SUCCESS,
    COMMAND_EXIT_FAILURE,
    COMMAND_EXIT_USAGE
} command_status_t;*/

typedef struct {
    const char *first_word;
    int argc;
    int (*handle)(int argc, const char **argv);
     const char * const *args;
} command_t;

typedef struct {
    const char *name;
    bool (*ctor)(void);
    void (*dtor)(void);
    const command_t *commands;
} module_t;

#endif /* COMMON_H */
