#include <stddef.h>
#include <string.h>
#include <histedit.h>

#include "common.h"
#include "struct/hashtable.h"

typedef enum {
//     ARG_TYPE_HEAD, // virtual
//     ARG_TYPE_TAIL, // virtual
    ARG_TYPE_LITERAL, // "list", "add", ...
    ARG_TYPE_NUMBER,
    ARG_TYPE_CHOICES, // comme le type de record dns
    ARG_TYPE_STRING // free string (eventually with help of completion)
} argument_type_t;

struct argument_t {
    size_t offset; // offsetof
    argument_type_t type;
    complete_t complete; // spécifique à ARG_TYPE_STRING et ARG_TYPE_CHOICES
    const char *string; // spécifique à ARG_TYPE_LITERAL
    command_status_t (*handle)(void *arguments, error_t **); // problème : où renseigne-t-on cette variable ?
    // Sur le mot le plus parlant ("list" par exemple) mais ça le rendrait propre à une commande ("list" de "account" serait alors différent de "domain" ou autres)
    // Sur le dernier argument ? Mais "account list", c'est "list" justement le dernier !
    graph_node_t *nextSibling;
    graph_node_t *previousSibling;
    graph_node_t *firstChild;
    graph_node_t *lastChild;
    void *data;
//     graph_t *owner;
};

struct graph_t {
//     DupFunc dup;
    DtorFunc dtor;
    HashTable *roots;
//     graph_node_t *head;
//     graph_node_t *tail;
};

graph_t *graph_new(void)
{
    graph_t *g;

    g = mem_new(*g);
//     g->head = graph_create_node(g, ?);
//     g->tail = graph_create_node(g, ?);
    g->roots = hashtable_ascii_cs_new(NULL, NULL, NULL);

    return g;
}

// graph_node_t */* const */graph_get_head(graph_t *g)
// {
//     return g->head;
// }
//
// graph_node_t */* const */graph_get_tail(graph_t *g)
// {
//     return g->tail;
// }
//
// graph_node_t *graph_node_append_child(graph_node_t *parent, void *data)
// {
//     //
// }

#define CREATE_ARG(node) \
    do { \
        node = mem_new(*node); \
        node->data = NULL; \
        node->string = NULL; \
        node->handle = NULL; \
        node->complete = NULL; \
        node->offset = (size_t) -1; \
        node->nextSibling = node->previousSibling = node->firstChild = node->lastChild = NULL; \
    } while (0);

static bool complete_literal(const char *argument, size_t argument_len, DPtrArray *possibilities, void *data)
{
    if (0 == strncmp(argument, (const char *) data, argument_len)) {
        dptrarray_push(possibilities, (void *) data);
    }

    return TRUE;
}

argument_t *argument_create_literal(const char *string, command_status_t (*handle)(void *, error_t **))
{
    argument_t *node;

    CREATE_ARG(node);
    node->type = ARG_TYPE_LITERAL;
    node->string = string;
    node->handle = handle;
    node->data = (void *) string;
    node->complete = complete_literal;

    return node;
}

static bool complete_choices(const char *argument, size_t argument_len, DPtrArray *possibilities, void *data)
{
    const char * const *v;

    for (v = (const char * const *) data; NULL != *v; v++) {
        if (0 == strncmp(argument, *v, argument_len)) {
            dptrarray_push(possibilities, (void *) *v);
        }
    }

    return TRUE;
}

argument_t *argument_create_choices(size_t offset, const char *hint, const char * const * values)
{
    argument_t *node;

    CREATE_ARG(node);
    node->string = hint;
    node->offset = offset;
    node->type = ARG_TYPE_CHOICES;
    node->complete = complete_choices;
    node->data = (void *) values;

    return node;
}

argument_t *argument_create_string(size_t offset, const char *hint, complete_t complete, void *data)
{
    argument_t *node;

    CREATE_ARG(node);
    node->string = hint;
    node->offset = offset;
    node->type = ARG_TYPE_STRING;
    node->complete = complete;
    node->data = data;

    return node;
}

// graph_node_t *graph_create_node(graph_t *g, void *data)
// {
//     graph_node_t *node;
//
//     node = mem_new(*node);
//     node->nextSibling = node->previousSibling = node->firstChild = node->lastChild = NULL;
//     node->data = data;
//     node->owner = g;
//
//     return node;
// }

void graph_destroy(graph_t *g)
{
    // TODO
    // pour chaque élément de g->roots
    // appel à une fonction récursive qui supprime les noeuds (en commençant par ceux de la fin)
    // ?
}

static void graph_node_insert_child(graph_t *g, graph_node_t *parent, graph_node_t *child)
{
    graph_node_t *current, *previous;

    // parent->firstChild = tête
    // parent->lastChild = queue
    // X->nextSibling = suivant
    // X->previousSibling = précédent
    for (current = parent->firstChild; NULL != current && child->type > current->type; current = current->nextSibling)
        ;
    if (current == child) {
        return;
    }
    if (NULL == current) {
        child->previousSibling = parent->lastChild;
        parent->lastChild = child;
        child->nextSibling = NULL;
        if (NULL != child->previousSibling) {
            child->previousSibling->nextSibling = child;
        }
        if (NULL == parent->firstChild) {
            parent->firstChild = child;
        }
    } else {
        child->nextSibling = current;
        child->previousSibling = current->previousSibling;
        if (NULL != current->previousSibling) {
            current->previousSibling->nextSibling = child;
        } else {
            parent->firstChild = child;
        }
        current->previousSibling = child;
    }
}

// créer un chemin complet
void graph_create_full_path(graph_t *g, graph_node_t *start, ...) /* SENTINEL */
{
    va_list nodes;
    graph_node_t *node, *parent;

//     parent = g->head;
    assert(NULL != g);
    assert(NULL != start);
    assert(ARG_TYPE_LITERAL == start->type);

    parent = start;
    hashtable_put(g->roots, (void *) start->string, start, NULL);
    va_start(nodes, start);
    while (NULL != (node = va_arg(nodes, graph_node_t *))) {
        graph_node_insert_child(g, parent, node);
        parent = node;
    }
    va_end(nodes);
}

// créer un chemin entre 2 sommets (end = NULL peut être NULL)
// vu que le point de départ doit exister, ça ne peut être une "racine"
void graph_create_path(graph_t *g, graph_node_t *start, graph_node_t *end, ...) /* SENTINEL */
{
    va_list nodes;
    graph_node_t *node, *parent;

    assert(NULL != g);
    assert(NULL != start);

    va_start(nodes, end);
    while (NULL != (node = va_arg(nodes, graph_node_t *))) {
        graph_node_insert_child(g, parent, node);
        parent = node;
    }
    va_end(nodes);
    if (NULL != end) {
        graph_node_insert_child(g, node, end);
    }
}

// créer tous les chemins/permutations possibles entre start et end (1 - 2 - 3, 1 - 3 - 2, 2 - 1 - 3, 2 - 3 - 1, 3 - 1 - 2, 3 - 2 - 1)
void graph_create_all_path(graph_node_t *start, graph_node_t *end, ...) /* SENTINEL */
{
    //
}

void graph_to_iterator(Iterator *it, graph_t *g, char **argv, int arc)
{
    //
}

static void traverse_graph_node(graph_node_t *node, int depth)
{
    graph_node_t *current;

    printf("%*c %s\n", depth * 4, ' ', node->string);
    for (current = node->firstChild; NULL != current; current = current->nextSibling) {
        traverse_graph_node(current, depth + 1);
    }
}

extern int str_split(const char *string, char ***args);

/* <TODO: DRY (share this with main.c)> */
typedef struct {
    graph_t *graph;
    Tokenizer *tokenizer;
    DPtrArray *possibilities;
} editline_data_t;
/* </TODO: DRY> */

static int strcmpp(const void *p1, const void *p2)
{
    return strcmp(*(char * const *) p1, *(char * const *) p2);
}

bool complete_from_hashtable_keys(const char *argument, size_t argument_len, DPtrArray *possibilities, void *data)
{
    Iterator it;

    hashtable_to_iterator(&it, (HashTable *) data);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        void *k;

        iterator_current(&it, &k);
        if (0 == strncmp(argument, (const char *) k, argument_len)) {
            dptrarray_push(possibilities, k);
        }
    }
    iterator_close(&it);
}

typedef struct {
    const char *name;
    bool (*matcher)(argument_t *, const char */*, size_t*/); // exact match (use strcmp, not strncmp - this is not intended for completion)
} argument_description_t;

static bool agument_literal_match(argument_t *arg, const char *value/*, size_t value_len*/)
{
    return 0 == strcmp(arg->string, value);
}

static bool argument_choices_match(argument_t *arg, const char *value/*, size_t value_len*/)
{
    const char * const *v;

    for (v = (const char * const *) arg->data; NULL != *v; v++) {
        if (0 == strcmp(value, *v)) {
            return TRUE;
        }
    }

    return FALSE;
}

static bool argument_string_match(argument_t *arg, const char *value/*, size_t value_len*/)
{
    return TRUE;
}

static argument_description_t descriptions[] = {
    [ ARG_TYPE_NUMBER ] = { "number", NULL },
    [ ARG_TYPE_LITERAL ] = { "literal", agument_literal_match },
    [ ARG_TYPE_CHOICES ] = { "choices", argument_choices_match },
    [ ARG_TYPE_STRING ] = { "(free) string", argument_string_match }
};

static graph_node_t *graph_node_find(graph_node_t *parent, const char *value)
{
    graph_node_t *child;

    for (child = parent->firstChild; NULL != child; child = child->nextSibling) {
        if (descriptions[child->type].matcher(child, value/*, strlen(value)*/)) {
            return child;
        }
    }

    return NULL;
}

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

#if 0
        {
            int i;

debug("cursorc = %d", cursorc);
            for (i = 0; i <= cursorc; i++) {
                debug("argv[%d] = %s", i, argv[i]);
            }
        }
#endif
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
//                 bool apply;
                graph_node_t *child;

// debug("ARG[0] = %s", NULL != arg->string ? arg->string : descriptions[arg->type].name);
//                 apply = TRUE;
                for (depth = 1; /*apply && */depth < cursorc && NULL != arg/* && NULL != argv[cursorc]*/; depth++) {
                    /*apply &= */(/*NULL != */(arg = graph_node_find(arg, NULL == argv[depth] ? "" : argv[depth])));
// debug("ARG[%d] = %s", depth, NULL == arg ? "(NULL)" : (NULL != arg->string ? arg->string : descriptions[arg->type].name));
//                     for (child = parent->firstChild; NULL != child; child = child->nextSibling) {
//                         apply |= descriptions[child->type].matcher(child, value);
//                     }
                }
                if (/*apply && */NULL != arg) {
                    graph_node_t *child;

// debug("ARG = %s", NULL != arg->string ? arg->string : descriptions[arg->type].name);
                    for (child = arg->firstChild; NULL != child; child = child->nextSibling) {
                        if (NULL != child->complete) {
                            child->complete(NULL == argv[cursorc] ? "" : argv[cursorc], cursoro, client_data->possibilities, child->data);
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
            if (FALSE) { // TODO: add space if more arguments are expected
                if (-1 == el_insertstr(el, " ")) {
                    res = CC_ERROR;
                } else {
                    res = CC_REFRESH;
                }
            }
            break;
        default:
        {
            Iterator it;

            puts("");
            dptrarray_sort(client_data->possibilities, strcmpp);
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
    argument_t *arg;
    char arguments[8192];
    command_status_t ret;

    ret = COMMAND_USAGE;
    if (args_count < 1) {
        return 0;
    }
    bzero(arguments, ARRAY_SIZE(arguments));
    if (hashtable_get(g->roots, args[0], (void **) &arg)) {
        int depth;
        graph_node_t *child;

        for (depth = 1; depth < args_count && NULL != arg; depth++) {
            arg = graph_node_find(arg, args[depth]);
            if (((size_t) -1) != arg->offset) {
                // TODO: typing
                *((const char **) (arguments + arg->offset)) = args[depth];
            }
        }
        if (NULL == arg) {
            // échec : pas assez d'arguments ou ça ne correspond à rien? (quand il n'y a pas de "chaîne libre")
        } else {
            // arg doit avoir la fin pour un de ses fils
            if (NULL != arg->handle) { // TEMPORARY
                ret = arg->handle(&arguments, error);
            } else { // TEMPORARY
                error_set(error, NOTICE, "unknown command"); // TEMPORARY
            } // TEMPORARY
        }
    }

    return ret;
}
void graph_display(graph_t *g)
{
    Iterator it;

    puts("----------");
    hashtable_to_iterator(&it, g->roots);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        argument_t *arg;

        arg = iterator_current(&it, NULL);
        traverse_graph_node(arg, 0);
    }
    iterator_close(&it);
    puts("----------");
}

#if 0
static command_status_t domain_list(void *arguments, error_t **UNUSED(error))
{
    debug("domain list");
}

typedef struct {
    char *a;
    char *b;
    char *domain;
    char *c;
    char *record;
    char *d;
    char *e;
} record_argument_t;

static command_status_t record_list(void *arguments, error_t **UNUSED(error))
{
    debug("record list for domain %s", ((record_argument_t *) arguments)->domain);
}

static command_status_t record_delete(void *arguments, error_t **UNUSED(error))
{
    debug("record delete for %s.%s", ((record_argument_t *) arguments)->record, ((record_argument_t *) arguments)->domain);
}

static command_status_t print_help(void *arguments, error_t **UNUSED(error))
{
    debug("help");
}

graph_t *graph_test(void)
{
    graph_t *g;
    argument_t *help, *domain, *record, *domlist, *reclist, *recdelete, *domainarg, *recordarg, *recnocache;
    HashTable *domains, *records;

    g = graph_new();
    domains = hashtable_ascii_cs_new(NULL, NULL, NULL);
    hashtable_put(domains, (void *) "mars", NULL, NULL);
    hashtable_put(domains, (void *) "mai", NULL, NULL);
    hashtable_put(domains, (void *) "aout", NULL, NULL);

    records = hashtable_ascii_cs_new(NULL, NULL, NULL);
    hashtable_put(records, (void *) "juin", NULL, NULL);
    hashtable_put(records, (void *) "juillet", NULL, NULL);
    hashtable_put(records, (void *) "avril", NULL, NULL);

    g = graph_new();
    help = argument_create_literal("help", print_help);
    domain = argument_create_literal("domain", NULL);
    record = argument_create_literal("record", NULL);
    domlist = argument_create_literal("list", domain_list);
    reclist = argument_create_literal("list", record_list);
    recnocache = argument_create_literal("nocache", record_list);
    recdelete = argument_create_literal("delete", record_delete);
    domainarg = argument_create_string(offsetof(record_argument_t, domain), complete_from_hashtable_keys, domains);
    recordarg = argument_create_string(offsetof(record_argument_t, record), complete_from_hashtable_keys, records);

    graph_create_full_path(g, help, NULL);
    graph_create_full_path(g, domain, domlist, NULL);
    graph_create_full_path(g, domain, domainarg, record, reclist, NULL);
    graph_create_path(g, reclist, NULL, recnocache, NULL);
    graph_create_full_path(g, domain, domainarg, record, recordarg, recdelete, NULL);

    graph_display(g);

    return g;
}
#endif
