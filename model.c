#include <stdlib.h>
#include <inttypes.h>
#include "model.h"

void modelized_destroy(const model_t *model, void *ptr)
{
    if (0 != model->size) {
        const model_field_t *f;

        for (f = model->fields; NULL != f->column_name; f++) {
            if (MODEL_TYPE_STRING == f->type) {
                char *v;

                v = *((char **) (ptr + f->offset));
                if (NULL != v) {
                    free(v);
                }
            }
        }
        free(ptr);
    }
}
