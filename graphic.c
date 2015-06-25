#include <float.h>
#include "common.h"
#include "date.h"
#include "graphic.h"

typedef struct graphic_link_t {
    time_t sse; // seconds since epoch
    double value;
    struct graphic_link_t *next;
} graphic_link_t;

struct graphic_t {
    double max;
    double min;
    size_t count;
    graphic_link_t *head;
    graphic_link_t *tail;
};

graphic_t *graphic_new(void)
{
    graphic_t *g;

    g = mem_new(*g);
    g->max = DBL_MIN;
    g->min = DBL_MAX;
    g->count = 0;
    g->head = g->tail = NULL;

    return g;
}

void graphic_store(graphic_t *g, time_t sse, double value)
{
    graphic_link_t *gl;

    gl = mem_new(*gl);
    gl->sse = sse;
    gl->next = NULL;
    gl->value = value;
    if (NULL == g->tail) {
        g->head = g->tail = gl;
    } else if (sse > g->tail->sse) {
        g->tail->next = gl;
        g->tail = gl;
    } else {
        debug("TODO");
    }
    if (value > g->max) {
        g->max = value;
    }
    if (value < g->min) {
        g->min = value;
    }
}

#define NB_TICKS 8
extern int console_width(void);

void graphic_display(graphic_t *g)
{
    int i, l;
    int width;
    double step;
    char buffer[512];
    graphic_link_t *gl;
    int row_header_len;

    row_header_len = STR_LEN(" dd/mm/YY HH:ii:ss |");
    width = console_width();
    width -= row_header_len;
    step = (g->max - g->min) / NB_TICKS;
    for (gl = g->head; NULL != gl; gl = gl->next) {
        putchar(' ');
        timestamp_to_localtime(gl->sse, buffer, ARRAY_SIZE(buffer));
        fputs(buffer, stdout);
        putchar(' ');
        putchar('|');
        l = (int) (((gl->value - g->min) / (g->max - g->min)) * width);
        for (i = 0; i < l; i++) {
            putchar('#');
        }
        putchar('\n');
    }
    for (i = 0; i < row_header_len - 1; i++) {
        putchar(' ');
    }
    for (i = 0; i < width; i++) {
        putchar(0 == i % (int) ((step * width) / NB_TICKS) ? '+' : '-');
    }
    putchar('\n');
    printf("%*c", row_header_len, '0');
    for (l = i = 1; i <= NB_TICKS; i++) {
        printf("%*c%.2f", (int) ((step * width) / NB_TICKS) - l, ' ', step * i);
        l = snprintf(buffer, ARRAY_SIZE(buffer), "%.2f", step * i);
    }
    putchar('\n');
}

void graphic_destroy(graphic_t *g)
{
    graphic_link_t *current, *next;

    current = g->head;
    while (NULL != current) {
        next = current->next;
        free(current);
        current = next;
    }
    free(g);
}

// INITIALIZER_DECL(graphic_test);
INITIALIZER_P(graphic_test)
{
    size_t i, c;
    graphic_t *g;

    c = 0;
    g = graphic_new();
    for (i = 8; 0 != i; i--) {
        graphic_store(g, c++, i + 1);
    }
    for (i = 0; i < 8; i++) {
        graphic_store(g, c++, i + 1);
    }
    graphic_display(g);
    graphic_destroy(g);
}
