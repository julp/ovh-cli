#ifndef ACCOUNT_API_H

# define ACCOUNT_API_H

# include "endpoints.h"
# include "struct/hashtable.h"

typedef struct {
    char *key;
    char *secret;
    int endpoint_id;
} application_t;

typedef struct {
    int id;
    bool isdefault;
    char *name;
    char *password;
    time_t expires_at;
    const char *consumer_key;
    int endpoint_id;
} account_t;

account_t *current_account;
application_t *current_application;

bool check_current_application_and_account(bool, error_t **);

#endif /* !ACCOUNT_API_H */
