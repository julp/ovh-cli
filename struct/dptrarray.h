#ifndef D_PTR_ARRAY_H

# define D_PTR_ARRAY_H

# include "common.h"
# include "iterator.h"

 typedef struct {
    void **data;
    size_t length;
    size_t allocated;
    DupFunc duper;
    void *default_value;
    DtorFunc dtor_func;
} DPtrArray;

# define dptrarray_at_unsafe(/*DPtrArray **/ da, /*uint*/ offset, T) \
    ((T *) ((da)->data)[(offset)])

void *dptrarray_at(DPtrArray *, size_t);
void dptrarray_clear(DPtrArray *);
void dptrarray_destroy(DPtrArray *);
void dptrarray_insert(DPtrArray *, size_t, void *);
size_t dptrarray_length(DPtrArray *);
DPtrArray *dptrarray_new(DupFunc, DtorFunc, void *) WARN_UNUSED_RESULT;
void *dptrarray_pop(DPtrArray *);
void *dptrarray_push(DPtrArray *, void *);
void dptrarray_remove_at(DPtrArray *, size_t);
void dptrarray_remove_range(DPtrArray *, size_t, size_t);
void dptrarray_set_size(DPtrArray *, size_t);
void *dptrarray_shift(DPtrArray *);
DPtrArray *dptrarray_sized_new(size_t, DupFunc, DtorFunc, void *) WARN_UNUSED_RESULT;
void dptrarray_swap(DPtrArray *, size_t, size_t);
void dptrarray_unshift(DPtrArray *, void *);
void *dptrarray_to_array(DPtrArray *, int, int);

void dptrarray_to_iterator(Iterator *, DPtrArray *);

#endif /* !D_PTR_ARRAY_H */
