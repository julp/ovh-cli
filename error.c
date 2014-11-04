#include <string.h>
#include "common.h"

#define ERROR_MAX_LEN 596

#ifdef DEBUG
const char *ubasename(const char *filename)
{
    const char *c;

    if (NULL == (c = strrchr(filename, DIRECTORY_SEPARATOR))) {
        return filename;
    } else {
        return c + 1;
    }
}
#endif /* DEBUG */

error_t *error_vnew(UGREP_FILE_LINE_FUNC_DC int type, const char *format, va_list args) /* WARN_UNUSED_RESULT */
{
    error_t *error;
    char buffer[ERROR_MAX_LEN + 1];
    int32_t total_length, part_length;

    total_length = 0;
    error = mem_new(*error);
    error->type = type;
    error->message = NULL;
#ifdef DEBUG
    // NOTE: *s*printf does not include the terminating null character
    part_length = snprintf(buffer, ERROR_MAX_LEN, "%s:%d: ", ubasename(__ugrep_file), __ugrep_line);
    assert(-1 != part_length);
    error->message = mem_new_n(*error->message, /*total_length + */part_length/* + EOL_LEN*/ + 1);
    memcpy(error->message/* + total_length*/, buffer, part_length);
    total_length += part_length;
#endif
    part_length = vsnprintf(buffer, ERROR_MAX_LEN, format, args);
    assert(-1 != part_length);
    error->message = mem_renew(error->message, *error->message, total_length + part_length/* + EOL_LEN*/ + 1);
    memcpy(error->message + total_length, buffer, part_length);
    total_length += part_length;
#ifdef DEBUG
    part_length = snprintf(buffer, ERROR_MAX_LEN, GRAY(" in %s()\n"), __ugrep_func);
    assert(-1 != part_length);
    error->message = mem_renew(error->message, *error->message, total_length + part_length/* + EOL_LEN*/ + 1);
    memcpy(error->message + total_length, buffer, part_length);
    total_length += part_length;
#endif
    /*memcpy(error->message + total_length, EOL, EOL_LEN);
    total_length += EOL_LEN;*/
#ifndef DEBUG
    error->message[total_length++] = '\n';
#endif
    error->message[total_length] = '\0';

    return error;
}

error_t *error_new(UGREP_FILE_LINE_FUNC_DC int type, const char *format, ...) /* WARN_UNUSED_RESULT */
{
    error_t *error;
    va_list args;

    va_start(args, format);
    error = error_vnew(UGREP_FILE_LINE_FUNC_RELAY_CC type, format, args);
    va_end(args);

    return error;
}

void _error_set(UGREP_FILE_LINE_FUNC_DC error_t **error, int type, const char *format, ...)
{
    va_list args;
    error_t *tmp;

    if (NULL != error) {
        va_start(args, format);
        tmp = error_vnew(UGREP_FILE_LINE_FUNC_RELAY_CC type, format, args);
        va_end(args);
        if (NULL == *error) {
            *error = tmp;
        } else {
            debug("overwrite attempt of a previous error: %s\nBy: %s", (*error)->message, tmp->message);
        }
    }
}

void error_destroy(error_t *error)
{
    if (NULL != error) {
        if (error->message) {
            free(error->message);
        }
        free(error);
        error = NULL;
    }
}

void error_propagate(error_t **dst, error_t *src)
{
    if (NULL == dst) {
        if (NULL != src) {
            error_destroy(src);
        }
    } else {
        if (NULL != *dst) {
            debug("overwrite attempt of a previous error: %s\nBy: %s", (*dst)->message, src->message);
        } else {
            *dst = src;
        }
    }
}
