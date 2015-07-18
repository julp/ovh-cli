#ifndef API_H

# define API_H

enum {
    RESPONSE_IGNORE,
    RESPONSE_XML,
    RESPONSE_HTML,
    RESPONSE_TEXT,
    RESPONSE_JSON
};

#define API_BASE_URL "%B"

#define REQUEST_FLAG_NONE       0
#define REQUEST_FLAG_SIGN       (1<<1)
#define REQUEST_FLAG_COPY       (1<<2)
#define REQUEST_FLAG_IGNORE_404 (1<<3)
#define REQUEST_FLAG_JSON       (1<<4)

typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE
} http_method_t;

typedef struct request_t request_t;

# include "common.h"

void request_add_post_field(request_t *, const char *, const char *);
void request_add_header1(request_t *, const char *);
bool request_add_header2(request_t *, const char *, const char *, error_t **);

void request_destroy(request_t *);
long request_response_status(request_t *);
request_t *request_new(uint32_t, http_method_t, const void *, error_t **, const char *, ...);
request_t *request_vnew(uint32_t, http_method_t, const void *, error_t **, const char *, va_list);

bool request_execute(request_t *, int, void **, error_t **);

# define REQUEST_XML_RESPONSE_WANTED(/* request_t * */ req) \
    request_add_header1(req, "Accept: application/xml")

const char *request_consumer_key(time_t *, error_t **);

#endif /* API_H */
