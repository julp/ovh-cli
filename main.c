#include <stdio.h>
#include <string.h>
#include <linenoise.h>
#include <libxml/parser.h>

#include "common.h"
#include "modules/api.h"
#include "commands/account.h"

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

static const module_t */*builtin_*/modules[] = {
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

#ifndef WITHOUT_LINENOISE
void completion(const char *buf, linenoiseCompletions *lc)
{
    linenoiseAddCompletion(lc, "hello");
}
#endif /* !WITHOUT_LINENOISE */

void cleanup(void)
{
    int i;

    for (i = ARRAY_SIZE(modules) - 1; i >= 0 ; i--) {
        if (NULL != modules[i]->dtor) {
            modules[i]->dtor();
        }
    }
}

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
#ifdef TEST
    {
        xmlDocPtr doc;
        request_t *req;

        req = request_get(API_BASE_URL "/domains/");
        request_sign(req);
        request_execute(req, RESPONSE_XML, (void **) &doc);

        return EXIT_FAILURE;
    }
#endif
    if (1 == argc) {
#ifndef WITHOUT_LINENOISE
        char *line, prompt[512], *p;

        *prompt = '\0';
        p = stpcpy(prompt, account_current());
        p = stpcpy(p, "> ");
        linenoiseSetCompletionCallback(completion);
        while(NULL != (line = linenoise(prompt))) {
#else
        char line[2048];

        printf("%s> ", account_current());
        fflush(stdout);
        while (NULL != fgets(line, STR_SIZE(line), stdin)) {
#endif /* !WITHOUT_LINENOISE */
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
#ifndef WITHOUT_LINENOISE
//             linenoiseHistoryAdd(line);
//             linenoiseHistorySave("history.txt");
            free(line);
#else
            printf("%s> ", account_current());
            fflush(stdout);
#endif /* !WITHOUT_LINENOISE */
        }
    } else {
        --argc;
        ++argv;
        ret = run_command(argc, (const char **) argv) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    return ret;
}
