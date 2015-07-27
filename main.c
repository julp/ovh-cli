#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <libxml/parser.h>
#include "common.h"
#include "command.h"
#include "endpoints.h"
#include <histedit.h>
#include "modules/api.h"
#include "modules/conv.h"
#include "modules/home.h"
#include "commands/account.h"
#include "struct/dptrarray.h"

typedef struct {
    graph_t *graph;
    Tokenizer *tokenizer;
    DPtrArray *possibilities;
} editline_data_t;

extern module_t home_module;
extern module_t sqlite_module;
extern module_t openssl_module;
extern module_t curl_module;
extern module_t libxml_module;
extern module_t conv_module;
extern module_t table_module;
extern module_t api_module;
extern module_t base_module;
extern module_t account_module;
#ifdef WITH_NLS
extern module_t nls_module;
#endif /* WITH_NLS */
#ifdef WITH_ME_MODULE
// ---
extern module_t me_module;
#endif /* WITH_ME_MODULE */
#ifdef WITH_KEY_MODULE
extern module_t key_module;
#endif /* WITH_KEY_MODULE */
#ifdef WITH_VPS_MODULE
extern module_t vps_module;
#endif /* WITH_VPS_MODULE */
#ifdef WITH_CLOUD_MODULE
extern module_t cloud_module;
#endif /* WITH_CLOUD_MODULE */
#ifdef WITH_DOMAIN_MODULE
extern module_t domain_module;
#endif /* WITH_DOMAIN_MODULE */
#ifdef WITH_SUPPORT_MODULE
extern module_t support_module;
#endif /* WITH_SUPPORT_MODULE */
#ifdef WITH_HOSTING_MODULE
extern module_t hosting_module;
#endif /* WITH_HOSTING_MODULE */
#ifdef WITH_DEDICATED_MODULE
extern module_t dedicated_module;
#endif /* WITH_DEDICATED_MODULE */

static int verbosity = 0;
/*static */graph_t *g = NULL;

static char optstr[] = "lqy";

enum {
    NOCONFIRM_OPT = 0x80,
//     LOGFILE_OPT,
};

static struct option long_options[] =
{
//     { "log",        no_argument,       NULL, 'l' },
//     { "logfile",    required_argument, NULL, LOGFILE_OPT },
    { "yes",        no_argument,       NULL, 'y' },
    { "silent",     no_argument,       NULL, 'q' },
    { "no-confirm", no_argument,       NULL, NOCONFIRM_OPT },
    { NULL,         no_argument,       NULL, 0 }
};

const char *endpoint_names[] = {
    "ovh-eu",
    "ovh-ca",
    "soyoustart-eu",
    "soyoustart-ca",
    "kimsufi-eu",
    "kimsufi-ca",
    "runabove-ca",
    NULL
};

#define S(s) \
    s, STR_LEN(s)

const endpoint_t endpoints[] = {
    {
        "ovh-eu",
        S("https://eu.api.ovh.com/1.0"),
        (const module_t * const []) {
#ifdef WITH_ME_MODULE
            &me_module,
#endif /* WITH_ME_MODULE */
#ifdef WITH_KEY_MODULE
            &key_module,
#endif /* WITH_KEY_MODULE */
#ifdef WITH_VPS_MODULE
            &vps_module,
#endif /* WITH_VPS_MODULE */
#ifdef WITH_CLOUD_MODULE
            &cloud_module,
#endif /* WITH_CLOUD_MODULE */
#ifdef WITH_DOMAIN_MODULE
            &domain_module,
#endif /* WITH_DOMAIN_MODULE */
#ifdef WITH_SUPPORT_MODULE
            &support_module,
#endif /* WITH_SUPPORT_MODULE */
#ifdef WITH_HOSTING_MODULE
            &hosting_module,
#endif /* WITH_HOSTING_MODULE */
#ifdef WITH_DEDICATED_MODULE
            &dedicated_module,
#endif /* WITH_DEDICATED_MODULE */
            NULL
        }
    },
    {
        "ovh-ca",
        S("https://ca.api.ovh.com/1.0"),
        (const module_t * const []) {
#ifdef WITH_ME_MODULE
            &me_module,
#endif /* WITH_ME_MODULE */
#ifdef WITH_KEY_MODULE
            &key_module,
#endif /* WITH_KEY_MODULE */
#ifdef WITH_VPS_MODULE
            &vps_module,
#endif /* WITH_VPS_MODULE */
#ifdef WITH_SUPPORT_MODULE
            &support_module,
#endif /* WITH_SUPPORT_MODULE */
#ifdef WITH_HOSTING_MODULE
            &hosting_module,
#endif /* WITH_HOSTING_MODULE */
#ifdef WITH_DEDICATED_MODULE
            &dedicated_module,
#endif /* WITH_DEDICATED_MODULE */
            NULL
        }
    },
    {
        "soyoustart-eu",
        S("https://eu.api.soyoustart.com/1.0"),
        (const module_t * const []) {
#ifdef WITH_ME_MODULE
            &me_module,
#endif /* WITH_ME_MODULE */
#ifdef WITH_KEY_MODULE
            &key_module,
#endif /* WITH_KEY_MODULE */
#ifdef WITH_SUPPORT_MODULE
            &support_module,
#endif /* WITH_SUPPORT_MODULE */
#ifdef WITH_DEDICATED_MODULE
            &dedicated_module,
#endif /* WITH_DEDICATED_MODULE */
            NULL
        }
    },
    {
        "soyoustart-ca",
        S("https://ca.api.soyoustart.com/1.0"),
        (const module_t * const []) {
#ifdef WITH_ME_MODULE
            &me_module,
#endif /* WITH_ME_MODULE */
#ifdef WITH_KEY_MODULE
            &key_module,
#endif /* WITH_KEY_MODULE */
#ifdef WITH_SUPPORT_MODULE
            &support_module,
#endif /* WITH_SUPPORT_MODULE */
#ifdef WITH_DEDICATED_MODULE
            &dedicated_module,
#endif /* WITH_DEDICATED_MODULE */
            NULL
        }
    },
    {
        "kimsufi-eu",
        S("https://eu.api.kimsufi.com/1.0"),
        (const module_t * const []) {
#ifdef WITH_ME_MODULE
            &me_module,
#endif /* WITH_ME_MODULE */
#ifdef WITH_KEY_MODULE
            &key_module,
#endif /* WITH_KEY_MODULE */
#ifdef WITH_SUPPORT_MODULE
            &support_module,
#endif /* WITH_SUPPORT_MODULE */
#ifdef WITH_DEDICATED_MODULE
            &dedicated_module,
#endif /* WITH_DEDICATED_MODULE */
            NULL
        }
    },
    {
        "kimsufi-ca",
        S("https://ca.api.kimsufi.com/1.0"),
        (const module_t * const []) {
#ifdef WITH_ME_MODULE
            &me_module,
#endif /* WITH_ME_MODULE */
#ifdef WITH_KEY_MODULE
            &key_module,
#endif /* WITH_KEY_MODULE */
#ifdef WITH_SUPPORT_MODULE
            &support_module,
#endif /* WITH_SUPPORT_MODULE */
#ifdef WITH_DEDICATED_MODULE
            &dedicated_module,
#endif /* WITH_DEDICATED_MODULE */
            NULL
        }
    },
    {
        "runabove-ca",
        S("https://api.runabove.com/1.0"),
        (const module_t * const []) {
#ifdef WITH_ME_MODULE
            &me_module,
#endif /* WITH_ME_MODULE */
#ifdef WITH_CLOUD_MODULE
            &cloud_module,
#endif /* WITH_CLOUD_MODULE */
#ifdef WITH_SUPPORT_MODULE
            &support_module,
#endif /* WITH_SUPPORT_MODULE */
            NULL
        }
    },
    { NULL, NULL, 0, NULL }
};

#undef S

static const module_t */*builtin_*/modules[] = {
    &home_module,    // R-dep: main.c, modules/sqlite.c
    &sqlite_module,  // R-dep: most of commands/*.c
    &openssl_module, // R-dep: modules/api.c
    &curl_module,    // R-dep: modules/api.c
    &libxml_module,  // R-dep: modules/api.c
    &conv_module,    // R-dep: main.c (argv convertions), modules/tables.c
    &account_module, // R-dep: most of commands/*.c
    &api_module,     // R-dep: most of commands/*.c
    &base_module,    // R-dep: none
#ifdef WITH_NLS
    &nls_module,     // R-dep: modules/table.c
#endif /* WITH_NLS */
    &table_module,   // R-dep: most of commands/*.c
    // ---
#ifdef WITH_ME_MODULE
    &me_module,
#endif /* WITH_ME_MODULE */
#ifdef WITH_KEY_MODULE
    &key_module,
#endif /* WITH_KEY_MODULE */
#ifdef WITH_VPS_MODULE
    &vps_module,
#endif /* WITH_VPS_MODULE */
#ifdef WITH_CLOUD_MODULE
    &cloud_module,
#endif /* WITH_CLOUD_MODULE */
#ifdef WITH_DOMAIN_MODULE
    &domain_module,
#endif /* WITH_DOMAIN_MODULE */
#ifdef WITH_SUPPORT_MODULE
    &support_module,
#endif /* WITH_SUPPORT_MODULE */
#ifdef WITH_HOSTING_MODULE
    &hosting_module,
#endif /* WITH_HOSTING_MODULE */
#ifdef WITH_DEDICATED_MODULE
    &dedicated_module
#endif /* WITH_DEDICATED_MODULE */
};

#ifndef EUSAGE
# define EUSAGE -2
#endif /* !EUSAGE */

#ifdef _MSC_VER
extern char __progname[];
#else
extern char *__progname;
#endif /* _MSC_VER */

static void usage(void)
{
    fprintf(
        stderr,
        "usage: %s [-%s]\n",
        __progname,
        optstr
    );
    exit(EUSAGE);
}

void print_error(error_t *error)
{
    if (NULL != error && error->type >= verbosity) {
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

#if WITHOUT_LIBEDIT_TOKENIZER
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
#else
static int str_split(const char *string, Tokenizer *tokenizer, char ***argv)
{
    int argc;

    tok_reset(tokenizer);
    if (-1 == tok_str(tokenizer, string, &argc, (const char ***) argv)) {
        return -1;
    } else {
        return argc;
    }
}
#endif /* WITHOUT_LIBEDIT_TOKENIZER */

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
    int c;
    size_t i;
    error_t *error;
    command_status_t ret;
    main_options_t mainopts;
    char history_path[MAXPATHLEN];

    error = NULL;
    g = graph_new();
    bzero(&mainopts, sizeof(mainopts));
    for (i = 0; i < ARRAY_SIZE(modules); i++) {
        if (NULL != modules[i]->early_init) {
            if (!modules[i]->early_init(&error)) {
                print_error(error);
            }
        }
    }
    atexit(cleanup);
    for (i = 0; i < ARRAY_SIZE(modules); i++) {
        if (NULL != modules[i]->late_init) {
            if (!modules[i]->late_init(&error)) {
                print_error(error);
            }
        }
    }
    for (i = 0; i < ARRAY_SIZE(modules); i++) {
        if (NULL != modules[i]->register_commands) {
            modules[i]->register_commands(g);
        }
    }

    while (-1 != (c = getopt_long(argc, argv, optstr, long_options, NULL))) {
        switch (c) {
            case 'q':
                verbosity = WARN;
                break;
            case 'y':
                mainopts.yes = TRUE;
                break;
            case NOCONFIRM_OPT:
                mainopts.noconfirm = TRUE;
                break;
            default:
                usage();
        }
    }

    if (optind == argc) {
        int count;
        EditLine *el;
        HistEvent ev;
        History *hist;
        const char *line;
        editline_data_t client_data;

        puts(_("needs help? Type help!"));
        build_path_from_home(OVH_HISTORY_FILENAME, history_path, ARRAY_SIZE(history_path));
        hist = history_init();
        client_data.graph = g;
        client_data.tokenizer = tok_init(NULL);
        client_data.possibilities = dptrarray_new(NULL, NULL, NULL);
        history(hist, &ev, H_SETSIZE, 100);
        history(hist, &ev, H_SETUNIQUE, 1);
        history(hist, &ev, H_LOAD, history_path);
        el = el_init(*argv, stdin, stdout, stderr);
        el_set(el, EL_CLIENTDATA, &client_data);
        el_set(el, EL_PROMPT, prompt);
        el_set(el, EL_HIST, history, hist);
        el_set(el, EL_ADDFN, "ed-complete", "Complete argument", graph_complete);
        el_set(el, EL_BIND, "^I", "ed-complete", NULL);
        while (NULL != (line = el_gets(el, &count)) && count > 0/* && -1 != count*/) {
           char **args;
            int args_len;
            char *utf8_line;

            error = NULL; // reinitialize it
            if ('\n' == *line) { // TODO: better fix? empty line \s*\n count as one argument?
                continue;
            }
            if (convert_string_local_to_utf8(line, count, &utf8_line, NULL, &error)) {
                args_len = str_split(utf8_line, client_data.tokenizer, &args);
                ret = graph_run_command(g, args_len, (const char **) args, (const main_options_t *) &mainopts, &error);
                convert_string_free(line, &utf8_line);
#if WITHOUT_LIBEDIT_TOKENIZER
                free(args[0]);
                free(args);
#endif /* WITHOUT_LIBEDIT_TOKENIZER */
            }
            print_error(error);
            if (!HAS_FLAG(ret, CMD_FLAG_SKIP_HISTORY)) {
                history(hist, &ev, H_ENTER, line);
            }
        }
        history(hist, &ev, H_SAVE, history_path);
        history_end(hist);
        tok_end(client_data.tokenizer);
        dptrarray_destroy(client_data.possibilities);
        el_end(el);
        puts("");
    } else {
        char **utf8_argv;

        argc -= optind;
        argv += optind;
        convert_array_local_to_utf8(argc, argv, &utf8_argv, &error);
        ret = graph_run_command(g, argc, (const char **) utf8_argv, (const main_options_t *) &mainopts, &error);
        print_error(error);
        convert_array_free(argc, argv, utf8_argv);
    }

    return COMMAND_CODE(ret);
}
