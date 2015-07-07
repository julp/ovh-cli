#include <string.h>
#include "common.h"
#include "sqlite.h"

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
//                     sss->output_binds[i].ptr = va_arg(ap, void *);
                    break;
                case 'i':
                    sss->output_binds[i].type = SQLITE_TYPE_INT;
//                     sss->output_binds[i].ptr = va_arg(ap, void *);
                    break;
                case 's':
                    sss->output_binds[i].type = SQLITE_TYPE_STRING;
//                     *((char ***) sss->output_binds[i].ptr) = /*(void *)*/ va_arg(ap, char **);
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
