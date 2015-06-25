#ifndef GRAPHIC_H

# define GRAPHIC_H

typedef struct graphic_t graphic_t;

void graphic_destroy(graphic_t *);
void graphic_display(graphic_t *);
graphic_t *graphic_new(void);
void graphic_store(graphic_t *, time_t, double);

#endif /* GRAPHIC_H */
