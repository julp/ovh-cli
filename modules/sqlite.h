#ifndef SQLITE_H

# define SQLITE_H

# include <sqlite3.h>
# include "struct/iterator.h"

typedef struct {
    int version;
    const char *statement;
} sqlite_migration_t;

void create_or_migrate(const char *, const char *, sqlite_migration_t *, size_t);

void statement_batched_finalize(sqlite3_stmt **, size_t);
bool statement_batched_prepare(const char **, sqlite3_stmt **, size_t);
void statement_to_iterator(Iterator *, sqlite3_stmt *, const char *, ...);

#endif /* SQLITE_H */
