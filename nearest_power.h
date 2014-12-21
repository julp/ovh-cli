#ifndef NEAREST_POWER_H

# define NEAREST_POWER_H

# include <limits.h>

# define SIZE_MAX_2 (SIZE_MAX << (sizeof(size_t) * CHAR_BIT - 1))

static inline size_t nearest_power(size_t requested_length, size_t minimal)
{
    if (requested_length > SIZE_MAX_2) {
        return SIZE_MAX;
    } else {
        int i = 1;
        requested_length = MAX(requested_length, minimal);
        while ((1UL << i) < requested_length) {
            i++;
        }

        return (1UL << i);
    }
}

#endif /* !NEAREST_POWER_H */
