#ifndef GRAPH_H

# define GRAPH_H

# include <stddef.h>
# include "model.h"

typedef struct completer_t completer_t;

typedef struct argument_t argument_t;
typedef argument_t graph_node_t;
typedef struct graph_t graph_t;

typedef command_status_t (*handle_t)(COMMAND_ARGS);
typedef bool (*complete_t)(void *, const char *, size_t, completer_t *, void *);

argument_t *argument_create_uint(size_t, const char *);
argument_t *argument_create_literal(const char *, handle_t, const char *);
argument_t *argument_create_relevant_literal(size_t, const char *, handle_t);
argument_t *argument_create_choices(size_t, const char *, const char * const *);
argument_t *argument_create_string(size_t, const char *, complete_t, void *);

void graph_create_full_path(graph_t *, ...) SENTINEL;
void graph_create_path(graph_t *, graph_node_t *, graph_node_t *, ...) SENTINEL;
void graph_create_all_path(graph_t *, graph_node_t *, graph_node_t *, ...);
void graph_destroy(graph_t *);
void graph_display(graph_t *);

graph_t *graph_new(void);
command_status_t graph_dispatch_command(graph_t *, int, const char **, const main_options_t *, error_t **);

argument_t *argument_create_choices_off_on(size_t, handle_t);
argument_t *argument_create_choices_false_true(size_t, handle_t);
argument_t *argument_create_choices_disable_enable(size_t, handle_t);

void completer_push(completer_t *, const char *, bool);
void completer_push_modelized(completer_t *, modelized_t *);

bool complete_from_statement(void *, const char *, size_t, completer_t *, void *);
bool complete_from_hashtable_keys(void *, const char *, size_t, completer_t *, void *);

char *graph_bash(graph_t *);

# ifdef TEST
void plug_update_subcommands(graph_t *, graph_node_t *, const model_t *, handle_t, const char *);
# endif /* TEST */

#endif /* !GRAPH_H */
