#include <string.h>
#include <curl/curl.h>
#include <openssl/evp.h>
#include <libxml/xpath.h>
#include <libxml/HTMLparser.h>

#include "modules/api.h"
#include "modules/libxml.h"
#include "commands/account.h"

struct buffer {
    char *ptr;
    size_t length;
    size_t allocated;
};

typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE
} http_method_t;

struct request_t {
    CURL *ch;
    char *url;
    // <CURLOPT_POSTFIELDS>
    const char *data;
    const char *pdata; // if pdata != data, pdata is a copy (= needs to be freed)
    // </CURLOPT_POSTFIELDS>
    http_method_t method;
    struct buffer buffer; // TODO: struct buffer => String
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
    [ HTTP_GET ] = { "GET", STR_LEN("GET"), CURLOPT_HTTPGET },
    [ HTTP_PUT ] = { "PUT", STR_LEN("PUT"), 0 },
    [ HTTP_POST ] = { "POST", STR_LEN("POST"), CURLOPT_POST },
    [ HTTP_DELETE ] = { "DELETE", STR_LEN("DELETE"), 0 }
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

/*static size_t hash_httppost_callback(void *arg, const char *buf, size_t len)
{
    EVP_DigestUpdate((EVP_MD_CTX *) arg, buf, len);

    return len;
}*/

// "$1$" + SHA1_HEX(AS+"+"+CK+"+"+METHOD+"+"+QUERY+"+"+BODY+"+"+TSTAMP)
void request_sign(request_t *req)
{
    EVP_MD_CTX ctx;
    int i, hash_len;
    char header[1024], buffer[1024], *p;
    unsigned char hash[EVP_MAX_MD_SIZE];
    const char * const end = header + ARRAY_SIZE(header);
    const char *consumer_key = account_key();

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
    /*if (NULL != req->formpost) {
        curl_formget(req->formpost, &ctx, hash_httppost_callback);
    } else */if (NULL != req->pdata) {
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
//     debug("%s+%s+%s+%s+%s+%s = %s", APPLICATION_SECRET, consumer_key, methods[req->method].name, req->url, NULL == req->pdata ? "" : req->pdata, buffer, header + STR_LEN("X-Ovh-Signature: $1$"));
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

static request_t *request_ctor(http_method_t method, const char *url, va_list args)
{
    CURLcode res;
    request_t *req;
    va_list cpyargs;

    req = mem_new(*req);
    req->headers = NULL;
    req->method = method;
    va_copy(cpyargs, args);
    req->url = mem_new_n(*req->url, vsnprintf(NULL, 0, url, cpyargs) + 1);
    va_end(cpyargs);
    vsprintf(req->url, url, args);
    req->pdata = req->data = NULL;
    req->ch = curl_easy_init();
    req->formpost = req->lastptr = NULL;
    req->timestamp = (unsigned int) time(NULL);
    req->buffer.length = 0;
    req->buffer.allocated = 8192;
    req->buffer.ptr = mem_new_n(*req->buffer.ptr, req->buffer.allocated);
    if (0 == methods[method].curlconst) {
        curl_easy_setopt(req->ch, CURLOPT_CUSTOMREQUEST, methods[method].name);
    } else {
        curl_easy_setopt(req->ch, methods[method].curlconst, 1L);
    }
//     curl_easy_setopt(req->ch, CURLOPT_USERAGENT, "ovh-cli");
    curl_easy_setopt(req->ch, CURLOPT_URL, req->url);
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

request_t *request_get(const char *url, ...) /* PRINTF(1, 2) */
{
    va_list args;
    request_t *req;

    va_start(args, url);
    req = request_ctor(HTTP_GET, url, args);
    va_end(args);

    return req;
}

request_t *request_post(const char *data, int copy, const char *url, ...) /* PRINTF(3, 4) */
{
    va_list args;
    request_t *req;

    va_start(args, url);
    req = request_ctor(HTTP_POST, url, args);
    va_end(args);
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

request_t *request_delete(const char *url, ...) /* PRINTF(1, 2) */
{
    va_list args;
    request_t *req;

    va_start(args, url);
    req = request_ctor(HTTP_DELETE, url, args);
    va_end(args);

    return req;
}

void request_add_post_field(request_t *req, const char *name, const char *value)
{
    /**
     * NOTE:
     * - s/CURLFORM_PTRNAME/CURLFORM_COPYNAME/ and s/CURLFORM_PTRCONTENTS/CURLFORM_COPYCONTENTS/ to avoid copies by libcurl
     * - cf curl_easy_escape for application/x-www-form-urlencoded
     **/
    curl_formadd(&req->formpost, &req->lastptr, CURLFORM_COPYNAME, name, CURLFORM_COPYCONTENTS, value, CURLFORM_END);
}

bool request_execute(request_t *req, int output_type, void **output, error_t **error)
{
    CURLcode res;
    long http_status;
    char *content_type;

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
    curl_easy_getinfo(req->ch, CURLINFO_CONTENT_TYPE, &content_type);
    curl_easy_getinfo(req->ch, CURLINFO_RESPONSE_CODE, &http_status);
    {
        char *url;

        curl_easy_getinfo(req->ch, CURLINFO_EFFECTIVE_URL, &url);
        debug("==========");
        debug("RESPONSE (HTTP status) = %ld", http_status);
        debug("RESPONSE (effective URL) = %s", url);
        debug("RESPONSE (Content-Type) = %s", content_type);
        debug("RESPONSE (Content-Length) = %zu", req->buffer.length);
        debug("RESPONSE (body) = %s", req->buffer.ptr);
        debug("==========");
    }
    if (200 != http_status) {
        // API may throw 403 if forbidden (consumer_key no more valid, object that doesn't belong to us) or 404 on unknown objects?
        if (NULL != content_type && (0 == strcmp(content_type, "application/xml") || 0 == strcmp(content_type, "text/xml"))) {
            xmlDocPtr doc;

            if (NULL != (doc = xmlParseMemory(req->buffer.ptr, req->buffer.length))) {
                xmlNodePtr root;

                if (NULL != (root = xmlDocGetRootElement(doc)) && xmlHasProp(root, BAD_CAST "message")) {
                    xmlChar *reason;

                    reason = xmlGetProp(root, BAD_CAST "message");
                    error_set(error, NOTICE, (const char *) reason);
                    xmlFree(reason);
                }
                xmlFreeDoc(doc);
            }
            if (NULL != error && NULL == *error) { // an error can be set and any was already set
                error_set(error, WARN, "failed to parse following xml: %s", req->buffer.ptr);
            }
        } else {
            error_set(error, WARN, "HTTP request to '%s' failed with status %ld", req->url, http_status);
        }
        return FALSE;
    } else {
        switch (output_type) {
            case RESPONSE_IGNORE:
                /* NOP */
                break;
            case RESPONSE_HTML:
            {
                htmlParserCtxtPtr ctxt;

                ctxt = htmlCreateMemoryParserCtxt(req->buffer.ptr, req->buffer.length);
                assert(NULL != ctxt);
//                 htmlCtxtUseOptions(ctxt, options);
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
                assert(FALSE);
        }
    }

    return TRUE;
}

#define STRINGIFY(x) #x
#define STRINGIFY_EXPANDED(x) STRINGIFY(x)
#define DEFAULT_CONSUMER_KEY_EXPIRATION 86400 /* 1 day */

#define JSON_RULES 1

#ifdef JSON_RULES
# include "json.h"
#endif /* JSON_RULES */

const char *request_consumer_key(const char *account, const char *password, time_t *expires_at, error_t **error)
{
    request_t *req;
    char *validationUrl;
    char *consumerKey;

    // POST /auth/credential
    {
        xmlDocPtr doc;
        xmlNodePtr root;

#ifndef JSON_RULES
        req = request_post(API_BASE_URL "/auth/credential", "{ \
    \"accessRules\": [ \
        { \
            \"method\": \"GET\", \
            \"path\": \"/*\" \
        }, \
        { \
            \"method\": \"POST\", \
            \"path\": \"/domain/zone/*\" \
        } \
    ], \
    \"redirection\":\"https://www.mywebsite.com/\" \
}", STRING_NOCOPY);
#else
        String *buffer;
        {
            json_document_t *doc;
            json_value_t root, rules, method;

#define JSON_ADD_RULE(parent, method, value) \
    do { \
        json_value_t object; \
 \
        object = json_object(); \
        json_object_set_property(object, "method", json_string(method)); \
        json_object_set_property(object, "path", json_string(value)); \
        json_array_push(parent, object); \
    } while (0);

            buffer = string_new();
            doc = json_document_new();
            root = json_object();
            rules = json_array();
            json_document_set_root(doc, root);
            json_object_set_property(root, "accessRules", rules);
//             json_object_set_property(root, "redirection", json_string("https://www.mywebsite.com/"));
            JSON_ADD_RULE(rules, "GET", "/*");
            JSON_ADD_RULE(rules, "POST", "/domain/zone/*");
            json_document_serialize(doc, buffer);
            json_document_destroy(doc);
        }
        req = request_post(buffer->ptr, STRING_NOCOPY, API_BASE_URL "/auth/credential");
#endif /* !JSON_RULES */
        request_add_header(req, "Accept: text/xml");
        request_add_header(req, "Content-type: application/json");
        request_add_header(req, "X-Ovh-Application: " APPLICATION_KEY);
        request_execute(req, RESPONSE_XML, (void **) &doc, error); // TODO: check returned value
#if 0
        puts("====================");
        xmlDocFormatDump(stdout, doc, 1);
        puts("====================");
#endif
        if (NULL == (root = xmlDocGetRootElement(doc))) {
            xmlFreeDoc(doc);
            request_dtor(req);
            return 0;
        }
        consumerKey = xmlGetPropAsString(root, "consumerKey");
        validationUrl = xmlGetPropAsString(root, "validationUrl");
        xmlFreeDoc(doc);
        request_dtor(req);
#ifdef JSON_RULES
        string_destroy(buffer);
#endif /* JSON_RULES */
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

            req = request_get("%s", validationUrl);
            request_execute(req, RESPONSE_HTML, (void **) &doc, error); // TODO: check returned value
#if 0
            puts("====================");
            htmlDocDump(stdout, doc);
            puts("====================");
#endif
            xmlXPathInit();
            if (NULL == (ctxt = xmlXPathNewContext(doc))) {
                xmlFreeDoc(doc);
                request_dtor(req);
                free(validationUrl);
                return 0;
            }
            if (NULL == (res = xmlXPathEvalExpression(BAD_CAST "string(//form//input[@name=\"credentialToken\"]/@value)", ctxt))) {
                xmlXPathFreeObject(res);
                xmlFreeDoc(doc);
                request_dtor(req);
                free(validationUrl);
                return 0;
            }
            token = strdup((char *) xmlXPathCastToString(res));
            xmlXPathFreeObject(res);
            if (NULL == (res = xmlXPathEvalExpression(BAD_CAST "string(//form//input[@type=\"password\"]/@name)", ctxt))) {
                xmlXPathFreeObject(res);
                xmlFreeDoc(doc);
                request_dtor(req);
                free(validationUrl);
                return 0;
            }
            password_field_name = strdup((char *) xmlXPathCastToString(res));
            xmlXPathFreeObject(res);
            if (NULL == (res = xmlXPathEvalExpression(BAD_CAST "string(//form//input[@type=\"text\"]/@name)", ctxt))) {
                xmlXPathFreeObject(res);
                xmlFreeDoc(doc);
                request_dtor(req);
                free(validationUrl);
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
        req = request_post(NULL, STRING_NOCOPY, "%s", validationUrl);
        request_add_post_field(req, "credentialToken", token);
        request_add_post_field(req, account_field_name, account);
        request_add_post_field(req, password_field_name, password);
//         request_add_post_field(req, "duration", "0");
        request_add_post_field(req, "duration", STRINGIFY_EXPANDED(DEFAULT_CONSUMER_KEY_EXPIRATION));
        request_execute(req, RESPONSE_IGNORE, NULL, error); // TODO: check returned value
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
    NULL,
    NULL
};
