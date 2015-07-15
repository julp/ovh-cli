#ifndef DATE_H

# define DATE_H

# include <time.h>
# include "common.h"
# include "struct/iterator.h"

int date_diff_in_days(time_t, time_t);
bool date_parse_to_timestamp(const char *, const char *, time_t *);
bool parse_duration(const char *, time_t *);

struct tm timestamp_to_tm(time_t t);
size_t timestamp_to_localtime(time_t, const char *, char *, size_t);
void time_to_iterator(Iterator *, time_t, time_t, int64_t);

#endif /* !DATE_H */
