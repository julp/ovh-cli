#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <limits.h>
#include <inttypes.h>

#include "common.h"
#include "json.h"
#include "date.h"
#include "struct/hashtable.h"
#include "struct/dptrarray.h"

/**
 * NOTE/limitations:
 * - any JSON value (except null, true and false) should not be used more than once. You have to recreate a new
 * json_value_t with json_<type> function even if the C value is known to be the same else you will try to free
 * the same value (pointer) multiple times (= segfault or invalid free)
 * - "deep" objects (arrays and objects) are not checked against recursion. It would be easy to use an array or
 * object as value to itself (= infinite loop)
 * - strings have to be UTF-8 encoded
 **/

#define JSON_INTEGER_MASK 1

#define JSON_IDENT_STRING "    "
#define JSON_NULL_AS_STRING "null"
#define JSON_TRUE_AS_STRING "true"
#define JSON_FALSE_AS_STRING "false"

static int json_array_write(json_document_t *, json_value_t, String *, uint32_t);
static int json_object_write(json_document_t *, json_value_t, String *, uint32_t);

static int json_null_write(json_document_t *doc, json_value_t UNUSED(value), String *buffer, uint32_t UNUSED(flags))
{
    string_append_string_len(buffer, JSON_NULL_AS_STRING, STR_LEN(JSON_NULL_AS_STRING));
    ++doc->values_by_depth[doc->current_depth];

    return TRUE;
}

static int json_true_write(json_document_t *doc, json_value_t UNUSED(value), String *buffer, uint32_t UNUSED(flags))
{
    string_append_string_len(buffer, JSON_TRUE_AS_STRING, STR_LEN(JSON_TRUE_AS_STRING));
    ++doc->values_by_depth[doc->current_depth];

    return TRUE;
}

static int json_false_write(json_document_t *doc, json_value_t UNUSED(value), String *buffer, uint32_t UNUSED(flags))
{
    string_append_string_len(buffer, JSON_FALSE_AS_STRING, STR_LEN(JSON_FALSE_AS_STRING));
    ++doc->values_by_depth[doc->current_depth];

    return TRUE;
}

static int json_string_write(json_document_t *doc, json_value_t value, String *buffer, uint32_t UNUSED(flags))
{
    json_node_t *node;

    assert(JSON_TYPE_STRING == json_get_type(value));

    node = (json_node_t *) value;
    string_append_json_string(buffer, (const char *) node->value);
    ++doc->values_by_depth[doc->current_depth];

    return TRUE;
}

static int json_number_write(json_document_t *doc, json_value_t value, String *buffer, uint32_t UNUSED(flags))
{
    assert(JSON_TYPE_NUMBER == json_get_type(value));

    if (HAS_FLAG(value, JSON_INTEGER_MASK)) {
        string_append_formatted(buffer, "%" PRIi64, (int64_t) (((int64_t) value) >> 1));
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
    int (*write)(json_document_t *, json_value_t, String *, uint32_t);
} handlers[] = {
    [ JSON_TYPE_NULL ] = { NULL, json_null_write },
    [ JSON_TYPE_TRUE ] = { NULL, json_true_write },
    [ JSON_TYPE_FALSE ] = { NULL, json_false_write },
    [ JSON_TYPE_NUMBER ] = { free, json_number_write },
    [ JSON_TYPE_STRING ] = { free, json_string_write },
    [ JSON_TYPE_ARRAY ] = { (DtorFunc) dptrarray_destroy, json_array_write },
    [ JSON_TYPE_OBJECT ] = { (DtorFunc) hashtable_destroy, json_object_write },
};

static const struct json_type_handler_t *json_get_type_handler(json_value_t v)
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

json_type_t json_get_type(json_value_t v)
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

static int json_array_write(json_document_t *doc, json_value_t value, String *buffer, uint32_t flags)
{
    Iterator it;
    json_node_t *node;

    assert(JSON_TYPE_ARRAY == json_get_type(value));

    node = (json_node_t *) value;
    dptrarray_to_iterator(&it, (DPtrArray *) node->value);
#if 0 /* to put '[', indented, on a new line */
    if (HAS_FLAG(flags, JSON_OPT_PRETTY_PRINT)) {
        string_append_char(buffer, '\n');
        string_append_n_times(buffer, JSON_IDENT_STRING, STR_LEN(JSON_IDENT_STRING), doc->current_depth);
    }
#endif
    string_append_char(buffer, '[');
    ++doc->values_by_depth[doc->current_depth];
    assert(doc->current_depth < JSON_MAX_DEPTH);
    ++doc->current_depth;
    doc->values_by_depth[doc->current_depth] = 0;
    for (iterator_first(&it); iterator_is_valid(&it); iterator_next(&it)) {
        json_value_t v;
        const struct json_type_handler_t *handler;

        v = (json_value_t) iterator_current(&it, NULL);
        if (doc->values_by_depth[doc->current_depth]) {
            string_append_char(buffer, ',');
        }
        if (HAS_FLAG(flags, JSON_OPT_PRETTY_PRINT)) {
            string_append_char(buffer, '\n');
            string_append_n_times(buffer, JSON_IDENT_STRING, STR_LEN(JSON_IDENT_STRING), doc->current_depth);
        }
        handler = json_get_type_handler(v);
        handler->write(doc, v, buffer, flags);
    }
    iterator_close(&it);
    --doc->current_depth;
    if (HAS_FLAG(flags, JSON_OPT_PRETTY_PRINT)) {
        string_append_char(buffer, '\n');
        string_append_n_times(buffer, JSON_IDENT_STRING, STR_LEN(JSON_IDENT_STRING), doc->current_depth);
    }
    string_append_char(buffer, ']');

    return TRUE;
}

static int json_object_write(json_document_t *doc, json_value_t value, String *buffer, uint32_t flags)
{
    Iterator it;
    json_node_t *node;

    assert(JSON_TYPE_OBJECT == json_get_type(value));

    node = (json_node_t *) value;
    hashtable_to_iterator(&it,(HashTable *) node->value);
    string_append_char(buffer, '{');
    ++doc->values_by_depth[doc->current_depth];
    assert(doc->current_depth < JSON_MAX_DEPTH);
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
        if (HAS_FLAG(flags, JSON_OPT_PRETTY_PRINT)) {
            string_append_char(buffer, '\n');
            string_append_n_times(buffer, JSON_IDENT_STRING, STR_LEN(JSON_IDENT_STRING), doc->current_depth);
        }
        string_append_json_string(buffer, key);
        string_append_char(buffer, ':');
        if (HAS_FLAG(flags, JSON_OPT_PRETTY_PRINT)) {
            string_append_char(buffer, ' ');
        }
        handler = json_get_type_handler(v);
        handler->write(doc, v, buffer, flags);
    }
    iterator_close(&it);
    --doc->current_depth;
    if (HAS_FLAG(flags, JSON_OPT_PRETTY_PRINT)) {
        string_append_char(buffer, '\n');
        string_append_n_times(buffer, JSON_IDENT_STRING, STR_LEN(JSON_IDENT_STRING), doc->current_depth);
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

json_value_t json_document_get_root(json_document_t *doc)
{
    assert(NULL != doc);

    return doc->root;
}

void json_document_set_root(json_document_t *doc, json_value_t new_root)
{
    assert(NULL != doc);
    assert(JSON_TYPE_ARRAY == json_get_type(new_root) || JSON_TYPE_OBJECT == json_get_type(new_root));

    if (0 != doc->root) {
        json_value_destroy(doc->root);
    }
    doc->root = new_root;
}

int json_document_serialize(json_document_t *doc, String *buffer, uint32_t flags)
{
    assert(NULL != doc);

    string_truncate(buffer);
    if (0 != doc->root) {
        const struct json_type_handler_t *handler;

        handler = json_get_type_handler(doc->root);
        handler->write(doc, doc->root, buffer, flags);
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

#define JSON_UNWRAP_SCALAR(jsonvalue, ctype, jsontype) \
    do { \
            json_node_t *node; \
 \
    assert(jsontype == json_get_type(jsonvalue)); \
 \
    node = (json_node_t *) jsonvalue; \
 \
        return *((ctype *) node->value); \
    } while (0);

#define JSON_WRAP_SCALAR(cvalue, ctype, jsontype) \
    do { \
        json_node_t *node; \
 \
        node = mem_new(*node); \
        node->type = jsontype; \
        node->value = mem_new(ctype); \
        *((ctype *) node->value) = cvalue; \
 \
        return (json_value_t) node; \
    } while (0);

// #define RSHIFT(x,y) ((x)>>(int)(y))
// #define RSHIFT(x,y) (((x)<0) ? ~((~(x))>>(int)(y)) : (x)>>(int)(y))
int64_t json_get_integer(json_value_t value)
{
    assert(HAS_FLAG(value, JSON_INTEGER_MASK) || JSON_TYPE_NUMBER == json_get_type(value));

    if (HAS_FLAG(value, JSON_INTEGER_MASK)) {
        return value >> 1;
    } else {
        //JSON_UNWRAP_SCALAR(value, int64_t, JSON_TYPE_INTEGER);
        return (int64_t) json_get_number(value);
    }
}

#define JSON_MAX_SIGNED ((int64_t) ((UINT64_C(1) << (sizeof(uintptr_t) * 8 - 2)) - 1))
#define JSON_MIN_SIGNED ((int64_t) (-(UINT64_C(1) << (sizeof(uintptr_t) * 8 - 2))))

json_value_t json_integer(int64_t value) /* WARN_UNUSED_RESULT */
{
    // https://github.com/ruby/ruby/blob/trunk/include/ruby/ruby.h
    // https://raw.githubusercontent.com/ruby/ruby/trunk/include/ruby/ruby.h
    if (value <= JSON_MAX_SIGNED && value >= JSON_MIN_SIGNED) {
        return ((((json_value_t) value) << 1) | JSON_INTEGER_MASK);
    } else {
        //JSON_WRAP_SCALAR(value, int64_t, JSON_TYPE_INTEGER);
        return json_number((double) value);
    }
}

double json_get_number(json_value_t value)
{
    JSON_UNWRAP_SCALAR(value, double, JSON_TYPE_NUMBER/*JSON_TYPE_DECIMAL*/);
}

json_value_t json_number(double value) /* WARN_UNUSED_RESULT */
{
    // TODO: assumes !inf/nan ?
    JSON_WRAP_SCALAR(value, double, JSON_TYPE_NUMBER/*JSON_TYPE_DECIMAL*/);
}

const char *json_get_string(json_value_t value)
{
    json_node_t *node;

    assert(JSON_TYPE_STRING == json_get_type(value));

    node = (json_node_t *) value;

    return node->value;
}

int json_get_enum(json_value_t value, const char * const *values, int fallback)
{
    int ret;
    json_node_t *node;
    const char * const *v;

    assert(JSON_TYPE_STRING == json_get_type(value));

    ret = fallback;
    node = (json_node_t *) value;
    for (v = values; NULL != *v; v++) {
        if (0 == strcmp(node->value, *v)) {
            ret = v - values;
            break;
        }
    }

    return ret;
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

    hashtable_put((HashTable *) node->value, 0, key, value, NULL);
}

bool json_object_get_property(json_value_t object, const char *key, json_value_t *value)
{
    json_node_t *node;

    assert(NULL != key);
    assert(JSON_TYPE_OBJECT == json_get_type(object));

    node = (json_node_t *) object;

    return hashtable_get((HashTable *) node->value, key, value);
}

void json_object_to_modelized(json_value_t object, modelized_t *ptr, bool copy, bool *nulls)
{
    const model_field_t *f;

    for (f = ptr->model->fields; NULL != f->column_name; f++) {
        json_value_t propvalue;

        if (json_object_get_property(object, f->column_name, &propvalue)) {
            bool isnull;

            isnull = json_null == propvalue;
            if (NULL != nulls) {
                nulls[f - ptr->model->fields] = isnull;
            }
            switch (f->type) {
                case MODEL_TYPE_BOOL:
                    *((bool *) (((char *) ptr) + f->offset)) = json_true == propvalue;
                    break;
                case MODEL_TYPE_INT:
                    *((int64_t *) (((char *) ptr) + f->offset)) = isnull ? 0 : json_get_integer(propvalue);
                    break;
                case MODEL_TYPE_ENUM:
                    *((int *) (((char *) ptr) + f->offset)) = isnull ? -1 : json_get_enum(propvalue, f->enum_values, -1);
                    break;
                case MODEL_TYPE_DATE:
                {
                    time_t v;

                    v = 0;
                    if (!isnull) {
                        // %F is equivalent to "%Y-%m-%d"
                        date_parse_to_timestamp(json_get_string(propvalue), "%F", &v);
                    }
                    *((time_t *) (((char *) ptr) + f->offset)) = v;
                    break;
                }
                case MODEL_TYPE_DATETIME:
                {
                    time_t v;

                    v = 0;
                    if (!isnull) {
                        // %F is equivalent to "%Y-%m-%d"
                        // %T is equivalent to "%H:%M:%S"
                        // %z is replaced by the time zone offset from UTC; a leading plus sign stands for east of UTC,
                        // a minus sign for west of UTC, hours and minutes follow with two digits each and no delimiter
                        // between them (common form for RFC 822 date headers)
                        date_parse_to_timestamp(json_get_string(propvalue), "%FT%T%z", &v);
                    }
                    *((time_t *) (((char *) ptr) + f->offset)) = v;
                    break;
                }
                case MODEL_TYPE_STRING:
                    *((char **) (((char *) ptr) + f->offset)) = isnull ? NULL : (copy ? strdup(json_get_string(propvalue)) : (char *) json_get_string(propvalue));
                    break;
                default:
                    assert(FALSE);
                    break;
            }
        }
    }
}

#if 0
size_t json_array_objects_to_modelized(json_value_t array, model_t model, char **ptr)
{
    Iterator it;
    size_t count;

    assert(JSON_TYPE_ARRAY == json_get_type(array));
    count = 0;
    // alloc array length * model->size or just call a callback?
    *ptr = malloc(model->size);
    // iterate and call json_object_to_modelized ?

    return count;
}
#endif

bool json_object_remove_property(json_value_t object, const char *key)
{
    json_node_t *node;

    assert(NULL != key);
    assert(JSON_TYPE_OBJECT == json_get_type(object));

    node = (json_node_t *) object;

    return hashtable_delete((HashTable *) node->value, key, TRUE);
}

void json_object_to_iterator(Iterator *it, json_value_t object)
{
    json_node_t *node;

    assert(JSON_TYPE_OBJECT == json_get_type(object));

    node = (json_node_t *) object;
    hashtable_to_iterator(it, (HashTable *) node->value);
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

// TODO: removal?
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

void json_array_to_iterator(Iterator *it, json_value_t array)
{
    json_node_t *node;

    assert(JSON_TYPE_ARRAY == json_get_type(array));

    node = (json_node_t *) array;
    dptrarray_to_iterator(it, (DPtrArray *) node->value);
}

/**
 * This JSON parser was widely inspired from Vincent Hanquez work,
 * released on GNU LGPL as published by the Free Software Foundation;
 * version 2.1 or version 3.0 only
 *
 * https://github.com/vincenthz/libjson
 *
 * Which was inspired from JSON_parser.c published by http://json.org
 **/

enum classes {
    C_SPACE, /* space */
    C_WHITE, /* other whitespace */
    C_LCURB, /* {  */
    C_RCURB, /* } */
    C_LSQRB, /* [ */
    C_RSQRB, /* ] */
    C_COLON, /* : */
    C_COMMA, /* , */
    C_QUOTE, /* " */
    C_BACKS, /* \ */
    C_SLASH, /* / */
    C_PLUS,  /* + */
    C_MINUS, /* - */
    C_DOT,   /* . */
    C_ZERO , /* 0 */
    C_DIGIT, /* 123456789 */
    C_a,     /* a */
    C_b,     /* b */
    C_cd,    /* cd */
    C_e,     /* e */
    C_f,     /* f */
    C_l,     /* l */
    C_n,     /* n */
    C_r,     /* r */
    C_s,     /* s */
    C_t,     /* t */
    C_u,     /* u */
    C_ABCDF, /* ABCDF */
    C_E,     /* E */
    C_OTHER, /* everything else */
    NR_CLASSES,
    C_ERROR = 0xfe
};

static const uint8_t character_class[] = {
    /*      0        1        2        3        4        5        6        7        8        9        A        B        C        D        E        F       */
    /* 0 */ C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_WHITE, C_WHITE, C_ERROR, C_ERROR, C_WHITE, C_ERROR, C_ERROR,
    /* 1 */ C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR, C_ERROR,
    /* 2 */ C_SPACE, C_OTHER, C_QUOTE, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_PLUS,  C_COMMA, C_MINUS, C_DOT,   C_SLASH,
    /* 3 */ C_ZERO,  C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_COLON, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER,
    /* 4 */ C_OTHER, C_ABCDF, C_ABCDF, C_ABCDF, C_ABCDF, C_E,     C_ABCDF, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER,
    /* 5 */ C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_LSQRB, C_BACKS, C_RSQRB, C_OTHER, C_OTHER,
    /* 6 */ C_OTHER, C_a,     C_b,     C_cd,    C_cd,    C_e,     C_f,     C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_l,     C_OTHER, C_n,     C_OTHER,
    /* 7 */ C_OTHER, C_OTHER, C_r,     C_s,     C_t,     C_u,     C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_OTHER, C_LCURB, C_OTHER, C_RCURB, C_OTHER, C_OTHER,
};

enum states {
    STATE_GO, /* start    */
    STATE_OK, /* ok       */
    STATE__O, /* object   */
    STATE__K, /* key      */
    STATE_CO, /* colon    */
    STATE__V, /* value    */
    STATE__A, /* array    */
    STATE__S, /* string   */
    STATE_ES, /* escape   */
    STATE_U1, /* u1       */
    STATE_U2, /* u2       */
    STATE_U3, /* u3       */
    STATE_U4, /* u4       */
    STATE_MI, /* minus    */
    STATE_ZE, /* zero     */
    STATE_IN, /* integer  */
    STATE_R1, /* fraction */
    STATE_R2, /* fraction */
    STATE_E1, /* e        */
    STATE_E2, /* ex       */
    STATE_E3, /* exp      */
    STATE_T1, /* tr       */
    STATE_T2, /* tru      */
    STATE_T3, /* true     */
    STATE_F1, /* fa       */
    STATE_F2, /* fal      */
    STATE_F3, /* fals     */
    STATE_F4, /* false    */
    STATE_N1, /* nu       */
    STATE_N2, /* nul      */
    STATE_N3, /* null     */
    STATE_D1, /* '\' for trail surrogate */
    STATE_D2, /* 'u' for trail surrogate */
    NR_STATES,
    STATE___ = 0xFF
};

enum actions {
    STATE_KS = 0x80, /* key separator */
    STATE_SP, /* comma separator */
    STATE_AB, /* array begin */
    STATE_AE, /* array ending */
    STATE_OB, /* object begin */
    STATE_OE, /* object end */
    STATE_FA, /* false */
    STATE_TR, /* true */
    STATE_NU, /* null */
    STATE_DE, /* double detected by exponent */
    STATE_DF, /* double detected by . */
    STATE_SE, /* string end */
    STATE_MX, /* integer detected by minus */
    STATE_ZX, /* integer detected by zero */
    STATE_IX, /* integer detected by 1-9 */
    STATE_UC  /* Unicode character read */
};

#define S(x) STATE_##x
static const uint8_t state_transition_table[NR_STATES][NR_CLASSES] = {
    /*            SP     WS     {      }      [      ]      :      ,      "      \      /      +      -      .      0      1..9   a      b      cd     e      f      l      n      r      s      t      u      ABCDF  E      other */
/* GO: start */
    [ S(GO) ] = { S(GO), S(GO), S(OB), S(__), S(AB), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__) },
/* OK */
    [ S(OK) ] = { S(OK), S(OK), S(__), S(OE), S(__), S(AE), S(__), S(SP), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__) },
/* _O: object */
    [ S(_O) ] = { S(_O), S(_O), S(__), S(OE), S(__), S(__), S(__), S(__), S(_S), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__) },
/* _K: key */
    [ S(_K) ] = { S(_K), S(_K), S(__), S(__), S(__), S(__), S(__), S(__), S(_S), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__) },
/* CO: colon */
    [ S(CO) ] = { S(CO), S(CO), S(__), S(__), S(__), S(__), S(KS), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__) },
/* _V: value */
    [ S(_V) ] = { S(_V), S(_V), S(OB), S(__), S(AB), S(__), S(__), S(__), S(_S), S(__), S(__), S(__), S(MX), S(__), S(ZX), S(IX), S(__), S(__), S(__), S(__), S(F1), S(__), S(N1), S(__), S(__), S(T1), S(__), S(__), S(__), S(__) },
/* _A: array */
    [ S(_A) ] = { S(_A), S(_A), S(OB), S(__), S(AB), S(AE), S(__), S(__), S(_S), S(__), S(__), S(__), S(MX), S(__), S(ZX), S(IX), S(__), S(__), S(__), S(__), S(F1), S(__), S(N1), S(__), S(__), S(T1), S(__), S(__), S(__), S(__) },
/* _S: string */
    [ S(_S) ] = { S(_S), S(__), S(_S), S(_S), S(_S), S(_S), S(_S), S(_S), S(SE), S(ES), S(_S), S(_S), S(_S), S(_S), S(_S), S(_S), S(_S), S(_S), S(_S), S(_S), S(_S), S(_S), S(_S), S(_S), S(_S), S(_S), S(_S), S(_S), S(_S), S(_S) },
/* ES: escape */
    [ S(ES) ] = { S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(_S), S(_S), S(_S), S(__), S(__), S(__), S(__), S(__), S(__), S(_S), S(__), S(__), S(_S), S(__), S(_S), S(_S), S(__), S(_S), S(U1), S(__), S(__), S(__) },
/* U1..4: \uXXXX */
    [ S(U1) ] = { S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(U2), S(U2), S(U2), S(U2), S(U2), S(U2), S(U2), S(__), S(__), S(__), S(__), S(__), S(__), S(U2), S(U2), S(__) },
    [ S(U2) ] = { S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(U3), S(U3), S(U3), S(U3), S(U3), S(U3), S(U3), S(__), S(__), S(__), S(__), S(__), S(__), S(U3), S(U3), S(__) },
    [ S(U3) ] = { S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(U4), S(U4), S(U4), S(U4), S(U4), S(U4), S(U4), S(__), S(__), S(__), S(__), S(__), S(__), S(U4), S(U4), S(__) },
    [ S(U4) ] = { S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(UC), S(UC), S(UC), S(UC), S(UC), S(UC), S(UC), S(__), S(__), S(__), S(__), S(__), S(__), S(UC), S(UC), S(__) },
/* MI: minus */
    [ S(MI) ] = { S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(ZE), S(IN), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__) },
/* ZE: zero */
    [ S(ZE) ] = { S(OK), S(OK), S(__), S(OE), S(__), S(AE), S(__), S(SP), S(__), S(__), S(__), S(__), S(__), S(DF), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__) },
/* IN: integer */
    [ S(IN) ] = { S(OK), S(OK), S(__), S(OE), S(__), S(AE), S(__), S(SP), S(__), S(__), S(__), S(__), S(__), S(DF), S(IN), S(IN), S(__), S(__), S(__), S(DE), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(DE), S(__) },
/* R1..R2: fraction */
    [ S(R1) ] = { S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(R2), S(R2), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__) },
    [ S(R2) ] = { S(OK), S(OK), S(__), S(OE), S(__), S(AE), S(__), S(SP), S(__), S(__), S(__), S(__), S(__), S(__), S(R2), S(R2), S(__), S(__), S(__), S(E1), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(E1), S(__) },
/* E1..E3: exp */
    [ S(E1) ] = { S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(E2), S(E2), S(__), S(E3), S(E3), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__) },
    [ S(E2) ] = { S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(E3), S(E3), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__) },
    [ S(E3) ] = { S(OK), S(OK), S(__), S(OE), S(__), S(AE), S(__), S(SP), S(__), S(__), S(__), S(__), S(__), S(__), S(E3), S(E3), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__) },
/* T1..3: true */
    [ S(T1) ] = { S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(T2), S(__), S(__), S(__), S(__), S(__), S(__) },
    [ S(T2) ] = { S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(T3), S(__), S(__), S(__) },
    [ S(T3) ] = { S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(TR), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__) },
/* F1..4: false */
    [ S(F1) ] = { S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(F2), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__) },
    [ S(F2) ] = { S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(F3), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__) },
    [ S(F3) ] = { S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(F4), S(__), S(__), S(__), S(__), S(__) },
    [ S(F4) ] = { S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(FA), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__) },
/* N1..3: null */
    [ S(N1) ] = { S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(N2), S(__), S(__), S(__) },
    [ S(N2) ] = { S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(N3), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__) },
    [ S(N3) ] = { S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(NU), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__) },
/* D1..D2: trail surrogate */
    [ S(D1) ] = { S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(D2), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__) },
    [ S(D2) ] = { S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(__), S(U1), S(__), S(__), S(__) },
};


#define JSON_CONSTANT_EXPECTED(name) \
    "JSON " name " constant was expected"
#define JSON_NULL_EXPECTED \
    JSON_CONSTANT_EXPECTED("null")
#define JSON_TRUE_EXPECTED \
    JSON_CONSTANT_EXPECTED("true")
#define JSON_FALSE_EXPECTED \
    JSON_CONSTANT_EXPECTED("false")

#define INVALID_UNICODE_ESCAPED_SEQUENCE(offset) \
    "invalid unicode escaped sequence found on its " offset " digit"

#define TRAIL_SURROGATE_EXPECTED \
    "unicode escape sequence expected for trail surrogate"

static const char * const state_error_table[] = {
/* GO: start */
    [ S(GO) ] = "beginning of object or array expected",
/* OK */
    [ S(OK) ] = "comma or ending of object or array expected",
/* _O: object */
    [ S(_O) ] = "key or ending of object expected",
/* _K: key */
    [ S(_K) ] = "string expected as object key",
/* CO: colon */
    [ S(CO) ] = "colon expected",
/* _V: value */
    [ S(_V) ] = "beginning of any JSON value expected",
/* _A: array */
    [ S(_A) ] = "beginning of any JSON value or ending of array expected",
/* _S: string */
    [ S(_S) ] = "invalid character found for a JSON string",
/* ES: escape */
    [ S(ES) ] = "invalid escaped sequence",
/* U1..4: \uXXXX */
    [ S(U1) ] = INVALID_UNICODE_ESCAPED_SEQUENCE("1st"),
    [ S(U2) ] = INVALID_UNICODE_ESCAPED_SEQUENCE("2nd"),
    [ S(U3) ] = INVALID_UNICODE_ESCAPED_SEQUENCE("3rd"),
    [ S(U4) ] = INVALID_UNICODE_ESCAPED_SEQUENCE("4th"),
/* MI: minus */
    [ S(MI) ] = "non digit found right after minus",
/* ZE: zero */
    [ S(ZE) ] = "not one of: '.', mantisse, ']', '}', ','",
/* IN: integer */
    [ S(IN) ] = "not one of: digit, '.', mantisse, ']', '}', ','",
/* R1..R2: fraction */
    [ S(R1) ] = "non digit found right after dot",
    [ S(R2) ] = "not one of: digit, '.', mantisse, ']', '}', ','",
/* E1..E3: exp */
    [ S(E1) ] = "digit or sign expected right after exponent symbol",
    [ S(E2) ] = "non digit found right after exponent symbol",
    [ S(E3) ] = "not one of: digit, ']', '}', ','",
/* T1..3: true */
    [ S(T1) ] = JSON_TRUE_EXPECTED,
    [ S(T2) ] = JSON_TRUE_EXPECTED,
    [ S(T3) ] = JSON_TRUE_EXPECTED,
/* F1..4: false */
    [ S(F1) ] = JSON_FALSE_EXPECTED,
    [ S(F2) ] = JSON_FALSE_EXPECTED,
    [ S(F3) ] = JSON_FALSE_EXPECTED,
    [ S(F4) ] = JSON_FALSE_EXPECTED,
/* N1..3: null */
    [ S(N1) ] = JSON_NULL_EXPECTED,
    [ S(N2) ] = JSON_NULL_EXPECTED,
    [ S(N3) ] = JSON_NULL_EXPECTED,
/* D1..D2: trail surrogate */
    [ S(D1) ] = TRAIL_SURROGATE_EXPECTED,
    [ S(D2) ] = TRAIL_SURROGATE_EXPECTED,
};

enum {
    BUFFER_POLICY_NOP,
    BUFFER_POLICY_COPY,
    BUFFER_POLICY_UNESCAPE,
};

#define N BUFFER_POLICY_NOP
#define C BUFFER_POLICY_COPY /* COPY */
#define U BUFFER_POLICY_UNESCAPE /* UNESCAPE */
static const uint8_t buffer_policy_table[NR_STATES][NR_CLASSES] = {
    /*            SP WS {  }  [  ]  :  ,  "  \  /  +  -  .  0  19 a  b  cd e  f  l  n  r  s  t  u  AF E  other */
/* GO: start */
    [ S(GO) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
/* OK */
    [ S(OK) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
/* _O: object */
    [ S(_O) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
/* _K: key */
    [ S(_K) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
/* CO: colon */
    [ S(CO) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
/* _V: value */
    [ S(_V) ] = { N, N, N, N, N, N, N, N, N, N, N, N, C, N, C, C, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
/* _A: array */
    [ S(_A) ] = { N, N, N, N, N, N, N, N, N, N, N, N, C, N, C, C, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
/* _S: string */
    [ S(_S) ] = { C, N, C, C, C, C, C, C, N, N, C, C, C, C, C, C, C, C, C, C, C, C, C, C, C, C, C, C, C, C },
/* ES: escape */
    [ S(ES) ] = { N, N, N, N, N, N, N, N, U, U, U, N, N, N, N, N, N, U, N, N, U, N, U, U, N, U, N, N, N, N },
/* U1..4: \uXXXX */
    [ S(U1) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, C, C, C, C, C, C, C, N, N, N, N, N, N, C, C, N },
    [ S(U2) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, C, C, C, C, C, C, C, N, N, N, N, N, N, C, C, N },
    [ S(U3) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, C, C, C, C, C, C, C, N, N, N, N, N, N, C, C, N },
    [ S(U4) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, C, C, C, C, C, C, C, N, N, N, N, N, N, C, C, N },
/* MI: minus */
    [ S(MI) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, C, C, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
/* ZE: zero */
    [ S(ZE) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, C, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
/* IN: integer */
    [ S(IN) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, C, C, C, N, N, N, C, N, N, N, N, N, N, N, N, C, N },
/* R1..R2: fraction */
    [ S(R1) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, C, C, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
    [ S(R2) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, C, C, N, N, N, C, N, N, N, N, N, N, N, N, C, N },
/* E1..E3: exp */
    [ S(E1) ] = { N, N, N, N, N, N, N, N, N, N, N, C, C, N, C, C, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
    [ S(E2) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, C, C, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
    [ S(E3) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, C, C, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
/* T1..3: true */
    [ S(T1) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
    [ S(T2) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
    [ S(T3) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
/* F1..4: false */
    [ S(F1) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
    [ S(F2) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
    [ S(F3) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
    [ S(F4) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
/* N1..3: null */
    [ S(N1) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
    [ S(N2) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
    [ S(N3) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
/* D1..D2: trail surrogate */
    [ S(D1) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
    [ S(D2) ] = { N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N },
};
#undef N
#undef C
#undef U
#undef S

enum modes {
    MODE_ARRAY,
    MODE_DONE,
    MODE_KEY,
    MODE_OBJECT,
};

typedef struct {
    int state;
    int top;
    int type;
    int depth;
    size_t offset;
    error_t **error;
    bool expecting_key;
    char *key_buffer;
    char *val_buffer;
    char *kp, *vp;
    char **buffer;
    char **bp;
    uint16_t utf16cu;
    int stack[JSON_MAX_DEPTH];
    int output_depth;
    json_type_t types[JSON_MAX_DEPTH]; // save some calls to json_get_type
    json_value_t values[JSON_MAX_DEPTH];
} json_parser_t;

typedef enum {
    JSON_PARSE_TYPE_NONE,
    JSON_PARSE_TYPE_INT,
    JSON_PARSE_TYPE_FLOAT,
    JSON_PARSE_TYPE_NULL,
    JSON_PARSE_TYPE_FALSE,
    JSON_PARSE_TYPE_TRUE,
//     JSON_PARSE_TYPE_STRING,
//     JSON_PARSE_TYPE_ARRAY,
//     JSON_PARSE_TYPE_OBJECT,
} json_parse_type_t;

#define RESET_BUFFER(jp, constptr, ptr) \
    do { \
        /**jp->constptr = '\0';*/ \
        jp->ptr = jp->constptr; \
    } while (0);

#define SWITCH_BUFFER_TO(jp, constptr, ptr) \
    do { \
        jp->bp = &jp->ptr; \
        jp->buffer = &jp->constptr; \
    } while(0);

static void json_push(json_parser_t *jp, json_value_t value, json_type_t type)
{
    assert(jp->output_depth >= -1);

    if (STATE_GO != jp->state) {
        switch (jp->types[jp->output_depth]) {
            case JSON_TYPE_ARRAY:
            {
                json_node_t *node;

                node = (json_node_t *) jp->values[jp->output_depth];
                dptrarray_push((DPtrArray *) node->value, (void *) value);
                break;
            }
            case JSON_TYPE_OBJECT:
            {
                json_node_t *node;

                node = (json_node_t *) jp->values[jp->output_depth];
                hashtable_put((HashTable *) node->value, 0, jp->key_buffer, value, NULL);
                RESET_BUFFER(jp, key_buffer, kp);
                break;
            }
            default:
                assert(FALSE);
                break;
        }
    }
    switch (type) {
        case JSON_TYPE_ARRAY:
        case JSON_TYPE_OBJECT:
            ++jp->output_depth;
            jp->types[jp->output_depth] = type;
            jp->values[jp->output_depth] = value;
            break;
        default:
            /* NOP */
            RESET_BUFFER(jp, val_buffer, vp);
            break;
    }
}

static int state_push(json_parser_t *jp, int mode)
{
    ++jp->top;
    if (jp->top >= jp->depth) {
        error_set(jp->error, WARN, "JSON maximum depth of %d was exceeded", jp->depth);
        return 1;
    }
    jp->stack[jp->top] = mode;

    return 0;
}

static int state_pop(json_parser_t *jp, int mode)
{
    if (jp->top < 0) {
        error_set(jp->error, WARN, "no remaining state to pop on the stack");
        return 1;
    }
    if (mode != jp->stack[jp->top]) {
        error_set(jp->error, WARN, "unexpected mode pop of stack");
        return 1;
    }
    --jp->top;

    return 0;
}

static const uint8_t hextable[] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
     0, 1, 2, 3, 4, 5, 6, 7, 8, 9,-1,-1,-1,-1,-1,-1,
    -1,10,11,12,13,14,15,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,10,11,12,13,14,15,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};

#define PUSH_CHAR(jp, character) \
    do { \
        **jp->bp = character; \
        ++*jp->bp; \
    } while (0);

#define hex(c) (hextable[(uint8_t) c])
#define U16_IS_LEAD(c)  (0xD800 == ((c) & 0xFFFFFC00))
#define U16_IS_TRAIL(c) (0xDC00 == ((c) & 0xFFFFFC00))
static int act_uc(json_parser_t *jp)
{
    int ret;
    uint32_t cp;

    assert(*jp->bp - *jp->buffer >= 4);

    ret = 0;
    cp = (hex(*(*jp->bp - 4)) << 12) | (hex(*(*jp->bp - 3)) << 8) | (hex(*(*jp->bp - 2)) << 4) | hex(*(*jp->bp - 1));
    *jp->bp -= 4;
    if (!jp->utf16cu && cp < 0x80) {
        PUSH_CHAR(jp, (char) cp);
    } else if (jp->utf16cu) {
        if (!U16_IS_TRAIL(cp)) {
            ret = 1;
            error_set(jp->error, WARN, "%s at offset %zu", TRAIL_SURROGATE_EXPECTED, jp->offset - 5);
        }
        cp = 0x10000 + ((jp->utf16cu & 0x3FF) << 10) + (cp & 0x3FF);
        PUSH_CHAR(jp, (char) ((cp >> 18) | 0xF0));
        PUSH_CHAR(jp, (char) (((cp >> 12) & 0x3F) | 0x80));
        PUSH_CHAR(jp, (char) (((cp >> 6) & 0x3F) | 0x80));
        PUSH_CHAR(jp, (char) ((cp & 0x3F) | 0x80));
        jp->utf16cu = 0;
    } else if (U16_IS_TRAIL(cp)) {
        ret = 1;
        error_set(jp->error, WARN, "unicode escape sequence expected for a lead surrogate before trail one at offset %zu", jp->offset - 5);
    } else if (U16_IS_LEAD(cp)) {
        jp->utf16cu = cp;
    } else if (cp < 0x800) {
        PUSH_CHAR(jp, (char) ((cp >> 6) | 0xC0));
        PUSH_CHAR(jp, (char) ((cp & 0x3F) | 0x80));
    } else {
        PUSH_CHAR(jp, (char) ((cp >> 12) | 0xE0));
        PUSH_CHAR(jp, (char) (((cp >> 6) & 0x3F) | 0x80));
        PUSH_CHAR(jp, (char) (((cp >> 0) & 0x3F) | 0x80));
    }
    jp->state = (jp->utf16cu) ? STATE_D1 : STATE__S;

    return ret;
}

/* object begin */
static int act_ob(json_parser_t *jp)
{
    jp->expecting_key = TRUE;
    json_push(jp, json_object(), JSON_TYPE_OBJECT);
    SWITCH_BUFFER_TO(jp, key_buffer, kp);

    return state_push(jp, MODE_OBJECT);
}

/* object end */
static int act_oe(json_parser_t *jp)
{
    --jp->output_depth;
    jp->expecting_key = FALSE;
    SWITCH_BUFFER_TO(jp, val_buffer, vp);

    return state_pop(jp, MODE_OBJECT);
}

/* array begin */
static int act_ab(json_parser_t *jp)
{
    json_push(jp, json_array(), JSON_TYPE_ARRAY);
    SWITCH_BUFFER_TO(jp, val_buffer, vp);

    return state_push(jp, MODE_ARRAY);
}

/* array_end */
static int act_ae(json_parser_t *jp)
{
    --jp->output_depth;

    return state_pop(jp, MODE_ARRAY);
}

/* string end */
static int act_se(json_parser_t *jp)
{
    **jp->bp = '\0';
    if (jp->expecting_key) {
        jp->state = STATE_CO;
        jp->expecting_key = FALSE;
        SWITCH_BUFFER_TO(jp, val_buffer, vp);
    } else {
        jp->state = STATE_OK;
        json_push(jp, json_string(*jp->buffer), JSON_TYPE_STRING);
    }

    return 0;
}

static int act_sp(json_parser_t *jp)
{
    if (0 == jp->top) {
        error_set(jp->error, WARN, "Comma found out of structure");
        return 1;
    }
    if (MODE_OBJECT == jp->stack[jp->top]) {
        jp->expecting_key = TRUE;
        jp->state = STATE__K;
        SWITCH_BUFFER_TO(jp, key_buffer, kp);
    } else {
        jp->state = STATE__V;
        SWITCH_BUFFER_TO(jp, val_buffer, vp);
    }

    return 0;
}

struct action_descr
{
    int (*call)(json_parser_t *);
    uint8_t type;
    uint8_t state; /* 0 if we let the callback set the value it want */
    uint8_t dobuffer;
};

static const struct action_descr actions_map[] = {
    [STATE_MX & ~0x80] = { NULL,   JSON_PARSE_TYPE_INT,   STATE_MI, 0 },
    [STATE_ZX & ~0x80] = { NULL,   JSON_PARSE_TYPE_INT,   STATE_ZE, 0 },
    [STATE_IX & ~0x80] = { NULL,   JSON_PARSE_TYPE_INT,   STATE_IN, 0 },
    [STATE_DE & ~0x80] = { NULL,   JSON_PARSE_TYPE_FLOAT, STATE_E1, 0 },
    [STATE_DF & ~0x80] = { NULL,   JSON_PARSE_TYPE_FLOAT, STATE_R1, 0 },
    [STATE_NU & ~0x80] = { NULL,   JSON_PARSE_TYPE_NULL,  STATE_OK, 0 },
    [STATE_FA & ~0x80] = { NULL,   JSON_PARSE_TYPE_FALSE, STATE_OK, 0 },
    [STATE_TR & ~0x80] = { NULL,   JSON_PARSE_TYPE_TRUE,  STATE_OK, 0 },
    [STATE_KS & ~0x80] = { NULL,   JSON_PARSE_TYPE_NONE,  STATE__V, 0 },
    [STATE_UC & ~0x80] = { act_uc, JSON_PARSE_TYPE_NONE,  0,        0 },
    [STATE_OB & ~0x80] = { act_ob, JSON_PARSE_TYPE_NONE,  STATE__O, 0 },
    [STATE_OE & ~0x80] = { act_oe, JSON_PARSE_TYPE_NONE,  STATE_OK, 1 },
    [STATE_AB & ~0x80] = { act_ab, JSON_PARSE_TYPE_NONE,  STATE__A, 0 },
    [STATE_AE & ~0x80] = { act_ae, JSON_PARSE_TYPE_NONE,  STATE_OK, 1 },
    [STATE_SE & ~0x80] = { act_se, JSON_PARSE_TYPE_NONE,  0,        0 },
    [STATE_SP & ~0x80] = { act_sp, JSON_PARSE_TYPE_NONE,  0,        1 },
};

#ifdef HAVE_STRTOD_L
# include <locale.h>
#endif /* HAVE_STRTOD_L */

static int do_action(json_parser_t *jp, int next_state)
{
    int ret;
    const struct action_descr *descr = &actions_map[next_state & ~0x80];

    ret = 0;
    if (descr->call) {
        if (descr->dobuffer) {
            switch (jp->type) {
                case JSON_PARSE_TYPE_NONE:
                    /* NOP */
                    break;
                case JSON_PARSE_TYPE_NULL:
                    json_push(jp, json_null, JSON_TYPE_NULL);
                    break;
                case JSON_PARSE_TYPE_TRUE:
                    json_push(jp, json_true, JSON_TYPE_TRUE);
                    break;
                case JSON_PARSE_TYPE_FALSE:
                    json_push(jp, json_false, JSON_TYPE_FALSE);
                    break;
                case JSON_PARSE_TYPE_INT:
                {
                    long long v;

                    PUSH_CHAR(jp, '\0');
                    v = strtoll(jp->val_buffer, NULL, 10);
                    if (((LLONG_MIN == v || LLONG_MAX == v) && ERANGE == errno)) {
//                         fprintf(stderr, "number %s out of range, stored as string\n", jp->val_buffer);
                        json_push(jp, json_string(jp->val_buffer), JSON_TYPE_STRING);
                    } else if (v > INT64_MAX || v < INT64_MIN) {
                        json_push(jp, json_number((double) v), JSON_TYPE_NUMBER);
                    } else {
                        json_push(jp, json_integer(v), JSON_TYPE_NUMBER);
                    }
                    break;
                }
                case JSON_PARSE_TYPE_FLOAT:
                {
                    double v;

#ifdef HAVE_STRTOD_L
                    locale_t c_locale;

                    c_locale = newlocale (LC_NUMERIC_MASK, "C", NULL);
                    v = strtod_l(jp->val_buffer, NULL, c_locale);
                    freelocale (c_locale);
#else
                    // WARNING: strtod is locale dependant (C/en_US = '.', fr_FR = ',')
                    v = strtod(jp->val_buffer, NULL);
#endif /* HAVE_STRTOD_L */
                    if (ERANGE == errno) {
                        json_push(jp, json_string(jp->val_buffer), JSON_TYPE_STRING);
                    } else {
                        json_push(jp, json_number(v), JSON_TYPE_NUMBER);
                    }
                    break;
                }
            }
        }
        ret = descr->call(jp);
    }
    if (descr->state) {
        jp->state = descr->state;
    }
    jp->type = descr->type;

    return ret;
}

static void json_parser_init(json_parser_t *jp, size_t length, error_t **error)
{
    bzero(jp, sizeof(*jp));
    jp->top = -1;
    jp->offset = 0;
    jp->utf16cu = 0;
    jp->values[0] = 0;
    jp->error = error;
    jp->state = STATE_GO;
    jp->output_depth = -1;
    jp->expecting_key = FALSE;
    jp->depth = JSON_MAX_DEPTH;
    jp->kp = jp->key_buffer = mem_new_n0(*jp->key_buffer, length + 1);
    jp->vp = jp->val_buffer = mem_new_n0(*jp->val_buffer, length + 1);
    SWITCH_BUFFER_TO(jp, val_buffer, vp);
    state_push(jp, MODE_DONE);
}

#define IS_STATE_ACTION(s) ((s) & 0x80)
json_document_t *json_document_parse(const char *string, error_t **error) /* WARN_UNUSED_RESULT */
{
    int ret;
    const char *p;
    int next_class; /* the next character class */
    uint8_t next_state; /* the next state */
    uint8_t buffer_policy;
    json_parser_t j, *jp;
    json_document_t *doc;
    const char * const end = string + strlen(string);

    jp = &j;
    ret = 0;
    doc = NULL;
    json_parser_init(jp, end - string, error);
    for (p = string; /*jp->offset < strlen(string)*/ p < end; jp->offset++, p++) {
        unsigned char ch = *p/*string[jp->offset]*/;

        next_class = (ch >= 128) ? C_OTHER : character_class[ch];
        if (C_ERROR == next_class) {
            error_set(error, WARN, "illegal character found at offset %" PRIuPTR " (got 0x%02X)", p - string, *p);
            goto err;
        }
        next_state = state_transition_table[jp->state][next_class];
        buffer_policy = buffer_policy_table[jp->state][next_class];
        if (STATE___ == next_state) {
            if ('\0' == *state_error_table[jp->state]) {
                error_set(error, WARN, "unexpected character at offset %" PRIuPTR " (got 0x%02X)", state_error_table[jp->state], p - string, *p);
            } else {
                error_set(error, WARN, "%s at offset %" PRIuPTR " (got 0x%02X)", state_error_table[jp->state], p - string, *p);
            }
            goto err;
        }
        switch (buffer_policy) {
            case BUFFER_POLICY_NOP:
                break;
            case BUFFER_POLICY_COPY:
                PUSH_CHAR(jp, *p);
                break;
            case BUFFER_POLICY_UNESCAPE:
                switch (*p) {
                    case 'b':
                        PUSH_CHAR(jp, '\b');
                        break;
                    case 'f':
                        PUSH_CHAR(jp, '\f');
                        break;
                    case 'n':
                        PUSH_CHAR(jp, '\n');
                        break;
                    case 'r':
                        PUSH_CHAR(jp, '\r');
                        break;
                    case 't':
                        PUSH_CHAR(jp, '\t');
                        break;
                    case '"':
                        PUSH_CHAR(jp, '"');
                        break;
                    case '/':
                        PUSH_CHAR(jp, '/');
                        break;
                    case '\\':
                        PUSH_CHAR(jp, '\\');
                        break;
                    default:
                        PUSH_CHAR(jp, '\\');
                        PUSH_CHAR(jp, *p);
                }
                break;
            default:
                assert(FALSE);
                break;
        }
        if (IS_STATE_ACTION(next_state)) {
            ret = do_action(jp, next_state);
        } else {
            jp->state = next_state;
        }
        if (0 != ret) {
            // NOTE: error is set by caller
            goto err;
        }
    }
    if (FALSE) {
err:
        if (0 != jp->values[0]) {
            json_value_destroy(jp->values[0]);
        }
    } else {
        if (STATE_OK == jp->state && 0 == state_pop(jp, MODE_DONE)) {
            doc = json_document_new();
            doc->root = jp->values[0];
        } else {
            error_set(error, WARN, "premature end of document");
            if (0 != jp->values[0]) {
                json_value_destroy(jp->values[0]);
            }
        }
    }
    free(jp->key_buffer);
    free(jp->val_buffer);

    return doc;
}

#define UT(string, expect) \
    do { \
        error_t *error; \
        json_document_t *doc; \
 \
        error = NULL; \
        if (NULL == (doc = json_document_parse(string, &error))) { \
            printf("KO: failed to parse >%s<: %s\n", string, NULL == error ? "undefined error" : error->message); \
            error_destroy(error); \
        } else { \
            String *buffer; \
 \
            buffer = string_new(); \
            json_document_serialize(doc, buffer, 0); \
            printf("OK: >%s< <=> >%s<\n", string, buffer->ptr); \
            json_document_destroy(doc); \
            string_destroy(buffer); \
        } \
    } while (0);

// INITIALIZER_DECL(json_test);
INITIALIZER_P(json_test)
{
    UT("true", FALSE);
//     UT("true,true", FALSE);
//     UT("truefalse", FALSE);
//     UT("\"abc\\ndef\"", FALSE);
//     UT("\"abc\\ndef", FALSE);
    UT("{{}}", FALSE);
    UT("{{}", FALSE);
    UT("{\"foo\"{}}", FALSE);
    UT("{\"foo{}}", FALSE);
    UT("{\"foo\":{}}", TRUE);
    UT("{\"foo\":[1234]}", TRUE);
    UT("{\"foo\":[1234.56]}", TRUE);
    UT("{\"abc\":{\"def\":\"ghi\",\"klm\":\"nop\"}}", TRUE);
    UT("{\"status\":\"ok\",\"engagedUpTo\":null}", TRUE);
    UT("{\"abc\":{\"def\":\"ghi\",\"klm\":\"nop\\p\"}}", FALSE);
    UT("{\"abc\":{\"def\":\"ghi\",\"klm\":\"nop\\u34P6\"}}", FALSE);
    UT("[ false, tre ]", FALSE);
    UT("[ true, [ false ] ]", TRUE);
    UT("[ true, [ false ], null ]", TRUE);
    UT("[][]", FALSE);
    UT("{\"foo\":{}}}", FALSE);
    UT("{\"foo\":{\"bar\":", TRUE);
    UT("[\"\\uD835\"]", FALSE);
    UT("[\"\\uDE3C\"]", FALSE);
    UT("[\"\\uD835;\\uDE3C\"]", FALSE);
    UT("[\"\\uDE3C\\uD835\"]", TRUE);
    UT("[\"123\\uD835\\uDE3C456\"]", TRUE);
    UT("[\"a\\u123\"]", FALSE);
    UT("[\"a\\u1D63D\"]", TRUE);
    UT("[\"\x01\"]", FALSE);
    UT("[\"\\u001D\"]", TRUE);
#if 64 == __WORDSIZE
    UT("[-4611686018427387905]", TRUE); /* JSON_MIN_SIGNED - 1 => double */
    UT("[-4611686018427387904]", TRUE); /* JSON_MIN_SIGNED     => int64_t */
    UT("[-4611686018427387903]", TRUE); /* JSON_MIN_SIGNED + 1 => int64_t */
    UT("[4611686018427387902]", TRUE);  /* JSON_MAX_SIGNED - 1 => int64_t */
    UT("[4611686018427387903]", TRUE);  /* JSON_MAX_SIGNED     => int64_t */
    UT("[4611686018427387904]", TRUE);  /* JSON_MAX_SIGNED + 1 => double */
    UT("[4611686018427387905]", TRUE);  /* JSON_MAX_SIGNED + 2 => double */
    // irrelevant as long long <=> int64_t
# if 0
    UT("[-9223372036854775808]", TRUE); /* INT64_MIN - 1 */
    UT("[-9223372036854775807]", TRUE); /* INT64_MIN     */
    UT("[-9223372036854775806]", TRUE); /* INT64_MIN + 1 */
    UT("[-9223372036854775805]", TRUE); /* INT64_MIN + 2 */
    UT("[9223372036854775807]", TRUE);  /* INT64_MAX - 1 */
    UT("[9223372036854775808]", TRUE);  /* INT64_MAX     */
# endif
#elif 32 == __WORDSIZE
    UT("[-2147483649]", TRUE); /* INT32_MIN - 1 => double */
    UT("[-2147483648]", TRUE); /* INT32_MIN     => double */
    UT("[-2147483647]", TRUE); /* INT32_MIN + 1 => double  */
    UT("[2147483648]", TRUE); /* INT32_MAX + 1 => double */
    UT("[2147483647]", TRUE); /* INT32_MAX     => double */
    UT("[2147483646]", TRUE); /* INT32_MAX - 1 => double  */
    UT("[-1073741825]", TRUE); /* JSON_MIN_SIGNED - 1 => double */
    UT("[-1073741824]", TRUE); /* JSON_MIN_SIGNED     => int64_t */
    UT("[-1073741823]", TRUE); /* JSON_MIN_SIGNED + 1 => int64_t */
    UT("[1073741824]", TRUE); /* JSON_MAX_SIGNED + 1 => double */
    UT("[1073741823]", TRUE); /* JSON_MAX_SIGNED     => int64_t */
    UT("[1073741822]", TRUE); /* JSON_MAX_SIGNED - 1 => int64_t */
#else
# error undefined/unexpected __WORDSIZE
#endif
}
