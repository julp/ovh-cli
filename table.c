#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>

#include "common.h"
#include "modules/conv.h"
#include "struct/dptrarray.h"

typedef enum {
    TABLE_TYPE_INT,
    TABLE_TYPE_ENUM,
    TABLE_TYPE_STRING,
    TABLE_TYPE_BOOLEAN,
} column_type_t;

typedef struct {
    const char *title;
    column_type_t type;
    size_t min_len, max_len, len;
} column_t;

typedef struct {
    size_t columns_count;
    column_t *columns;
    DPtrArray *rows;
} table_t;

typedef struct {
    size_t l;
    uintptr_t v;
} value_t;

typedef struct {
    value_t *values;
} row_t;

#define DEFAULT_WIDTH 80
static int console_width(void)
{
    int columns;

    if (isatty(STDOUT_FILENO)) {
        struct winsize w;

        if (NULL != getenv("COLUMNS") && 0 != atoi(getenv("COLUMNS"))) {
            columns = atoi(getenv("COLUMNS"));
        } else if (-1 != ioctl(STDOUT_FILENO, TIOCGWINSZ, &w)) {
            columns = w.ws_col;
        } else {
            columns = DEFAULT_WIDTH;
        }
    } else {
        columns = -1; // unlimited
    }

    return columns;
}

#include <wchar.h>
static bool cplen(const char *string, size_t *string_len, error_t **error)
{
    size_t cp_len;
    mbstate_t mbstate;

    assert(NULL != string_len);

    *string_len = 0;
    bzero(&mbstate, sizeof(mbstate));
    while ((cp_len = mbrlen(string, MB_CUR_MAX, &mbstate)) > 0) {
        string += cp_len;
        ++*string_len;
    }
    if (((size_t) -1) == cp_len) {
        // An encoding error has occurred. The next n or fewer bytes do not contribute to a valid multibyte character.
        error_set(error, FATAL, "mbrlen: (size_t) -1 /* TODO */");
    } else if (((size_t) -2) == cp_len) {
        // The next n (MB_CUR_MAX) contribute to, but do not complete, a valid multibyte character sequence, and all n bytes have been processed
        error_set(error, FATAL, "mbrlen: (size_t) -2 /* TODO */");
    }

    return 0 == cp_len;
}

static void row_destroy(void *data)
{
    row_t *r;

    r = (row_t *) data;
    free(r->values);
    free(r);
}

table_t *table_new(size_t columns_count, ...)
{
    size_t i;
    table_t *t;
    va_list ap;

    assert(columns_count > 0);

    t = mem_new(*t);
    t->columns_count = columns_count;
    t->columns = mem_new_n(*t->columns, columns_count);
    t->rows = dptrarray_new(NULL, row_destroy, NULL); // TODO: dtor
    // string "title", int type
    va_start(ap, columns_count);
    for (i = 0; i < columns_count; i++) {
        t->columns[i].title = va_arg(ap, const char *);
        t->columns[i].type = va_arg(ap, column_type_t);
        t->columns[i].len = t->columns[i].min_len = t->columns[i].max_len = strlen(t->columns[i].title);
    }
    va_end(ap);

    return t;
}

void table_destroy(table_t *t)
{
    assert(NULL != t);

    free(t->columns);
    dptrarray_destroy(t->rows);
    free(t);
}

void table_store(table_t *t, ...)
{
    size_t i;
    row_t *r;
    va_list ap;

    assert(NULL != t);

    r = mem_new(*r);
    r->values = mem_new_n(*r->values, t->columns_count);
    va_start(ap, t);
    for (i = 0; i < t->columns_count; i++) {
        switch (t->columns[i].type) {
            case TABLE_TYPE_STRING:
            {
                size_t s_len;
                const char *s;

                s = va_arg(ap, const char *);
                s_len = strlen(s);
                r->values[i].v = (uintptr_t) s; // TODO: conversion
                r->values[i].l = s_len; // TODO: len in cp
                if (s_len > t->columns[i].max_len) {
                    t->columns[i].max_len = s_len;
                }
                break;
            }
            case TABLE_TYPE_INT:
            {
                int v;
                size_t len;

                v = va_arg(ap, int);
                len = snprintf(NULL, 0, "%d", v);
                r->values[i].v = (uintptr_t) v;
                r->values[i].l = len;
                if (len > t->columns[i].max_len) {
                    t->columns[i].len = t->columns[i].min_len = t->columns[i].max_len = len;
                }
                break;
            }
            default:
                assert(FALSE);
                break;
        }
    }
    va_end(ap);
    dptrarray_push(t->rows, r);
}

static void table_print_separator_line(table_t *t)
{
    size_t i, j;

    putchar('+');
    for (i = 0; i < t->columns_count; i++) {
        for (j = 0; j < t->columns[i].len + 2; j++) {
            putchar('-');
        }
        putchar('+');
    }
    putchar('\n');
}

static size_t wordwrap(size_t max_len, const char *string, size_t string_len, const char ***parts)
{
    size_t i, parts_count;

    i = 0;
    parts_count = (string_len / max_len) + 1;
    *parts = mem_new_n(**parts, parts_count + 1);
    if (parts_count > 1) {
        size_t index;

        index = 0;
        while (i < parts_count) {
            (*parts)[i++] = strndup(string + index, max_len);
            index += max_len;
        }
    } else {
        (*parts)[i++] = string; // don't free parts[0] if string == parts[0] (or 1 == parts_count)
    }
    (*parts)[i] = NULL;

    return parts_count;
}

#define TABLE_FLAG_NONE 0
#define TABLE_FLAG_NO_HEADERS (1<<0)
void table_display(table_t *t, uint32_t flags)
{
    size_t i;
    Iterator it;
    int width;

    assert(NULL != t);

//     if (dptrarray_size(t->rows) > 0) {
    width = console_width();
    if (width > 0) {
        int min_len_sum, candidates_for_grow;

        candidates_for_grow = min_len_sum = 0;
        for (i = 0; i < t->columns_count; i++) {
            min_len_sum += t->columns[i].min_len;
            candidates_for_grow += t->columns[i].max_len > t->columns[i].min_len;
        }
        min_len_sum += STR_LEN("| ") + STR_LEN(" | ") * (t->columns_count - 1) + STR_LEN(" |");
        if (min_len_sum < width) {
            int diff;

            diff = width - min_len_sum;
            for (i = 0; i < t->columns_count; i++) {
                if (t->columns[i].max_len > t->columns[i].min_len) {
                    t->columns[i].len = t->columns[i].min_len + diff / candidates_for_grow;
                }
            }
        }
    }
    if (!HAS_FLAG(flags, TABLE_FLAG_NO_HEADERS)) {
        // +--- ... ---+
        table_print_separator_line(t);
        // | ... | : column headers
        putchar('|');
        for (i = 0; i < t->columns_count; i++) {
            printf(" %-*s |", (int) t->columns[i].len, t->columns[i].title);
        }
        putchar('\n');
    }
    // +--- ... ---+
    table_print_separator_line(t);
    // | ... | : data
    dptrarray_to_iterator(&it, t->rows);
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        row_t *r;
        int j, lines_needed;
#define MAX_COLUMNS 12
        char **value_parts[MAX_COLUMNS]; // si alloué dynamiquement, le remonter dans la fonction
        size_t value_parts_count[MAX_COLUMNS];

        lines_needed = 1;
        r = iterator_current(&it, NULL);
        bzero(&value_parts_count, sizeof(value_parts_count));
        for (i = 0; i < t->columns_count; i++) {
            /*if (r->values[i].l > t->columns[i].len && (r->values[i].l / t->columns[i].len + 1) > lines_needed) {
                lines_needed = r->values[i].l / t->columns[i].len + 1;
            }*/
            switch (t->columns[i].type) {
                case TABLE_TYPE_STRING:
                    value_parts_count[i] = wordwrap(t->columns[i].len, (const char *) r->values[i].v, r->values[i].l, &value_parts[i]);
                    if (value_parts_count[i] > lines_needed) {
                        lines_needed = value_parts_count[i];
                    }
                    break;
                case TABLE_TYPE_INT:
                    /* NOP */
                    break;
                default:
                    assert(FALSE);
                    break;
            }
        }
        for (j =  0; j < lines_needed; j++) {
            putchar('|');
            for (i = 0; i < t->columns_count; i++) {
                if (0 == j || j < value_parts_count[i]) {
                    switch (t->columns[i].type) {
                        case TABLE_TYPE_STRING:
    //                         printf(" %-*s |", (int) t->columns[i].len, (const char *) r->values[i].v);
                            printf(" %-*s |", (int) t->columns[i].len, value_parts[i][j]);
                            break;
                        case TABLE_TYPE_INT:
                            printf(" %*d |", (int) t->columns[i].len, (int) r->values[i].v);
                            break;
                        default:
                            assert(FALSE);
                            break;
                    }
                } else {
                    printf(" %*c |", (int) t->columns[i].len, ' ');
                }
            }
            putchar('\n');
        }
        // TODO: free value_parts
    }
    iterator_close(&it);
    // +--- ... ---+
    table_print_separator_line(t);
}

INITIALIZER_DECL(table_test);
INITIALIZER_P(table_test)
{
    table_t *t;

    t = table_new(3, "id", TABLE_TYPE_INT, "subdomain", TABLE_TYPE_STRING, "target", TABLE_TYPE_STRING);
    table_store(t, 1, "abc", "def");
    table_store(t, 2, "ghi", "jkl");
    table_store(t, 3, "mno", "pqr");
#if 1
    table_store(t, 4, "stu", "01234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789");
#endif
    table_display(t, TABLE_FLAG_NONE);
    table_destroy(t);
}
