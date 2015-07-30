#ifndef ACCOUNT_H

# define ACCOUNT_H

typedef struct {
    modelized_t data;
    int id;
    bool isdefault;
    char *name;
    char *password;
    time_t expires_at;
    const char *consumer_key;
    int endpoint_id;
} account_t;

typedef struct {
    modelized_t data;
    char *key;
    char *secret;
    int endpoint_id;
} application_t;

const account_t *current_account;
const application_t *current_application;

bool account_set_last_fetch_for(const char *, error_t **);
bool account_get_last_fetch_for(const char *, time_t *, error_t **);

const char *account_current(void);

void account_invalidate_consumer_key(error_t **);
void account_current_set_data(const char *, void *);
bool account_current_get_data(const char *, void **);
void account_register_module_callbacks(const char *, DtorFunc, void (*)(void **));

#endif /* ACCOUNT_H */
