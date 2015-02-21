#ifndef DATE_H

# define DATE_H

# include <time.h>
# include "common.h"

int date_diff_in_days(time_t, time_t);
bool date_parse(const char *, const char *, struct tm *, error_t **);
bool date_parse_simple(const char *, const char *, time_t *, error_t **);
bool parse_duration(const char *, time_t *);

#endif /* !DATE_H */
