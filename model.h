#ifndef MODEL_H

# define MODEL_H

# include "common.h"

typedef enum {
    MODEL_TYPE_INT,
    MODEL_TYPE_BOOL,
    MODEL_TYPE_ENUM,
    MODEL_TYPE_DATE,
    MODEL_TYPE_STRING,
    MODEL_TYPE_DATETIME,
    _MODEL_TYPE_LAST = MODEL_TYPE_DATETIME
} model_field_type_t;

#define MODEL_FLAG_NONE     (0)
#define MODEL_FLAG_RO       (1<<0)
#define MODEL_FLAG_PRIMARY  (1<<1)
#define MODEL_FLAG_UNIQUE   (1<<2)
#define MODEL_FLAG_NULLABLE (1<<3)
#define MODEL_FLAG_INTERNAL (1<<4)

#define DECL_FIELD_STRUCT_NAME

#define DECL_FIELD_SIMPLE(i18n_name, ovh_name, struct_member_name, type, flags) \
    { i18n_name, ovh_name, type, offsetof(DECL_FIELD_STRUCT_NAME, struct_member_name), 0, NULL, flags }

#define DECL_FIELD_INT(i18n_name, ovh_name, flags) \
    DECL_FIELD_SIMPLE(i18n_name, #ovh_name, ovh_name, MODEL_TYPE_INT, flags)

#define DECL_FIELD_BOOL(i18n_name, ovh_name, flags) \
    DECL_FIELD_SIMPLE(i18n_name, #ovh_name, ovh_name, MODEL_TYPE_BOOL, flags)

#define DECL_FIELD_DATE(i18n_name, ovh_name, flags) \
    DECL_FIELD_SIMPLE(i18n_name, #ovh_name, ovh_name, MODEL_TYPE_DATE, flags)

#define DECL_FIELD_STRING(i18n_name, ovh_name, flags) \
    DECL_FIELD_SIMPLE(i18n_name, #ovh_name, ovh_name, MODEL_TYPE_STRING, flags)

#define DECL_FIELD_DATETIME(i18n_name, ovh_name, flags) \
    DECL_FIELD_SIMPLE(i18n_name, #ovh_name, ovh_name, MODEL_TYPE_DATETIME, flags)

#define DECL_FIELD_ENUM(i18n_name, ovh_name, flags, enum_values) \
    { i18n_name, #ovh_name, MODEL_TYPE_ENUM, offsetof(DECL_FIELD_STRUCT_NAME, ovh_name), 0, enum_values, flags }

#define MODELIZED_SET(obj, member, new_value) \
    do { \
        obj->member = new_value; \
        obj->member##_changed = TRUE; \
        ((modelized_t *) obj)->changed = TRUE; \
    } while (0);

#define MODELIZED_SET_STRING(obj, member, new_value) \
    do { \
        if (NULL != obj->member) { \
            free((void *) obj->member); \
            obj->member = NULL; \
        } \
        if (NULL != new_value) { \
            obj->member = strdup(new_value); \
        } \
        obj->member##_changed = TRUE; \
        ((modelized_t *) obj)->changed = TRUE; \
    } while (0);

#define DECL_MEMBER(name, type) \
    type name; \
    bool name##_changed

#define DECL_MEMBER_INT(name) \
    DECL_MEMBER(name, int)

#define DECL_MEMBER_ENUM(name) \
    DECL_MEMBER_INT(name)

#define DECL_MEMBER_BOOL(name) \
    DECL_MEMBER(name, bool)

#define DECL_MEMBER_DATE(name) \
    DECL_MEMBER(name, time_t)

#define DECL_MEMBER_STRING(name) \
    DECL_MEMBER(name, char *)

#define DECL_MEMBER_DATETIME(name) \
    DECL_MEMBER_DATE(name)

typedef struct {
    const char *i18n_key;
    const char *ovh_name;
    model_field_type_t type;
    size_t offset;
    uintptr_t null_replacement;
    const char * const * enum_values;
    uint32_t flags;
} model_field_t;

#define MODEL_FIELD_SENTINEL \
    { NULL, NULL, 0, 0, 0, NULL, 0 }

typedef struct {
    size_t size;
    const char *name;
    const char *(*to_name)(void *); // ou simplement size_t vers offsetof du model_field_t utiliser ?
    const char *(*to_s)(void *); // ou on copie la chaîne à la suite du buffer (String?) ? - void (*to_name)(void *, String *)
    const model_field_t *fields;
    size_t fields_count;
    const model_field_t *pk;
} model_t;

typedef struct {
    bool changed;
    bool persisted;
    const model_t *model;
} modelized_t;

#if 0
typedef struct {
    void *data; // For RAM: the hashtable?
    bool (*init)(void *, error_t *); // model initialization? For SQLite: precalculate SQL statements?
    bool (*all)(Iterator *, void *, error_t *); // select for list
    bool (*save)(modelized_t *, void *, error_t *); // insert/update (upsert?) for add, update and result of listing HTTP query?
    bool (*delete)(modelized_t *, void *, error_t *); // removal
    bool (*preload)(void *, error_t *); // run listing HTTP query? (only for RAM, not SQLite backend)
    bool (*find_by_name)(const char *name, modelized_t **, void *, error_t *); // completion and/or arguments parsing?
} model_backend_t;
#endif

extern const size_t model_type_size_map[];

modelized_t *modelized_copy(modelized_t *);
model_t *model_new(const char *, size_t, model_field_t *, size_t);
void model_destroy(model_t *);

const model_field_t *model_find_field_by_name(const model_t *, const char *, size_t);

void modelized_destroy(modelized_t *);
modelized_t *modelized_new(const model_t *);
void modelized_init(const model_t *, modelized_t *);

#endif /* !MODEL_H */
