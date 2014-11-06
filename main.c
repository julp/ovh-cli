#include <stdio.h>
#include <string.h>
#include <libxml/parser.h>
#include "common.h"
#ifdef WITH_EDITLINE
# include <histedit.h>
#endif /* WITH_EDITLINE */
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
#ifdef WITH_NLS
extern module_t nls_module;
#endif /* WITH_NLS */
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
#ifdef WITH_NLS
    &nls_module,
#endif /* WITH_NLS */
    &domain_module,
    &dedicated_module,
    &me_module
};

const size_t /*builtin_*/modules_count = ARRAY_SIZE(/*builtin_*/modules);

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

// TODO: remove backslash before " characters
static int str_split(const char *string, char ***args)
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

static int run_command(int args_count, const char **args, error_t **error)
{
    size_t i;
    const command_t *c;

    if (args_count < 1) {
        return 0;
    }
    for (i = 0; i < ARRAY_SIZE(modules); i++) {
        if (NULL != modules[i]->commands) {
            for (c = modules[i]->commands; NULL != c->handle; c++) {
                if (NULL != c->args) {
                    int j; // argument index
                    bool apply; // do arguments apply to the current command
                    const char * const *v;
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
                            return c->handle(argc, argv, error);
                        }
                    } else {
                        // si ça matche quand même sur ARG_MODULE_NAME, abandonner et afficher l'aide du module ?
                    }
                }
            }
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

#ifdef WITH_EDITLINE
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
    const LineInfo *li;
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
    {
        size_t i;
        const command_t *c;

        for (i = 0; i < ARRAY_SIZE(modules); i++) {
            if (NULL != modules[i]->commands) {
                const char *prev;

                prev = NULL;
                for (c = modules[i]->commands; NULL != c->handle; c++) {
                    if (cursorc <= c->argc) {
                        int j; /* signed here !!! */
                        bool apply;
                        const char *v;

                        apply = TRUE;
                        if (ARG_ANY_VALUE == c->args[cursorc]) { // free argument, no completion available
                            continue;
                        }
                        for (j = 0; apply && j < cursorc; j++) {
                            if (ARG_MODULE_NAME == c->args[j]) {
                                apply &= 0 == strcmp(modules[i]->name, argv[j]);
                            } else if (ARG_ANY_VALUE == c->args[j]) {
                                // NOP
                            } else {
                                apply &= 0 == strcmp(c->args[j], argv[j]);
                            }
                        }
                        if (!apply) { // the beginning of the command doesn't match, skip current command
                            continue;
                        } else if (ARG_MODULE_NAME == c->args[cursorc]) {
                            v = modules[i]->name;
                        } else {
                            v = c->args[cursorc];
                        }
                        if (0 == strncmp(v, argv[cursorc], cursoro)) {
#if 0
                            // TODO: replace a space after completed arg if more argument follow like before (imply to not simply dptrarray_push a string)
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
                                dptrarray_push(possibilities, (void *) v);
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
#endif /* WITH_EDITLINE */

int main(int argc, char **argv)
{
    int ret;
    size_t i;
    error_t *error;

    error = NULL;
    ret = EXIT_SUCCESS;
    for (i = 0; i < ARRAY_SIZE(modules); i++) {
        if (NULL != modules[i]->early_init) {
            modules[i]->early_init();
        }
    }
    for (i = 0; i < ARRAY_SIZE(modules); i++) {
        if (NULL != modules[i]->late_init) {
            modules[i]->late_init();
        }
    }
    atexit(cleanup);
    if (1 == argc) {
#ifdef WITH_EDITLINE
        int count;
        EditLine *el;
        History *hist;
        const char *line;

        puts(_("needs help? Type help!"));
        hist = history_init();
        {
            HistEvent ev;

            history(hist, &ev, H_SETSIZE, 100);
            history(hist, &ev, H_SETUNIQUE, 1);
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
#endif /* WITH_EDITLINE */
            int args_len;
            char **args;

            error = NULL; // reinitialize it
            args_len = str_split(line, &args);
            run_command(args_len, (const char **) args, &error);
            print_error(error);
            free(args[0]);
#ifdef WITH_EDITLINE
            history(hist, &ev, H_ENTER, line);
#else
            printf("%s> ", account_current());
            fflush(stdout);
#endif /* WITH_EDITLINE */
        }
#ifdef WITH_EDITLINE
        history_end(hist);
        el_end(el);
        puts("");
#endif /* WITH_EDITLINE */
    } else {
        --argc;
        ++argv;
        ret = run_command(argc, (const char **) argv, &error) ? EXIT_SUCCESS : EXIT_FAILURE;
        print_error(error);
    }

    return ret;
}
