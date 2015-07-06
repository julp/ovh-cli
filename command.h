#ifndef COMMAND_H

# define COMMAND_H

typedef enum {
    COMMAND_SUCCESS,
    COMMAND_FAILURE,
    COMMAND_USAGE
} command_status_t;

# define RELAY_COMMAND_ARGS arg, mainopts, error
# define COMMAND_ARGS void *arg, const main_options_t *mainopts, error_t **error

# include "common.h"
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
    bool (*early_init)(void);
    bool (*late_init)(error_t **);
    void (*dtor)(void);
} module_t;

# include <sqlite3.h>
sqlite3 *db;

#endif /* !COMMAND_H */
