#include <sys/types.h>
#include <sys/stat.h>
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

typedef struct {
    sqlite_bind_type_t type;
    void *ptr;
} sqlite_statement_bind_t;

typedef enum {
    SSST_MODEL_BASED,
    SSST_INDIVIDUAL_BINDS,
} sqlite_statement_state_type_t;

typedef struct {
    /**
     * Keep result of last sqlite3_step call
     */
    int ret;
    sqlite_statement_state_type_t type;
    union {
        struct {
            size_t output_binds_count;
            sqlite_statement_bind_t *output_binds;
        };
        struct {
            char *ptr;
            model_t model;
        };
    };
} sqlite_statement_state_t;

static sqlite3 *db;
static int user_version;
static char db_path[MAXPATHLEN];

/**
 * TODO:
 * - merge sqlite_statement_state_t into sqlite_statement_t ?
 */

/**
 * NOTE:
 * - for sqlite3_column_* functions, the first column is 0
 * - in the other hand, for sqlite3_bind_* functions, the first parameter is 1
 */

enum {
    // account
    STMT_GET_USER_VERSION,
    STMT_SET_USER_VERSION,
    // count
    STMT_COUNT
};

static sqlite_statement_t statements[STMT_COUNT] = {
    [ STMT_GET_USER_VERSION ] = DECL_STMT("PRAGMA user_version", "", ""),
    [ STMT_SET_USER_VERSION ] = DECL_STMT("PRAGMA user_version = " STRINGIFY_EXPANDED(OVH_CLI_VERSION_NUMBER), "", ""), // PRAGMA doesn't permit parameter
};

static bool statement_iterator_is_valid(const void *UNUSED(collection), void **state)
{
    sqlite_statement_state_t *sss;

    assert(NULL != state);
    sss = *(sqlite_statement_state_t **) state;

    return SQLITE_ROW == sss->ret;
}

static void statement_iterator_first(const void *collection, void **state)
{
    sqlite_statement_t *stmt;
    sqlite_statement_state_t *sss;

    assert(NULL != state);
    assert(NULL != collection);

    stmt = (sqlite_statement_t *) collection;
    sss = *(sqlite_statement_state_t **) state;
    sss->ret = sqlite3_step(stmt->prepared);
}

static void statement_iterator_next(const void *collection, void **state)
{
    sqlite_statement_t *stmt;
    sqlite_statement_state_t *sss;

    assert(NULL != state);
    assert(NULL != collection);

    stmt = (sqlite_statement_t *) collection;
    sss = *(sqlite_statement_state_t **) state;
    sss->ret = sqlite3_step(stmt->prepared);
}

static void statement_iterator_current(const void *collection, void **state, void **UNUSED(value), void **UNUSED(key))
{
    sqlite_statement_t *stmt;
    sqlite_statement_state_t *sss;

    assert(NULL != state);
    assert(NULL != collection);

    stmt = (sqlite_statement_t *) collection;
    sss = *(sqlite_statement_state_t **) state;
    if (0 != sss->output_binds_count) {
        size_t i;

        for (i = 0; i < sss->output_binds_count; i++) {
            switch (sss->output_binds[i].type) {
                case SQLITE_TYPE_BOOL:
                    *((bool *) sss->output_binds[i].ptr) = /*!!*/sqlite3_column_int64(stmt->prepared, i);
                    break;
                case SQLITE_TYPE_INT:
                    *((int64_t *) sss->output_binds[i].ptr) = sqlite3_column_int64(stmt->prepared, i);
                    break;
                case SQLITE_TYPE_STRING:
                {
                    const unsigned char *v;

                    if (NULL == (v = sqlite3_column_text(stmt->prepared, i))) {
                        *((char **) sss->output_binds[i].ptr) = NULL;
                    } else {
                        *((char **) sss->output_binds[i].ptr) = strdup((char *) v);
                    }
                    break;
                }
                case SQLITE_TYPE_IGNORE:
                    // NOP
                    break;
                default:
                    assert(FALSE);
                    break;
            }
        }
    }
}

static void statement_iterator_close(void *state)
{
    sqlite_statement_state_t *sss;

    assert(NULL != state);

    sss = (sqlite_statement_state_t *) state;
    if (0 != sss->output_binds_count) {
        free(sss->output_binds);
    }
    free(sss);
}

/**
 * Initializes the given iterator to loop on the result set associated
 * to the given prepared statement. The value of each column for each
 * row are dup to the given address.
 *
 * @param it the iterator to set
 * @param stmt the result set of the statement on which to iterate
 * @param ... a list of address where value are copied
 *
 * @note for strings, the value - if not NULL - is strdup. You have to
 * free it when you don't need it anymore.
 */
void statement_to_iterator(Iterator *it, sqlite_statement_t *stmt, ...)
{
    va_list ap;
    sqlite_statement_state_t *sss;

    va_start(ap, stmt);
    sss = mem_new(*sss);
    sss->type = SSST_INDIVIDUAL_BINDS;
    sss->output_binds_count = strlen(stmt->outbinds);
    if (0 == sss->output_binds_count) {
        sss->output_binds = NULL;
    } else {
        size_t i;

        sss->output_binds = mem_new_n(*sss->output_binds, sss->output_binds_count);
        for (i = 0; i < sss->output_binds_count; i++) {
            switch (stmt->outbinds[i]) {
                case 'b':
                    sss->output_binds[i].type = SQLITE_TYPE_BOOL;
                    break;
                case 'i':
                    sss->output_binds[i].type = SQLITE_TYPE_INT;
                    break;
                case 's':
                    sss->output_binds[i].type = SQLITE_TYPE_STRING;
                    break;
                case ' ':
                case '-':
                    // ignore
                    sss->output_binds[i].type = SQLITE_TYPE_IGNORE;
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

static void _statement_model_set_output_bind(sqlite_statement_t *stmt, model_t model, char *ptr)
{
    int i, l;

    // TODO: use an hashtable ?
    for (i = 0, l = sqlite3_column_count(stmt->prepared); i < l; i++) {
        bool match;
        const model_field_t *f;
        const char *column_name;

        column_name = sqlite3_column_name(stmt->prepared, i);
        for (match = FALSE, f = model.fields; !match && NULL != f->column_name; f++) {
            if (0 == strcmp(f->column_name, column_name)) {
                switch (f->type) {
                    case MODEL_TYPE_BOOL:
                        *((bool *) (ptr + f->offset)) = /*!!*/sqlite3_column_int(stmt->prepared, i);
                        break;
                    case MODEL_TYPE_INT:
                    case MODEL_TYPE_ENUM:
                    case MODEL_TYPE_DATE:
                    case MODEL_TYPE_DATETIME:
                        *((int64_t *) (ptr + f->offset)) = sqlite3_column_int64(stmt->prepared, i);
                        break;
                    case MODEL_TYPE_STRING:
                    {
                        char *uv;
                        const unsigned char *sv;

                        sv = sqlite3_column_text(stmt->prepared, i);
                        if (NULL == sv) {
                            uv = NULL;
                        } else {
                            uv = strdup((char *) sv);
                        }
                        *((char **) (ptr + f->offset)) = uv;
                        break;
                    }
                    default:
                        assert(FALSE);
                        break;
                }
                match = TRUE;
            }
        }
        if (!match) {
            debug(_("Column '%s' unmapped for output (query: %s)"), column_name, sqlite3_sql(stmt->prepared));
//             assert(FALSE);
        }
    }
}

static void statement_model_iterator_current(const void *collection, void **state, void **UNUSED(value), void **UNUSED(key))
{
    sqlite_statement_t *stmt;
    sqlite_statement_state_t *sss;

    assert(NULL != state);
    assert(NULL != collection);

    stmt = (sqlite_statement_t *) collection;
    sss = *(sqlite_statement_state_t **) state;
    _statement_model_set_output_bind(stmt, sss->model, sss->ptr);
}

void statement_model_to_iterator(Iterator *it, sqlite_statement_t *stmt, model_t model, char *ptr)
{
    sqlite_statement_state_t *sss;

    sss = mem_new(*sss);
    sss->ptr = ptr;
    sss->model = model;
    sss->type = SSST_MODEL_BASED;
    iterator_init(
        it, stmt, sss,
        statement_iterator_first, NULL,
        statement_model_iterator_current,
        statement_iterator_next, NULL,
        statement_iterator_is_valid,
        free
    );
}

/**
 * Prepares an array of *count* sqlite_statement_t
 *
 * @param statements a group of statements to pre-prepare
 * @param count the number of *statements*
 * @param error the error to populate when failing
 *
 * @return FALSE and set *error* if an error occur
 */
bool statement_batched_prepare(sqlite_statement_t *statements, size_t count, error_t **error)
{
    size_t i;
    bool ret;

    ret = TRUE;
    for (i = 0; ret && i < count; i++) {
        ret &= SQLITE_OK == sqlite3_prepare_v2(db, statements[i].statement, -1, &statements[i].prepared, NULL);
    }
    if (!ret) {
        --i; // return on the statement which actually failed
        error_set(error, FATAL, _("%s for %s"), sqlite3_errmsg(db), statements[i]);
        while (i-- != 0) { // finalize the initialized ones
            sqlite3_finalize(statements[i].prepared);
            statements[i].prepared = NULL;
        }
    }

    return ret;
}

/**
 * Frees  an array of *count* sqlite_statement_t previouly preprepared
 * with statement_batched_prepare.
 *
 * @param statements a group of statements to free
 * @param count the number of *statements*
 */
void statement_batched_finalize(sqlite_statement_t *statements, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        if (NULL != statements[i].prepared) {
            sqlite3_finalize(statements[i].prepared);
        }
    }
}

/**
 * Returns the value of the sequence for the last inserted row
 *
 * @return the ROWID of the last inserted row
 */
int sqlite_last_insert_id(void)
{
    return sqlite3_last_insert_rowid(db);
}

/**
 * Returns the number of rows affected (ie inserted, modified or deleted)
 * by the last query
 *
 * @return the number of rows affected by the query
 */
int sqlite_affected_rows(void)
{
    return sqlite3_changes(db);
}

/**
 * Runs SQL migrations if needed to create new tables or update schemas
 * of existant ones.
 *
 * @param table_name the name of the table to create or update
 * @param create_stmt the complete statement (CREATE TABLE) to create it
 *   if it doesn't already exist
 * @param migrations an array of *migrations_count* migrations to run if
 *   their *version* is lower than the current ovh-cli version number.
 *   It can safely be set to NULL when *migrations_count* = 0.
 * @param migrations_count the number of migrations
 * @param error the error to populate when failing
 *
 * @return FALSE and set *error* if an error occur
 */
bool create_or_migrate(const char *table_name, const char *create_stmt, sqlite_migration_t *migrations, size_t migrations_count, error_t **error)
{
    int ret;
    size_t i;
    sqlite3_stmt *stmt;
    char buffer[2048], *errmsg;

    ret = snprintf(buffer, ARRAY_SIZE(buffer), "PRAGMA table_info(\"%s\")", table_name);
    if (ret < 0 || ((size_t) ret) >= ARRAY_SIZE(buffer)) {
        error_set(error, FATAL, _("can't create table %s: %s"), table_name, _("buffer overflow"));
        return FALSE;
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
                // NOP: error is handled below
                break;
        }
        sqlite3_reset(stmt);
        sqlite3_finalize(stmt);
    }
    if (SQLITE_OK != ret) {
        error_set(error, FATAL, "%s", errmsg);
        sqlite3_free(errmsg);
    }

    return SQLITE_OK == ret;
}

/**
 * Associates (input) values to parameters (binds) to the statement before its execution.
 * It only affects values to parameters, the query is not executed.
 *
 * @param stmt the prepared statement
 * @param nulls values of the input parameters to override with NULL (the SQL "value")
 *   Set it to NULL to ignore this parameter (same as if *nulls* was filled with FALSE)
 * @param ... list of values to bind (in the same order as the statement)
 */
void statement_bind(sqlite_statement_t *stmt, const bool *nulls, ...)
{
    va_list ap;
    const char *p;
#if 0
    int no, count;

    sqlite3_reset(stmt->prepared);
    for (no = 1, count = sqlite3_bind_parameter_count(stmt->prepared); no <= count; no++) {
        sqlite3_bind_null(stmt->prepared, no);
    }
#else
    sqlite3_reset(stmt->prepared);
    sqlite3_clear_bindings(stmt->prepared);
#endif
    assert(strlen(stmt->inbinds) == ((size_t) sqlite3_bind_parameter_count(stmt->prepared)));
    va_start(ap, nulls);
    for (p = stmt->inbinds; '\0' != *p; p++) {
        if (NULL == nulls || !nulls[p - stmt->inbinds]) {
            switch (*p) {
                case 'n':
                    va_arg(ap, void *);
                    sqlite3_bind_null(stmt->prepared, p - stmt->inbinds + 1);
                    break;
                case 'd':
                {
                    double v;

                    v = va_arg(ap, double);
                    sqlite3_bind_double(stmt->prepared, p - stmt->inbinds + 1, v);
                    break;
                }
                case 'b':
                {
                    bool v;

                    v = va_arg(ap, bool);
                    sqlite3_bind_int(stmt->prepared, p - stmt->inbinds + 1, v);
                    break;
                }
                case 'i':
                {
                    int64_t v;

                    v = va_arg(ap, int64_t);
                    sqlite3_bind_int64(stmt->prepared, p - stmt->inbinds + 1, v);
                    break;
                }
                case 's':
                {
                    char *v;

                    v = va_arg(ap, char *);
                    sqlite3_bind_text(stmt->prepared, p - stmt->inbinds + 1, v, -1, SQLITE_TRANSIENT);
                    break;
                }
                default:
                    assert(FALSE);
                    break;
            }
        }
    }
    va_end(ap);
}

/**
 * TODO
 *
 * @param stmt
 * @param model
 * @param nulls
 * @param ptr
 */
void statement_bind_from_model(sqlite_statement_t *stmt, model_t model, const bool *nulls, char *ptr/*, ...*/)
{
    const model_field_t *f;

#if 0
    int no, count;

    sqlite3_reset(stmt->prepared);
    for (no = 1, count = sqlite3_bind_parameter_count(stmt->prepared); no <= count; no++) {
        sqlite3_bind_null(stmt->prepared, no);
    }
#else
    sqlite3_reset(stmt->prepared);
    sqlite3_clear_bindings(stmt->prepared);
#endif
    for (f = model.fields; NULL != f->column_name; f++) {
        int paramno;

        if (0 != (paramno = sqlite3_bind_parameter_index(stmt->prepared, f->column_name))) {
            if (NULL == nulls || !nulls[paramno]) {
                switch (f->type) {
#if 0
                    case SQLITE_TYPE_DOUBLE:
                        sqlite3_bind_double(stmt->prepared, paramno, *((double *) (ptr + f->offset)));
                        break;
#endif
                    case MODEL_TYPE_BOOL:
                        sqlite3_bind_int(stmt->prepared, paramno, *((bool *) (ptr + f->offset)));
                        break;
                    case MODEL_TYPE_INT:
                    case MODEL_TYPE_ENUM:
                    case MODEL_TYPE_DATE:
                    case MODEL_TYPE_DATETIME:
                        sqlite3_bind_int64(stmt->prepared, paramno, *((int64_t *) (ptr + f->offset)));
                        break;
                    case MODEL_TYPE_STRING:
                        sqlite3_bind_text(stmt->prepared, paramno, *((char **) (ptr + f->offset)), -1, SQLITE_TRANSIENT);
                        break;
                    default:
                        assert(FALSE);
                        break;
                }
            }
        }
    }
}

/**
 * Executes the query (on the first call after a statement_bind*) and copies
 * the value of each column of the current line of the result set to the
 * given addresses.
 *
 * @param stmt the statement to execute and read
 * @param error the error to populate when failing
 * @param ... the list of output addresses which receive the different values
 *
 * @return TRUE if a line was read ; FALSE on error (error is set with it) or
 * if there was no remaining lines to read (caller can test if NULL == *error
 * to ditinguish both cases)
 */
bool statement_fetch(sqlite_statement_t *stmt, error_t **error, ...)
{
    bool ret;
    va_list ap;

    ret = FALSE;
    va_start(ap, error);
    switch (sqlite3_step(stmt->prepared)) {
        case SQLITE_ROW:
        {
            const char *p;

            assert(((size_t) sqlite3_column_count(stmt->prepared)) >= strlen(stmt->outbinds)); // allow unused result columns at the end
            for (p = stmt->outbinds; '\0' != *p; p++) {
                switch (*p) {
                    case 'b':
                    {
                        bool *v;

                        v = va_arg(ap, bool *);
                        *v = /*!!*/sqlite3_column_int(stmt->prepared, p - stmt->outbinds);
                        break;
                    }
                    case 'i':
                    {
                        int64_t *v;

                        v = va_arg(ap, int64_t *);
                        *v = sqlite3_column_int64(stmt->prepared, p - stmt->outbinds);
                        break;
                    }
                    case 's':
                    {
                        char **uv;
                        const unsigned char *sv;

                        uv = va_arg(ap, char **);
                        sv = sqlite3_column_text(stmt->prepared, p - stmt->outbinds);
                        if (NULL == sv) {
                            *uv = NULL;
                        } else {
                            *uv = strdup((char *) sv);
                        }
                        break;
                    }
                    case ' ':
                    case '-':
                        // ignore
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
            // empty result set (no error and return FALSE to caller, it should known it doesn't remain any data to read)
            break;
        default:
            error_set(error, WARN, _("%s for %s"), sqlite3_errmsg(db), sqlite3_sql(stmt->prepared));
            break;
    }
    va_end(ap);

    return ret;
}

/**
 * TODO
 *
 * @param stmt
 * @param model
 * @param ptr
 * @param error
 *
 * @return
 */
bool statement_fetch_to_model(sqlite_statement_t *stmt, model_t model, char *ptr, error_t **error)
{
    bool ret;

    ret = FALSE;
    switch (sqlite3_step(stmt->prepared)) {
        case SQLITE_ROW:
        {
            _statement_model_set_output_bind(stmt, model, ptr);
            ret = TRUE;
            break;
        }
        case SQLITE_DONE:
            // empty result set (no error and return FALSE to caller, it should known it doesn't remain any data to read)
            break;
        default:
            error_set(error, WARN, _("%s for %s"), sqlite3_errmsg(db), sqlite3_sql(stmt->prepared));
            break;
    }

    return ret;
}

static bool sqlite_early_ctor(error_t **error)
{
    int ret;
    char *home;
    mode_t old_umask;

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
    old_umask = umask(077);
    if (SQLITE_OK != (ret = sqlite3_open(db_path, &db))) {
        error_set(error, FATAL, _("can't open sqlite database %s: %s"), db_path, sqlite3_errmsg(db));
        return FALSE;
    }
    umask(old_umask);
    // preprepare own statement
    statement_batched_prepare(statements, STMT_COUNT, error);
    // fetch user_version
    if (SQLITE_ROW != sqlite3_step(statements[STMT_GET_USER_VERSION].prepared)) {
        error_set(error, FATAL, _("can't retrieve database version: %s"), sqlite3_errmsg(db));
        return FALSE;
    }
    user_version = sqlite3_column_int(statements[STMT_GET_USER_VERSION].prepared, 0);

    return TRUE;
}

static bool sqlite_late_ctor(error_t **error)
{
    if (OVH_CLI_VERSION_NUMBER > user_version) {
//         statement_bind(statements[STMT_SET_USER_VERSION], "i", OVH_CLI_VERSION_NUMBER); // PRAGMA doesn't handle parameter
        statement_bind(&statements[STMT_SET_USER_VERSION], NULL);
        statement_fetch(&statements[STMT_SET_USER_VERSION], error);
    }

    return NULL == *error;
}

static void sqlite_dtor(void)
{
    statement_batched_finalize(statements, STMT_COUNT);
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
