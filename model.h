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

typedef struct {
    const char *column_name;
    model_field_type_t type;
    size_t offset;
    uintptr_t null_replacement;
    const char * const * enum_values;
} model_field_t;

typedef struct {
    size_t size;
    const char *(*to_s)(void *); // ou simplement size_t vers offsetof du champ à utiliser ?
    const char *(*to_name)(void *); // ou on copie la chaîne à la suite du buffer (String?) ? - void (*to_name)(void *, String *)
    const model_field_t *fields;
} model_t;

#define MODEL_FIELD_SENTINEL \
    { NULL, 0, 0, 0, NULL }

#endif /* !MODEL_H */
