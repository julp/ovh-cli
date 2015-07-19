#ifndef CONV_H

# define CONV_H

bool is_output_utf8(void);
void convert_array_free(int, char **, char **);
bool convert_array_local_to_utf8(int, char **, char ***, error_t **);
void convert_string_free(const char *, char **);
bool convert_string_local_to_utf8(const char *, size_t, char **, size_t *, error_t **);
bool convert_string_utf8_to_local(const char *, size_t, char **, size_t *, error_t **);

#endif /* !CONV_H */
