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

void request_add_post_field(request_t *, const char *, const char *);
void request_add_header(request_t *, const char *);

void request_dtor(request_t *);
request_t *request_get(const char *);
request_t *request_delete(const char *);
request_t *request_post(const char *, const char *, int);

void request_sign(request_t *);
int request_execute(request_t *, int, void **);

# include <time.h>

const char *request_consumer_key(const char *, const char *, time_t *);

#endif /* API_H */
