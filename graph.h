#ifndef GRAPH_H

# define GRAPH_H

#include "struct/dptrarray.h"

typedef struct argument_t argument_t;
typedef argument_t graph_node_t;
typedef struct graph_t graph_t;

typedef command_status_t (*handle_t)(void *, error_t **);
typedef bool (*complete_t)(void *, const char *, size_t, DPtrArray *, void *);

#define graph_create_path(/*graph_t * */ g, /*graph_node_t * */ start, /*graph_node_t * */ end, ...) \
    _graph_create_path(UGREP_FILE_LINE_FUNC_CC g, start, end, ## __VA_ARGS__)
#define graph_create_full_path(/*graph_t * */ g, /*graph_node_t * */ start, ...) \
    _graph_create_full_path(UGREP_FILE_LINE_FUNC_CC g, start, ## __VA_ARGS__)

argument_t *argument_create_literal(const char *, handle_t);
argument_t *argument_create_relevant_literal(size_t, const char *, handle_t);
argument_t *argument_create_choices(size_t, const char *, const char * const *);
argument_t *argument_create_string(size_t, const char *, complete_t, void *);
bool complete_from_hashtable_keys(void *, const char *, size_t, DPtrArray *, void *);
// void graph_create_all_path(graph_node_t *, graph_node_t *, ...) SENTINEL;
void _graph_create_path(UGREP_FILE_LINE_FUNC_DC graph_t *, graph_node_t *, graph_node_t *, ...) SENTINEL;
void _graph_create_full_path(UGREP_FILE_LINE_FUNC_DC graph_t *, graph_node_t *, ...) SENTINEL;
void graph_destroy(graph_t *);
void graph_display(graph_t *);
graph_t *graph_new(void);
command_status_t graph_run_command(graph_t *, int, const char **, error_t **);

argument_t *argument_create_choices_off_on(size_t, handle_t);
argument_t *argument_create_choices_disable_enable(size_t, handle_t);

#endif /* !GRAPH_H */
