#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <strings.h>
#include "model.h"
#include "common.h"

void modelized_destroy(modelized_t *ptr)
{
    assert(NULL != ptr->model);
    if (0 != ptr->model->size) {
        const model_field_t *f;

        if (NULL != ptr->model->fields) {
            for (f = ptr->model->fields; NULL != f->column_name; f++) {
                if (MODEL_TYPE_STRING == f->type) {
                    char *v;

                    v = *((char **) (((char *) ptr) + f->offset));
                    if (NULL != v) {
                        free(v);
                    }
                }
            }
            free(ptr);
        }
    }
}

void modelized_init(const model_t *model, modelized_t *ptr)
{
    if (0 != model->size) {
        bzero(ptr, model->size);
    }
    ptr->model = model;
}

modelized_t *modelized_new(const model_t *model)
{
    modelized_t *ptr;

    assert(0 != model->size);
    ptr = malloc(model->size);
    modelized_init(model, ptr);

    return ptr;
}

const model_field_t *model_find_field_by_name(const model_t *model, const char *field_name, size_t field_name_len)
{
    const model_field_t *f, *match;

    for (match = NULL, f = model->fields; NULL == match && NULL != f->column_name; f++) {
        if (0 == strncmp(f->column_name, field_name, field_name_len)) {
            match = f;
        }
    }

    return match;
}

#if 0
size_t model_fields_count(const model_t *model, uint32_t flags, bool negated)
{
    size_t columns_count;
    const model_field_t *f;

    for (columns_count = 0, f = model->fields; NULL != f->column_name; f++) {
        if (negated == (0 != HAS_FLAG(f->flags, flags))) {
            ++columns_count;
        }
    }

    return columns_count;
}
#endif

#if 0
void modelized_name_to_s(modelized_t *ptr, char *buffer, size_t buffer_size)
{
    //
}

void modelized_id_to_s(modelized_t *ptr, char *buffer, size_t buffer_size)
{
    //
}

void modelized_id_from_s(modelized_t *ptr, argument_t *arg, char *buffer) // in graph.[ch]
{
    //
}
#endif