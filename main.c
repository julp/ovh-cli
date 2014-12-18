#include <stdio.h>
#include <string.h>
#include <libxml/parser.h>
#include "common.h"
#include <histedit.h>
#include "modules/api.h"
#include "modules/conv.h"
#include "commands/account.h"
#include "struct/dptrarray.h"

typedef struct {
    graph_t *graph;
    Tokenizer *tokenizer;
    DPtrArray *possibilities;
} editline_data_t;

extern module_t openssl_module;
extern module_t curl_module;
extern module_t libxml_module;
extern module_t conv_module;
extern module_t api_module;
extern module_t base_module;
extern module_t account_module;
// ---
#ifdef WITH_NLS
extern module_t nls_module;
#endif /* WITH_NLS */
extern module_t me_module;
extern module_t vps_module;
extern module_t domain_module;
extern module_t hosting_module;
extern module_t dedicated_module;

static graph_t *g = NULL;

static const module_t */*builtin_*/modules[] = {
    &openssl_module,
    &curl_module,
    &libxml_module,
    &conv_module,
    &api_module,
    &base_module,
    &account_module,
    // ---
#ifdef WITH_NLS
    &nls_module,
#endif /* WITH_NLS */
    &me_module,
    &vps_module,
    &domain_module,
    &hosting_module,
    &dedicated_module
};

void print_error(error_t *error)
{
    if (NULL != error/* && error->type >= verbosity*/) {
        int type;

        type = error->type;
        switch (type) {
            case INFO:
                fprintf(stderr, "[ " GREEN("INFO") " ] ");
                break;
            case NOTICE:
                fprintf(stderr, "[ " YELLOW("NOTE") " ] ");
                break;
            case WARN:
                fprintf(stderr, "[ " YELLOW("WARN") " ] ");
                break;
            case FATAL:
                fprintf(stderr, "[ " RED("ERR ") " ] ");
                break;
            default:
                type = FATAL;
                fprintf(stderr, "[ " RED("BUG ") " ] Unknown error type for:\n");
                break;
        }
        fputs(error->message, stderr);
        error_destroy(error);
        if (FATAL == type) {
            exit(EXIT_FAILURE);
        }
    }
}

void report(int type, const char *format, ...)
{
//     if (type >= verbosity) {
        va_list args;

        switch (type) {
            case INFO:
                fprintf(stderr, "[ " GREEN("INFO") " ] ");
                break;
            case NOTICE:
                fprintf(stderr, "[ " YELLOW("NOTE") " ] ");
                break;
            case WARN:
                fprintf(stderr, "[ " YELLOW("WARN") " ] ");
                break;
            case FATAL:
                fprintf(stderr, "[ " RED("ERR ") " ] ");
                break;
        }
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
        if (FATAL == type) {
            exit(EXIT_FAILURE);
        }
//     }
}

#if 1
// TODO: remove backslash before " characters
int str_split(const char *string, char ***args)
{
    char *dup;
    int count, i;
    const char *p;
    bool in_quotes;
    size_t dup_len;

    p = string;
    i = count = 0;
    while (' ' == *string) {
        ++string;
    }
    in_quotes = '"' == *string;
    if (NULL == (dup = strdup(string + in_quotes))) {
        return -1;
    }
    dup_len = strlen(dup);
    if ('\n' == dup[dup_len - 1]) {
        dup[--dup_len] = '\0';
        if (dup_len > 0 && '\r' == dup[dup_len - 1]) {
            dup[--dup_len] = '\0';
        }
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
#else
static int str_split(const char *string, Tokenizer *tokenizer, char ***argv)
{
    int argc;

    tok_reset(tokenizer);
    if (-1 == tok_str(tokenizer, string, &argc, argv)) {
        return -1;
    } else {
        return argc;
    }
}
#endif

void cleanup(void)
{
    int i;

    for (i = ARRAY_SIZE(modules) - 1; i >= 0 ; i--) {
        if (NULL != modules[i]->dtor) {
            modules[i]->dtor();
        }
    }
    graph_destroy(g);
}

static const char *prompt(EditLine *UNUSED(e))
{
    char *p;
    static char prompt[512];

    *prompt = '\0';
    p = stpcpy(prompt, account_current());
    p = stpcpy(p, "> ");

    return prompt;
}

extern unsigned char graph_complete(EditLine *, int);

// LANG="fr_FR.ISO-8859-1" ./ovh domain $'\xE9' record list
int main(int argc, char **argv)
{
    int ret;
    size_t i;
    error_t *error;

    error = NULL;
    ret = EXIT_SUCCESS;
    g = graph_new();
    for (i = 0; i < ARRAY_SIZE(modules); i++) {
        if (NULL != modules[i]->early_init) {
            modules[i]->early_init();
        }
    }
    atexit(cleanup);
    for (i = 0; i < ARRAY_SIZE(modules); i++) {
        if (NULL != modules[i]->late_init) {
            modules[i]->late_init();
        }
    }
    for (i = 0; i < ARRAY_SIZE(modules); i++) {
        if (NULL != modules[i]->register_commands) {
            modules[i]->register_commands(g);
        }
    }
    if (1 == argc) {
        int count;
        EditLine *el;
        History *hist;
        const char *line;
        editline_data_t client_data;

        puts(_("needs help? Type help!"));
        hist = history_init();
        {
            HistEvent ev;

            history(hist, &ev, H_SETSIZE, 100);
            history(hist, &ev, H_SETUNIQUE, 1);
        }
        client_data.graph = g;
        client_data.tokenizer = tok_init(NULL);
        client_data.possibilities = dptrarray_new(NULL, NULL, NULL);
        el = el_init(*argv, stdin, stdout, stderr);
        el_set(el, EL_CLIENTDATA, &client_data);
        el_set(el, EL_PROMPT, prompt);
        el_set(el, EL_HIST, history, hist);
        el_set(el, EL_ADDFN, "ed-complete", "Complete argument", graph_complete);
        el_set(el, EL_BIND, "^I", "ed-complete", NULL);
        while (NULL != (line = el_gets(el, &count))/* && -1 != count*/) {
            char **args;
            int args_len;
            HistEvent ev;
            char *utf8_line;

            error = NULL; // reinitialize it
            if (convert_string_local_to_utf8(line, count, &utf8_line, NULL, &error)) {
                args_len = str_split(utf8_line, &args);
                graph_run_command(g, args_len, (const char **) args, &error);
                convert_string_free(line, &utf8_line);
                free(args[0]);
                free(args);
            }
            print_error(error);
            history(hist, &ev, H_ENTER, line);
        }
        history_end(hist);
        tok_end(client_data.tokenizer);
        dptrarray_destroy(client_data.possibilities);
        el_end(el);
        puts("");
    } else {
        char **utf8_argv;

        --argc;
        ++argv;
        convert_array_local_to_utf8(argc, argv, &utf8_argv, &error);
        ret = graph_run_command(g, argc, (const char **) utf8_argv, &error) ? EXIT_SUCCESS : EXIT_FAILURE;
        print_error(error);
        convert_array_free(argc, argv, utf8_argv);
    }

    return ret;
}
