#ifndef TABLE_H

# define TABLE_H

# define TABLE_FLAG_NONE       (0)
# define TABLE_FLAG_NO_HEADERS (1<<0)
# define TABLE_FLAG_DELEGATE   (1<<1)

# define TABLE_TYPE_FLAGS 0xFF00

# define TABLE_TYPE(x) \
    ((x) & ~TABLE_TYPE_FLAGS)

# define TABLE_TYPE_DELEGATE 0x0100

typedef enum {
    TABLE_SORT_ASC,
    TABLE_SORT_DESC,
    _TABLE_SORT_COUNT,
} table_sort_t;

# include "model.h"

# define TABLE_TYPE_INT      MODEL_TYPE_INT
# define TABLE_TYPE_BOOL     MODEL_TYPE_BOOL
# define TABLE_TYPE_BOOLEAN  MODEL_TYPE_BOOL
# define TABLE_TYPE_ENUM     MODEL_TYPE_ENUM
# define TABLE_TYPE_DATE     MODEL_TYPE_DATE
# define TABLE_TYPE_STRING   MODEL_TYPE_STRING
# define TABLE_TYPE_DATETIME MODEL_TYPE_DATETIME
# define _TABLE_TYPE_LAST    _MODEL_TYPE_LAST

#if 0
typedef enum {
    TABLE_TYPE_INT,
    TABLE_TYPE_INTEGER = TABLE_TYPE_INT,
    TABLE_TYPE_ENUM,
    TABLE_TYPE_STRING,
    TABLE_TYPE_BOOL,
    TABLE_TYPE_BOOLEAN = TABLE_TYPE_BOOL,
    TABLE_TYPE_DATE,
    TABLE_TYPE_DATETIME,
    _TABLE_TYPE_LAST = TABLE_TYPE_DATETIME
} column_type_t;
#endif

typedef struct table_t table_t;

void table_destroy(table_t *);
void table_display(table_t *, uint32_t);
table_t *table_new(size_t, ...);
void table_sort(table_t *, size_t, table_sort_t);
void table_store(table_t *, ...);

table_t *table_new_from_model(const model_t *, uint32_t);
void table_store_modelized(table_t *, modelized_t *);

#endif /* !TABLE_H */
