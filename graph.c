#include <stddef.h>
#include <string.h>
#include <histedit.h>

#include "common.h"
#include "struct/hashtable.h"

typedef enum {
//     ARG_TYPE_HEAD, // virtual
//     ARG_TYPE_TAIL, // virtual
    ARG_TYPE_END, // dummy
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
    handle_t handle; // problème : où renseigne-t-on cette variable ?
    // Sur le mot le plus parlant ("list" par exemple) mais ça le rendrait propre à une commande ("list" de "account" serait alors différent de "domain" ou autres)
    // Sur le dernier argument ? Mais "account list", c'est "list" justement le dernier !
    graph_node_t *nextSibling;
    graph_node_t *previousSibling;
    graph_node_t *firstChild;
    graph_node_t *lastChild;
    void *data; // TODO: removal in favor of *_data
    void *completion_data; // data for completion
    void *command_data; // data to run command
//     graph_t *owner;
};

struct graph_t {
    DtorFunc dtor;
    HashTable *roots;
    HashTable *nodes;
    graph_node_t *end;
};

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

graph_t *graph_new(void)
{
    graph_t *g;

    g = mem_new(*g);
    CREATE_ARG(g->end);
    g->end->string = "(END)";
    g->roots = hashtable_ascii_cs_new(NULL, NULL, NULL);

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

    CREATE_ARG(node);
    node->type = ARG_TYPE_LITERAL;
    node->string = string;
    node->handle = handle;
    node->data = (void *) string;
    node->complete = complete_literal;

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

static void traverse_graph_node1(graph_node_t *node, DtorFunc dtor, HashTable *ht)
{
    graph_node_t *current, *next;

    current = node->firstChild;
    while (NULL != current) {
        next = current->nextSibling;
#ifdef LAZY_WAY
        hashtable_quick_put_ex(ht, HT_PUT_ON_DUP_KEY_PRESERVE, (hash_t) current, current, current, NULL);
#else
        if (!hashtable_quick_contains(ht, (hash_t) current, current)) {
            hashtable_quick_put_ex(ht, HT_PUT_ON_DUP_KEY_PRESERVE, (hash_t) current, current, current, NULL);
#endif
            traverse_graph_node1(current, dtor, ht);
            free(current);
#ifndef LAZY_WAY
        }
//         free(current);
#endif
        current = next;
    }
}

static void traverse_graph_root1(graph_t *g, DtorFunc dtor, HashTable *ht)
{
    Iterator it;

    hashtable_to_iterator(&it, g->roots);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        argument_t *arg;

        arg = iterator_current(&it, NULL);
#ifdef LAZY_WAY
        hashtable_quick_put_ex(ht, HT_PUT_ON_DUP_KEY_PRESERVE, (hash_t) arg, arg, arg, NULL);
#else
        if (!hashtable_quick_contains(ht, (hash_t) arg, arg)) {
            hashtable_quick_put_ex(ht, HT_PUT_ON_DUP_KEY_PRESERVE, (hash_t) arg, arg, arg, NULL);
#endif
            traverse_graph_node1(arg, dtor, ht);
            free(arg);
#ifndef LAZY_WAY
        }
//         free(arg);
#endif
    }
    iterator_close(&it);
}

void graph_destroy(graph_t *g)
{
    HashTable *ht;

    assert(NULL != g);

    // HashFunc, EqualFunc, DupFunc, DtorFunc, DtorFunc
#ifdef LAZY_WAY
    ht = hashtable_new(value_hash, value_equal, NULL, free, NULL);
#else
    ht = hashtable_new(value_hash, value_equal, NULL, NULL, NULL);
#endif
    traverse_graph_root1(g, free, ht);
    hashtable_destroy(ht);
    hashtable_destroy(g->roots);
    free(g);
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
    graph_node_insert_child(g, parent, g->end);
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
        graph_node_insert_child(g, parent, end);
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

bool complete_from_hashtable_keys(void *arguments, const char *argument, size_t argument_len, DPtrArray *possibilities, void *data)
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

static bool argument_end_match(argument_t *arg, const char *value/*, size_t value_len*/)
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
    graph_node_t *child;

    for (child = parent->firstChild; NULL != child; child = child->nextSibling) {
        if (descriptions[child->type].matcher(child, value/*, strlen(value)*/)) {
            return child;
        }
    }

    return NULL;
}

static bool graph_node_end_in_children(/*graph_t *g,*/graph_node_t *node)
{
    graph_node_t *child;

    for (child = node->firstChild; NULL != child; child = child->nextSibling) {
        if (ARG_TYPE_END == child->type/*child == g->end*/) {
            return TRUE;
        }
    }

    return FALSE;
}

// TODO:
// - separate completion (2 DPtrArray ?) to distinguish commands to values (eg: domain<tab> will propose "bar.tld", "foo.tld", "list", "xxx.tld")
// - fill underlaying hashtable on completion to have consistent completion? (eg: domain<tab> will only propose "list" if we haven't yet run "domain list")
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

#if 0
        {
            int i;

debug("cursorc = %d", cursorc);
            for (i = 0; i <= cursorc; i++) {
                debug("argv[%d] = %s", i, argv[i]);
            }
        }
#endif
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
                    if (((size_t) -1) != arg->offset) {
                        // TODO: typing (bool, uint32_t et autres si nécessaire)
                        *((const char **) (arguments + arg->offset)) = argv[depth];
                    }
                }
                if (/*apply && */NULL != arg) {
                    graph_node_t *child;

// debug("ARG = %s", NULL != arg->string ? arg->string : descriptions[arg->type].name);
                    for (child = arg->firstChild; NULL != child; child = child->nextSibling) {
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
    while ('\0' != r) {
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

                    offset = va_arg(ap, size_t);
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

                    s = va_arg(ap, const char *);
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
#include <math.h>
                    uint32_t num;
                    size_t num_len;

                    num = va_arg(ap, uint32_t);
                    num_len = log10(num) + 1;
                    dst_len += num_len;
                    if (dst_size > dst_len) {
                        size_t i;
                        uint32_t m;

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
            *w++ = *r;
        }
        ++r;
    }
    va_end(cpy);
    if (dst_size > dst_len) {
        *w++ = '\0';
    }

    return dst_len;
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
        graph_node_t *child;

        handle = arg->handle;
        for (depth = 1; depth < args_count && NULL != arg; depth++) {
            prev_arg = arg;
            if (NULL == (arg = graph_node_find(arg, args[depth]))) {
                error_set(error, NOTICE, "too many arguments");
                traverse_graph_node(prev_arg, 0);
                handle = NULL;
                return COMMAND_USAGE;
            }
            if (((size_t) -1) != arg->offset) {
                // TODO: typing (bool, uint32_t et autres si nécessaire)
                *((const char **) (arguments + arg->offset)) = args[depth];
            }
            if (NULL != arg && NULL != arg->handle) {
                handle = arg->handle;
            }
        }
        if (NULL == handle || !graph_node_end_in_children(arg)) {
            error_set(error, NOTICE, "unknown command");
            traverse_graph_node(arg, 0);
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

    hashtable_to_iterator(&it, g->roots);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        argument_t *arg;

        arg = iterator_current(&it, NULL);
        traverse_graph_node(arg, 0);
    }
    iterator_close(&it);
}
