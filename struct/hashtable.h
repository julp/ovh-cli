#ifndef HASHTABLE_H

# define HASHTABLE_H

# include "common.h"
# include "iterator.h"

typedef struct _HashTable HashTable;

typedef uint32_t hash_t;
typedef hash_t (*HashFunc)(const void *);

# define HT_FOREACH_ACCEPT   (1<<1)
# define HT_FOREACH_REJECT   (1<<2)
# define HT_FOREACH_CONTINUE (1<<3)
# define HT_FOREACH_DELETE   (1<<4)
# define HT_FOREACH_STOP     (1<<5)

# define HT_PUT_ON_DUP_KEY_PRESERVE (1<<1)
/*# define HT_PUT_ON_DUP_KEY_NO_DTOR  (1<<2)*/

hash_t ascii_hash_ci(const void *);
hash_t ascii_hash_cs(const void *);
HashTable *hashtable_ascii_cs_new(DupFunc, DtorFunc, DtorFunc);
void hashtable_clear(HashTable *);
bool hashtable_contains(HashTable *, const void *);
bool hashtable_quick_contains(HashTable *, hash_t, const void *);
bool hashtable_delete(HashTable *, const void *, bool);
void hashtable_destroy(HashTable *);
void hashtable_foreach(HashTable *, ForeachFunc);
void hashtable_foreach_reverse(HashTable *, ForeachFunc);
void hashtable_foreach_reverse_with_arg(HashTable *, ForeachFunc, void *);
void hashtable_foreach_reverse_with_args(HashTable *, ForeachFunc, int, ...);
void hashtable_foreach_with_arg(HashTable *, ForeachFunc, void *);
bool hashtable_get(HashTable *, const void *, void **);
bool hashtable_quick_get(HashTable *, hash_t, const void *, void **);
hash_t hashtable_hash(HashTable *, const void *);
bool ascii_equal_ci(const void *, const void *);
bool ascii_equal_cs(const void *, const void *);
HashTable *hashtable_value_new(DupFunc, DtorFunc, DtorFunc);
HashTable *hashtable_new(HashFunc, EqualFunc, DupFunc, DtorFunc, DtorFunc);
HashTable *hashtable_sized_new(size_t, HashFunc, EqualFunc, DupFunc, DtorFunc, DtorFunc);
void hashtable_put(HashTable *, void *, void *, void **);
bool hashtable_put_ex(HashTable *, uint32_t, void *, void *, void **);
bool hashtable_quick_put_ex(HashTable *, uint32_t, hash_t, void *, void *, void **);
size_t hashtable_size(HashTable *);
bool value_equal(const void *, const void *);
// hash_t value_hash(const void *);
void *hashtable_first(HashTable *);

void hashtable_to_iterator(Iterator *, HashTable *);

# ifdef DEBUG
void hashtable_print(HashTable *);
# endif /* DEBUG */

#endif /* !HASHTABLE_H */
