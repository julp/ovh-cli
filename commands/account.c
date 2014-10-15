#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <libxml/parser.h>

#include "common.h"
#include "modules/api.h"
#include "struct/hashtable.h"

#include <limits.h>
#if !defined(MAXPATHLEN) && defined(PATH_MAX)
# define MAXPATHLEN PATH_MAX
#endif /* !MAXPATHLEN && PATH_MAX */

enum {
    DTOR_NOCALL,
    DTOR_CALL
};

typedef struct {
    char *account;
    char *password;
    time_t expires_at;
    char *consumer_key;
} account_t;

typedef struct {
    char path[MAXPATHLEN];
    HashTable *accounts;
    account_t *autosel;
    account_t *current;
} account_command_data_t;

static account_command_data_t *acd;

enum {
    SECONDS_DONE,
    MINUTES_DONE,
    HOURS_DONE,
    DAYS_DONE,
    NOTHING_DONE
};

#define duration_test(string, expected_result, expected_value) \
    do { \
        bool r; \
        time_t v; \
         \
        if (expected_result == (r = parse_duration(string, &v))) { \
            if (r && v != expected_value) { \
                printf("parse_duration('%s') failed (expected = %lld ; got = %lld)\n", string, (long long) expected_value, (long long) v); \
            } \
        } else { \
            printf("parse_duration('%s') failed (expected = %d ; got = %d)\n", string, expected_result, r); \
        } \
    } while (0);

// illimited | (?:\d *days?)? *(?:\d *hours?)? *(?:\d *minutes?)? *(?:\d *seconds?)?
static bool parse_duration(const char *duration, time_t *value)
{
    if ('\0' == *duration) {
        return FALSE;
    } else if (0 == strcasecmp(duration, "illimited")) {
        *value = 0;
        return TRUE;
    } else {
        long v;
        int part;
        const char *p;

        v = 0;
        *value = 0;
        part = NOTHING_DONE;
        for (p = duration; '\0' != *p; /*p++*/) {
            switch (*p) {
                case ' ':
                    ++p;
                    continue;
                    break;
                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':
                {
                    char *endptr;

                    if (0 != v) { // 2 consecutive numbers
                        return FALSE;
                    }
                    v = strtol(p, &endptr, 10);
                    p = endptr;
                    break;
                }
                case 'd':
                case 'D':
                    if (part <= DAYS_DONE || (0 != strncasecmp(p, "days", STR_LEN("days")) && 0 != strncasecmp(p, "day", STR_LEN("day"))) || 0 == v) {
                        return FALSE;
                    } else {
                        p += 0 == strncasecmp(p, "days", STR_LEN("days")) ? STR_LEN("days") : STR_LEN("day");
                        *value += v * 24 * 60 * 60;
                        part = DAYS_DONE;
                        v = 0;
                    }
                    break;
                case 'h':
                case 'H':
                    if (part <= HOURS_DONE || (0 != strncasecmp(p, "hours", STR_LEN("hours")) && 0 != strncasecmp(p, "hour", STR_LEN("hour"))) || 0 == v) {
                        return FALSE;
                    } else {
                        p += 0 == strncasecmp(p, "hours", STR_LEN("hours")) ? STR_LEN("hours") : STR_LEN("hour");
                        *value += v * 60 * 60;
                        part = HOURS_DONE;
                        v = 0;
                    }
                    break;
                case 'm':
                case 'M':
                    if (part <= MINUTES_DONE || (0 != strncasecmp(p, "minutes", STR_LEN("minutes")) && 0 != strncasecmp(p, "minute", STR_LEN("minute"))) || 0 == v) {
                        return FALSE;
                    } else {
                        p += 0 == strncasecmp(p, "minutes", STR_LEN("minutes")) ? STR_LEN("minutes") : STR_LEN("minute");
                        *value += v * 60;
                        part = MINUTES_DONE;
                        v = 0;
                    }
                    break;
                case 's':
                case 'S':
                    if (part <= SECONDS_DONE || (0 != strncasecmp(p, "seconds", STR_LEN("seconds")) && 0 != strncasecmp(p, "second", STR_LEN("second"))) || 0 == v) {
                        return FALSE;
                    } else {
                        p += 0 == strncasecmp(p, "seconds", STR_LEN("seconds")) ? STR_LEN("seconds") : STR_LEN("second");
                        *value += v;
                        part = SECONDS_DONE;
                        v = 0;
                    }
                    break;
                default:
                    return FALSE;
            }
        }
        return 0 != *value && 0 == v; // nothing really parsed (eg: 0 days) && no pending number (eg: 3 days 12)
    }
}

const char *account_current(void)
{
    if (NULL == acd->current) {
        return "(none)";
    } else {
        return acd->current->account;
    }
}

const char *account_key(void)
{
    assert(NULL != acd->current);

    return acd->current->consumer_key;
}

static int account_save(void)
{
    Iterator it;
    xmlDocPtr doc;
    xmlNodePtr root;

    doc = xmlNewDoc(BAD_CAST "1.0");
    root = xmlNewNode(NULL, BAD_CAST "ovh");
    xmlDocSetRootElement(doc, root);
    hashtable_to_iterator(&it, acd->accounts);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        xmlNodePtr node;
        account_t *account;

        account = (account_t *) iterator_current(&it, NULL);
        if (NULL == (node = xmlNewNode(NULL, BAD_CAST "account"))) {
            return 0;
        }
        if (NULL == xmlSetProp(node, BAD_CAST "account", BAD_CAST account->account)) {
            xmlFreeNode(node);
            return 0;
        }
        if (NULL == xmlSetProp(node, BAD_CAST "password", BAD_CAST account->password)) {
            xmlFreeNode(node);
            return 0;
        }
        if (account == acd->autosel) {
            if (NULL == xmlSetProp(node, BAD_CAST "default", BAD_CAST "1")) {
                xmlFreeNode(node);
                return 0;
            }
        }
        if (NULL != account->consumer_key) {
            char buffer[512];

            if (NULL == xmlSetProp(node, BAD_CAST "consumer_key", BAD_CAST account->consumer_key)) {
                xmlFreeNode(node);
                return 0;
            }
            if (snprintf(buffer, ARRAY_SIZE(buffer), "%lld", (long long) account->expires_at) >= ARRAY_SIZE(buffer)) {
                return 0;
            }
            if (NULL == xmlSetProp(node, BAD_CAST "expires_at", BAD_CAST buffer)) {
                xmlFreeNode(node);
                return 0;
            }
        }
        if (NULL == xmlAddChild(root, node)) {
            xmlFreeNode(node);
            return 0;
        }
    }
    iterator_close(&it);
    if (-1 == xmlSaveFormatFile(acd->path, doc, 1)) {
        fprintf(stderr, "Could not save file into '%s'", acd->path);
        return 0;
    }

    return 1;
}

static int account_load(void)
{
    xmlDocPtr doc;
    struct stat st;
    xmlNodePtr root, n;

    if ((-1 != (stat(acd->path, &st)))/* && S_ISREG(st.st_mode)*/) {
        xmlInitParser();
        xmlKeepBlanksDefault(0);
        if (NULL == (doc = xmlParseFile(acd->path))) {
            //
        }
        if (NULL == (root = xmlDocGetRootElement(doc))) {
            //
        }
        for (n = root->children; n != NULL; n = n->next) {
            account_t *a;

            if (0 != xmlStrcmp(n->name, BAD_CAST "account")) {
                continue;
            }
            a = mem_new(*a);
            a->account = strdup((char *) xmlGetProp(n, BAD_CAST "account"));
            a->password = strdup((char *) xmlGetProp(n, BAD_CAST "password"));
            if (NULL != xmlHasProp(n, BAD_CAST "consumer_key")) {
                a->consumer_key = strdup((char *) xmlGetProp(n, BAD_CAST "consumer_key"));
                a->expires_at = (time_t) atoi((char *) xmlGetProp(n, BAD_CAST "expires_at"));
            }
            hashtable_put(acd->accounts, a->account, a, NULL);
            if (xmlHasProp(n, BAD_CAST "default")) {
                acd->current = acd->autosel = a;
            }
        }
        if (hashtable_size(acd->accounts) > 0) {
            if (NULL == acd->current) {
                acd->current = (account_t *) hashtable_first(acd->accounts);
            }
            if (NULL == acd->current->consumer_key) {
                acd->current->consumer_key = request_consumer_key(acd->current->account, acd->current->password);
                acd->current->expires_at = time(NULL) + 24 * 60 * 60; // TODO
                account_save();
            }
        }
    }

    return 1;
}

static void account_account_dtor(void *data)
{
    account_t *account;

    account = (account_t *) data;
    free(account->account);
    free(account->password);
    free(account->consumer_key);
    free(account);
}

static bool account_ctor(void)
{
    char *home;
    char buffer[MAXPATHLEN];

    acd = mem_new(*acd);
    acd->autosel = acd->current = NULL;
    acd->accounts = hashtable_ascii_cs_new(NULL, NULL, account_account_dtor);
    buffer[0] = '\0';
    if (NULL == (home = getenv("HOME"))) {
# ifdef _MSC_VER
#  ifndef CSIDL_PROFILE
#   define CSIDL_PROFILE 40
#  endif /* CSIDL_PROFILE */
        if (NULL == (home = getenv("USERPROFILE"))) {
            HRESULT hr;
            LPITEMIDLIST pidl = NULL;

            hr = SHGetSpecialFolderLocation(NULL, CSIDL_PROFILE, &pidl);
            if (S_OK == hr) {
                SHGetPathFromIDList(pidl, buffer);
                home = buffer;
                CoTaskMemFree(pidl);
            }
        }
# else
        struct passwd *pwd;

        if (NULL != (pwd = getpwuid(getuid()))) {
            home = pwd->pw_dir;
        }
# endif /* _MSC_VER */
    }

    if (NULL != home) {
        if (snprintf(acd->path, ARRAY_SIZE(acd->path), "%s%c%s", home, DIRECTORY_SEPARATOR, OVH_SHELL_CONFIG_FILE) >= (int) ARRAY_SIZE(acd->path)) {
            return FALSE;
        }
    }
    account_load();
#if 1
    duration_test("3 day 1 days", FALSE, 0);
    duration_test("3 seconds 1 hour", FALSE, 0);
    duration_test("12 11 hours", FALSE, 0);
    duration_test("3 days 1", FALSE, 0);
    duration_test("3 days 1 second", TRUE, 3 * 24 * 60 * 60 + 1);
#endif

    return TRUE;
}

static void account_dtor(void)
{
    hashtable_destroy(acd->accounts);
    free(acd);
}

static int account_list(int UNUSED(argc), const char **UNUSED(argv))
{
    Iterator it;

    hashtable_to_iterator(&it, acd->accounts);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        account_t *account;

        account = (account_t *) iterator_current(&it, NULL);
        if (account == acd->current) {
            printf("=>");
        } else {
            printf("  ");
        }
        if (account == acd->autosel) {
            printf("* ");
        } else {
            printf(" ");
        }
        // TODO: expiration date of CK (if any) ?
        printf("%s", account->account);
        printf("\n");
    }
    iterator_close(&it);

    return 1;
}

/**
 * account add [nickname] [password] ([consumer key] expires in|at [date])
 *
 * NOTE:
 * - in order to not record password, use an empty string (with "")
 * - default expiration of consumer key is 0 (unlimited)
 **/
static int account_add(int argc, const char **argv)
{
    hash_t h;
    time_t expires_at;
    const char *account, *password, *consumer_key;

    consumer_key = NULL;
    expires_at = (time_t) 0;
    switch (argc) {
        case 6:
            if (0 != strcmp(argv[3], "expires")) {
                return 0;
            }
            if (0 == strcmp(argv[4], "in")) {
                if (!parse_duration(argv[5], &expires_at)) {
                    return 0;
                }
            } else if (0 == strcmp(argv[4], "at")) {
                char *endptr;
                struct tm ltm = { 0 };

                endptr = strptime(argv[5], "%c", &ltm);
                if (NULL == endptr || '\0' != *endptr) {
                    return 0;
                }
                expires_at = mktime(&ltm);
            } else {
                return 0;
            }
            /* no break */
        case 3:
            consumer_key = strdup(argv[2]);
            /* no break */
        case 2:
            password = argv[1];
            account = argv[0];
            break;
        default:
            return 0;
    }
    h = hashtable_hash(acd->accounts, account);
#if 0
    if (hashtable_quick_contains(acd->accounts, h, account)) {
        fprintf(stderr, "An account named '%s' already exists\n", account);
    } else {
#else
    {
#endif
        account_t *a;

        a = mem_new(*a);
        a->account = strdup(account);
        a->password = strdup(password);
        a->consumer_key = consumer_key;
        a->expires_at = expires_at;
//         hashtable_quick_put_ex(acd->accounts, HT_PUT_ON_DUP_KEY_PRESERVE, h, (void *) account, a, NULL);
        hashtable_quick_put_ex(acd->accounts, 0, h, (void *) account, a, NULL);
        account_save();
    }

    return 1;
}

static int account_default_set(int argc, const char **argv)
{
    int ret;
    void *ptr;
    const char *account;

    assert(1 == argc);
    account = argv[0];
    if ((ret = hashtable_get(acd->accounts, account, &ptr))) {
        acd->autosel = ptr;
        account_save();
    } else {
        fprintf(stderr, "Any account named '%s' was found\n", account);
    }

    return ret;
}

static int account_delete(int argc, const char **argv)
{
    int ret;
    const char *account;

    assert(1 == argc);
    account = argv[0];
    if ((ret = hashtable_delete(acd->accounts, account, DTOR_CALL))) {
        account_save();
    } else {
        fprintf(stderr, "Any account named '%s' was found\n", account);
    }

    return ret;
}

static int account_switch(int argc, const char **argv)
{
    int ret;
    account_t *ptr;
    const char *account;

    assert(1 == argc);
    account = argv[0];
    if ((ret = hashtable_get(acd->accounts, account, (void **) &ptr))) {
        acd->current = ptr;
        if (NULL == acd->current->consumer_key) {
            acd->current->consumer_key = request_consumer_key(acd->current->account, acd->current->password);
            acd->current->expires_at = time(NULL) + 24 * 60 * 60; // TODO
            account_save();
        }
    } else {
        fprintf(stderr, "Any account named '%s' was found\n", account);
    }

    return ret;
}

static const command_t account_commands[] = {
    { "list", 0, account_list },
    { "add", -1, account_add },
    { "delete", 1, account_delete },
    { "switch", 1, account_switch },
    { "default", 1, account_default_set },
    { NULL }
};

DECLARE_MODULE(account) = {
    "account",
    account_ctor,
    account_dtor,
    account_commands
};
