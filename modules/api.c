#include <inttypes.h>
#include <string.h>
#include <curl/curl.h>
#include <openssl/evp.h>
#include <libxml/xpath.h>
#include <libxml/HTMLparser.h>

#include "json.h"
#include "modules/api.h"
#include "modules/libxml.h"
#include "commands/account.h"
#include "struct/xtring.h"

struct request_t {
    CURL *ch;
    char *url;
    uint32_t flags;
    long http_status;
    // <CURLOPT_POSTFIELDS>
    const char *data;
    const char *pdata; // if pdata != data, pdata is a copy (= needs to be freed)
    // </CURLOPT_POSTFIELDS>
    String *buffer;
    http_method_t method;
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

typedef struct {
    bool on_off;
} api_argument_t;

static bool http_log = TRUE;

bool api_ctor(void)
{
    md = EVP_sha1();

    return TRUE;
}

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    String *buffer;
    size_t total_size;

    total_size = size * nmemb;
    buffer = (String *) userp;
    string_append_string_len(buffer, contents, total_size);

    return total_size;
}

void request_add_header(request_t *req, const char *header)
{
    // NOTE: curl_slist_append() copies the string
    req->headers = curl_slist_append(req->headers, header);
}

static bool request_sign(request_t *req, error_t **error)
{
    EVP_MD_CTX ctx;
    unsigned int i, hash_len;
    char header[1024], buffer[1024], *p;
    unsigned char hash[EVP_MAX_MD_SIZE];
    const char * const end = header + ARRAY_SIZE(header);
    const char *consumer_key = account_key();

    if (NULL == consumer_key) {
        return FALSE;
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
    if (NULL != req->pdata) {
        EVP_DigestUpdate(&ctx, req->pdata, strlen(req->pdata));
    }
    // </data>
    EVP_DigestUpdate(&ctx, "+", STR_LEN("+"));
    // <timestamp>
    if (snprintf(buffer, ARRAY_SIZE(buffer), "%u", req->timestamp) >= (int) ARRAY_SIZE(buffer)) {
        error_set(error, FATAL, "buffer overflow"); // should not happen
        return FALSE;
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
        p += snprintf(p, end - p, "%02x", hash[i]); // TODO: check
    }
    request_add_header(req, header);
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

    return TRUE;
}

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

#if 0
#include <math.h>
#endif
static size_t urlf(char *dst, size_t dst_size, const char *fmt, va_list ap)
{
    char *w;
    const char *r;
    size_t dst_len;

    w = dst;
    r = fmt;
    dst_len = 0;
    while ('\0' != *r) {
        if ('%' == *r) {
            ++r;
            switch (*r) {
                case '%':
                    ++dst_len;
                    if (dst_size > dst_len) {
                        *w++ = '%';
                    }
                    break;
                case 's':
                {
                    const char *s, *p;

                    if (NULL != (s = va_arg(ap, const char *))) {
                        for (p = s; '\0' != *p; p++) {
                            if (unreserved[(unsigned char) *p]) {
                                ++dst_len;
                                if (dst_size > dst_len) {
                                    *w++ = *p;
                                }
                            } else {
                                dst_len += STR_LEN("%XX");
                                if (dst_size > dst_len) {
                                    w += sprintf(w, "%%%02X", (unsigned char) *p);
                                }
                            }
                        }
                    }
                    break;
                }
                case 'S': /* string but do not escape */
                {
                    size_t s_len;
                    const char *s;

                    if (NULL != (s = va_arg(ap, const char *))) {
                        s_len = strlen(s);
                        dst_len += s_len;
                        if (dst_size > dst_len) {
                            memcpy(w, s, s_len);
                            w += s_len;
                        }
                    }
                    break;
                }
                case 'u': /* PRIu32 */
                {
                    uint32_t num;
                    size_t num_len;

                    num = va_arg(ap, uint32_t);
                    num_len = snprintf(NULL, 0, "%" PRIu32, num);
                    dst_len += num_len;
                    if (dst_size > dst_len) {
                        w += sprintf(w, "%" PRIu32, num);
                    }
#if 0
                    num = va_arg(ap, uint32_t);
                    num_len = log10(num) + 1;
                    dst_len += num_len;
                    if (dst_size > dst_len) {
                        size_t i;

                        i = num_len;
                        do {
                            w[i--] = '0' + num % 10;
                            num /= 10;
                        } while (0 != num);
                        w += num_len;
                    }
#endif
                    break;
                }
            }
        } else {
            ++dst_len;
            if (dst_size > dst_len) {
                *w++ = *r;
            }
        }
        ++r;
    }
    if (dst_size > dst_len) {
        *w++ = '\0';
    }

    return dst_len;
}

request_t *request_vnew(uint32_t flags, http_method_t method, const void *pdata, const char *url, va_list args)
{
    request_t *req;
    va_list cpyargs;
    size_t url_len, url_size;

    assert(NULL != url);

    req = mem_new(*req);
    req->flags = flags;
    req->headers = NULL;
    req->method = method;
    va_copy(cpyargs, args);
#if 0
    req->url = mem_new_n(*req->url, vsnprintf(NULL, 0, url, cpyargs) + 1);
#else
    url_size = urlf(NULL, 0, url, cpyargs) + 1;
    req->url = mem_new_n(*req->url, url_size);
#endif
    va_end(cpyargs);
#if 0
    vsprintf(req->url, url, args);
#else
    url_len = urlf(req->url, url_size, url, args);
    assert(url_len < url_size);
#endif
    req->pdata = req->data = NULL;
    req->ch = curl_easy_init();
    req->buffer = string_new();
    req->formpost = req->lastptr = NULL;
    req->timestamp = (unsigned int) time(NULL);
    req->pdata = req->data = pdata;
    if (NULL != pdata) {
        if (HAS_FLAG(flags, REQUEST_FLAG_JSON)) {
            String *buffer;

            buffer = string_new();
            json_document_serialize(
                (json_document_t *) pdata, buffer,
#ifdef DEBUG
                JSON_OPT_PRETTY_PRINT
#else
                0
#endif /* DEBUG */
            );
            req->pdata = string_orphan(buffer);
            request_add_header(req, "Content-Type: application/json; charset=utf-8");
        } else {
            if (HAS_FLAG(flags, REQUEST_FLAG_COPY)) {
                req->pdata = strdup(pdata);
            }
        }
    }
    /**
     * If you want to do a zero-byte POST, you need to set CURLOPT_POSTFIELDSIZE explicitly to zero,
     * as simply setting CURLOPT_POSTFIELDS to NULL or "" just effectively disables the sending of
     * the specified string. libcurl will instead assume that you'll send the POST data using the
     * read callback!
     **/
    if (NULL != req->pdata && '\0' != *req->pdata) {
        curl_easy_setopt(req->ch, CURLOPT_POSTFIELDS, req->pdata);
    } else {
        if (HTTP_POST == req->method) {
            curl_easy_setopt(req->ch, CURLOPT_POSTFIELDSIZE, 0L);
        }
    }
    if (0 == methods[method].curlconst) {
        curl_easy_setopt(req->ch, CURLOPT_CUSTOMREQUEST, methods[method].name);
    } else {
        curl_easy_setopt(req->ch, methods[method].curlconst, 1L);
    }
    curl_easy_setopt(req->ch, CURLOPT_URL, req->url);
    curl_easy_setopt(req->ch, CURLOPT_USERAGENT, "ovh-cli");
    curl_easy_setopt(req->ch, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(req->ch, CURLOPT_WRITEDATA, (void *) req->buffer);

    return req;
}

request_t *request_new(uint32_t flags, http_method_t method, const void *pdata, const char *url, ...)
{
    va_list args;
    request_t *req;

    va_start(args, url);
    req = request_vnew(flags, method, pdata, url, args);
    va_end(args);

    return req;
}

void request_destroy(request_t *req)
{
    assert(NULL != req);

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
    if (NULL != req->buffer) {
        string_destroy(req->buffer);
    }
    free(req);
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

long request_response_status(request_t *req)
{
    return req->http_status;
}

bool request_execute(request_t *req, int output_type, void **output, error_t **error)
{
    CURLcode res;
    char *content_type;

    if (HAS_FLAG(req->flags, REQUEST_FLAG_SIGN)) {
        if (!request_sign(req, error)) {
            return FALSE;
        }
    }
#ifndef DEBUG
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
        error_set(error, WARN, "HTTP request failed: %s", curl_easy_strerror(res));
        return FALSE;
    }
    curl_easy_getinfo(req->ch, CURLINFO_CONTENT_TYPE, &content_type);
    curl_easy_getinfo(req->ch, CURLINFO_RESPONSE_CODE, &req->http_status);
    if (http_log) {
        FILE *fp;

        if (NULL != (fp = fopen("http.log", "a"))) {
            char *url;
            struct curl_slist *e;

            curl_easy_getinfo(req->ch, CURLINFO_EFFECTIVE_URL, &url);
            fputs("==========\n", fp);
            fprintf(fp, "<<< (request) %s %s\n", methods[req->method].name, req->url);
            fputs("Headers:\n", fp);
            for (e = req->headers; NULL != e; e = e->next) {
                fprintf(fp, "- %s\n", e->data);
            }
            if (NULL != req->pdata) {
                fputs("Body:\n", fp);
                fputs(req->pdata, fp);
                fputc('\n', fp);
            }
            fprintf(fp, "\n>>> (reponse) URL = %s\n", url);
            fprintf(fp, "HTTP status = %ld\n", req->http_status);
            fprintf(fp, "Content-Type = %s\n", content_type);
            fprintf(fp, "Content-Length = %zu\n", req->buffer->len);
            fputs("Body:\n", fp);
            fputs(req->buffer->ptr, fp);
            fputs("\n==========\n", fp);
            fclose(fp);
        }
    }
    if (HAS_FLAG(req->flags, REQUEST_FLAG_IGNORE_404) && 404L == req->http_status) {
        return TRUE;
    } else if (200L != req->http_status) {
        // API may throw 403 if forbidden (consumer_key no more valid, object that doesn't belong to us) or 404 on unknown objects
        if (NULL != content_type && (0 == strcmp(content_type, "application/xml") || 0 == strcmp(content_type, "text/xml"))) {
            xmlDocPtr doc;

            if (NULL != (doc = xmlParseMemory(req->buffer->ptr, req->buffer->len))) {
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
                error_set(error, WARN, "failed to parse following xml: %s", req->buffer->ptr);
            }
        } else if (NULL != content_type && (0 == strncmp(content_type, "application/json", STR_LEN("application/json")))) { // strncmp to ignore "; charset=utf-8" part
            json_document_t *doc;

            if (NULL != (doc = json_document_parse(req->buffer->ptr, error))) {
                json_value_t root, reason;

                root = json_document_get_root(doc);
                if (json_object_get_property(root, "message", &reason)) {
                    error_set(error, NOTICE, json_get_string(reason));
                }
                json_document_destroy(doc);
            }
        } else {
            error_set(error, WARN, "HTTP request to '%s' failed with status %ld", req->url, req->http_status);
        }
        return FALSE;
    } else {
        switch (output_type) {
            case RESPONSE_IGNORE:
                return TRUE;
            case RESPONSE_TEXT:
            {
                *output = req->buffer->ptr;
                break;
            }
            case RESPONSE_JSON:
            {
                *output = json_document_parse(req->buffer->ptr, error);
                break;
            }
            case RESPONSE_HTML:
            {
                htmlParserCtxtPtr ctxt;

                ctxt = htmlCreateMemoryParserCtxt(req->buffer->ptr, req->buffer->len);
                assert(NULL != ctxt);
//                 htmlCtxtUseOptions(ctxt, options);
                if (0 == htmlParseDocument(ctxt)) {
                    *output = ctxt->myDoc;
                } else {
                    error_set(error, WARN, "failed to parse HTML document");
                }
                htmlFreeParserCtxt(ctxt);
                break;
            }
            case RESPONSE_XML:
            {
#if 0
                xmlParserCtxtPtr ctxt;

                if (NULL == (ctxt = xmlCreateMemoryParserCtxt(req->buffer->ptr, req->buffer->len))) {
                    return NULL;
                }
                xmlCtxtUseOptions(ctxt, 0);
                if (xmlParseDocument(ctxt) < 1) { // segfaults ?!?
                    return NULL;
                }
                *output = ctxt->myDoc;
                xmlFreeParserCtxt(ctxt);
#else
                *output = xmlParseMemory(req->buffer->ptr, req->buffer->len);
#endif
                break;
            }
            default:
                assert(FALSE);
                break;
        }
    }

    return NULL != *output;
}

#define STRINGIFY(x) #x
#define STRINGIFY_EXPANDED(x) STRINGIFY(x)
#define DEFAULT_CONSUMER_KEY_EXPIRATION 86400 /* 1 day */

const char *request_consumer_key(const char *account, const char *password, time_t *expires_at, error_t **error)
{
    request_t *req;
    char *validationUrl;
    char *consumerKey;

    // POST /auth/credential
    {
        xmlDocPtr doc;
        xmlNodePtr root;
        json_document_t *reqdoc;

        {
            json_value_t root, rules;

#define JSON_ADD_RULE(parent, method, value) \
    do { \
        json_value_t object; \
 \
        object = json_object(); \
        json_object_set_property(object, "method", json_string(method)); \
        json_object_set_property(object, "path", json_string(value)); \
        json_array_push(parent, object); \
    } while (0);

            reqdoc = json_document_new();
            root = json_object();
            rules = json_array();
            json_document_set_root(reqdoc, root);
            json_object_set_property(root, "accessRules", rules);
//             json_object_set_property(root, "redirection", json_string("https://www.mywebsite.com/"));
            JSON_ADD_RULE(rules, "GET", "/*");
            JSON_ADD_RULE(rules, "PUT", "/me/*");
            JSON_ADD_RULE(rules, "POST", "/me/*");
            JSON_ADD_RULE(rules, "DELETE", "/me/*");
            JSON_ADD_RULE(rules, "POST", "/ip/*");
            JSON_ADD_RULE(rules, "DELETE", "/ip/*");
            JSON_ADD_RULE(rules, "PUT", "/domain/zone/*");
            JSON_ADD_RULE(rules, "POST", "/domain/zone/*");
            JSON_ADD_RULE(rules, "DELETE", "/domain/zone/*");
            JSON_ADD_RULE(rules, "PUT", "/dedicated/server/*");
            JSON_ADD_RULE(rules, "POST", "/dedicated/server/*");
            JSON_ADD_RULE(rules, "DELETE", "/dedicated/server/*");

#undef JSON_ADD_RULE
        }
        req = request_new(REQUEST_FLAG_NONE | REQUEST_FLAG_JSON, HTTP_POST, reqdoc, API_BASE_URL "/auth/credential");
        REQUEST_XML_RESPONSE_WANTED(req);
        request_add_header(req, "Content-type: application/json");
        request_add_header(req, "X-Ovh-Application: " APPLICATION_KEY);
        request_execute(req, RESPONSE_XML, (void **) &doc, error); // TODO: check returned value
        json_document_destroy(reqdoc);
#if 0
        puts("====================");
        xmlDocFormatDump(stdout, doc, 1);
        puts("====================");
#endif
        if (NULL == (root = xmlDocGetRootElement(doc))) {
            xmlFreeDoc(doc);
            request_destroy(req);
            return 0;
        }
        consumerKey = xmlGetPropAsString(root, "consumerKey");
        validationUrl = xmlGetPropAsString(root, "validationUrl");
        xmlFreeDoc(doc);
        request_destroy(req);
    }
    if (NULL == password || '\0' == *password) {
        error_set(
            error,
            NOTICE,
            _("you have not registered your password so you have to confirm the current consumer key %s yourself by validating it at: %s\n" \
            "Once done, if you choose to set a limited validity, don't forget to run: ovh account %s update expires in \"<duration>\""),
            consumerKey,
            validationUrl,
            account
        );
        *expires_at = 0;
    } else {
        char *token;
        char *account_field_name;
        char *password_field_name;

        // GET validationUrl
        {
            xmlDocPtr doc;
            xmlXPathObjectPtr res;
            xmlXPathContextPtr ctxt;

            req = request_new(REQUEST_FLAG_NONE, HTTP_GET, NULL, "%S", validationUrl);
            request_execute(req, RESPONSE_HTML, (void **) &doc, error); // TODO: check returned value
#if 0
            puts("====================");
            htmlDocDump(stdout, doc);
            puts("====================");
#endif
            if (NULL == (ctxt = xmlXPathNewContext(doc))) {
                xmlFreeDoc(doc);
                request_destroy(req);
                free(validationUrl);
                return 0;
            }
            if (NULL == (res = xmlXPathEvalExpression(BAD_CAST "string(//form//input[@name=\"credentialToken\"]/@value)", ctxt))) {
                xmlXPathFreeObject(res);
                xmlFreeDoc(doc);
                request_destroy(req);
                free(validationUrl);
                return 0;
            }
            token = strdup((char *) xmlXPathCastToString(res));
            xmlXPathFreeObject(res);
            if (NULL == (res = xmlXPathEvalExpression(BAD_CAST "string(//form//input[@type=\"password\"]/@name)", ctxt))) {
                xmlXPathFreeObject(res);
                xmlFreeDoc(doc);
                request_destroy(req);
                free(validationUrl);
                return 0;
            }
            password_field_name = strdup((char *) xmlXPathCastToString(res));
            xmlXPathFreeObject(res);
            if (NULL == (res = xmlXPathEvalExpression(BAD_CAST "string(//form//input[@type=\"text\"]/@name)", ctxt))) {
                xmlXPathFreeObject(res);
                xmlFreeDoc(doc);
                request_destroy(req);
                free(validationUrl);
                return 0;
            }
            account_field_name = strdup((char *) xmlXPathCastToString(res));
            xmlXPathFreeObject(res);
            xmlXPathFreeContext(ctxt);
            xmlFreeDoc(doc);
            request_destroy(req);
        }
        // POST validationUrl
        req = request_new(REQUEST_FLAG_NONE, HTTP_POST, NULL, "%S", validationUrl);
        request_add_post_field(req, "credentialToken", token);
        request_add_post_field(req, account_field_name, account);
        request_add_post_field(req, password_field_name, password);
//         request_add_post_field(req, "duration", "0");
        request_add_post_field(req, "duration", STRINGIFY_EXPANDED(DEFAULT_CONSUMER_KEY_EXPIRATION));
        request_execute(req, RESPONSE_IGNORE, NULL, error); // TODO: check returned value
        request_destroy(req);
        free(token);
        free(account_field_name);
        free(password_field_name);
        *expires_at = time(NULL) + DEFAULT_CONSUMER_KEY_EXPIRATION;
    }
    free(validationUrl);

    return consumerKey;
}

static command_status_t log_on_off(COMMAND_ARGS)
{
    api_argument_t *args;

    USED(error);
    USED(mainopts);
    args = (api_argument_t *) arg;
    http_log = args->on_off;

    return COMMAND_SUCCESS;
}

void api_regcomm(graph_t *g)
{
    argument_t *lit_log, *arg_log_on_off;

    lit_log = argument_create_literal("log", NULL);
    arg_log_on_off = argument_create_choices_off_on(offsetof(api_argument_t, on_off), log_on_off);

    graph_create_full_path(g, lit_log, arg_log_on_off, NULL);
}

DECLARE_MODULE(api) = {
    "api",
    api_regcomm,
    api_ctor,
    NULL,
    NULL
};
