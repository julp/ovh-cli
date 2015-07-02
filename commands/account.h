#ifndef ACCOUNT_H

# define ACCOUNT_H

const char *account_current(void);

void account_current_set_data(const char *, void *);
bool account_current_get_data(const char *, void **);
void account_register_module_callbacks(const char *, DtorFunc, void (*)(void **));

#endif /* ACCOUNT_H */
