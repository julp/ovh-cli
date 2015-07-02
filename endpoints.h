#ifndef ENDPOINTS_H

# define ENDPOINTS_H

typedef struct {
    const char *name;
    const char *base;
    size_t base_len;
    const module_t * const *supported;
} endpoint_t;

extern const endpoint_t endpoints[];
extern const char *endpoint_names[];

#endif /* !ENDPOINTS_H */
