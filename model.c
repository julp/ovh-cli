#include <stdlib.h>
#include <inttypes.h>
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
