#ifndef SQLITE_H

# define SQLITE_H

# include <sqlite3.h>
# include "model.h"
# include "struct/iterator.h"

typedef enum {
    SQLITE_TYPE_BOOL,
    SQLITE_TYPE_BOOLEAN = SQLITE_TYPE_BOOL,
    SQLITE_TYPE_INT,
    SQLITE_TYPE_STRING,
    SQLITE_TYPE_IGNORE
} sqlite_bind_type_t;

typedef struct {
    int version;
    const char *statement;
} sqlite_migration_t;

typedef struct {
    const char *statement;
    const char *inbinds;
    const char *outbinds;
    sqlite3_stmt *prepared;
} sqlite_statement_t;

#define DECL_STMT(sql, inbinds, outbinds) \
    { sql, inbinds, outbinds, NULL }

int sqlite_affected_rows(void);
int sqlite_last_insert_id(void);

bool create_or_migrate(const char *, const char *, sqlite_migration_t *, size_t, error_t **);

void statement_bind(sqlite_statement_t *, const bool *, ...);
void statement_bind_from_model(sqlite_statement_t *, model_t, const bool *, char *);
bool statement_fetch(sqlite_statement_t *, error_t **, ...);
bool statement_fetch_to_model(sqlite_statement_t *, model_t, char *, error_t **);
void statement_batched_finalize(sqlite_statement_t *, size_t);
bool statement_batched_prepare(sqlite_statement_t *, size_t, error_t **);
void statement_to_iterator(Iterator *, sqlite_statement_t *, ...);
void statement_model_to_iterator(Iterator *, sqlite_statement_t *, model_t, char *);

command_status_t statement_to_table(const model_t *, sqlite_statement_t *, void *);

#endif /* SQLITE_H */
