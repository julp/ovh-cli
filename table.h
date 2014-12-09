#ifndef TABLE_H

# define TABLE_H

# define TABLE_FLAG_NONE       0
# define TABLE_FLAG_NO_HEADERS (1<<0)

typedef enum {
    TABLE_TYPE_INT,
    TABLE_TYPE_ENUM,
    TABLE_TYPE_STRING,
    TABLE_TYPE_BOOLEAN,
} column_type_t;

typedef struct table_t table_t;

void table_destroy(table_t *);
void table_display(table_t *, uint32_t);
table_t *table_new(size_t, ...);
void table_store(table_t *, ...);

#endif /* !TABLE_H */
