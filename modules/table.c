#include <string.h>
#include <wchar.h>
#include <locale.h>
#include <ctype.h>

#include "command.h"
#include "modules/table.h"
#include "util.h"
#include "date.h"
#include "modules/conv.h"
#include "struct/dptrarray.h"

typedef struct resource_t {
    const void *ptr;
//     func_dtor_t dtor_func;
    struct resource_t *next;
} resource_t;

typedef struct {
    const char *title;
    column_type_t type;
    size_t *enum_values_len;
    const char **enum_values;
    size_t enum_max_value_len;
    size_t min_len, max_len, len, title_len;
} column_t;

struct table_t {
    DPtrArray *rows;
    column_t *columns;
    resource_t *strings;
    size_t columns_count;
};

typedef struct {
    bool f;
    size_t l;
    uintptr_t v;
} value_t;

typedef struct {
    value_t *values;
} row_t;

const char * const false_true[] = {
    N_("false"),
    N_("true"),
    NULL
};

#if 0
// https://en.wikipedia.org/wiki/Box-drawing_character

{
    "═", "║", // HORIZONTAL ; VERTICAL
    { "╔", "╦", "╗", }, // TOP | HORIZONTAL | VERTICAL | LEFT ; TOP | HORIZONTAL | VERTICAL ; TOP | HORIZONTAL | VERTICAL | RIGHT

    { "╠", "╬", "╣", }, // HORIZONTAL | VERTICAL | LEFT ; HORIZONTAL | VERTICAL ; HORIZONTAL | VERTICAL | RIGHT

    { "╚", "╩", "╝", },

    "─", "│",
    { "┌", "┬", "┐", },
    { "├", "┼", "┤", },
    { "└", "┴", "┘", },

    "-", "|",
    { "+", "+", "+" },
    { "+", "+", "+" },
    { "+", "+", "+" },
},
#endif

/**
 * NOTE:
 * - column titles are expected to be already in "local" charset (gettext, if they are translated, will do it for us)
 * - same for "true"/"false"
 * - columns data are expected to be in UTF-8, we handle their conversion
 * - TABLE_TYPE_DATE and TABLE_TYPE_DATETIME are not intended for format with non-fixed length output (only simple formats like dd/mm/yyyy, yyyy-mm-dd, etc)
 **/

/**
 * TODO:
 * - refactoring
 * - pipe to PAGER if too much lines
 * - map TABLE_TYPE_BOOL on TABLE_TYPE_ENUM ? (copy t->false_true* to t->columns[i].enum*)
 **/

// TODO: if the string contains \n: return length of the longest line?
static bool cplen(const char *string, size_t *string_len, error_t **error)
{
    size_t cp_len;
    mbstate_t mbstate;

    assert(NULL != string_len);

    *string_len = 0;
    setlocale(LC_ALL, "");
    bzero(&mbstate, sizeof(mbstate));
    while ((cp_len = mbrlen(string, MB_CUR_MAX, &mbstate)) > 0) {
        if (((size_t) -1) == cp_len || ((size_t) -2) == cp_len) {
            break;
        }
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

static const char *false_true_string[2];
static size_t false_true_len[2], max_false_true_len, max_date_len, max_datetime_len;

static bool table_ctor(error_t **error)
{
    // speed up true/false conversions
    false_true_string[FALSE] = _("false");
    cplen(false_true_string[FALSE], &false_true_len[FALSE], error);
    false_true_string[TRUE] = _("true");
    cplen(false_true_string[TRUE], &false_true_len[TRUE], error);
    max_false_true_len = MAX(false_true_len[FALSE], false_true_len[TRUE]);
    // preflight datetime length
    max_date_len = STR_LEN("yyyy/mm/dd");
    max_datetime_len = STR_LEN("yyyy/mm/dd hh:ii:ss");

    return TRUE;
}

static void row_destroy(void *data)
{
    row_t *r;

    r = (row_t *) data;
    free(r->values);
    free(r);
}

void int_store(table_t *UNUSED(t), va_list ap, column_t *c, value_t *val)
{
    int v;

    v = va_arg(ap, int);
    val->f = FALSE;
    val->v = (uintptr_t) v;
    val->l = snprintf(NULL, 0, "%d", v);
    if (val->l > c->max_len) {
        c->len = c->min_len = c->max_len = val->l;
    }
}

void bool_store(table_t *UNUSED(t), va_list ap, column_t *c, value_t *val)
{
    bool v;

    v = va_arg(ap, bool);
    val->f = FALSE;
    val->v = (uintptr_t) v;
    val->l = false_true_len[v];
    if (val->l > c->max_len) {
        c->len = c->min_len = c->max_len = max_false_true_len;
    }
}

void enum_store(table_t *UNUSED(t), va_list ap, column_t *c, value_t *val)
{
    int v;

    v = va_arg(ap, int);
    val->f = FALSE;
    val->v = (uintptr_t) /*c->enum_values[*/v/*]*/;
    val->l = c->enum_values_len[v];
    if (val->l > c->max_len) {
        c->len = c->min_len = c->max_len = c->enum_max_value_len;
    }
}

void string_store(table_t *t, va_list ap, column_t *c, value_t *val)
{
    char *s_local;
    error_t *error;
    const char *s_utf8;

    // TODO: real error handling! Let caller handle this by adding a error_t **error in argument?
error = NULL;
    if (NULL == (s_utf8 = va_arg(ap, const char *))) {
        s_local = (char *) "-";
        val->f = FALSE;
        val->l = STR_LEN("-");
    } else {
        if (HAS_FLAG(c->type, TABLE_TYPE_DELEGATE)) {
            resource_t *r;

            r = mem_new(*r);
            r->next = t->strings;
            r->ptr = s_utf8;
            t->strings = r;
        }
        convert_string_utf8_to_local(s_utf8, strlen(s_utf8), &s_local, NULL, &error);
print_error(error);
error = NULL;
        cplen(s_local, &val->l, &error);
print_error(error);
        val->f = s_local != s_utf8;
    }
    val->v = (uintptr_t) s_local; // TODO: conversion UTF-8 => local
//     val->l = s_local_len;
    if (val->l > c->max_len) {
        c->max_len = val->l;
    }
}

void date_store(table_t *UNUSED(t), va_list ap, column_t *c, value_t *val)
{
    time_t v;

    v = va_arg(ap, time_t);
    val->f = FALSE;
    val->v = (uintptr_t) v;
    val->l = max_date_len;
    if (val->l > c->max_len) {
        c->len = c->min_len = c->max_len = max_date_len;
    }
}

void datetime_store(table_t *UNUSED(t), va_list ap, column_t *c, value_t *val)
{
    time_t v;

    v = va_arg(ap, time_t);
    val->f = FALSE;
    val->v = (uintptr_t) v;
    val->l = max_datetime_len;
    if (val->l > c->max_len) {
        c->len = c->min_len = c->max_len = max_datetime_len;
    }
}

static int strcmpp_asc(const void *p1, const void *p2, void *arg)
{
//     return strcmp(*(char * const *) p2, *(char * const *) p1);
    return strcoll((const char *) (*((row_t **) p1))->values[*(size_t *) arg].v, (const char *) (*((row_t **) p2))->values[*(size_t *) arg].v);
}

static int strcmpp_desc(const void *p1, const void *p2, void *arg)
{
    return strcoll((const char *) (*((row_t **) p2))->values[*(size_t *) arg].v, (const char *) (*((row_t **) p1))->values[*(size_t *) arg].v);
}

static int intcmpp_asc(const void *p1, const void *p2, void *arg)
{
    return (*((row_t **) p1))->values[*(size_t *) arg].v - (*((row_t **) p2))->values[*(size_t *) arg].v;
}

static int intcmpp_desc(const void *p1, const void *p2, void *arg)
{
    return (*((row_t **) p2))->values[*(size_t *) arg].v - (*((row_t **) p1))->values[*(size_t *) arg].v;
}

static struct {
#if 0
    void (*initialize)(void); // callback for table_new
#endif
    void (*store)(table_t *, va_list, column_t *, value_t *); // callback for table_store
#if 0
    size_t (*display)(void); // callback for table_display
    void (*free)(void); // callback for table_display (free after displaying)
#endif
    CmpFuncArg sort[_TABLE_SORT_COUNT];
} type_handlers[] = {
    [ TABLE_TYPE_INT ]      = { int_store, { [ TABLE_SORT_ASC ] = intcmpp_asc, [ TABLE_SORT_DESC ] = intcmpp_desc } },
    [ TABLE_TYPE_BOOL ]     = { bool_store, { [ TABLE_SORT_ASC ] = intcmpp_asc, [ TABLE_SORT_DESC ] = intcmpp_desc } },
    [ TABLE_TYPE_ENUM ]     = { enum_store, { [ TABLE_SORT_ASC ] = intcmpp_asc, [ TABLE_SORT_DESC ] = intcmpp_desc } },
    [ TABLE_TYPE_STRING ]   = { string_store, { [ TABLE_SORT_ASC ] = strcmpp_asc, [ TABLE_SORT_DESC ] = strcmpp_desc } },
    [ TABLE_TYPE_DATE ]     = { date_store, { [ TABLE_SORT_ASC ] = intcmpp_asc, [ TABLE_SORT_DESC ] = intcmpp_desc } },
    [ TABLE_TYPE_DATETIME ] = { datetime_store, { [ TABLE_SORT_ASC ] = intcmpp_asc, [ TABLE_SORT_DESC ] = intcmpp_desc } }
};

table_t *table_new(size_t columns_count, ...)
{
    size_t i;
    table_t *t;
    va_list ap;

    t = mem_new(*t);
    t->strings = NULL;
    t->columns_count = columns_count;
    t->columns = mem_new_n(*t->columns, columns_count);
    t->rows = dptrarray_new(NULL, row_destroy, NULL);
    // string "title", int type
    va_start(ap, columns_count);
    for (i = 0; i < columns_count; i++) {
        t->columns[i].title = va_arg(ap, const char *);
        t->columns[i].type = va_arg(ap, column_type_t);
        cplen(t->columns[i].title, &t->columns[i].title_len, NULL);
        t->columns[i].len = t->columns[i].min_len = t->columns[i].max_len = t->columns[i].title_len;
        if (TABLE_TYPE_ENUM == t->columns[i].type) {
            size_t enum_size;
            const char * const *v;
            const char * const *values;

            enum_size = 0;
            t->columns[i].enum_max_value_len = 0;
            values = va_arg(ap, const char * const *);
            for (v = values; NULL != *v; v++) {
                ++enum_size;
            }
            t->columns[i].enum_values = mem_new_n(*t->columns[i].enum_values, enum_size);
            t->columns[i].enum_values_len = mem_new_n(*t->columns[i].enum_values_len, enum_size);
            for (v = values; NULL != *v; v++) {
                t->columns[i].enum_values[v - values] = _(*v);
                cplen(t->columns[i].enum_values[v - values], &t->columns[i].enum_values_len[v - values], NULL);
                if (t->columns[i].enum_values_len[v - values] > t->columns[i].enum_max_value_len) {
                    t->columns[i].enum_max_value_len = t->columns[i].enum_values_len[v - values];
                }
            }
        }
    }
    va_end(ap);

    return t;
}

table_t *table_model_new(model_t model)
{
    table_t *t;

    t = mem_new(*t);
    t->strings = NULL;

    return t;
}

void table_destroy(table_t *t)
{
    size_t i;

    assert(NULL != t);

    if (NULL != t->strings) {
        resource_t *current, *next;

        current = t->strings;
        while (NULL != current) {
            next = current->next;
            free((void *) current->ptr);
            free(current);
            current = next;
        }
    }
    for (i = 0; i < t->columns_count; i++) {
        if (TABLE_TYPE_ENUM == t->columns[i].type) {
            free(t->columns[i].enum_values);
            free(t->columns[i].enum_values_len);
        }
    }
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
        assert(TABLE_TYPE(t->columns[i].type) <= _TABLE_TYPE_LAST);
        assert(NULL != type_handlers[TABLE_TYPE(t->columns[i].type)].store);

        type_handlers[TABLE_TYPE(t->columns[i].type)].store(t, ap, &t->columns[i], &r->values[i]);
    }
    va_end(ap);
    dptrarray_push(t->rows, r);
}

#if 0
static bool camel_to_snake_case(const char *camel, char *snake, size_t snake_size, size_t *snake_len)
{
    size_t snake_len;

    assert(NULL != snake_len);
    while ('\0' != *camel) {
        if (snake_len + STR_SIZE("_X") > snake_size) { // at least 3 characters for '_' + tolower(*camel) + '\0'
            return FALSE;
        }
        if (isupper(*camel)) {
            snake[*snake_len++] = '_';
            snake[*snake_len++] = tolower(*camel);
        } else {
            snake[*snake_len++] = *camel;
        }
        ++camel;
    }
    snake[*snake_len] = '\0';

    return TRUE;
}
#endif

void table_model_store(table_t *t, char *ptr)
{
#if 0
//     bool ok;
    size_t i;
    row_t *r;
//     char *buffer[512];
    const model_field_t *f;

    i = 0;
    r = mem_new(*r);
    r->values = mem_new_n(*r->values, t->columns_count);
    for (f = t->model.fields, i = 0; NULL != f->column_name; f++) {
        if (TRUE/*!f->hidden*/) {
            assert(TABLE_TYPE(t->columns[i].type) <= _TABLE_TYPE_LAST);
            assert(NULL != type_handlers[TABLE_TYPE(t->columns[i].type)].store);

            type_handlers[TABLE_TYPE(t->columns[i].type)].store(t, ap, &t->columns[i], &r->values[i]);
            ++i;
        }
    }
    dptrarray_push(t->rows, r);
#endif
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

typedef struct {
    size_t charlen;
    const char *part;
} break_t;

#include "nearest_power.h"
static size_t string_break(size_t max_len, const char *string, size_t string_len, break_t **breaks)
{
    size_t i, breaks_len, breaks_size;

    i = 0;
    // NOTE: string_len unit is "character"
    breaks_len = (string_len + max_len - 1) / max_len;
    breaks_size = nearest_power(breaks_len, 8);
    *breaks = mem_new_n(**breaks, breaks_size);
    if (breaks_len <= 1 && NULL == strchr(string, '\n')) {
        (*breaks)[i].charlen = string_len;
        (*breaks)[i++].part = string; // don't free breaks[0].part if string == breaks[0].part (or 1 == breaks_count)
    } else {
        const char *p;
        size_t start, end; // byte index

        p = string;
        start = end = 0;
        while ('\0' != *p) {
            bool forced;
            size_t cp_len, cp_count;

            forced = FALSE;
            if (i >= breaks_size) {
                breaks_size <<= 1;
                *breaks = mem_renew(*breaks, **breaks, breaks_size);
            }
            for (cp_count = 0; !forced && cp_count < max_len && (cp_len = mblen(p, MB_CUR_MAX)) > 0; cp_count++) { // TODO: < 0 with size_t can't work
                forced = 1 == cp_len && '\n' == *p;
                p += cp_len;
                end += cp_len;
            }
            (*breaks)[i].charlen = cp_count - forced;
            (*breaks)[i++].part = strndup(string + start, end - start - forced);
            start = end;
        }
    }

    return i;
}

/**
 * Sort (in ASC order) table rows based on the value of a specific column
 *
 * @param t the table to sort
 * @param colno the index of the column to sort on
 *
 * @note don't use this on data from SQL statement: use an ORDER clause.
 * By default, there is no sorting at all: order of data insertion is
 * preserved.
 */
void table_sort(table_t *t, size_t colno, table_sort_t order)
{
    assert(NULL != t);
    assert(colno < t->columns_count);
    assert(TABLE_TYPE(t->columns[colno].type) <= _TABLE_TYPE_LAST);
    assert(NULL != type_handlers[TABLE_TYPE(t->columns[colno].type)].sort[order]);

    dptrarray_sort(t->rows, type_handlers[TABLE_TYPE(t->columns[colno].type)].sort[order], &colno);
}

void table_display(table_t *t, uint32_t flags)
{
    int width;
    size_t i, k;
    Iterator it;
    break_t **breaks;
    size_t *breaks_count;

    assert(NULL != t);

    width = console_width();
    if (width > 0) {
        int min_len_sum, candidates_for_growth;

        candidates_for_growth = min_len_sum = 0;
        for (i = 0; i < t->columns_count; i++) {
            min_len_sum += t->columns[i].min_len;
            candidates_for_growth += t->columns[i].max_len > t->columns[i].min_len;
        }
        min_len_sum += STR_LEN("| ") + STR_LEN(" | ") * (t->columns_count - 1) + STR_LEN(" |");
        if (min_len_sum < width) {
            int diff;

            diff = width - min_len_sum;
            for (i = 0; candidates_for_growth > 0 && i < t->columns_count; i++) {
                if (t->columns[i].max_len > t->columns[i].min_len && t->columns[i].max_len < t->columns[i].min_len + diff / candidates_for_growth) {
                    diff -= t->columns[i].max_len - t->columns[i].min_len;
                    t->columns[i].len = t->columns[i].min_len = t->columns[i].max_len;
                    --candidates_for_growth;
                }
            }
            if (diff > 0 && candidates_for_growth > 0) {
                for (i = 0; i < t->columns_count; i++) {
                    if (t->columns[i].max_len > t->columns[i].min_len) {
                        t->columns[i].len = t->columns[i].min_len + diff / candidates_for_growth;
                    }
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
            putchar(' ');
            fputs(t->columns[i].title, stdout);
//             printf(" %*c |", t->columns[i].len - t->columns[i].title_len, ' ');
            for (k = t->columns[i].title_len; k < t->columns[i].len; k++) {
                putchar(' ');
            }
            fputs(" |", stdout);
        }
        putchar('\n');
    }
    // +--- ... ---+
    table_print_separator_line(t);
    if (dptrarray_length(t->rows) > 0) {
        // | ... | : data
        dptrarray_to_iterator(&it, t->rows);
        breaks = mem_new_n(*breaks, t->columns_count);
        breaks_count = mem_new_n(*breaks_count, t->columns_count);
        for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
            row_t *r;
            size_t j, lines_needed;

            lines_needed = 1;
            r = iterator_current(&it, NULL);
            bzero(breaks_count, sizeof(breaks_count) * t->columns_count);
            for (i = 0; i < t->columns_count; i++) {
                /*if (r->values[i].l > t->columns[i].len && (r->values[i].l / t->columns[i].len + 1) > lines_needed) {
                    lines_needed = r->values[i].l / t->columns[i].len + 1;
                }*/
                switch (TABLE_TYPE(t->columns[i].type)) {
                    case TABLE_TYPE_STRING:
                        breaks_count[i] = string_break(t->columns[i].len, (const char *) r->values[i].v, r->values[i].l, &breaks[i]);
                        if (breaks_count[i] > lines_needed) {
                            lines_needed = breaks_count[i];
                        }
                        break;
                    case TABLE_TYPE_INT:
                    case TABLE_TYPE_BOOL:
                    case TABLE_TYPE_ENUM:
                    case TABLE_TYPE_DATE:
                    case TABLE_TYPE_DATETIME:
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
                    putchar(' ');
                    if (0 == j || j < breaks_count[i]) {
                        size_t written;

                        switch (TABLE_TYPE(t->columns[i].type)) {
                            case TABLE_TYPE_STRING:
                                fputs(breaks[i][j].part, stdout);
                                written = breaks[i][j].charlen;
                                break;
                            case TABLE_TYPE_INT:
                                written = printf("%*d", (int) t->columns[i].len, (int) r->values[i].v);
                                break;
                            case TABLE_TYPE_ENUM:
                                fputs(t->columns[i].enum_values[r->values[i].v], stdout);
                                written = t->columns[i].enum_values_len[r->values[i].v];
                                break;
                            case TABLE_TYPE_BOOL:
                                fputs(false_true_string[r->values[i].v], stdout);
                                written = false_true_len[r->values[i].v];
                                break;
                            case TABLE_TYPE_DATE:
                            case TABLE_TYPE_DATETIME:
                            {
                                struct tm tm;
                                char buffer[512];

                                if (
                                    ((time_t) 0 == (time_t) r->values[i].v)
                                    || (NULL == localtime_r((time_t *) &r->values[i].v, &tm))
                                    || ((written = strftime(buffer, ARRAY_SIZE(buffer), TABLE_TYPE_DATE == TABLE_TYPE(t->columns[i].type) ? "%x" : "%x %X", &tm)) < 1)
                                ) {
                                    written = STR_LEN("-");
                                    memcpy(buffer, "-", STR_SIZE("-"));
                                }
                                fputs(buffer, stdout);
                                break;
                            }
                            default:
                                assert(FALSE);
                                break;
                        }
                        for (k = written; k < t->columns[i].len; k++) {
                            putchar(' ');
                        }
                    } else {
                        printf("%*c", (int) t->columns[i].len, ' ');
                    }
                    fputs(" |", stdout);
                }
                putchar('\n');
            }
            for (i = 0; i < t->columns_count; i++) {
                if (TABLE_TYPE_STRING == TABLE_TYPE(t->columns[i].type)) {
                    if (breaks_count[i] > 1) {
                        for (j = 0; j < breaks_count[i]; j++) {
                            free((void *) breaks[i][j].part);
                        }
                    }
                    free(breaks[i]);
                }
                // <TODO: better place in row_destroy>
                if (r->values[i].f) {
                    free((void *) r->values[i].v);
                }
                // </TODO>
            }
        }
        iterator_close(&it);
        free(breaks);
        free(breaks_count);
        // +--- ... ---+
        table_print_separator_line(t);
    }
}

#define DIGIT_0 "\xF0\x9D\x9F\x8E" /* 1D7CE, Nd */
#define DIGIT_1 "\xF0\x9D\x9F\x8F"
#define DIGIT_2 "\xF0\x9D\x9F\x90"
#define DIGIT_3 "\xF0\x9D\x9F\x91"
#define DIGIT_4 "\xF0\x9D\x9F\x92"
#define DIGIT_5 "\xF0\x9D\x9F\x93"
#define DIGIT_6 "\xF0\x9D\x9F\x94"
#define DIGIT_7 "\xF0\x9D\x9F\x95"
#define DIGIT_8 "\xF0\x9D\x9F\x96"
#define DIGIT_9 "\xF0\x9D\x9F\x97"

#define UTF8
#ifdef UTF8
// # define STRING DIGIT_0 DIGIT_1 DIGIT_2 DIGIT_3 DIGIT_4 DIGIT_5 DIGIT_6 DIGIT_7 DIGIT_8 DIGIT_9
// # define STRING "\xEF\xAC\x80\xEF\xAC\x80\xEF\xAC\x80\xEF\xAC\x80\xEF\xAC\x80\xEF\xAC\x80\xEF\xAC\x80\xEF\xAC\x80\xEF\xAC\x80\xEF\xAC\x80"
#define STRING "éïàùçè"
#else
# define STRING "0123456789"
#endif
static const char long_string[] = \
    /* 1..10 */ STRING \
    /* 11..20 */ STRING \
    /* 21..30 */ STRING \
    /* 41..50 */ STRING \
    /* 51..60 */ STRING \
    /* 61..70 */ STRING \
    /* 71..80 */ STRING \
    /* 81..90 */ STRING \
    /* 91..100 */ STRING \
    /* 101..110 */ STRING \
    /* 111..120 */ STRING \
    /* 121..130 */ STRING \
    /* 131..140 */ STRING \
    /* 141..150 */ STRING \
    /* 151..160 */ STRING \
    /* 161..170 */ STRING \
    /* 171..180 */ STRING \
    /* 181..190 */ STRING \
    /* 191..200 */ STRING \
    /* 201..210 */ STRING \
    /* 211..220 */ STRING \
    /* 221..230 */ STRING \
    /* 231..240 */ STRING \
    /* 241..250 */ STRING \
    /* 251..260 */ STRING \
    /* 261..270 */ STRING \
    /* 271..280 */ STRING \
    /* 281..290 */ STRING \
    /* 291..300 */ STRING \
    /* 301..310 */ STRING \
    /* 311..320 */ STRING \
    /* 321..330 */ STRING;

// From domain (dnssec status)
static const char * const test_enum[] = {
    N_("enabled"),
    N_("disabled"),
    N_("enableInProgress"),
    N_("disableInProgress"),
    NULL
};

// can't use constructor, output_encoding (modules/conv.c is not yet set)
// INITIALIZER_DECL(table_test);
INITIALIZER_P(table_test)
{
    table_t *t;

#ifdef WITH_NLS
    extern bool nls_ctor(void);
    nls_ctor();
#endif /* WITH_NLS */
    t = table_new(5, "id", TABLE_TYPE_INT, "subdomain", TABLE_TYPE_STRING | TABLE_TYPE_DELEGATE, "target", TABLE_TYPE_STRING, "éïàùçè", TABLE_TYPE_STRING, "status", TABLE_TYPE_ENUM, test_enum);
    table_store(t, 1, strdup("abc"), "def", "", 0);
    table_store(t, 2, strdup("ghi"), "jkl", long_string, 1);
    table_store(t, 3, strdup("mno"), long_string, "pqr", 2);
    table_store(t, 4, strdup("stu"), long_string, long_string, 3);
    table_store(t, 5, strdup("é"), "é", "é", 2);
    table_store(t, 6, strdup("é"), "é", "abc\ndéf", 1);
    table_sort(t, 1, TABLE_SORT_ASC);
//     table_sort(t, 0, TABLE_SORT_ASC);
    table_display(t, TABLE_FLAG_NONE);
    table_destroy(t);
}

DECLARE_MODULE(table) = {
    "table",
    NULL,
    NULL,
    table_ctor,
    NULL,
    NULL
};