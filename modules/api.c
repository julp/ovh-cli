#include <string.h>
#include <curl/curl.h>
#include <openssl/evp.h>
#include <libxml/xpath.h>
#include <libxml/parser.h>
#include <libxml/HTMLparser.h>

#include "common.h"
#include "modules/api.h"
#include "commands/account.h"

struct buffer {
    char *ptr;
    size_t length;
    size_t allocated;
};

typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT
} http_method_t;

struct request_t {
    CURL *ch;
    const char *url;
    // <CURLOPT_POSTFIELDS>
    const char *data;
    const char *pdata; // if pdata != data, pdata is a copy (= needs to be freed)
    // </CURLOPT_POSTFIELDS>
    http_method_t method;
    struct buffer buffer;
    unsigned int timestamp;
    struct curl_slist *headers;
    struct curl_httppost *formpost, *lastptr;
};

static const EVP_MD *md = NULL;

static struct {
    const char *name;
    size_t name_len;
    CURLoption curlconst;
} methods[] = {
    [HTTP_GET] = { "GET", STR_LEN("GET"), CURLOPT_HTTPGET },
    [HTTP_PUT] = { "PUT", STR_LEN("PUT"), CURLOPT_PUT },
    [HTTP_POST] = { "POST", STR_LEN("POST"), CURLOPT_POST }
};

bool api_ctor(void)
{
    md = EVP_sha1();

    return TRUE;
}

static inline size_t nearest_power(size_t requested_length)
{
    int i;

    while ((1U << i) < requested_length) {
        i++;
    }

    return (1U << i);
}

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    struct buffer *b;
    size_t total_size;

    total_size = size * nmemb;
    b = (struct buffer *) userp;
    if (total_size >= (b->allocated - b->length)) {
        b->allocated = nearest_power(total_size);
        b->ptr = mem_renew(b->ptr, *b->ptr, b->allocated);
    }
    memcpy(b->ptr + b->length, contents, total_size);
    b->length += total_size;
    b->ptr[b->length] = '\0';

    return total_size;
}

void request_add_header(request_t *req, const char *header)
{
    // NOTE: curl_slist_append() copies the string
    req->headers = curl_slist_append(req->headers, header);
}

static size_t hash_httppost_callback(void *arg, const char *buf, size_t len)
{
    EVP_DigestUpdate((EVP_MD_CTX *) arg, buf, len);

    return len;
}

// "$1$" + SHA1_HEX(AS+"+"+CK+"+"+METHOD+"+"+QUERY+"+"+BODY+"+"+TSTAMP)
void request_sign(request_t *req)
{
    EVP_MD_CTX ctx;
    int i, hash_len;
    char header[1024], buffer[1024], *p;
    unsigned char hash[EVP_MAX_MD_SIZE];
    const char * const end = header + ARRAY_SIZE(header);
#ifdef TEST
    const char consumer_key[] = "MtSwSrPpNjqfVSmJhLbPyr2i45lSwPU1";
#else
    const char *consumer_key = account_key();
#endif /* TEST */

    if (NULL == consumer_key) {
        return; // TODO: let caller know we failed
    }
    EVP_MD_CTX_init(&ctx);
    EVP_DigestInit_ex(&ctx, md, NULL);
    EVP_DigestUpdate(&ctx, APPLICATION_SECRET, STR_LEN(APPLICATION_SECRET));
    EVP_DigestUpdate(&ctx, "+", STR_LEN("+"));
    EVP_DigestUpdate(&ctx, consumer_key, strlen(consumer_key));
    EVP_DigestUpdate(&ctx, "+", STR_LEN("+"));
    EVP_DigestUpdate(&ctx, methods[req->method].name, methods[req->method].name_len);
    EVP_DigestUpdate(&ctx, "+", STR_LEN("+"));
    EVP_DigestUpdate(&ctx, req->url, strlen(req->url));
    EVP_DigestUpdate(&ctx, "+", STR_LEN("+"));
    // <data>
    if (NULL != req->formpost) {
        curl_formget(req->formpost, &ctx, hash_httppost_callback);
    } else if (NULL != req->pdata) {
        EVP_DigestUpdate(&ctx, req->pdata, strlen(req->pdata));
    }
    // </data>
    EVP_DigestUpdate(&ctx, "+", STR_LEN("+"));
    // <timestamp>
    if (snprintf(buffer, ARRAY_SIZE(buffer), "%u", req->timestamp) >= (int) ARRAY_SIZE(buffer)) {
        // failed
    }
    EVP_DigestUpdate(&ctx, buffer, strlen(buffer));
    // </timestamp>
//     strcpy(hash, "X-Ovh-Signature: $1$");
    EVP_DigestFinal_ex(&ctx, hash, &hash_len);
    EVP_MD_CTX_cleanup(&ctx);
    request_add_header(req, "X-Ovh-Application: " APPLICATION_KEY);
    // X-Ovh-Signature header
    *header = '\0';
    p = stpcpy(header, "X-Ovh-Signature: $1$");
    for (i = 0; i < hash_len; i++) {
        p += snprintf(p, end - p, "%02x", hash[i]);
    }
    request_add_header(req, header);
debug("%s+%s+%s+%s+%s+%s = %s", APPLICATION_SECRET, consumer_key, methods[req->method].name, req->url, "", buffer, header + STR_LEN("X-Ovh-Signature: $1$"));
    // X-Ovh-Timestamp header
    *header = '\0';
    p = stpcpy(header, "X-Ovh-Timestamp: ");
    p = stpcpy(p, buffer);
    request_add_header(req, header);
    // X-Ovh-Consumer header
    *header = '\0';
    p = stpcpy(header, "X-Ovh-Consumer: ");
    p = stpcpy(p, consumer_key);
    request_add_header(req, header);
}

static request_t *request_ctor(const char *url, http_method_t method)
{
    CURLcode res;
    request_t *req;

    req = mem_new(*req);
    req->headers = NULL;
    req->method = method;
    req->url = strdup(url);
    req->pdata = req->data = NULL;
    req->ch = curl_easy_init();
    req->formpost = req->lastptr = NULL;
#ifdef TEST
    req->timestamp = 1366560945;
#else
    req->timestamp = (unsigned int) time(NULL);
#endif /* TEST */
    req->buffer.length = 0;
    req->buffer.allocated = 8192;
    req->buffer.ptr = mem_new_n(*req->buffer.ptr, req->buffer.allocated);
    curl_easy_setopt(req->ch, methods[method].curlconst, 1L);
#ifdef TEST
    {
        char buffer[1024] = URL;

        strcat(buffer, url + STR_LEN(API_BASE_URL));
debug("buffer = %s", buffer);
        curl_easy_setopt(req->ch, CURLOPT_URL, buffer);
    }
#else
    curl_easy_setopt(req->ch, CURLOPT_URL, url);
#endif /* TEST */
    curl_easy_setopt(req->ch, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(req->ch, CURLOPT_WRITEDATA, (void *) &req->buffer);

    return req;
}

void request_dtor(request_t *req)
{
    free((void *) req->url);
    if (NULL != req->headers) {
        curl_slist_free_all(req->headers);
    }
    if (NULL != req->formpost) {
        curl_formfree(req->formpost);
    }
    if (req->data != req->pdata) {
        free((void *) req->pdata);
    }
    curl_easy_cleanup(req->ch);
    free(req->buffer.ptr);
}

request_t *request_get(const char *url)
{
    request_t *req;

    req = request_ctor(url, HTTP_GET);

    return req;
}

request_t *request_post(const char *url, const char *data, int copy)
{
    request_t *req;

    req = request_ctor(url, HTTP_POST);
    if (NULL != data && '\0' != *data) {
        req->pdata = req->data = data;
        if (copy) {
            // NOTE: we don't use CURLOPT_COPYPOSTFIELDS because we need this string later for hashing/signature
            req->pdata = strdup(data);
        }
        curl_easy_setopt(req->ch, CURLOPT_POSTFIELDS, req->pdata);
    }

    return req;
}

#if 0
static const int8_t unreserved[] = {
    /*      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, A, B, C, D, E, F */
    /* 0 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 1 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 2 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0,
    /* 3 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
    /* 4 */ 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 5 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,
    /* 6 */ 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 7 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0,
    /* 8 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 9 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* A */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* B */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* C */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* D */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* E */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* F */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void xtring_add_post_field(XString *this, const char *name, const char *value)
{
    size_t needs;
    const char *p;

    needs = 0;
    if (this->len > 0) {
        needs += STR_LEN("&");
    }
    for (p = name; '\0' != *p; p++) {
        needs += 1 + (!unreserved[(unsigned char) *p]) * (STR_LEN("%XX") - 1);
    }
    if ('\0' != *value) {
        needs += STR_LEN("=");
        for (p = value; '\0' != *p; p++) {
            needs += 1 + (!unreserved[(unsigned char) *p]) * (STR_LEN("%XX") - 1);
        }
    }
    _maybe_expand_of(this, needs);
    if (this->len > 0) {
        this->str[this->len++] = '&';
    }
    for (p = name; '\0' != *p; p++) {
        if (unreserved[(unsigned char) *p]) {
            this->str[this->len++] = *p;
        } else {
            snprintf(this->ptr + this->len, STR_SIZE("%XX"), "%%%02X", *p);
        }
    }
    if ('\0' != *value) {
        this->str[this->len++] = '=';
        for (p = value; '\0' != *p; p++) {
            if (unreserved[(unsigned char) *p]) {
                this->str[this->len++] = *p;
            } else {
                snprintf(this->ptr + this->len, STR_SIZE("%XX"), "%%%02X", *p);
            }
        }
    }
}
#endif

void request_add_post_field(request_t *req, const char *name, const char *value)
{
    /**
     * NOTE:
     * - s/CURLFORM_PTRNAME/CURLFORM_COPYNAME/ and s/CURLFORM_PTRCONTENTS/CURLFORM_COPYCONTENTS/ to avoid copies by libcurl
     * - cf curl_easy_escape for application/x-www-form-urlencoded
     **/
    curl_formadd(&req->formpost, &req->lastptr, CURLFORM_COPYNAME, name, CURLFORM_COPYCONTENTS, value, CURLFORM_END);
}

int request_execute(request_t *req, int output_type, void **output)
{
    CURLcode res;

#if 0
    if (RESPONSE_IGNORE == output_type) {
        curl_easy_setopt(req->ch, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(req->ch, CURLOPT_WRITEFUNCTION, NULL);
        curl_easy_setopt(req->ch, CURLOPT_WRITEDATA, NULL);
    }
#endif
    curl_easy_setopt(req->ch, CURLOPT_HTTPHEADER, req->headers);
    if (NULL != req->formpost) {
        curl_easy_setopt(req->ch, CURLOPT_HTTPPOST, req->formpost);
    }
    if (CURLE_OK != (res = curl_easy_perform(req->ch))) {
        fprintf(stderr, "Request failed: %s\n", curl_easy_strerror(res));
        return 0;
    }
    debug("req->buffer.ptr = %s", req->buffer.ptr);
    debug("req->buffer.length = %zu", req->buffer.length);
    switch (output_type) {
        case RESPONSE_IGNORE:
            /* NOP */
            break;
        case RESPONSE_HTML:
        {
            htmlParserCtxtPtr ctxt;

            ctxt = htmlCreateMemoryParserCtxt(req->buffer.ptr, req->buffer.length);
//             htmlCtxtUseOptions(ctxt, options);
            htmlParseDocument(ctxt);
            *((xmlDocPtr *) output) = ctxt->myDoc;
            htmlFreeParserCtxt(ctxt);
            break;
        }
        case RESPONSE_XML:
        {
#if 0
            xmlParserCtxtPtr ctxt;

            if (NULL == (ctxt = xmlCreateMemoryParserCtxt(req->buffer.ptr, req->buffer.length))) {
                return NULL;
            }
            xmlCtxtUseOptions(ctxt, 0);
            if (xmlParseDocument(ctxt) < 1) { // segfaults ?!?
                return NULL;
            }
            doc = ctxt->myDoc;
            xmlFreeParserCtxt(ctxt);
#else
            *((xmlDocPtr *) output) = xmlParseMemory(req->buffer.ptr, req->buffer.length);
#endif
            break;
        }
        default:
            assert(0);
    }

    return 1;
}

#define STRINGIFY(x) #x
#define STRINGIFY_EXPANDED(x) STRINGIFY(x)
#define DEFAULT_CONSUMER_KEY_EXPIRATION 86400

const char *request_consumer_key(const char *account, const char *password, time_t *expires_at)
{
    request_t *req;
    char *validationUrl;
    char *consumerKey;

    // POST /auth/credential
    {
        xmlDocPtr doc;
        xmlNodePtr root;

        req = request_post(API_BASE_URL "/auth/credential", "{ \
    \"accessRules\": [ \
        { \
            \"method\": \"GET\", \
            \"path\": \"/*\" \
        } \
    ], \
    \"redirection\":\"https://www.mywebsite.com/\" \
}", STRING_NOCOPY);
        request_add_header(req, "Accept: text/xml");
        request_add_header(req, "Content-type: application/json");
        request_add_header(req, "X-Ovh-Application: " APPLICATION_KEY);
        request_execute(req, RESPONSE_XML, (void **) &doc); // TODO: check returned value
#if 0
        printf("====================\n");
        xmlDocFormatDump(stdout, doc, 1);
        printf("====================\n");
#endif
        if (NULL == (root = xmlDocGetRootElement(doc))) {
            xmlFreeDoc(doc);
            request_dtor(req);
            return 0;
        }
        // TODO: assert(NULL != xmlGetProp(...)));
        validationUrl = strdup((char *) xmlGetProp(root, BAD_CAST "validationUrl"));
        consumerKey = strdup((char *) xmlGetProp(root, BAD_CAST "consumerKey"));
        xmlFreeDoc(doc);
        request_dtor(req);
    }

    {
        char *token;
        char *account_field_name;
        char *password_field_name;

        // GET validationUrl
        {
            xmlDocPtr doc;
            xmlXPathObjectPtr res;
            xmlXPathContextPtr ctxt;

            req = request_get(validationUrl);
            request_execute(req, RESPONSE_HTML, (void **) &doc); // TODO: check returned value
#if 0
            printf("====================\n");
            htmlDocDump(stdout, doc);
            printf("====================\n");
#endif
            xmlXPathInit();
            if (NULL == (ctxt = xmlXPathNewContext(doc))) {
                xmlFreeDoc(doc);
                request_dtor(req);
                free(validationUrl);
                debug("xmlXPathNewContext");
                return 0;
            }
            if (NULL == (res = xmlXPathEvalExpression(BAD_CAST "string(//form//input[@name=\"credentialToken\"]/@value)", ctxt))) {
                xmlXPathFreeObject(res);
                xmlFreeDoc(doc);
                request_dtor(req);
                free(validationUrl);
                debug("xmlXPathEvalExpression");
                return 0;
            }
            token = strdup((char *) xmlXPathCastToString(res));
            xmlXPathFreeObject(res);
            if (NULL == (res = xmlXPathEvalExpression(BAD_CAST "string(//form//input[@type=\"password\"]/@name)", ctxt))) {
                xmlXPathFreeObject(res);
                xmlFreeDoc(doc);
                request_dtor(req);
                free(validationUrl);
                debug("xmlXPathEvalExpression");
                return 0;
            }
            password_field_name = strdup((char *) xmlXPathCastToString(res));
            xmlXPathFreeObject(res);
            if (NULL == (res = xmlXPathEvalExpression(BAD_CAST "string(//form//input[@type=\"text\"]/@name)", ctxt))) {
                xmlXPathFreeObject(res);
                xmlFreeDoc(doc);
                request_dtor(req);
                free(validationUrl);
                debug("xmlXPathEvalExpression");
                return 0;
            }
            account_field_name = strdup((char *) xmlXPathCastToString(res));
            xmlXPathFreeObject(res);
debug("token = %s", token);
debug("account field name = %s", account_field_name);
debug("password field name = %s", password_field_name);
            xmlXPathFreeContext(ctxt);
            xmlFreeDoc(doc);
            request_dtor(req);
        }

        // POST validationUrl
        req = request_post(validationUrl, NULL, STRING_NOCOPY);
        request_add_post_field(req, "credentialToken", token);
        request_add_post_field(req, account_field_name, account);
        request_add_post_field(req, password_field_name, password);
//         request_add_post_field(req, "duration", "0");
        request_add_post_field(req, "duration", STRINGIFY_EXPANDED(DEFAULT_CONSUMER_KEY_EXPIRATION));
        request_execute(req, RESPONSE_IGNORE, NULL); // TODO: check returned value
        request_dtor(req);

        free(token);
        free(account_field_name);
        free(password_field_name);
    }
    free(validationUrl);
    *expires_at = time(NULL) + DEFAULT_CONSUMER_KEY_EXPIRATION;

    return consumerKey;
}

DECLARE_MODULE(api) = {
    "api",
    api_ctor,
    NULL,
    NULL
};
