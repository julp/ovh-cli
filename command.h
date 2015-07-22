#ifndef COMMAND_H

# define COMMAND_H

# include "common.h"

enum {
    COMMAND_SUCCESS = 0,
    COMMAND_FAILURE = 1,
    COMMAND_USAGE   = 2
};

# define CMD_FLAG_NO_DATA      0x0100
# define CMD_FLAG_SKIP_HISTORY 0x0200
# define CMD_FLAGS             0xFF00

# define COMMAND_CODE(x) \
    ((x) & ~CMD_FLAGS)

typedef uint32_t command_status_t;

# define RELAY_COMMAND_ARGS arg, mainopts, error
# define COMMAND_ARGS void *arg, const main_options_t *mainopts, error_t **error

# include "graph.h"
# include "json.h"

# define DECLARE_MODULE(foo) \
    module_t foo##_module

# define JSON_ADD_RULE(parent, method, value) \
    do { \
        json_value_t object; \
 \
        object = json_object(); \
        json_object_set_property(object, "method", json_string(method)); \
        json_object_set_property(object, "path", json_string(value)); \
        json_array_push(parent, object); \
    } while (0);

typedef struct {
    const char *name;
    void (*register_commands)(graph_t *);
    void (*register_rules)(json_value_t, bool);
    bool (*early_init)(error_t **);
    bool (*late_init)(error_t **);
    void (*dtor)(void);
} module_t;

# include <sqlite3.h>

#endif /* !COMMAND_H */
