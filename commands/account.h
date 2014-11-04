#ifndef ACCOUNT_H

# define ACCOUNT_H

const char *account_key(void);
const char *account_current(void);

void account_current_set_data(const char *, void *);
bool account_current_get_data(const char *, void **);

#endif /* ACCOUNT_H */
