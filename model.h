#ifndef MODEL_H

# define MODEL_H

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
} model_t;

typedef struct {
    const model_t *model;
} modelized_t;

model_t *model_new(const char *, size_t, model_field_t *, size_t);
void model_destroy(model_t *);

const model_field_t *model_find_field_by_name(const model_t *, const char *, size_t);

void modelized_destroy(modelized_t *);
modelized_t *modelized_new(const model_t *);
void modelized_init(const model_t *, modelized_t *);

#endif /* !MODEL_H */
