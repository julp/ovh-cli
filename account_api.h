#ifndef ACCOUNT_API_H

# define ACCOUNT_API_H

# include "endpoints.h"
# include "struct/hashtable.h"

typedef struct {
    char *key;
    char *secret;
    int endpoint_id;
//     const endpoint_t *endpoint;
} application_t;

typedef struct {
    char *account;
    char *password;
    time_t expires_at;
    const char *consumer_key;
    HashTable *modules_data;
    int endpoint_id;
//     const endpoint_t *endpoint;
} account_t;

account_t * const *current_account;
const application_t *current_application;

bool check_current_application_and_account(bool, error_t **);

#endif /* !ACCOUNT_API_H */
