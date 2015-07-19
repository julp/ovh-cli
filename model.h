#ifndef MODEL_H

# define MODEL_H

typedef enum {
    MODEL_TYPE_INT,
    MODEL_TYPE_BOOL,
    MODEL_TYPE_BOOLEAN = MODEL_TYPE_BOOL,
    MODEL_TYPE_ENUM,
    MODEL_TYPE_DATE,
    MODEL_TYPE_STRING,
    MODEL_TYPE_DATETIME,
} model_field_type_t;

typedef struct {
    const char *column_name;
    model_field_type_t type;
    size_t offset;
    uintptr_t null_replacement;
} model_field_t;

typedef struct {
    const model_field_t *fields;
} model_t;

#define MODEL_FIELD_SENTINEL \
    { NULL, 0, 0, 0 }

#endif /* !MODEL_H */
