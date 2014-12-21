#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>

#include "common.h"
#include "table.h"
#include "date.h"
#include "modules/conv.h"
#include "struct/dptrarray.h"

typedef struct {
    const char *title;
    column_type_t type;
    size_t min_len, max_len, len, title_len;
} column_t;

struct table_t {
    DPtrArray *rows;
    column_t *columns;
    size_t columns_count;
    char *false_true_string[2];
    size_t false_true_len[2], max_false_true_len;
};

typedef struct {
    bool f;
    size_t l;
    uintptr_t v;
} value_t;

typedef struct {
    value_t *values;
} row_t;

#if 0
    _("false")
    _("true")
#endif

/**
 * NOTE:
 * - column titles are expected to be already in "local" charset (gettext, if they are translated, will do it for us)
 * - same for "true"/"false"
 * - columns data are expected to be in UTF-8, we handle their conversion
 **/

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
#include <locale.h>
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

    t = mem_new(*t);
    // speed up true/false conversions
    t->false_true_string[FALSE] = _("false");
    cplen(t->false_true_string[FALSE], &t->false_true_len[FALSE], NULL);
    t->false_true_string[TRUE] = _("true");
    cplen(t->false_true_string[TRUE], &t->false_true_len[TRUE], NULL);
    t->max_false_true_len = MAX(t->false_true_len[FALSE], t->false_true_len[TRUE]);
    // </>
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
                char *s_local;
                error_t *error;
                const char *s_utf8;

                // TODO: real error handling! Let caller handle this by adding a error_t **error in argument?
error = NULL;
                if (NULL == (s_utf8 = va_arg(ap, const char *))) {
                    s_local = (char *) "-";
                    r->values[i].f = FALSE;
                    r->values[i].l = STR_LEN("-");
                } else {
                    convert_string_utf8_to_local(s_utf8, strlen(s_utf8), &s_local, NULL, &error);
print_error(error);
error = NULL;
                    cplen(s_local, &r->values[i].l, &error);
print_error(error);
                    r->values[i].f = s_local != s_utf8;
                }
                r->values[i].v = (uintptr_t) s_local; // TODO: conversion UTF-8 => local
//                 r->values[i].l = s_local_len;
                if (r->values[i].l > t->columns[i].max_len) {
                    t->columns[i].max_len = r->values[i].l;
                }
                break;
            }
            case TABLE_TYPE_INT:
            {
                int v;

                v = va_arg(ap, int);
                r->values[i].v = (uintptr_t) v;
                r->values[i].l = snprintf(NULL, 0, "%d", v);
                if (r->values[i].l > t->columns[i].max_len) {
                    t->columns[i].len = t->columns[i].min_len = t->columns[i].max_len = r->values[i].l;
                }
                break;
            }
            case TABLE_TYPE_BOOLEAN:
            {
                bool v;

                v = va_arg(ap, bool);
                r->values[i].v = (uintptr_t) v;
                r->values[i].l = t->max_false_true_len;
                if (r->values[i].l > t->columns[i].max_len) {
                    t->columns[i].len = t->columns[i].min_len = t->columns[i].max_len = t->max_false_true_len;
                }
                break;
            }
            case TABLE_TYPE_DATETIME:
            {
                struct tm v;
                char buffer[512];
                struct tm ltm = { 0 };

                v = va_arg(ap, struct tm);
                if (0 == memcmp(&v, &ltm, sizeof(ltm))) {
                    r->values[i].v = (uintptr_t) "-";
                    r->values[i].l = STR_LEN("-");
                } else {
                    r->values[i].l = strftime(buffer, ARRAY_SIZE(buffer), "%x %X", &v);
                    assert(r->values[i].l > 0);
                    r->values[i].v = (uintptr_t) strdup(buffer);
                    r->values[i].f = TRUE;
                }
                if (r->values[i].l > t->columns[i].max_len) {
                    t->columns[i].len = t->columns[i].min_len = t->columns[i].max_len = r->values[i].l;
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

static int strcmpp(const void *p1, const void *p2, void *arg)
{
//     return strcmp(*(char * const *) p2, *(char * const *) p1);
    return strcoll((const char *) (*((row_t **) p1))->values[*(size_t *) arg].v, (const char *) (*((row_t **) p2))->values[*(size_t *) arg].v);
}

static int intcmpp(const void *p1, const void *p2, void *arg)
{
    return (*((row_t **) p1))->values[*(size_t *) arg].v - (*((row_t **) p2))->values[*(size_t *) arg].v;
}

void table_sort(table_t *t, size_t colno/*, int order*/)
{
    CmpFuncArg cmpfn;

    assert(NULL != t);
    assert(colno < t->columns_count);

#if 0
    CmpFuncArg cmpfn[/* nb table types */][/* 2 : asc/desc */] = {
        [ TABLE_TYPE_INT ] = {},
        [ TABLE_TYPE_STRING ] = {},
        [ TABLE_TYPE_BOOLEAN ] = {},
    };
#endif
    switch (t->columns[colno].type) {
        case TABLE_TYPE_STRING:
            cmpfn = strcmpp;
            break;
        case TABLE_TYPE_INT:
            cmpfn = intcmpp;
            break;
        default:
            assert(FALSE);
    }
    dptrarray_sort(t->rows, cmpfn, &colno);
}

void table_display(table_t *t, uint32_t flags)
{
    int width;
    size_t i, k;
    Iterator it;
    break_t **breaks;
    size_t *breaks_count;

    assert(NULL != t);

//     if (dptrarray_size(t->rows) > 0) {
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
            switch (t->columns[i].type) {
                case TABLE_TYPE_STRING:
                    breaks_count[i] = string_break(t->columns[i].len, (const char *) r->values[i].v, r->values[i].l, &breaks[i]);
                    if (breaks_count[i] > lines_needed) {
                        lines_needed = breaks_count[i];
                    }
                    break;
                case TABLE_TYPE_INT:
                case TABLE_TYPE_BOOLEAN:
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
                if (0 == j || j < breaks_count[i]) {
                    switch (t->columns[i].type) {
                        case TABLE_TYPE_STRING:
                        {
                            putchar(' ');
                            fputs(breaks[i][j].part, stdout);
                            for (k = breaks[i][j].charlen; k < t->columns[i].len; k++) {
                                putchar(' ');
                            }
                            fputs(" |", stdout);
                            break;
                        }
                        case TABLE_TYPE_INT:
                            printf(" %*d |", (int) t->columns[i].len, (int) r->values[i].v);
                            break;
                        case TABLE_TYPE_BOOLEAN:
                            putchar(' ');
                            fputs(t->false_true_string[r->values[i].v], stdout);
                            for (k = t->false_true_len[r->values[i].v]; k < t->columns[i].len; k++) {
                                putchar(' ');
                            }
                            fputs(" |", stdout);
                            break;
                        case TABLE_TYPE_DATETIME:
                            putchar(' ');
                            fputs((const char *) r->values[i].v, stdout);
                            for (k = r->values[i].l; k < t->columns[i].len; k++) {
                                putchar(' ');
                            }
                            fputs(" |", stdout);
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
        for (i = 0; i < t->columns_count; i++) {
            if (TABLE_TYPE_STRING == t->columns[i].type) {
                if (breaks_count[i] > 1) {
                    for (j = 0; j < breaks_count[i]; j++) {
                        free((void *) breaks[i][j].part);
                    }
                }
                free(breaks[i]);
                // <TODO: better place in row_destroy>
                if (r->values[i].f) {
                    free((void *) r->values[i].v);
                }
                // </TODO>
            }
        }
    }
    iterator_close(&it);
    free(breaks);
    free(breaks_count);
    // +--- ... ---+
    table_print_separator_line(t);
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

// can't use constructor, output_encoding (modules/conv.c is not yet set)
// INITIALIZER_DECL(table_test);
INITIALIZER_P(table_test)
{
    table_t *t;

    t = table_new(4, "id", TABLE_TYPE_INT, "subdomain", TABLE_TYPE_STRING, "target", TABLE_TYPE_STRING, "éïàùçè", TABLE_TYPE_STRING);
    table_store(t, 1, "abc", "def", "");
    table_store(t, 2, "ghi", "jkl", long_string);
    table_store(t, 3, "mno", long_string, "pqr");
    table_store(t, 4, "stu", long_string, long_string);
    table_store(t, 5, "é", "é", "é");
    table_store(t, 6, "é", "é", "abc\ndéf");
    table_sort(t, 1);
//     table_sort(t, 0);
    table_display(t, TABLE_FLAG_NONE);
    table_destroy(t);
}
