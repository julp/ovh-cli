#ifndef ACCOUNT_H

# define ACCOUNT_H

typedef struct {
    modelized_t data;
    DECL_MEMBER_INT(id);
    DECL_MEMBER_BOOL(is_default);
    DECL_MEMBER_STRING(name);
    DECL_MEMBER_STRING(password);
    DECL_MEMBER_DATETIME(expires_at);
    DECL_MEMBER_STRING(consumer_key);
    DECL_MEMBER_ENUM(endpoint_id);
} account_t;

typedef struct {
    modelized_t data;
    DECL_MEMBER_STRING(app_key);
    DECL_MEMBER_STRING(secret);
    DECL_MEMBER_ENUM(endpoint_id);
} application_t;

#define key app_key

model_t *account_model;
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
