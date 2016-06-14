#include <string.h>
#include <wchar.h>
#include <locale.h>
#include <ctype.h>
#include <unistd.h>

#include "command.h"
#include "modules/table.h"
#include "util.h"
#include "date.h"
#include "modules/conv.h"
#include "struct/dptrarray.h"
#include "nearest_power.h"

#define DEFAULT_PAGER "less"

typedef struct resource_t {
    const void *ptr;
//     func_dtor_t dtor_func;
    struct resource_t *next;
} resource_t;

typedef struct {
    const char *title;
    model_field_type_t type;
    size_t *enum_values_len;
    const char **enum_values;
    size_t enum_max_value_len;
    size_t min_len, max_len, len, title_len;
} column_t;

struct table_t {
    uint32_t flags;
    DPtrArray *rows;
    column_t *columns;
    resource_t *strings;
    size_t columns_count;
    const model_t *model;
};

typedef struct {
    bool f;
    size_t l;
    uintptr_t v;
} value_t;

typedef struct {
    value_t *values;
} row_t;

typedef struct {
    size_t charlen;
    const char *part;
} break_t;

const char * const false_true[] = {
    N_("false"),
    N_("true"),
    NULL
};

enum {
    TABLE_BORDER_NONE,
    TABLE_BORDER_ASCII,
    TABLE_BORDER_UTF8_SIMPLE,
    TABLE_BORDER_UTF8_DOUBLED,
};

// https://en.wikipedia.org/wiki/Box-drawing_character
static const struct {
    const char *h;
    const char *v;
    const char *o[3][3];
} borders[] = {
    [ TABLE_BORDER_NONE ] = {
        "", "",
        {
            { "", "", "" },
            { "", "", "" },
            { "", "", "" },
        }
    },
    [ TABLE_BORDER_ASCII ] = {
        "-", "|",
        {
            { "+", "+", "+" },
            { "+", "+", "+" },
            { "+", "+", "+" },
        }
    },
    [ TABLE_BORDER_UTF8_SIMPLE ] = {
        "─", "│",
        {
            { "┌", "┬", "┐", },
            { "├", "┼", "┤", },
            { "└", "┴", "┘", },
        }
    },
    [ TABLE_BORDER_UTF8_DOUBLED ] = {
        "═", "║", // HORIZONTAL ; VERTICAL
        {
            { "╔", "╦", "╗", }, // TOP | HORIZONTAL | VERTICAL | LEFT ; TOP | HORIZONTAL | VERTICAL ; TOP | HORIZONTAL | VERTICAL | RIGHT
            { "╠", "╬", "╣", }, // HORIZONTAL | VERTICAL | LEFT ; HORIZONTAL | VERTICAL ; HORIZONTAL | VERTICAL | RIGHT
            { "╚", "╩", "╝", },
        }
    },
};

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

static int border_type;
static const char *false_true_string[2];
static size_t false_true_len[2], max_false_true_len, max_date_len, max_datetime_len;

static bool table_ctor(error_t **error)
{
    border_type = is_output_utf8() ? TABLE_BORDER_UTF8_SIMPLE : TABLE_BORDER_ASCII;
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

static void _int_store_real(column_t *c, value_t *val, int v)
{
    val->f = FALSE;
    val->v = (uintptr_t) v;
    val->l = snprintf(NULL, 0, "%d", v);
    if (val->l > c->max_len) {
        c->len = c->min_len = c->max_len = val->l;
    }
}

static void int_store(table_t *UNUSED(t), column_t *c, value_t *val, va_list ap)
{
    int v;

    v = va_arg(ap, int);
    _int_store_real(c, val, v);
}

static void int_store_modelized(table_t *UNUSED(t), column_t *c, value_t *val, char *ptr)
{
    _int_store_real(c, val, *((int *) ptr));
}

static void bool_colinit_from_ap(table_t *UNUSED(t), column_t *c, va_list UNUSED(ap))
{
    if (max_false_true_len > c->max_len) {
        c->len = c->min_len = c->max_len = max_false_true_len;
    }
}

static void bool_colinit_from_model(table_t *UNUSED(t), column_t *c, const model_field_t *UNUSED(f))
{
    if (max_false_true_len > c->max_len) {
        c->len = c->min_len = c->max_len = max_false_true_len;
    }
}

static void _bool_store_real(value_t *val, bool v)
{
    val->f = FALSE;
    val->v = (uintptr_t) v;
    val->l = false_true_len[v];    
}

static void bool_store(table_t *UNUSED(t), column_t *UNUSED(c), value_t *val, va_list ap)
{
    bool v;

    v = va_arg(ap, bool);
    _bool_store_real(val, v);
}

static void bool_store_modelized(table_t *UNUSED(t), column_t *UNUSED(c), value_t *val, char *ptr)
{
    _bool_store_real(val, *((bool *) ptr));
}

static void enum_colinit_from_ap(table_t *UNUSED(t), column_t *c, va_list ap)
{
    size_t enum_size;
    const char * const *v;
    const char * const *values;

    enum_size = 0;
    c->enum_max_value_len = 0;
    values = va_arg(ap, const char * const *);
    for (v = values; NULL != *v; v++) {
        ++enum_size;
    }
    c->enum_values = mem_new_n(*c->enum_values, enum_size);
    c->enum_values_len = mem_new_n(*c->enum_values_len, enum_size);
    for (v = values; NULL != *v; v++) {
        c->enum_values[v - values] = _(*v);
        cplen(c->enum_values[v - values], &c->enum_values_len[v - values], NULL);
        if (c->enum_values_len[v - values] > c->enum_max_value_len) {
            c->enum_max_value_len = c->enum_values_len[v - values];
        }
    }
}

static void enum_colinit_from_model(table_t *UNUSED(t), column_t *c, const model_field_t *f)
{
    size_t enum_size;
    const char * const *v;

    enum_size = 0;
    c->enum_max_value_len = 0;
    for (v = f->enum_values; NULL != *v; v++) {
        ++enum_size;
    }
    c->enum_values = mem_new_n(*c->enum_values, enum_size);
    c->enum_values_len = mem_new_n(*c->enum_values_len, enum_size);
    for (v = f->enum_values; NULL != *v; v++) {
        c->enum_values[v - f->enum_values] = _(*v);
        cplen(c->enum_values[v - f->enum_values], &c->enum_values_len[v - f->enum_values], NULL);
        if (c->enum_values_len[v - f->enum_values] > c->enum_max_value_len) {
            c->enum_max_value_len = c->enum_values_len[v - f->enum_values];
        }
    }
}

static void _enum_store_real(column_t *c, value_t *val, int v)
{
    val->f = FALSE;
    val->v = (uintptr_t) /*c->enum_values[*/v/*]*/;
    val->l = c->enum_values_len[v];
    if (val->l > c->max_len) {
        c->len = c->min_len = c->max_len = c->enum_max_value_len;
    }
}

static void enum_store(table_t *UNUSED(t), column_t *c, value_t *val, va_list ap)
{
    int v;

    v = va_arg(ap, int);
    _enum_store_real(c, val, v);
}

static void enum_store_modelized(table_t *UNUSED(t), column_t *c, value_t *val, char *ptr)
{
    _enum_store_real(c, val, *((int *) ptr));
}

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

static void _string_store_real(table_t *t, column_t *c, value_t *val, const char *s_utf8)
{
    char *s_local;
    error_t *error;

    // TODO: real error handling! Let caller handle this by adding a error_t **error in argument?
error = NULL;
    if (NULL == s_utf8) {
        s_local = (char *) "-";
        val->f = FALSE;
        val->l = STR_LEN("-");
    } else {
        if (HAS_FLAG(c->type, TABLE_TYPE_DELEGATE) || HAS_FLAG(t->flags, TABLE_FLAG_DELEGATE)) {
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

static void string_store(table_t *t, column_t *c, value_t *val, va_list ap)
{
    const char *s_utf8;

    s_utf8 = va_arg(ap, const char *);
    _string_store_real(t, c, val, s_utf8);
}

static void string_store_modelized(table_t *t, column_t *c, value_t *val, char *ptr)
{
    _string_store_real(t, c, val, *((char **) ptr));
}

static void date_colinit_from_ap(table_t *UNUSED(t), column_t *c, va_list UNUSED(ap))
{
    if (max_date_len > c->max_len) {
        c->len = c->min_len = c->max_len = max_date_len;
    }
}

static void date_colinit_from_model(table_t *UNUSED(t), column_t *c, const model_field_t *UNUSED(f))
{
    if (max_date_len > c->max_len) {
        c->len = c->min_len = c->max_len = max_date_len;
    }
}

static void _date_store_real(value_t *val, time_t v, bool with_time)
{
    val->f = FALSE;
    val->v = (uintptr_t) v;
    val->l = with_time ? max_datetime_len : max_date_len;
}

static void date_store(table_t *UNUSED(t), column_t *UNUSED(c), value_t *val, va_list ap)
{
    time_t v;

    v = va_arg(ap, time_t);
    _date_store_real(val, v, FALSE);
}

static void date_store_modelized(table_t *UNUSED(t), column_t *UNUSED(c), value_t *val, char *ptr)
{
    _date_store_real(val, *((time_t *) ptr), FALSE);
}

static void datetime_colinit_from_ap(table_t *UNUSED(t), column_t *c, va_list UNUSED(ap))
{
    if (max_datetime_len > c->max_len) {
        c->len = c->min_len = c->max_len = max_datetime_len;
    }
}

static void datetime_colinit_from_model(table_t *UNUSED(t), column_t *c, const model_field_t *UNUSED(f))
{
    if (max_datetime_len > c->max_len) {
        c->len = c->min_len = c->max_len = max_datetime_len;
    }
}

static void datetime_store(table_t *UNUSED(t), column_t *UNUSED(c), value_t *val, va_list ap)
{
    time_t v;

    v = va_arg(ap, time_t);
    _date_store_real(val, v, TRUE);
}

static void datetime_store_modelized(table_t *UNUSED(t), column_t *UNUSED(c), value_t *val, char *ptr)
{
    _date_store_real(val, *((time_t *) ptr), TRUE);
}

static int strcmpp_asc(QSORT_CB_ARGS(const void *p1, const void *p2, void *arg))
{
    assert(NULL != p1);
    assert(NULL != p2);

//     return strcmp(*(char * const *) p2, *(char * const *) p1);
    return strcoll((const char *) (*((row_t **) p1))->values[*(size_t *) arg].v, (const char *) (*((row_t **) p2))->values[*(size_t *) arg].v);
}

static int strcmpp_desc(QSORT_CB_ARGS(const void *p1, const void *p2, void *arg))
{
    assert(NULL != p1);
    assert(NULL != p2);

    return strcoll((const char *) (*((row_t **) p2))->values[*(size_t *) arg].v, (const char *) (*((row_t **) p1))->values[*(size_t *) arg].v);
}

static int intcmpp_asc(QSORT_CB_ARGS(const void *p1, const void *p2, void *arg))
{
    return (*((row_t **) p1))->values[*(size_t *) arg].v - (*((row_t **) p2))->values[*(size_t *) arg].v;
}

static int intcmpp_desc(QSORT_CB_ARGS(const void *p1, const void *p2, void *arg))
{
    return (*((row_t **) p2))->values[*(size_t *) arg].v - (*((row_t **) p1))->values[*(size_t *) arg].v;
}

static struct {
    void (*colinit_from_ap)(table_t *, column_t *, va_list);
    void (*colinit_from_model)(table_t *, column_t *, const model_field_t *f);
    void (*store)(table_t *, column_t *, value_t *, va_list); // callback for table_store
    void (*store_modelized)(table_t *, column_t *, value_t *, char *);
    size_t (*breakline)(size_t, const char *, size_t, break_t **);
#if 0
    size_t (*display)(void); // callback for table_display
    void (*free)(void); // callback for table_display (free after displaying)
#endif
    CmpFuncArg sort[_TABLE_SORT_COUNT];
} type_handlers[] = {
    [ TABLE_TYPE_INT ]      = { NULL,                     NULL,                        int_store,      int_store_modelized,      NULL,         { [ TABLE_SORT_ASC ] = intcmpp_asc, [ TABLE_SORT_DESC ] = intcmpp_desc } },
    [ TABLE_TYPE_BOOL ]     = { bool_colinit_from_ap,     bool_colinit_from_model,     bool_store,     bool_store_modelized,     NULL,         { [ TABLE_SORT_ASC ] = intcmpp_asc, [ TABLE_SORT_DESC ] = intcmpp_desc } },
    [ TABLE_TYPE_ENUM ]     = { enum_colinit_from_ap,     enum_colinit_from_model,     enum_store,     enum_store_modelized,     NULL,         { [ TABLE_SORT_ASC ] = intcmpp_asc, [ TABLE_SORT_DESC ] = intcmpp_desc } },
    [ TABLE_TYPE_STRING ]   = { NULL,                     NULL,                        string_store,   string_store_modelized,   string_break, { [ TABLE_SORT_ASC ] = strcmpp_asc, [ TABLE_SORT_DESC ] = strcmpp_desc } },
    [ TABLE_TYPE_DATE ]     = { date_colinit_from_ap,     date_colinit_from_model,     date_store,     date_store_modelized,     NULL,         { [ TABLE_SORT_ASC ] = intcmpp_asc, [ TABLE_SORT_DESC ] = intcmpp_desc } },
    [ TABLE_TYPE_DATETIME ] = { datetime_colinit_from_ap, datetime_colinit_from_model, datetime_store, datetime_store_modelized, NULL,         { [ TABLE_SORT_ASC ] = intcmpp_asc, [ TABLE_SORT_DESC ] = intcmpp_desc } }
};

table_t *table_new(size_t columns_count, ...)
{
    size_t i;
    table_t *t;
    va_list ap;

    t = mem_new(*t);
    t->flags = 0;
    t->strings = NULL;
    t->columns_count = columns_count;
    t->columns = mem_new_n(*t->columns, columns_count);
    t->rows = dptrarray_new(NULL, row_destroy, NULL);
    // string "title", int type
    va_start(ap, columns_count);
    for (i = 0; i < columns_count; i++) {
        t->columns[i].title = va_arg(ap, const char *);
        t->columns[i].type = va_arg(ap, model_field_type_t);
        cplen(t->columns[i].title, &t->columns[i].title_len, NULL);
        t->columns[i].len = t->columns[i].min_len = t->columns[i].max_len = t->columns[i].title_len;
        assert(TABLE_TYPE(t->columns[i].type) <= _TABLE_TYPE_LAST);
        if (NULL != type_handlers[TABLE_TYPE(t->columns[i].type)].colinit_from_ap) {
            type_handlers[TABLE_TYPE(t->columns[i].type)].colinit_from_ap(t, &t->columns[i], ap);
        }
    }
    va_end(ap);

    return t;
}

table_t *table_new_from_model(const model_t *model, uint32_t flags)
{
    size_t i;
    table_t *t;
    const model_field_t *f;

    t = mem_new(*t);
    t->flags = flags;
    t->model = model;
    t->strings = NULL;
    t->columns_count = 0;
    for (f = model->fields; NULL != f->ovh_name; f++) {
        if (!HAS_FLAG(f->flags, MODEL_FLAG_INTERNAL)) {
            ++t->columns_count;
        }
    }
    t->columns = mem_new_n(*t->columns, t->columns_count);
    t->rows = dptrarray_new(NULL, row_destroy, NULL);
    for (i = 0, f = t->model->fields; NULL != f->ovh_name; f++) {
        if (!HAS_FLAG(f->flags, MODEL_FLAG_INTERNAL)) {
            t->columns[i].title = gettext(f->i18n_key);
            t->columns[i].type = f->type;
            cplen(t->columns[i].title, &t->columns[i].title_len, NULL);
            t->columns[i].len = t->columns[i].min_len = t->columns[i].max_len = t->columns[i].title_len;
            if (NULL != type_handlers[t->columns[i].type].colinit_from_model) {
                type_handlers[t->columns[i].type].colinit_from_model(t, &t->columns[i], f);
            }
            ++i;
        }
    }
    
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

        type_handlers[TABLE_TYPE(t->columns[i].type)].store(t, &t->columns[i], &r->values[i], ap);
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

void table_store_modelized(table_t *t, modelized_t *ptr)
{
//     bool ok;
    size_t i;
    row_t *r;
//     char *buffer[512];
    const model_field_t *f;

    assert(NULL != t);
    assert(NULL != ptr);
    assert(t->model == ptr->model);

    i = 0;
    r = mem_new(*r);
    r->values = mem_new_n(*r->values, t->columns_count);
    for (f = t->model->fields, i = 0; NULL != f->ovh_name; f++) {
        if (!HAS_FLAG(f->flags, MODEL_FLAG_INTERNAL)) {
            assert(t->columns[i].type <= _TABLE_TYPE_LAST);
            assert(NULL != type_handlers[t->columns[i].type].store_modelized);

            type_handlers[t->columns[i].type].store_modelized(t, &t->columns[i], &r->values[i], ((char *) ptr) + f->offset);
            ++i;
        }
    }
    dptrarray_push(t->rows, r);
}

typedef enum {
    TABLE_FIRST_LINE,
    TABLE_MIDDLE_LINE,
    TABLE_LAST_LINE,
} table_separator_line_t;

static void table_print_separator_line(table_t *t, FILE *fout, table_separator_line_t line_type)
{
    size_t i, j;

    fputs(borders[border_type].o[line_type][0], fout);
    for (i = 0; i < t->columns_count; i++) {
        for (j = 0; j < t->columns[i].len + 2; j++) {
            fputs(borders[border_type].h, fout);
        }
        fputs(borders[border_type].o[line_type][(i == t->columns_count - 1) ? 2 : 1], fout);
    }
    fputc('\n', fout);
}

/**
 * Sort table rows based on the values of a specific column
 *
 * @param t the table to sort
 * @param colno the index of the column to sort on
 * @param order asc or desc order (one of the constants TABLE_SORT_(A|DE)SC)
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
    FILE *fout;
    size_t i, k;
    Iterator it;
    break_t **breaks;
    size_t *breaks_count;

    assert(NULL != t);

    fout = stdout;
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
    if (isatty(STDOUT_FILENO)) {
        int height;

        height = console_height();
        if (height > 0 && dptrarray_length(t->rows) + 4 > (size_t) height) { // TODO: prepare line breaking in a first step to known exactely how many lines will be displayed
            const char *pager;

            if (NULL == (pager = getenv("PAGER"))) {
                pager = DEFAULT_PAGER;
            }
            if (NULL == (fout = popen(pager, "w"))) {
                fout = stdout;
            }
        }
    }
    if (!HAS_FLAG(flags, TABLE_FLAG_NO_HEADERS)) {
        // +--- ... ---+
        table_print_separator_line(t, fout, TABLE_FIRST_LINE);
        // | ... | : column headers
        fputs(borders[border_type].v, fout);
        for (i = 0; i < t->columns_count; i++) {
            fputc(' ', fout);
            fputs(t->columns[i].title, fout);
//             fprintf(fout, " %*c |", t->columns[i].len - t->columns[i].title_len, ' ');
            for (k = t->columns[i].title_len; k < t->columns[i].len; k++) {
                fputc(' ', fout);
            }
            fputc(' ', fout);
            fputs(borders[border_type].v, fout);
        }
        fputc('\n', fout);
    }
    if (dptrarray_length(t->rows) > 0) {
        // +--- ... ---+
        table_print_separator_line(t, fout, HAS_FLAG(flags, TABLE_FLAG_NO_HEADERS) ? TABLE_FIRST_LINE : TABLE_MIDDLE_LINE);
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
#if 0
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
#else
                assert(TABLE_TYPE(t->columns[i].type) <= _TABLE_TYPE_LAST);
                if (NULL != type_handlers[TABLE_TYPE(t->columns[i].type)].breakline) {
                    breaks_count[i] = type_handlers[TABLE_TYPE(t->columns[i].type)].breakline(t->columns[i].len, (const char *) r->values[i].v, r->values[i].l, &breaks[i]);
                    if (breaks_count[i] > lines_needed) {
                        lines_needed = breaks_count[i];
                    }
                }
#endif
            }
            for (j =  0; j < lines_needed; j++) {
                fputs(borders[border_type].v, fout);
                for (i = 0; i < t->columns_count; i++) {
                    fputc(' ', fout);
                    if (0 == j || j < breaks_count[i]) {
                        size_t written;

                        switch (TABLE_TYPE(t->columns[i].type)) {
                            case TABLE_TYPE_STRING:
                                fputs(breaks[i][j].part, fout);
                                written = breaks[i][j].charlen;
                                break;
                            case TABLE_TYPE_INT:
                                written = fprintf(fout, "%*d", (int) t->columns[i].len, (int) r->values[i].v);
                                break;
                            case TABLE_TYPE_ENUM:
                                fputs(t->columns[i].enum_values[r->values[i].v], fout);
                                written = t->columns[i].enum_values_len[r->values[i].v];
                                break;
                            case TABLE_TYPE_BOOL:
                                fputs(false_true_string[r->values[i].v], fout);
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
                                fputs(buffer, fout);
                                break;
                            }
                            default:
                                assert(FALSE);
                                break;
                        }
                        for (k = written; k < t->columns[i].len; k++) {
                            fputc(' ', fout);
                        }
                    } else {
                        fprintf(fout, "%*c", (int) t->columns[i].len, ' ');
                    }
                    fputc(' ', fout);
                    fputs(borders[border_type].v, fout);
                }
                fputc('\n', fout);
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
    }
    // +--- ... ---+
    table_print_separator_line(t, fout, TABLE_LAST_LINE);
    if (stdout != fout) {
        pclose(fout);
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
    extern bool nls_ctor(error_t **error);
#endif /* WITH_NLS */
    extern bool convert_ctor(error_t **error);

    convert_ctor(NULL);
#ifdef WITH_NLS
    nls_ctor(NULL);
#endif /* WITH_NLS */
    table_ctor(NULL);

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
