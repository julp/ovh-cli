#ifndef XTRING_H

# define XTRING_H

typedef struct {
    char *ptr;
    size_t len;
    size_t allocated;
} String;

# include "common.h"

String *string_adopt_string(char *);
String *string_adopt_string_len(char *, size_t);
void string_append_char(String *, char);
void string_append_formatted(String *, const char *, ...) PRINTF(2, 3);
void string_append_json_string(String *, const char *);
void string_append_n_times(String *, const char *, size_t, size_t);
void string_append_string(String *, const char *);
void string_append_string_len(String *, const char *, int32_t);
void string_chomp(String *);
int32_t string_delete_len(String *, size_t, size_t);
void string_destroy(String *);
void string_dump(String *);
String *string_dup(const String *) WARN_UNUSED_RESULT;
String *string_dup_string(const char *);
String *string_dup_string_len(const char *, size_t);
int string_empty(const String *);
int string_endswith(String *, const char *, size_t);
void string_formatted(String *, const char *, ...) PRINTF(2, 3);
int32_t string_insert_len(String *, size_t, const char *, size_t);
void string_ltrim(String *);
String *string_new(void) WARN_UNUSED_RESULT;
char *string_orphan(String *);
void string_prepend_char(String *, char);
void string_prepend_string(String *, const char *);
void string_prepend_string_len(String *, const char *, int32_t);
void string_rtrim(String *);
String *string_sized_new(size_t) WARN_UNUSED_RESULT;
int string_startswith(String *, const char *, size_t);
int32_t string_subreplace_len(String *, const char *, size_t, size_t, size_t);
void string_sync(const String *, String *, double);
void string_trim(String *);
void string_truncate(String *);

#endif /* !XTRING_H */
