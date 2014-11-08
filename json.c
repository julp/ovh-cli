#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#include "common.h"
#include "json.h"
#include "struct/hashtable.h"
#include "struct/dptrarray.h"

// http://rubini.us/doc/en/memory-system/object-layout/
// https://github.com/ruby/ruby/blob/trunk/include/ruby/ruby.h

/**
 * NOTE/limitations:
 * - any JSON value (except null, true and false) should not be used more than once. You have to recreate a new
 * json_value_t with json_<type> function even if the C value is known to be the same else you will try to free
 * the same value (pointer) multiple times (= segfault or invalid free)
 * - "deep" objects (arrays and objects) are not checked against recursion. It would be easy to use an array or
 * object as value to itself (= infinite loop)
 * - strings have to be UTF-8 encoded
 **/

static int json_array_write(json_document_t *, json_value_t, String *);
static int json_object_write(json_document_t *, json_value_t, String *);
static inline json_type_t json_get_type(json_value_t);

static int json_null_write(json_document_t *doc, json_value_t UNUSED(value), String *buffer)
{
    string_append_string_len(buffer, "null", STR_LEN("null"));
    ++doc->values_by_depth[doc->current_depth];

    return TRUE;
}

static int json_true_write(json_document_t *doc, json_value_t UNUSED(value), String *buffer)
{
    string_append_string_len(buffer, "true", STR_LEN("true"));
    ++doc->values_by_depth[doc->current_depth];

    return TRUE;
}

static int json_false_write(json_document_t *doc, json_value_t UNUSED(value), String *buffer)
{
    string_append_string_len(buffer, "false", STR_LEN("false"));
    ++doc->values_by_depth[doc->current_depth];

    return TRUE;
}

static int json_string_write(json_document_t *doc, json_value_t value, String *buffer)
{
    json_node_t *node;

    assert(JSON_TYPE_STRING == json_get_type(value));

    node = (json_node_t *) value;
    string_append_json_string(buffer, (const char *) node->value);
    ++doc->values_by_depth[doc->current_depth];

    return TRUE;
}

static int json_number_write(json_document_t *doc, json_value_t value, String *buffer)
{
    assert(JSON_TYPE_NUMBER == json_get_type(value));

    if (HAS_FLAG(value, JSON_INTEGER_MASK)) {
        string_append_formatted(buffer, "%" PRIi64, (value >> 1));
    } else {
        json_node_t *node;

        node = (json_node_t *) value;
        string_append_formatted(buffer, "%.12f", *((double *) node->value));
    }
    ++doc->values_by_depth[doc->current_depth];

    return TRUE;
}

static struct json_type_handler_t {
    DtorFunc dtor;
    int (*write)(json_document_t *, json_value_t, String *);
} handlers[] = {
    [ JSON_TYPE_NULL ] = { NULL, json_null_write },
    [ JSON_TYPE_TRUE ] = { NULL, json_true_write },
    [ JSON_TYPE_FALSE ] = { NULL, json_false_write },
    [ JSON_TYPE_NUMBER ] = { free, json_number_write },
    [ JSON_TYPE_STRING ] = { free, json_string_write },
    [ JSON_TYPE_ARRAY ] = { (DtorFunc) dptrarray_destroy, json_array_write },
    [ JSON_TYPE_OBJECT ] = { (DtorFunc) hashtable_destroy, json_object_write },
};

static inline const struct json_type_handler_t *json_get_type_handler(json_value_t v)
{
    const struct json_type_handler_t *handler;

    if (json_null == v) {
        handler = &handlers[JSON_TYPE_NULL];
    } else if (json_true == v) {
        handler = &handlers[JSON_TYPE_TRUE];
    } else if (json_false == v) {
        handler = &handlers[JSON_TYPE_FALSE];
    } else if (HAS_FLAG(v, JSON_INTEGER_MASK)) {
        handler = &handlers[JSON_TYPE_NUMBER];
    } else {
        json_node_t *node;

        node = (json_node_t *) v;
        handler = &handlers[node->type];
    }

    return handler;
}

static inline json_type_t json_get_type(json_value_t v)
{
    json_type_t type;

    if (json_null == v) {
        type = JSON_TYPE_NULL;
    } else if (json_true == v) {
        type = JSON_TYPE_TRUE;
    } else if (json_false == v) {
        type = JSON_TYPE_FALSE;
    } else if (HAS_FLAG(v, JSON_INTEGER_MASK)) {
        type = JSON_TYPE_NUMBER;
    } else {
        json_node_t *node;

        node = (json_node_t *) v;
        type = node->type;
    }

    return type;
}

static int json_array_write(json_document_t *doc, json_value_t value, String *buffer)
{
    Iterator it;
    json_node_t *node;

    assert(JSON_TYPE_ARRAY == json_get_type(value));

    node = (json_node_t *) value;
    dptrarray_to_iterator(&it, (DPtrArray *) node->value);
//     if (TRUE) {
//         string_append_char(buffer, '\n');
//         string_append_n_times(buffer, "    ", STR_LEN("    "), doc->current_depth);
//     }
    string_append_char(buffer, '[');
    ++doc->values_by_depth[doc->current_depth];
    ++doc->current_depth;
    doc->values_by_depth[doc->current_depth] = 0;
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        json_value_t v;
        const struct json_type_handler_t *handler;

        v = (json_value_t) iterator_current(&it, NULL);
        if (doc->values_by_depth[doc->current_depth]) {
            string_append_char(buffer, ',');
        }
        if (TRUE) {
            string_append_char(buffer, '\n');
            string_append_n_times(buffer, "    ", STR_LEN("    "), doc->current_depth);
        }
        handler = json_get_type_handler(v);
        handler->write(doc, v, buffer);
    }
    iterator_close(&it);
    --doc->current_depth;
    if (TRUE) {
        string_append_char(buffer, '\n');
        string_append_n_times(buffer, "    ", STR_LEN("    "), doc->current_depth);
    }
    string_append_char(buffer, ']');

    return TRUE;
}

static int json_object_write(json_document_t *doc, json_value_t value, String *buffer)
{
    Iterator it;
    json_node_t *node;

    assert(JSON_TYPE_OBJECT == json_get_type(value));

    node = (json_node_t *) value;
    hashtable_to_iterator(&it,(HashTable *) node->value);
    string_append_char(buffer, '{');
    ++doc->values_by_depth[doc->current_depth];
    ++doc->current_depth;
    doc->values_by_depth[doc->current_depth] = 0;
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        json_value_t v;
        const char *key;
        const struct json_type_handler_t *handler;

        v = (json_value_t) iterator_current(&it, (void **) &key);
        if (doc->values_by_depth[doc->current_depth]) {
            string_append_char(buffer, ',');
        }
        if (TRUE) {
            string_append_char(buffer, '\n');
            string_append_n_times(buffer, "    ", STR_LEN("    "), doc->current_depth);
        }
        string_append_json_string(buffer, key);
//         string_append_char(buffer, ':');
        string_append_string_len(buffer, ": ", STR_LEN(": "));
        handler = json_get_type_handler(v);
        handler->write(doc, v, buffer);
    }
    iterator_close(&it);
    --doc->current_depth;
    if (TRUE) {
        string_append_char(buffer, '\n');
        string_append_n_times(buffer, "    ", STR_LEN("    "), doc->current_depth);
    }
    string_append_char(buffer, '}');

    return TRUE;
}

static void json_value_destroy(json_value_t value)
{
    const struct json_type_handler_t *handler;

    handler = json_get_type_handler(value);
    if (NULL != handler->dtor && !HAS_FLAG(value, JSON_INTEGER_MASK)) {
        json_node_t *node;

        node = (json_node_t *) value;
        handler->dtor(node->value);
        free(node);
    }
}

json_document_t *json_document_new(void) /* WARN_UNUSED_RESULT */
{
    json_document_t *doc;

    doc = mem_new(*doc);
    doc->root = 0;
    doc->current_depth = 0;
    doc->values_by_depth[doc->current_depth] = 0;

    return doc;
}

void json_document_set_root(json_document_t *doc, json_value_t new_root)
{
    assert(NULL != doc);

    if (0 != doc->root) {
        json_value_destroy(doc->root);
    }
    doc->root = new_root;
}

int json_document_serialize(json_document_t *doc, String *buffer/*, uint32_t flags*/)
{
    assert(NULL != doc);

    string_truncate(buffer);
    if (0 != doc->root) {
        const struct json_type_handler_t *handler;

        handler = json_get_type_handler(doc->root);
        handler->write(doc, doc->root, buffer);
    }

    return TRUE;
}

void json_document_destroy(json_document_t *doc)
{
    assert(NULL != doc);

    if (0 != doc->root) {
        json_value_destroy(doc->root);
    }
    free(doc);
}

json_value_t json_integer(int64_t value) /* WARN_UNUSED_RESULT */
{
    if (HAS_FLAG(value, (UINT64_C(1) << (sizeof(uintptr_t) * 8 - 1)))) {
        return json_number((double) value);
    } else {
        return ((((json_value_t) value) << 1) | JSON_INTEGER_MASK);
    }
}

json_value_t json_number(double value) /* WARN_UNUSED_RESULT */
{
    json_node_t *node;

    node = mem_new(*node);
    node->type = JSON_TYPE_NUMBER;
    node->value = mem_new(double);
    *((double *) node->value) = value;
    // TODO: assumes !inf/nan ?

    return (json_value_t) node;
}

json_value_t json_string(const char *value) /* WARN_UNUSED_RESULT */
{
    json_node_t *node;

    node = mem_new(*node);
    node->type = JSON_TYPE_STRING;
    node->value = strdup(value);

    return (json_value_t) node;
}

json_value_t json_object(void) /* WARN_UNUSED_RESULT */
{
    json_node_t *node;

    node = mem_new(*node);
    node->type = JSON_TYPE_OBJECT;
    node->value = hashtable_ascii_cs_new((DupFunc) strdup, (DtorFunc) free, (DtorFunc) json_value_destroy);

    return (json_value_t) node;
}

bool json_object_has_property(json_value_t object, const char *key)
{
    json_node_t *node;

    assert(NULL != key);
    assert(JSON_TYPE_OBJECT == json_get_type(object));

    node = (json_node_t *) object;

    return hashtable_contains((HashTable *) node->value, key);
}

void json_object_set_property(json_value_t object, const char *key, json_value_t value)
{
    json_node_t *node;

    assert(NULL != key);
    assert(JSON_TYPE_OBJECT == json_get_type(object));

    node = (json_node_t *) object;

    hashtable_put((HashTable *) node->value, (void *) key, (void *) value, NULL);
}

bool json_object_get_property(json_value_t object, const char *key, json_value_t *value)
{
    json_node_t *node;

    assert(NULL != key);
    assert(JSON_TYPE_OBJECT == json_get_type(object));

    node = (json_node_t *) object;

    return hashtable_get((HashTable *) node->value, (void *) key, (void **) value);
}

bool json_object_remove_property(json_value_t object, const char *key)
{
    json_node_t *node;

    assert(NULL != key);
    assert(JSON_TYPE_OBJECT == json_get_type(object));

    node = (json_node_t *) object;

    return hashtable_delete((HashTable *) node->value, key, TRUE);
}

json_value_t json_array(void) /* WARN_UNUSED_RESULT */
{
    json_node_t *node;

    node = mem_new(*node);
    node->type = JSON_TYPE_ARRAY;
    node->value = dptrarray_new(NULL, (DtorFunc) json_value_destroy, (void *) json_null);

    return (json_value_t) node;
}

json_value_t json_array_get_at(json_value_t array, size_t index)
{
    json_node_t *node;

    assert(JSON_TYPE_ARRAY == json_get_type(array));

    node = (json_node_t *) array;

    return (json_value_t) dptrarray_at((DPtrArray *) node->value, index);
}

void json_array_set_at(json_value_t array, size_t index, json_value_t value)
{
    json_node_t *node;

    assert(JSON_TYPE_ARRAY == json_get_type(array));

    node = (json_node_t *) array;
    dptrarray_insert((DPtrArray *) node->value, index, (void *) value);
}

void json_array_push(json_value_t array, json_value_t value)
{
    json_node_t *node;

    assert(JSON_TYPE_ARRAY == json_get_type(array));

    node = (json_node_t *) array;
    dptrarray_push((DPtrArray *) node->value, (void *) value);
}
