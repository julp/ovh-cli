#ifndef UTIL_H

# define UTIL_H

# include "common.h"

int console_width(void);
int console_height(void);
bool confirm(const main_options_t *, const char *, ...);
int launch_editor(char **, const char *, error_t **);

#endif /* !UTIL_H */
