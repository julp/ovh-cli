#include <stdio.h>
#include <string.h>
#if defined(WITH_READLINE)
# include <readline/readline.h>
# include <readline/history.h>
#elif defined(WITH_EDITLINE)
# include <histedit.h>
#endif
#include <libxml/parser.h>

#include "common.h"
#include "modules/api.h"
#include "commands/account.h"
#include "struct/dptrarray.h"

extern module_t openssl_module;
extern module_t curl_module;
extern module_t libxml_module;
extern module_t conv_module;
extern module_t api_module;
extern module_t base_module;
extern module_t account_module;
// ---
extern module_t domain_module;
extern module_t dedicated_module;
extern module_t me_module;

const module_t */*builtin_*/modules[] = {
    &openssl_module,
    &curl_module,
    &libxml_module,
    &conv_module,
    &api_module,
    &base_module,
    &account_module,
    // ---
    &domain_module,
    &dedicated_module,
    &me_module
};

const size_t /*builtin_*/modules_count = ARRAY_SIZE(/*builtin_*/modules);

// TODO: remove backslash before " characters
static int str_split(const char *string, char ***args)
{
    char *dup;
    int count, i;
    const char *p;
    bool in_quotes;

    p = string;
    i = count = 0;
    while (' ' == *string) {
        ++string;
    }
    in_quotes = '"' == *string;
    if (NULL == (dup = strdup(string + in_quotes))) {
        return -1;
    }
    for (p = dup; '\0' != *p; p++) {
        if (in_quotes) {
            if ('"' == *p && '\\' != *(p - 1)) {
                in_quotes = FALSE;
                ++count;
            }
        } else {
            if (' ' == *p) {
                while (' ' == *(p + 1)) {
                    ++p;
                }
                if ('"' == *(p + 1)) {
                    in_quotes = TRUE;
                    ++p;
                }
                ++count;
            }
        }
    }
    in_quotes = '"' == *string; // reinitialize it in case of non-terminated "
    *args = mem_new_n(**args, count + 2);
    (*args)[i++] = dup;
    while ('\0' != *dup) {
        if (in_quotes) {
            if ('"' == *dup && '\\' != *(dup - 1)) {
                in_quotes = FALSE;
                *dup = '\0';
            }
        } else {
            if (' ' == *dup) {
                *dup = '\0';
                while (' ' == *(dup + 1)) {
                    ++dup;
                }
                if ('"' == *(dup + 1)) {
                    in_quotes = TRUE;
                    ++dup;
                }
                (*args)[i++] = dup + 1;
            }
        }
        ++dup;
    }
    (*args)[i] = NULL;

    return i;
}

static int run_command(int args_count, const char **args)
{
    size_t i;
    const command_t *c;

    if (args_count < 1) {
        return 0;
    }
    for (i = 0; i < ARRAY_SIZE(modules); i++) {
        if (NULL != modules[i]->commands) {
            for (c = modules[i]->commands; NULL != c->first_word; c++) {
                if (NULL != c->args) {
                    int j; // argument index
                    bool apply; // do arguments apply to the current command
                    const char **v;
                    int argc;
                    const char *argv[12];
                    bool ignore_args; // ignore first predefined fixed arguments

                    j = 0;
                    argc = 0;
                    ignore_args = apply = TRUE;
// debug("%s", modules[i]->name);
                    for (v = c->args; apply && NULL != *v && j < args_count; v++) {
                        if (ARG_MODULE_NAME == *v) {
// debug("- %s", modules[i]->name);
                            apply &= 0 == strcmp(modules[i]->name, args[j]);
                        } else {
                            if (ARG_ANY_VALUE == *v) {
// debug("- %s", args[j]);
                                ignore_args = FALSE;
                            } else {
// debug("- %s", *v);
                                apply &= 0 == strcmp(*v, args[j]);
                            }
                            if (!ignore_args) {
                                argv[argc++] = args[j];
                            }
                        }
                        ++j;
                    }
                    // TODO: check extra/remaining arguments
                    if (apply) {
                        if (NULL != *v) {
                            fprintf(stderr, "not enough arguments\n");
                            return 0;
                        } else {
                            argv[argc] = NULL;
                            return c->handle(argc, argv);
                        }
                    } else {
                        // si ça matche quand même sur ARG_MODULE_NAME, abandonner et afficher l'aide du module ?
                    }
                }
            }
#if 0
            if (NULL == modules[i]->name) {
                for (c = modules[i]->commands; NULL != c->first_word; c++) {
                    if (0 == strcmp(c->first_word, args[0])) {
                        if (c->argc < 0 || (args_count - 1) == c->argc) {
                            return c->handle(args_count - 1, args + 1);
                        } else {
                            // TODO: display module help ?
                            fprintf(stderr, "wrong arguments count: %s expects %d arguments\n", c->first_word, c->argc);
                            return 0;
                        }
                    }
                }
            } else {
                if (0 == strcmp(modules[i]->name, args[0])) {
                    for (c = modules[i]->commands; NULL != c->first_word; c++) {
                        if (0 == strcmp(c->first_word, args[1])) {
                            if (c->argc < 0 || (args_count - 2) == c->argc) {
                                return c->handle(args_count - 2, args + 2);
                            } else {
                                // TODO: display module help ?
                                fprintf(stderr, "wrong arguments count: %s %s expects %d arguments\n", modules[i]->name, c->first_word, c->argc);
                                return 0;
                            }
                        }
                    }
                }
            }
#endif
        }
    }
    fprintf(stderr, "no command corresponds\n");

    return 0;
}

void cleanup(void)
{
    int i;

    for (i = ARRAY_SIZE(modules) - 1; i >= 0 ; i--) {
        if (NULL != modules[i]->dtor) {
            modules[i]->dtor();
        }
    }
}

#if defined(WITH_READLINE)
char *command_generator(const char *text, int state)
{
    size_t i;
    const command_t *c;

    for (i = 0; i < ARRAY_SIZE(modules); i++) {
        if (NULL != modules[i]->commands) {
            for (c = modules[i]->commands; NULL != c->first_word; c++) {
//                 if (NULL != c->args) {
//                     const char **v;
//
//                     for (v = c->args; apply && NULL != *v && j < args_count; v++) {
//                     }
//                 }
                if (0 == strncmp(c->first_word, text, len)) {
                    return c->first_word;
                }
            }
        }
    }

    return NULL;
}

char **command_completion(const char *text, int start, int end)
{
    char **matches;

    matches = NULL;
    if (start == 0) {
        matches = rl_completion_matches(text, command_generator);
    }

    return matches;
}
#elif defined(WITH_EDITLINE)
static const char *prompt(EditLine *UNUSED(e))
{
    char *p;
    static char prompt[512];

    *prompt = '\0';
    p = stpcpy(prompt, account_current());
    p = stpcpy(p, "> ");

    return prompt;
}

static int strcmpp(const void *p1, const void *p2)
{
    return strcmp(*(char * const *) p1, *(char * const *) p2);
}

static unsigned char complete(EditLine *el, int ch)
{
    /*static */Tokenizer *t = NULL;
    LineInfo *li;
    const char **argv;
    int argc;
    int cursorc;
    int cursoro;
    int arraylen;
    int res = CC_ERROR;
    DPtrArray *possibilities;

    li = el_line(el);
    t = tok_init(NULL); // TODO: don't create/destroy it each call - just reset it
    possibilities = dptrarray_new(NULL, NULL, NULL); // TODO: don't create/destroy it each call - just clear it
    if (-1 == tok_line(t, li, &argc, &argv, &cursorc, &cursoro)) {
        //
    }
    if (0 == cursorc) {
        size_t i;
        const command_t *c;

        for (i = 0; i < ARRAY_SIZE(modules); i++) {
            if (NULL != modules[i]->commands) {
                const char *prev;

                prev = NULL;
                for (c = modules[i]->commands; NULL != c->first_word; c++) {
                    if (NULL != c->args) {
    //                     const char **v;
    //
    //                     for (v = c->args; apply && NULL != *v && j < args_count; v++) {
    //                     }
                        const char *v;

                        v = c->args[0];
                        if (ARG_MODULE_NAME == c->args[0]) {
                            v = modules[i]->name;
                        }
                        if (0 == strncmp(v, argv[cursorc], cursoro)) {
#if 0
                            if (-1 == el_insertstr(el, v + cursoro)) {
                                res = CC_ERROR;
                            } else {
                                res = CC_REFRESH;
                            }
                            if (NULL != c->args[1]) {
                                if (-1 == el_insertstr(el, " ")) {
                                    res = CC_ERROR;
                                } else {
                                    res = CC_REFRESH;
                                }
                            }
                            break;
#else
                            if (NULL == prev || 0 != strcmp(v, prev)) {
                                dptrarray_push(possibilities, v);
                            }
                            prev = v;
#endif
                        }
                    }
                }
            }
        }
    }
    tok_end(t);
    switch (dptrarray_length(possibilities)) {
        case 0:
            res = CC_ERROR;
            break;
        case 1:
            if (-1 == el_insertstr(el, dptrarray_at_unsafe(possibilities, 0, const char) + cursoro)) {
                res = CC_ERROR;
            } else {
                res = CC_REFRESH;
            }
            break;
        default:
        {
            Iterator it;

            puts("");
            dptrarray_sort(possibilities, strcmpp);
            dptrarray_to_iterator(&it, possibilities);
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
    dptrarray_destroy(possibilities);

    return res;
}
#endif

int main(int argc, char **argv)
{
    int ret;
    size_t i;

    ret = EXIT_SUCCESS;
    for (i = 0; i < ARRAY_SIZE(modules); i++) {
        if (NULL != modules[i]->ctor) {
            modules[i]->ctor();
        }
    }
    atexit(cleanup);
#if 0
    {
#include "struct/dptrarray.h"

        Iterator it;
        DPtrArray *ary;

        ary = dptrarray_new(NULL, NULL, NULL);
        dptrarray_push(ary, "abc");
        dptrarray_push(ary, "def");
        dptrarray_push(ary, "ghi");

        dptrarray_to_iterator(&it, ary);
        printf("[[[[[]]]]]\n");
//         for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        for (iterator_last(&it); iterator_is_valid(&it); iterator_previous(&it)) {
            char *v;
            int idx, *pidx;

            pidx = &idx;
            v = iterator_current(&it, &idx);
            printf("%d %s\n", idx, v);
//             break;
        }
        iterator_close(&it);
        dptrarray_destroy(ary);
        printf("[[[[[]]]]]\n");
    }
    {
#include <math.h>
#include "json.h"

        String *buffer;
        json_document_t *doc;
        json_value_t h1, h2, h3, a1, a2, a3;

        h1 = json_object();
        h2 = json_object();
        h3 = json_object();
        a1 = json_array();
        a2 = json_array();
        a3 = json_array();
        json_object_set_property(h1, "ab\nc", h2);
        json_object_set_property(h2, "de\"f", h3);
        json_object_set_property(h2, "foo", json_string("bar"));
        json_object_set_property(h3, "gh/i", json_integer(123));
        json_object_set_property(h3, "jkl", json_number(M_PI));
        json_object_set_property(h3, "xxx", a1);
        json_array_push(a1, a2);
        json_array_push(a2, a3);
        json_array_push(a1, json_integer(123));
        json_array_push(a2, json_integer(456));
        json_array_push(a3, json_integer(789));
        buffer = string_new();
        doc = json_document_new();
//         json_document_set_root(doc, json_null);
//         json_document_set_root(doc, json_true);
        json_document_set_root(doc, h1);
        json_document_serialize(doc, buffer);
        json_document_destroy(doc);
debug(">%s<", buffer->ptr);
        string_destroy(buffer);
    }
#endif
    if (1 == argc) {
#if defined(WITH_READLINE)
        rl_readline_name = "ovh";
        rl_attempted_completion_function = command_completion;
        while (1) {
            char *line, prompt[512], *p;

            *prompt = '\0';
            p = stpcpy(prompt, account_current());
            p = stpcpy(p, "> ");

            line = readline(prompt);
#elif defined(WITH_EDITLINE)
        int count;
        char *line;
        EditLine *el;
        History *hist;

        hist = history_init();
        {
            HistEvent ev;

            history(hist, &ev, H_SETSIZE, 100);
        }
        el = el_init(*argv, stdin, stdout, stderr);
        el_set(el, EL_PROMPT, prompt);
        el_set(el, EL_HIST, history, hist);
        el_set(el, EL_ADDFN, "ed-complete", "Complete argument", complete);
        el_set(el, EL_BIND, "^I", "ed-complete", NULL);
        while (NULL != (line = el_gets(el, &count))/* && -1 != count*/) {
            HistEvent ev;
#else
        char line[2048];

        printf("%s> ", account_current());
        fflush(stdout);
        while (NULL != fgets(line, STR_SIZE(line), stdin)) {
#endif
            int args_len;
            char **args;
            size_t line_len;

            line_len = strlen(line);
            if ('\n' == line[line_len - 1]) {
                line[--line_len] = '\0';
                if (line_len > 0 && '\r' == line[line_len - 1]) {
                    line[--line_len] = '\0';
                }
            }
            args_len = str_split(line, &args);
            run_command(args_len, (const char **) args);
            free(args[0]);
#if defined(WITH_READLINE)
            free(line);
#elif defined(WITH_EDITLINE)
            history(hist, &ev, H_ENTER, line);
#else
            printf("%s> ", account_current());
            fflush(stdout);
#endif
        }
#ifdef WITH_EDITLINE
        history_end(hist);
        el_end(el);
        puts("");
#endif
    } else {
        --argc;
        ++argv;
        ret = run_command(argc, (const char **) argv) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    return ret;
}
