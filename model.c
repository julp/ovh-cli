#include <time.h> /* time_t */
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <strings.h>
#include "model.h"

#ifdef HAVE_LIBBSD_STRLCPY
# include <bsd/string.h>
#endif /* HAVE_LIBBSD_STRLCPY */

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
 * Persists a data (a new one or save changes)
 *
 * @param ptr the data to delete
 * @param error the error to populate on failure
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
 * Delete a data from its backend
 *
 * @param ptr the data to delete
 * @param error the error to populate on failure
 *
 * @return FALSE on failure
 */
bool modelized_delete(modelized_t *ptr, error_t **error)
{
    if (NULL != ptr->model->backend && NULL != ptr->model->backend->delete) {
        return ptr->model->backend->delete(ptr, ptr->model->backend_data, error);
    }

    return FALSE;
}

/**
 * Initialize an iterator to loop on all specified data
 *
 * @param it the iterator to initialize
 * @param model the model which describe the data
 * @param error the error to populate on failure
 *
 * @return FALSE on failure
 */
bool modelized_all(Iterator *it, const model_t *model, error_t **error)
{
    if (NULL != model->backend && NULL != model->backend->all) {
        return model->backend->all(it, model, model->backend_data, error);
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
 * @param name_field_name the name of the field which refers to the name of the data
 * @param backend the callbacks for reading or writing data
 * @param error the error to populate on failure
 *
 * @return the allocated model
 */
model_t *model_new(const char *name, size_t size, model_field_t *fields, size_t fields_count, const char *name_field_name, model_backend_t *backend, error_t **error)
{
    model_t *model;

    assert(NULL != name_field_name && '\0' != *name_field_name);

    model = mem_new(*model);
    model->name = name;
    model->pk = NULL;
    model->name_field_name = NULL;
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
                if (0 == strcmp(name_field_name, f->ovh_name)) {
                    model->name_field_name = f;
                }
            }
        }
    }
    model->backend = backend;
    model->backend_data = NULL;
    if (NULL != model->backend && NULL != model->backend->init) {
        model->backend_data = model->backend->init(model, error);
    }

    assert(NULL != model->name_field_name);

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

/**
 * Get a the name (used in completion) of a data
 *
 * @param ptr the data
 * @param buffer the output buffer to fill in
 * @param buffer_size the capacity of *buffer*
 *
 * @return the number of bytes copied to *buffer*
 */
size_t modelized_name_to_s(modelized_t *ptr, char *buffer, size_t buffer_size)
{
    size_t buffer_length;

    assert(NULL != ptr);
    assert(NULL != buffer);
    assert(NULL != ptr->model);
    assert(NULL != ptr->model->name_field_name);

    buffer_length = 0;
    switch (ptr->model->name_field_name->type) {
        case MODEL_TYPE_INT:
        {
            int ret;

            ret = snprintf(buffer, buffer_size, "%d", VOIDP_TO_X(ptr, ptr->model->name_field_name->offset, int));
            assert(ret > 0 && ((size_t) ret) <= buffer_size);
            buffer_length = (size_t) ret;
            break;
        }
        case MODEL_TYPE_ENUM:
        {
            buffer_length = strlcpy(buffer, ptr->model->name_field_name->enum_values[VOIDP_TO_X(ptr, ptr->model->name_field_name->offset, int)], buffer_size);
            assert(buffer_length < buffer_size);
            break;
        }
        case MODEL_TYPE_STRING:
        {
            char *v;

            v = VOIDP_TO_X(ptr, ptr->model->name_field_name->offset, char *);
            assert(NULL != v);
            if ('\0' == *v) {
//                 assert(buffer_length >= STR_SIZE("\"\""));
                buffer_length = strlcpy(buffer, "\"\"", buffer_size);
            } else {
                buffer_length = strlcpy(buffer, v, buffer_size);
            }
            assert(buffer_length < buffer_size);
            break;
        }
        default:
            assert(FALSE);
            break;
    }

    return buffer_length;
}

#include "modules/table.h"
command_status_t model_to_table(const model_t *model, error_t **error)
{
    table_t *t;
    Iterator it;
    char obj[8192];
    command_status_t ret;

    assert(NULL != model->backend);
    assert(NULL != model->backend->all);

    ret = COMMAND_SUCCESS;
    assert(ARRAY_SIZE(obj) >= model->size);
    t = table_new_from_model(model, TABLE_FLAG_DELEGATE);
    model->backend->all(&it, model, model->backend_data, error);
    iterator_first(&it);
    if (iterator_is_valid(&it)) {
        do {
            iterator_current(&it, (void **) &obj);
            table_store_modelized(t, (modelized_t *) &obj);
            iterator_next(&it);
        } while (iterator_is_valid(&it));
    } else {
        ret |= CMD_FLAG_NO_DATA;
    }
    iterator_close(&it);
    table_display(t, TABLE_FLAG_NONE);
    table_destroy(t);

    return ret;
}

/**
 * Helper for completion
 *
 * @param it the iterator from which to extract data
 * @param possibilities a recipient which contains all possible values
 *
 * @return TRUE (can't fail)
 */
bool complete_from_modelized(Iterator *it, completer_t *possibilities)
{
    for (iterator_first(it); iterator_is_valid(it); iterator_next(it)) {
        modelized_t *object;

        object = iterator_current(it, NULL);
        completer_push_modelized(possibilities, object);
    }
    iterator_close(it);

    return TRUE;
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

void modelized_id_to_s(modelized_t *ptr, char *buffer, size_t buffer_size)
{
    //
}

void modelized_id_from_s(modelized_t *ptr, argument_t *arg, char *buffer) // in graph.[ch]
{
    //
}
#endif
