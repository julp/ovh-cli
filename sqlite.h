#ifndef SQLITE_H

# define SQLITE_H

# include <sqlite3.h>
# include "struct/iterator.h"

void statement_to_iterator(Iterator *, sqlite3_stmt *, const char *, ...);

#endif /* SQLITE_H */
