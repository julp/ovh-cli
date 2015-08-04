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
            for (f = ptr->model->fields; NULL != f->ovh_name; f++) {
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

modelized_t *modelized_copy(modelized_t *src/*, bool copy*/)
{
    modelized_t *copy;

    assert(NULL != src->model);
    assert(0 != src->model->size);

    copy = malloc(src->model->size);
    memcpy(copy, src, src->model->size);
    if (/*copy && */0 != src->model->size) {
        const model_field_t *f;

        if (NULL != src->model->fields) {
            for (f = src->model->fields; NULL != f->ovh_name; f++) {
                if (MODEL_TYPE_STRING == f->type) {
                    char *v;

                    v = *((char **) (((char *) src) + f->offset));
                    if (NULL != v) {
                        *((char **) (((char *) copy) + f->offset)) = strdup(v);
                    }
                }
            }
        }
    }

    return copy;
}

const model_field_t *model_find_field_by_name(const model_t *model, const char *field_name, size_t field_name_len)
{
    const model_field_t *f, *match;

    for (match = NULL, f = model->fields; NULL == match && NULL != f->ovh_name; f++) {
        if (0 == strncmp(f->ovh_name, field_name, field_name_len)) {
            match = f;
        }
    }

    return match;
}

model_t *model_new(const char *name, size_t size, model_field_t *fields, size_t fields_count)
{
    model_t *model;

    model = mem_new(*model);
    model->pk = NULL;
    model->name = name;
    model->size = size;
    model->fields = fields;
    model->fields_count = fields_count;
    if (0 != model->size) {
        const model_field_t *f;

        if (NULL != model->fields) {
            for (f = model->fields; NULL != f->ovh_name; f++) {
                if (HAS_FLAG(f->flags, MODEL_FLAG_PRIMARY)) {
                    model->pk = f;
                }
            }
        }
    }

    return model;
}

void model_destroy(model_t *model)
{
    free(model);
}

void modelized_belongs_to(modelized_t *owner, modelized_t *owned)
{
    //
}

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