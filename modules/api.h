#ifndef API_H

# define API_H

enum {
    RESPONSE_IGNORE,
    RESPONSE_XML,
    RESPONSE_HTML,
    RESPONSE_TEXT,
    RESPONSE_JSON
};

#define REQUEST_FLAG_NONE 0
#define REQUEST_FLAG_SIGN (1<<1)
#define REQUEST_FLAG_COPY (1<<2)
// #define REQUEST_FLAG_XML  (1<<3)
// #define REQUEST_FLAG_JSON (1<<4)

typedef struct request_t request_t;

# include "common.h"

void request_add_post_field(request_t *, const char *, const char *);
void request_add_header(request_t *, const char *);

void request_dtor(request_t *);
request_t *request_get(uint32_t, const char *, ...) PRINTF(2, 3);
request_t *request_delete(uint32_t, const char *, ...) PRINTF(2, 3);
request_t *request_put(uint32_t, const char *, const char *, ...) PRINTF(3, 4);
request_t *request_post(uint32_t, const char *, const char *, ...) PRINTF(3, 4);

bool request_execute(request_t *, int, void **, error_t **);

# define REQUEST_XML_RESPONSE_WANTED(/* request_t * */ req) \
    request_add_header(req, "Accept: application/xml")

# define __USE_XOPEN
# include <time.h>

const char *request_consumer_key(const char *, const char *, time_t *, error_t **);

#endif /* API_H */
