#ifndef GRAPH_MAIN_H

# define GRAPH_MAIN_H

# include <histedit.h>

typedef struct {
    graph_t *graph;
    Tokenizer *tokenizer;
} editline_data_t;

unsigned char graph_complete(EditLine *, int);

#endif /* !GRAPH_MAIN_H */
