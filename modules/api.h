#ifndef API_H

# define API_H

enum {
    RESPONSE_IGNORE,
    RESPONSE_XML,
    RESPONSE_HTML
};

enum {
    STRING_COPY,
    STRING_NOCOPY
};

typedef struct request_t request_t;

# include "common.h"

void request_add_post_field(request_t *, const char *, const char *);
void request_add_header(request_t *, const char *);

void request_dtor(request_t *);
request_t *request_get(const char *, ...) PRINTF(1, 2);
request_t *request_delete(const char *, ...) PRINTF(1, 2);
request_t *request_post(const char *, int, const char *, ...) PRINTF(3, 4);

void request_sign(request_t *);
int request_execute(request_t *, int, void **);

# include <time.h>

const char *request_consumer_key(const char *, const char *, time_t *);

#endif /* API_H */
