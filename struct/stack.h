#ifndef STACK_H

# define STACK_H

# include "common.h"

typedef struct _Stack Stack;

Stack *stack_new(DtorFunc df) WARN_UNUSED_RESULT;
void stack_push(Stack *, void *);
void *stack_pop(Stack *);
void *stack_top(Stack *);
bool stack_empty(Stack *);
void stack_destroy(Stack *);
size_t stack_length(Stack *);

#endif /* !STACK_H */
