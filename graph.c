#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <histedit.h>

#include "common.h"
#include "struct/hashtable.h"

typedef enum {
    ARG_TYPE_END, // dummy
    ARG_TYPE_LITERAL, // "list", "add", ...
    ARG_TYPE_NUMBER,
    ARG_TYPE_CHOICES, // comme le type de record dns
    ARG_TYPE_STRING // free string (eventually with help of completion)
} argument_type_t;

// méthode toString pour intégrer un élément quelconque (record_t *, server_t *, ...) à la ligne de commande (ie le résultat de la complétion) ?
// méthode toString pour représenter un élément quelconque (record_t *, server_t *, ...) parmi les propositions à faire à l'utilisateur ?
struct argument_t {
    size_t offset; // offsetof
    argument_type_t type;
    complete_t complete; // spécifique à ARG_TYPE_STRING et ARG_TYPE_CHOICES
    const char *string;
    handle_t handle;
    DPtrArray *children;
    void *data; // TODO: removal in favor of *_data
    void *completion_data; // data for completion
    void *command_data; // data to run command
//     graph_t *owner;
};

struct graph_t {
    HashTable *roots;
    HashTable *nodes;
    graph_node_t *end;
};

#define CREATE_ARG(node, arg_type, string_or_hint) \
    do { \
        node = mem_new(*node); \
        node->data = NULL; \
        node->handle = NULL; \
        node->type = arg_type; \
        node->complete = NULL; \
        node->offset = (size_t) -1; \
        node->string = string_or_hint; \
        node->children = NULL; \
    } while (0);

static void graph_node_destroy(void *data)
{
    graph_node_t *node;

    node = (graph_node_t *) data;
    if (NULL != node->children) {
        dptrarray_destroy(node->children);
    }
    free(node);
}

graph_t *graph_new(void)
{
    graph_t *g;

    g = mem_new(*g);
    CREATE_ARG(g->end, ARG_TYPE_END, "(END)");
    g->roots = hashtable_ascii_cs_new(NULL, NULL, graph_node_destroy);
    g->nodes = hashtable_new(value_hash, value_equal, NULL, NULL, graph_node_destroy);

    return g;
}

static bool complete_literal(void *UNUSED(parsed_arguments), const char *current_argument, size_t current_argument_len, DPtrArray *possibilities, void *data)
{
    if (0 == strncmp(current_argument, (const char *) data, current_argument_len)) {
        dptrarray_push(possibilities, (void *) data);
    }

    return TRUE;
}

argument_t *argument_create_literal(const char *string, handle_t handle)
{
    argument_t *node;

    CREATE_ARG(node, ARG_TYPE_LITERAL, string);
    node->handle = handle;
    node->data = (void *) string;
    node->complete = complete_literal;

    return node;
}

argument_t *argument_create_relevant_literal(size_t offset, const char *string, handle_t handle)
{
    argument_t *node;

    node = argument_create_literal(string, handle);
    node->offset = offset;

    return node;
}

static bool complete_choices(void *UNUSED(parsed_arguments), const char *current_argument, size_t current_argument_len, DPtrArray *possibilities, void *data)
{
    const char * const *v;

    for (v = (const char * const *) data; NULL != *v; v++) {
        if (0 == strncmp(current_argument, *v, current_argument_len)) {
            dptrarray_push(possibilities, (void *) *v);
        }
    }

    return TRUE;
}

argument_t *argument_create_choices(size_t offset, const char *hint, const char * const * values)
{
    argument_t *node;

    CREATE_ARG(node, ARG_TYPE_CHOICES, hint);
    node->offset = offset;
    node->complete = complete_choices;
    node->data = (void *) values;

    return node;
}

static const char * const off_on[] = {
    "off",
    "on",
    NULL
};

argument_t *argument_create_choices_off_on(size_t offset, handle_t handle)
{
    argument_t *node;

    CREATE_ARG(node, ARG_TYPE_CHOICES, "<on/off>");
    node->handle = handle;
    node->offset = offset;
    node->complete = complete_choices;
    node->data = (void *) off_on;

    return node;
}

static const char * const disable_enable[] = {
    "disable",
    "enable",
    NULL
};

argument_t *argument_create_choices_disable_enable(size_t offset, handle_t handle)
{
    argument_t *node;

    CREATE_ARG(node, ARG_TYPE_CHOICES, "<enable/disable>");
    node->handle = handle;
    node->offset = offset;
    node->complete = complete_choices;
    node->data = (void *) disable_enable;

    return node;
}

argument_t *argument_create_string(size_t offset, const char *hint, complete_t complete, void *data)
{
    argument_t *node;

    CREATE_ARG(node, ARG_TYPE_STRING, hint);
    node->offset = offset;
    node->complete = complete;
    node->data = data;

    return node;
}

void graph_destroy(graph_t *g)
{
    assert(NULL != g);

    hashtable_destroy(g->roots);
    hashtable_destroy(g->nodes);
    free(g);
}

static int graph_node_compare(graph_node_t *a, graph_node_t *b)
{
    if (a == b) {
        return 0;
    } else if (NULL == a || NULL == b) {
        return (NULL != a) - (NULL != b);
    } else {
        int diff;

        if (0 == (diff = a->type - b->type)) {
            return strcmp(a->string, b->string);
        } else {
            return diff;
        }
    }
}

static void graph_node_insert_child(/*UGREP_FILE_LINE_FUNC_DC */graph_t *g, graph_node_t *parent, graph_node_t *child)
{
    size_t i, l;
    graph_node_t *current;

    current = NULL;
    if (NULL == parent->children) {
        parent->children = dptrarray_new(NULL, NULL, NULL);
        hashtable_put_ex(g->nodes, HT_PUT_ON_DUP_KEY_PRESERVE, (void *) child, child, NULL);
        dptrarray_push(parent->children, child);
    } else {
        for (i = 0, l = dptrarray_length(parent->children); i < l; i++) {
            current = dptrarray_at_unsafe(parent->children, i, graph_node_t);
            if (!(graph_node_compare(child, current) > 0)) {
                break;
            }
        }
        if (0 == graph_node_compare(child, current)) {
//             if (child->type == ARG_TYPE_END) {
//                 free(child);
//             }
            return;
        }
        hashtable_put_ex(g->nodes, HT_PUT_ON_DUP_KEY_PRESERVE, (void *) child, child, NULL);
        if (NULL == current) {
            dptrarray_push(parent->children, child);
        } else {
            dptrarray_insert(parent->children, i, child);
        }
    }
}

// créer un chemin complet
void /*_*/graph_create_full_path(/*UGREP_FILE_LINE_FUNC_DC */graph_t *g, graph_node_t *start, ...) /* SENTINEL */
{
    va_list nodes;
    graph_node_t /**end, */*node, *parent;

    assert(NULL != g);
    assert(NULL != start);
    assert(ARG_TYPE_LITERAL == start->type);

    parent = start;
    hashtable_put_ex(g->roots, HT_PUT_ON_DUP_KEY_PRESERVE, (void *) start->string, start, NULL);
    va_start(nodes, start);
    while (NULL != (node = va_arg(nodes, graph_node_t *))) {
        graph_node_insert_child(/*UGREP_FILE_LINE_FUNC_RELAY_CC */g, parent, node);
        parent = node;
    }
    va_end(nodes);
//     CREATE_ARG(end, ARG_TYPE_END, "(END)");
    graph_node_insert_child(/*UGREP_FILE_LINE_FUNC_RELAY_CC */g, parent, g->end);
}

// créer un chemin entre 2 sommets (end peut être NULL)
// vu que le point de départ doit exister, ça ne peut être une "racine"
void /*_*/graph_create_path(/*UGREP_FILE_LINE_FUNC_DC */graph_t *g, graph_node_t *start, graph_node_t *end, ...) /* SENTINEL */
{
    va_list nodes;
    graph_node_t *node, *parent;

    assert(NULL != g);
    assert(NULL != start);

    parent = start;
    va_start(nodes, end);
    while (NULL != (node = va_arg(nodes, graph_node_t *))) {
        graph_node_insert_child(/*UGREP_FILE_LINE_FUNC_RELAY_CC */g, parent, node);
        parent = node;
    }
    va_end(nodes);
    if (NULL == end) {
//         CREATE_ARG(end, ARG_TYPE_END, "(END)");
        graph_node_insert_child(/*UGREP_FILE_LINE_FUNC_RELAY_CC */g, parent, g->end);
    } else {
        graph_node_insert_child(/*UGREP_FILE_LINE_FUNC_RELAY_CC */g, parent, end);
    }
//     graph_node_insert_child(/*UGREP_FILE_LINE_FUNC_RELAY_CC */g, parent, end);
}

/*size_t fact(size_t n)
{
    size_t f;

    f = 1;
    while(0 != n) {
        f *= n--;
    }

    return f;
}*/

#define MAX_ALTERNATE_PATHS 12
// créer tous les chemins/permutations possibles entre start et end (1 - 2 - 3, 1 - 3 - 2, 2 - 1 - 3, 2 - 3 - 1, 3 - 1 - 2, 3 - 2 - 1)
void /*_*/graph_create_all_path(/*UGREP_FILE_LINE_FUNC_DC */graph_t *g, graph_node_t *start, graph_node_t *end, ...)
{
    va_list ap;
    size_t i, group_count, subpaths_count;
    graph_node_t *real_end, *parent, *node, *starts[MAX_ALTERNATE_PATHS], *ends[MAX_ALTERNATE_PATHS];

    assert(NULL != start);

    real_end = end;
    va_start(ap, end);
    subpaths_count = 0;
    while (0 != (group_count = va_arg(ap, int))) {
        assert(subpaths_count < MAX_ALTERNATE_PATHS);
        starts[subpaths_count] = node = va_arg(ap, graph_node_t *);
        assert(NULL != node);
        graph_node_insert_child(/*UGREP_FILE_LINE_FUNC_RELAY_CC */g, start, node);
        parent = node;
        // link nodes of subgroup between them if not yet done
        for (i = 2; i <= group_count; i++) {
            node = va_arg(ap, graph_node_t *);
            assert(NULL != node);
            graph_node_insert_child(/*UGREP_FILE_LINE_FUNC_RELAY_CC */g, parent, node);
            parent = node;
        }
        ends[subpaths_count] = parent;
        if (NULL == end) {
//             CREATE_ARG(real_end, ARG_TYPE_END, "(END)");
            real_end = g->end;
        }
        graph_node_insert_child(/*UGREP_FILE_LINE_FUNC_RELAY_CC */g, parent, real_end);
        ++subpaths_count;
    }
    va_end(ap);
#if 0
    if (subpaths_count > 0) {
        graph_node_t *tmp;
        size_t j, k, a[MAX_ALTERNATE_PATHS], b[MAX_ALTERNATE_PATHS], c[MAX_ALTERNATE_PATHS + 1];

        bzero(c, sizeof(c));
        for (i = 0; i < MAX_ALTERNATE_PATHS; i++) {
            a[i] = b[i] = i;
        }
        while (TRUE) {
            parent = start;
            for (i = 0; i < subpaths_count; i++) {
                printf("%s ", nodes[i]->string);
                graph_node_insert_child(/*UGREP_FILE_LINE_FUNC_RELAY_CC */g, parent, nodes[i]);
            }
            printf("\n");
            k = 1;
            while (c[k] == k) {
                c[k] = 0;
                k = k + 1;
            }
            if (k == subpaths_count) {
                break;
            } else {
                c[k] = c[k] + 1;
            }
            tmp = nodes[a[0]];
            nodes[a[0]] = nodes[a[b[k]]];
            nodes[a[b[k]]] = tmp;
            j = 1;
            k = k - 1;
            while (j < k) {
                j = j + 1;
                k = k - 1;
            }
        }
    }
#else
    for (i = 0; i < subpaths_count; i++) {
        size_t j;

        for (j = 0; j < subpaths_count; j++) {
            if (j != i) {
                graph_node_insert_child(/*UGREP_FILE_LINE_FUNC_RELAY_CC */g, ends[i], starts[j]);
            }
        }
    }
#endif
}

static void traverse_graph_node_ex(graph_node_t *node, HashTable *visited, int depth, bool indent)
{
    size_t i, l;
    bool has_end;
    size_t children_count;
    graph_node_t *current;

    if (ARG_TYPE_LITERAL == node->type && !hashtable_put_ex(visited, HT_PUT_ON_DUP_KEY_PRESERVE, node, NULL, NULL)) {
        return;
    }
    has_end = FALSE;
    children_count = 0;
    for (i = 0, l = dptrarray_length(node->children); i < l; i++) {
        current = dptrarray_at_unsafe(node->children, i, graph_node_t);
        ++children_count;
        has_end |= ARG_TYPE_END == current->type;
    }
    if (indent) {
        printf("%*c", depth * 4, ' ');
    }
    putchar(' ');
    fputs(node->string, stdout);
    if (children_count > 1 || has_end) {
        putchar('\n');
    }
    for (i = 0/*, l = dptrarray_length(node->children)*/; i < l; i++) {
        current = dptrarray_at_unsafe(node->children, i, graph_node_t);
        if (ARG_TYPE_END != current->type) {
            traverse_graph_node_ex(current, visited, 1 == children_count ? depth : depth + 1, 1 != children_count);
        }
    }
}

static void traverse_graph_node(graph_node_t *node, int depth, bool indent)
{
    size_t i, l;
    bool has_end;
    size_t children_count;
    graph_node_t *current;

    has_end = FALSE;
    children_count = 0;
    for (i = 0, l = dptrarray_length(node->children); i < l; i++) {
        current = dptrarray_at_unsafe(node->children, i, graph_node_t);
        ++children_count;
        has_end |= ARG_TYPE_END == current->type;
    }
    if (indent) {
        printf("%*c", depth * 4, ' ');
    }
    putchar(' ');
    fputs(node->string, stdout);
    if (children_count > 1 || has_end) {
        putchar('\n');
    }
    for (i = 0/*, l = dptrarray_length(node->children)*/; i < l; i++) {
        current = dptrarray_at_unsafe(node->children, i, graph_node_t);
        if (ARG_TYPE_END != current->type) {
            traverse_graph_node(current, 1 == children_count ? depth : depth + 1, 1 != children_count);
        }
    }
}

/* <TODO: DRY (share this with main.c)> */
typedef struct {
    graph_t *graph;
    Tokenizer *tokenizer;
    DPtrArray *possibilities;
} editline_data_t;
/* </TODO: DRY> */

static int strcmpp(const void *p1, const void *p2, void *UNUSED(arg))
{
    return strcmp(*(char * const *) p1, *(char * const *) p2);
}

bool complete_from_hashtable_keys(void *UNUSED(parsed_arguments), const char *current_argument, size_t current_argument_len, DPtrArray *possibilities, void *data)
{
    Iterator it;

    hashtable_to_iterator(&it, (HashTable *) data);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        void *k;

        iterator_current(&it, &k);
        if (0 == strncmp(current_argument, (const char *) k, current_argument_len)) {
            dptrarray_push(possibilities, k);
        }
    }
    iterator_close(&it);

    return TRUE;
}

typedef struct {
    const char *name;
    bool (*matcher)(argument_t *, const char *); // exact match (use strcmp, not strncmp - this is not intended for completion)
} argument_description_t;

static bool agument_literal_match(argument_t *arg, const char *value)
{
    return 0 == strcmp(arg->string, value);
}

static bool argument_choices_match(argument_t *arg, const char *value)
{
    const char * const *v;

    for (v = (const char * const *) arg->data; NULL != *v; v++) {
        if (0 == strcmp(value, *v)) {
            return TRUE;
        }
    }

    return FALSE;
}

static bool argument_string_match(argument_t *UNUSED(arg), const char *UNUSED(value))
{
    return TRUE;
}

static bool argument_end_match(argument_t *UNUSED(arg), const char *UNUSED(value))
{
    return FALSE;
}

static argument_description_t descriptions[] = {
    [ ARG_TYPE_NUMBER ] = { "number", NULL },
    [ ARG_TYPE_END ] = { "", argument_end_match },
    [ ARG_TYPE_LITERAL ] = { "literal", agument_literal_match },
    [ ARG_TYPE_CHOICES ] = { "choices", argument_choices_match },
    [ ARG_TYPE_STRING ] = { "a free string", argument_string_match }
};

static graph_node_t *graph_node_find(graph_node_t *parent, const char *value)
{
    if (NULL != parent->children) {
        size_t i, l;

        for (i = 0, l = dptrarray_length(parent->children); i < l; i++) {
            graph_node_t *child;

            child = dptrarray_at_unsafe(parent->children, i, graph_node_t);
            if (descriptions[child->type].matcher(child, value)) {
                return child;
            }
        }
    }

    return NULL;
}

static bool graph_node_end_in_children(graph_node_t *node)
{
    if (NULL != node->children) {
        size_t i, l;

        for (i = 0, l = dptrarray_length(node->children); i < l; i++) {
            graph_node_t *child;

            child = dptrarray_at_unsafe(node->children, i, graph_node_t);
            if (ARG_TYPE_END == child->type) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

#include <math.h>
static size_t my_vsnprintf(void *args, char *dst, size_t dst_size, const char *fmt, va_list ap)
{
    char *w;
    va_list cpy;
    const char *r;
    size_t dst_len;

    w = dst;
    r = fmt;
    dst_len = 0;
    va_copy(cpy, ap);
    while ('\0' != *r) {
        if ('%' == *r) {
            ++r;
            switch (*r) {
                case '%':
                    ++dst_len;
                    if (dst_size > dst_len) {
                        *w++ = '%';
                    }
                    break;
                case 'S':
                {
                    const char *s;
                    size_t offset, s_len;

                    offset = va_arg(cpy, size_t);
                    s = *((const char **) (args + offset));
                    s_len = strlen(s);
                    dst_len += s_len;
                    if (dst_size > dst_len) {
                        memcpy(w, s, s_len);
                        w += s_len;
                    }
                    break;
                }
                case 's':
                {
                    size_t s_len;
                    const char *s;

                    s = va_arg(cpy, const char *);
                    s_len = strlen(s);
                    dst_len += s_len;
                    if (dst_size > dst_len) {
                        memcpy(w, s, s_len);
                        w += s_len;
                    }
                    break;
                }
                case 'U':
                {
                    // TODO
                    break;
                }
                case 'u':
                {
                    uint32_t num;
                    size_t num_len;

                    num = va_arg(cpy, uint32_t);
                    num_len = log10(num) + 1;
                    dst_len += num_len;
                    if (dst_size > dst_len) {
                        size_t i;

                        i = num_len;
                        do {
                            w[i--] = '0' + num % 10;
                            num /= 10;
                        } while (0 != num);
                        w += num_len;
                    }
                    break;
                }
            }
        } else {
            ++dst_len;
            if (dst_size > dst_len) {
                *w++ = *r;
            }
        }
        ++r;
    }
    va_end(cpy);
    if (dst_size > dst_len) {
        *w++ = '\0';
    }

    return dst_len;
}

static int string_array_to_index(argument_t *arg, const char *value)
{
    const char * const *v;
    const char * const *values;

    assert(ARG_TYPE_CHOICES == arg->type);
    values = (const char * const *) arg->data;
    for (v = values; NULL != *v; v++) {
        if (0 == strcmp(value, *v)) {
            return v - values;
        }
    }

    return -1;
}

// TODO:
// - separate completion (2 DPtrArray ?) to distinguish commands to values (eg: domain<tab> will propose "bar.tld", "foo.tld", "list", "xxx.tld")
// - indicate the command is complete? (eg: domain list<tab>)
unsigned char graph_complete(EditLine *el, int UNUSED(ch))
{
    const char **argv;
    const LineInfo *li;
    editline_data_t *client_data;
    int argc, res, cursorc, cursoro;

    res = CC_ERROR;
    li = el_line(el);
    if (0 != el_get(el, EL_CLIENTDATA, &client_data)) {
        return res;
    }
    tok_reset(client_data->tokenizer);
    dptrarray_clear(client_data->possibilities);
    if (-1 == tok_line(client_data->tokenizer, li, &argc, &argv, &cursorc, &cursoro)) { // TODO: handle cases tok_line returns value > 0
        return res;
    } else {
        argument_t *arg;
        char arguments[8192];

        bzero(arguments, ARRAY_SIZE(arguments));
        if (0 == cursorc) {
            Iterator it;

            hashtable_to_iterator(&it, client_data->graph->roots);
            for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
                argument_t *arg;

                arg = iterator_current(&it, NULL);
                assert(ARG_TYPE_LITERAL == arg->type);
                if (0 == strncmp(arg->string, argv[0], cursoro)) {
                    dptrarray_push(client_data->possibilities, (void *) arg->string);
                }
            }
            iterator_close(&it);
        } else {
            if (hashtable_get(client_data->graph->roots, argv[0], (void **) &arg)) {
                int depth;

                for (depth = 1; depth < cursorc && NULL != arg; depth++) {
                    if (NULL == (arg = graph_node_find(arg, NULL == argv[depth] ? "" : argv[depth]))) {
                        // too much parameters
                        break;
                    }
                    if (((size_t) -1) != arg->offset) {
                        if (ARG_TYPE_LITERAL == arg->type) {
                            *((bool *) (arguments + arg->offset)) = TRUE;
                        } else if (ARG_TYPE_CHOICES == arg->type) {
                            *((int *) (arguments + arg->offset)) = string_array_to_index(arg, argv[depth]);
                        } else {
                            *((const char **) (arguments + arg->offset)) = argv[depth];
                        }
                    }
                }
                if (NULL != arg) {
                    size_t i, l;

                    for (i = 0, l = dptrarray_length(arg->children); i < l; i++) {
                        graph_node_t *child;

                        child = dptrarray_at_unsafe(arg->children, i, graph_node_t);
                        if (NULL != child->complete) {
                            child->complete((void *) arguments, NULL == argv[cursorc] ? "" : argv[cursorc], cursoro, client_data->possibilities, child->data);
                        }
                    }
                }
            }
        }
    }
    switch (dptrarray_length(client_data->possibilities)) {
        case 0:
            res = CC_ERROR;
            break;
        case 1:
            if (-1 == el_insertstr(el, dptrarray_at_unsafe(client_data->possibilities, 0, const char) + cursoro)) {
                res = CC_ERROR;
            } else {
                res = CC_REFRESH;
            }
#if TODO
            if (FALSE) { // TODO: add space if more arguments are expected
                if (-1 == el_insertstr(el, " ")) {
                    res = CC_ERROR;
                } else {
                    res = CC_REFRESH;
                }
            }
#endif
            break;
        default:
        {
            Iterator it;

            puts("");
            dptrarray_sort(client_data->possibilities, strcmpp, NULL);
            dptrarray_to_iterator(&it, client_data->possibilities);
            for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
                fputc('\t', stdout);
                fputs(iterator_current(&it, NULL), stdout);
                fputc('\n', stdout);
            }
            iterator_close(&it);
            res = CC_REDISPLAY;
            break;
        }
    }

    return res;
}

command_status_t graph_run_command(graph_t *g, int args_count, const char **args, error_t **error)
{
    char arguments[8192];
    command_status_t ret;
    argument_t *prev_arg, *arg;

    ret = COMMAND_USAGE;
    if (args_count < 1) {
        return 0;
    }
    bzero(arguments, ARRAY_SIZE(arguments));
    if (hashtable_get(g->roots, args[0], (void **) &arg)) {
        int depth;
        handle_t handle;

        handle = arg->handle;
        for (depth = 1; depth < args_count && NULL != arg; depth++) {
            prev_arg = arg;
            if (NULL == (arg = graph_node_find(arg, args[depth]))) {
                error_set(error, NOTICE, "too many arguments"); // also a literal or a choice does not match what is planned (eg: domain foo.tld dnssec on - "on" instead of status/enable/disable)
//                 traverse_graph_node(prev_arg, 0, TRUE);
                handle = NULL;
                return COMMAND_USAGE;
            }
            if (((size_t) -1) != arg->offset) {
                if (ARG_TYPE_LITERAL == arg->type) {
                    *((bool *) (arguments + arg->offset)) = TRUE;
                } else if (ARG_TYPE_CHOICES == arg->type) {
                    *((int *) (arguments + arg->offset)) = string_array_to_index(arg, args[depth]);
                } else {
                    *((const char **) (arguments + arg->offset)) = args[depth];
                }
            }
            if (NULL != arg && NULL != arg->handle) {
                handle = arg->handle;
            }
        }
        if (NULL == handle || !graph_node_end_in_children(arg)) {
            error_set(error, NOTICE, "unknown command");
//             traverse_graph_node(arg, 0, TRUE);
        } else {
            ret = handle((void *) arguments, error);
        }
    } else {
        graph_display(g);
        error_set(error, NOTICE, "unknown command");
    }

    return ret;
}

void graph_display(graph_t *g)
{
    Iterator it;
    HashTable *visited;

    visited = hashtable_new(value_hash, value_equal, NULL, NULL, NULL);
    hashtable_to_iterator(&it, g->roots);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        argument_t *arg;

        arg = iterator_current(&it, NULL);
        traverse_graph_node_ex(arg, visited, 0, TRUE);
    }
    iterator_close(&it);
    hashtable_destroy(visited);
}
