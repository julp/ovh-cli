#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#include <string.h>
#include "common.h"
#include "command.h"
#include "sqlite.h"

#include <limits.h>
#if !defined(MAXPATHLEN) && defined(PATH_MAX)
# define MAXPATHLEN PATH_MAX
#endif /* !MAXPATHLEN && PATH_MAX */

typedef enum {
    SQLITE_TYPE_BOOL,
    SQLITE_TYPE_INT,
    SQLITE_TYPE_STRING
} sqlite_bind_type_t;

typedef struct {
    sqlite_bind_type_t type;
    void *ptr;
} sqlite_statement_bind_t;

typedef struct {
    int ret;
    size_t output_binds_count;
    sqlite_statement_bind_t *output_binds;
} sqlite_statement_state_t;

static sqlite3 *db;
static int user_version;
static char db_path[MAXPATHLEN];

enum {
    // account
    STMT_GET_USER_VERSION,
    STMT_SET_USER_VERSION,
    // count
    STMT_COUNT
};

static const char *statements[STMT_COUNT] = {
    [ STMT_GET_USER_VERSION ] = "PRAGMA user_version",
    [ STMT_SET_USER_VERSION ] = "PRAGMA user_version = " STRINGIFY_EXPANDED(OVH_CLI_VERSION_NUMBER), // PRAGMA doesn't permit parameter
};

static sqlite3_stmt *prepared[STMT_COUNT];

static bool statement_iterator_is_valid(const void *UNUSED(collection), void **state)
{
    sqlite_statement_state_t *sss;

    assert(NULL != state);
    sss = *(sqlite_statement_state_t **) state;

    return SQLITE_ROW == sss->ret;
}

static void statement_iterator_first(const void *collection, void **state)
{
    sqlite3_stmt *stmt;
    sqlite_statement_state_t *sss;

    assert(NULL != state);
    assert(NULL != collection);

    stmt = (sqlite3_stmt *) collection;
    sss = *(sqlite_statement_state_t **) state;
    sss->ret = sqlite3_step(stmt);
}

static void statement_iterator_current(const void *collection, void **state, void **UNUSED(value), void **UNUSED(key))
{
    sqlite3_stmt *stmt;
    sqlite_statement_state_t *sss;

    assert(NULL != state);
    assert(NULL != collection);

    stmt = (sqlite3_stmt *) collection;
    sss = *(sqlite_statement_state_t **) state;
    if (0 != sss->output_binds_count) {
        size_t i;

        for (i = 0; i < sss->output_binds_count; i++) {
            switch (sss->output_binds[i].type) {
                case SQLITE_TYPE_BOOL:
                    *((bool *) sss->output_binds[i].ptr) = /*!!*/sqlite3_column_int(stmt, i);
                    break;
                case SQLITE_TYPE_INT:
                    *((int *) sss->output_binds[i].ptr) = sqlite3_column_int(stmt, i);
                    break;
                case SQLITE_TYPE_STRING:
                {
                    const unsigned char *v;

                    if (NULL == (v = sqlite3_column_text(stmt, i))) {
                        *((char **) sss->output_binds[i].ptr) = NULL;
                    } else {
                        *((char **) sss->output_binds[i].ptr) = strdup((char *) v);
                    }
                    break;
                }
                default:
                    assert(FALSE);
                    break;
            }
        }
    }
}

static void statement_iterator_next(const void *collection, void **state)
{
    sqlite3_stmt *stmt;
    sqlite_statement_state_t *sss;

    assert(NULL != state);
    assert(NULL != collection);

    stmt = (sqlite3_stmt *) collection;
    sss = *(sqlite_statement_state_t **) state;
    sss->ret = sqlite3_step(stmt);
}

static void statement_iterator_close(void *state)
{
    sqlite_statement_state_t *sss;

    assert(NULL != state);
//     assert(NULL != collection);

    sss = (sqlite_statement_state_t *) state;
    if (0 != sss->output_binds_count) {
        free(sss->output_binds);
    }
//     sqlite3_reset((sqlite3_stmt *) collection);
    free(sss);
}

bool statement_fetch(sqlite3_stmt *stmt, error_t **error, ...)
{
    bool ret;
    va_list ap;

    ret = FALSE;
    va_start(ap, error);
    switch (sqlite3_step(stmt)) {
        case SQLITE_ROW:
        {
            int i, colcount;

            colcount = sqlite3_column_count(stmt);
            for (i = 0; i < colcount; i++) {
                switch (sqlite3_column_type(stmt, i)) {
                    case SQLITE_INTEGER:
                    {
                        int *v;

                        v = va_arg(ap, int *);
                        *v = sqlite3_column_int(stmt, i);
                        break;
                    }
                    case SQLITE_FLOAT:
                        // ?
                        break;
                    case SQLITE_TEXT:
                    {
                        char **uv;
                        const unsigned char *sv;

                        uv = va_arg(ap, char **);
                        sv = sqlite3_column_text(stmt, i);
                        if (NULL == sv) {
                            *uv = NULL;
                        } else {
                            *uv = strdup((char *) sv);
                        }
                        break;
                    }
                    case SQLITE_BLOB:
                        break;
                    case SQLITE_NULL:
                        // ?
                        break;
                    default:
                        assert(FALSE);
                        break;
                }
            }
            ret = TRUE;
            break;
        }
        case SQLITE_DONE:
            break;
        default:
            error_set(error, WARN, _(""));
            break;
    }
    va_end(ap);

    return ret;
}

void statement_to_iterator(Iterator *it, sqlite3_stmt *stmt, const char *outbinds, ...)
{
    va_list ap;
    sqlite_statement_state_t *sss;

    va_start(ap, outbinds);
    sss = mem_new(*sss);
    sss->output_binds_count = strlen(outbinds);
    if (0 == sss->output_binds_count) {
        sss->output_binds = NULL;
    } else {
        size_t i;

        sss->output_binds = mem_new_n(*sss->output_binds, sss->output_binds_count);
        for (i = 0; i < sss->output_binds_count; i++) {
            switch (outbinds[i]) {
                case 'b':
                    sss->output_binds[i].type = SQLITE_TYPE_BOOL;
                    break;
                case 'i':
                    sss->output_binds[i].type = SQLITE_TYPE_INT;
                    break;
                case 's':
                    sss->output_binds[i].type = SQLITE_TYPE_STRING;
                    break;
                default:
                    assert(FALSE);
                    break;
            }
            sss->output_binds[i].ptr = va_arg(ap, void *);
        }
    }
    va_end(ap);
    iterator_init(
        it, stmt, sss,
        statement_iterator_first, NULL,
        statement_iterator_current,
        statement_iterator_next, NULL,
        statement_iterator_is_valid,
        statement_iterator_close
    );
}

bool statement_batched_prepare(const char **statements, sqlite3_stmt **preprepared, size_t count)
{
    size_t i;
    bool ret;

    ret = TRUE;
    for (i = 0; ret && i < count; i++) {
        ret &= SQLITE_OK == sqlite3_prepare_v2(db, statements[i], -1, &preprepared[i], NULL);
    }
    if (!ret) {
        while (--i != 0) {
            sqlite3_finalize(preprepared[i]);
        }
    }

    return ret;
}

void statement_batched_finalize(sqlite3_stmt **preprepared, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        sqlite3_finalize(preprepared[i]);
    }
}

int sqlite_last_insert_id(void)
{
    return sqlite3_last_insert_rowid(db);
}

int sqlite_affected_rows(void)
{
    return sqlite3_changes(db);
}

void create_or_migrate(const char *table_name, const char *create_stmt, sqlite_migration_t *migrations, size_t migrations_count/*, error_t **error*/)
{
    int ret;
    size_t i;
    sqlite3_stmt *stmt;
    char buffer[2048], *errmsg;

    ret = snprintf(buffer, ARRAY_SIZE(buffer), "PRAGMA table_info(\"%s\")", table_name);
    if (ret < 0 || ((size_t) ret) >= ARRAY_SIZE(buffer)) {
//         error_set(error, FATAL, _("can't create table %s: %s"), table_name, _("buffer overflow"));
        debug(_("can't create table %s: %s"), table_name, _("buffer overflow"));
        return;
    }
    if (SQLITE_OK == (ret = sqlite3_prepare_v2(db, buffer, -1, &stmt, NULL))) {
        switch (sqlite3_step(stmt)) {
            case SQLITE_DONE:
                ret = sqlite3_exec(db, create_stmt, NULL, NULL, &errmsg);
                break;
            case SQLITE_ROW:
                for (i = 0; SQLITE_OK == ret && i < migrations_count; i++) {
                    if (OVH_CLI_VERSION_NUMBER > migrations[i].version) {
                        ret = sqlite3_exec(db, create_stmt, NULL, NULL, &errmsg);
                    }
                }
                break;
            default:
                // error
                break;
        }
        sqlite3_reset(stmt);
        sqlite3_finalize(stmt);
    }
    if (SQLITE_OK != ret) {
//         error_set(error, FATAL, "%s", errmsg);
        debug("%s", errmsg);
        sqlite3_free(errmsg);
    }
}

int64_t statement_execute(sqlite3_stmt *stmt, /*error_t **error, */const char *inbinds, ...)
{
    va_list ap;
    const char *p;
#if 0
    int no, count;

    sqlite3_reset(stmt);
    for (no = 1, count = sqlite3_bind_parameter_count(stmt); no <= count; no++) {
        sqlite3_bind_null(stmt, no);
    }
#else
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
#endif
    va_start(ap, inbinds);
    for (p = inbinds; '\0' != *p; p++) {
        switch (*inbinds) {
            case 'n':
                va_arg(ap, void *);
                sqlite3_bind_null(stmt, p - inbinds);
                break;
            case 'd':
            {
                double v;

                v = va_arg(ap, double);
                sqlite3_bind_double(stmt, p - inbinds, v);
                break;
            }
            case 'b':
            case 'i':
            {
                int v;

                v = va_arg(ap, int);
                sqlite3_bind_int(stmt, p - inbinds, v);
                break;
            }
            case 's':
            {
                char *v;

                v = va_arg(ap, char *);
                sqlite3_bind_text(stmt, p - inbinds, v, -1, SQLITE_TRANSIENT);
                break;
            }
            default:
                assert(FALSE);
                break;
        }
    }
    va_end(ap);
    sqlite3_step(stmt); // pas compatible avec SELECT

    return sqlite3_changes(db);
    return sqlite3_last_insert_rowid(db);
}

static bool sqlite_early_ctor(void)
{
    int ret;
    char *home;

    *db_path = '\0';
    if (NULL == (home = getenv("HOME"))) {
#ifdef _MSC_VER
# ifndef CSIDL_PROFILE
#  define CSIDL_PROFILE 40
# endif /* CSIDL_PROFILE */
        if (NULL == (home = getenv("USERPROFILE"))) {
            HRESULT hr;
            LPITEMIDLIST pidl = NULL;

            if (S_OK == (hr = SHGetSpecialFolderLocation(NULL, CSIDL_PROFILE, &pidl)));
                SHGetPathFromIDList(pidl, db_path);
                home = db_path;
                CoTaskMemFree(pidl);
            }
        }
#else
        struct passwd *pwd;

        if (NULL != (pwd = getpwuid(getuid()))) {
            home = pwd->pw_dir;
        }
#endif /* _MSC_VER */
    }
    if (NULL != home) {
        ret = snprintf(db_path, ARRAY_SIZE(db_path), "%s%c%s", home, DIRECTORY_SEPARATOR, OVH_DB_FILENAME);
        if (ret < 0 || ((size_t) ret) >= ARRAY_SIZE(db_path)) {
            return FALSE;
        }
    }
    if ('\0' == *db_path) {
        return FALSE;
    }
    // open database
    if (SQLITE_OK != (ret = sqlite3_open(db_path, &db))) {
//         error_set(error, FATAL, _("can't open sqlite database %s: %s"), db_path, sqlite3_errmsg(db));
        printf(_("can't open sqlite database %s: %s"), db_path, sqlite3_errmsg(db));
        return FALSE;
    }
    // preprepare own statement
    statement_batched_prepare(statements, prepared, STMT_COUNT);
    // fetch user_version
    if (SQLITE_ROW != sqlite3_step(prepared[STMT_GET_USER_VERSION])) {
//         error_set(error, FATAL, _("can't retrieve database version: %s"), db_path, sqlite3_errmsg(db));
        printf(_("can't retrieve database version: %s"), sqlite3_errmsg(db));
        return FALSE;
    }
    user_version = sqlite3_column_int(prepared[STMT_GET_USER_VERSION], 0);

    return TRUE;
}

static bool sqlite_late_ctor(error_t **error)
{
    if (OVH_CLI_VERSION_NUMBER > user_version) {
//         sqlite3_bind_int(prepared[STMT_SET_USER_VERSION], 1, OVH_CLI_VERSION_NUMBER);
        assert(SQLITE_DONE == sqlite3_step(prepared[STMT_SET_USER_VERSION]));
        sqlite3_reset(prepared[STMT_SET_USER_VERSION]);
    }

    return TRUE;
}

static void sqlite_dtor(void)
{
    statement_batched_finalize(prepared, STMT_COUNT);
    sqlite3_close(db);
}

DECLARE_MODULE(sqlite) = {
    "sqlite",
    NULL,
    NULL,
    sqlite_early_ctor,
    sqlite_late_ctor,
    sqlite_dtor
};
