#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <strings.h>
#include "model.h"

const size_t model_type_size_map[] = {
    [ MODEL_TYPE_INT ]      = sizeof(int),
    [ MODEL_TYPE_BOOL ]     = sizeof(bool),
    [ MODEL_TYPE_DATE ]     = sizeof(time_t),
    [ MODEL_TYPE_ENUM ]     = sizeof(int),
    [ MODEL_TYPE_STRING ]   = sizeof(char *),
    [ MODEL_TYPE_DATETIME ] = sizeof(time_t),
};

/**
 * Free memory used by a data
 * Only for stack allocated variable!
 * (created by model_new)
 *
 * @param ptr the data to free
 */
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

/**
 * Use a heap allocated variable as a container.
 * Be sure sizeof(*ptr) >= model->size bytes
 * and cast its address on call to (modelized_t *).
 *
 * Eg:
 *   foo_t foo;
 *   modelized_init(foo_model, (modelized_t *) &foo)
 *
 * @param model the description of the data represented by *ptr*
 * @param ptr the data to initialize
 */
void modelized_init(const model_t *model, modelized_t *ptr)
{
    if (0 != model->size) {
        bzero(ptr, model->size);
    }
    ptr->model = model;
    ptr->persisted = ptr->changed = FALSE;
}

/**
 * Allocate a new data which is described by a model
 *
 * @param model the data description
 *
 * @return a new "blank" allocated data
 */
modelized_t *modelized_new(const model_t *model)
{
    modelized_t *ptr;

    assert(0 != model->size);

    ptr = malloc(model->size);
    modelized_init(model, ptr);

    return ptr;
}

/**
 * TODO
 *
 * @param ptr
 * @param error
 *
 * @return FALSE on failure
 */
bool modelized_save(modelized_t *ptr, error_t **error)
{
    if (NULL != ptr->model->backend && NULL != ptr->model->backend->save) {
        return ptr->model->backend->save(ptr, ptr->model->backend_data, error);
    }

    return FALSE;
}

/**
 * Copy a data which is described by a model
 *
 * @param src the data to copy
 *
 * @return the copy of *src*
 */
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

/**
 * Get a model field from its name
 *
 * @param model the model
 * @param field_name the field name
 * @param field_name_len the length of field_name
 *
 * @return the field or NULL if unknown
 */
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

/**
 * Creates a new model
 *
 * @param name its name
 * @param size the size of a data
 * @param fields its fields which describe it
 * @param fields_count the number of fields
 *
 * @return the allocated model
 */
model_t *model_new(const char *name, size_t size, model_field_t *fields, size_t fields_count, model_backend_t *backend, error_t **error)
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
    model->backend = backend;
    model->backend_data = NULL;
    if (NULL != model->backend && NULL != model->backend->init) {
        model->backend_data = model->backend->init(model, error);
    }

    return model;
}

/**
 * Destroy a model
 *
 * @param model the model to destroy
 */
void model_destroy(model_t *model)
{
    if (NULL != model->backend && NULL != model->backend->free) {
        model->backend->free(model->backend_data);
    }
    free(model);
}

#if 0
void modelized_belongs_to(modelized_t *owner, modelized_t *owned)
{
    //
}

void modelized_has_one(modelized_t *owner, modelized_t *owned, bool reverse)
{
    //
    if (reverse) {
        modelized_belongs_to(owner, owned);
    }
}

void modelized_has_many(modelized_t *owner, modelized_t *owned, bool reverse)
{
    //
    if (reverse) {
        modelized_belongs_to(owner, owned);
    }
}

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
