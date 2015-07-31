#ifndef HASHTABLE_H

# define HASHTABLE_H

# include "common.h"
# include "iterator.h"

typedef struct _HashTable HashTable;

typedef uintptr_t ht_key_t; // key_t is defined for ftok
typedef uintptr_t ht_hash_t;
typedef ht_hash_t (*HashFunc)(ht_key_t);
typedef bool (*EqualFunc)(ht_key_t, ht_key_t);

# define HT_PUT_ON_DUP_KEY_PRESERVE (1<<1)
/*# define HT_PUT_ON_DUP_KEY_NO_DTOR  (1<<2)*/

bool _hashtable_contains(HashTable *, ht_key_t);
HashTable *hashtable_copy(HashTable *, DupFunc, DupFunc);
bool _hashtable_delete(HashTable *, ht_key_t, bool);
bool _hashtable_get(HashTable *, ht_key_t, void **);
ht_hash_t _hashtable_hash(HashTable *, ht_key_t);
bool _hashtable_put(HashTable *, uint32_t, ht_key_t, void *, void **);
bool _hashtable_quick_contains(HashTable *, ht_hash_t, ht_key_t);
bool _hashtable_quick_delete(HashTable *, ht_hash_t, ht_key_t, bool);
bool _hashtable_quick_get(HashTable *, ht_hash_t, ht_key_t, void **);
bool _hashtable_quick_put(HashTable *, uint32_t, ht_hash_t, ht_key_t, void *, void **);
bool ascii_equal_ci(ht_key_t, ht_key_t);
bool ascii_equal_cs(ht_key_t, ht_key_t);
ht_hash_t ascii_hash_ci(ht_key_t);
ht_hash_t ascii_hash_cs(ht_key_t);
HashTable *hashtable_ascii_ci_new(DupFunc, DtorFunc, DtorFunc);
HashTable *hashtable_ascii_cs_new(DupFunc, DtorFunc, DtorFunc);
void hashtable_clear(HashTable *);
void hashtable_destroy(HashTable *);
bool _hashtable_direct_contains(HashTable *, ht_hash_t);
bool _hashtable_direct_delete(HashTable *, ht_hash_t, bool);
bool _hashtable_direct_get(HashTable *, ht_hash_t, void **);
bool _hashtable_direct_put(HashTable *, uint32_t, ht_hash_t, void *, void **);
HashTable *hashtable_new(HashFunc, EqualFunc, DupFunc, DtorFunc, DtorFunc);
HashTable *hashtable_sized_new(size_t, HashFunc, EqualFunc, DupFunc, DtorFunc, DtorFunc);
size_t hashtable_size(HashTable *);
bool value_equal(ht_key_t, ht_key_t);
ht_hash_t value_hash(ht_key_t);

#define hashtable_hash(ht, k) \
    _hashtable_hash(ht, (ht_key_t) k)

#define hashtable_direct_contains(ht, h) \
    _hashtable_direct_contains(ht, (ht_hash_t) h)

#define hashtable_contains(ht, k) \
    _hashtable_contains(ht, (ht_key_t) k)

#define hashtable_quick_contains(ht, h, k) \
    _hashtable_quick_contains(ht, h, (ht_key_t) k)

#define hashtable_direct_put(ht, f, h, nv, ov) \
    _hashtable_direct_put(ht, f, (ht_hash_t) h, (void *) nv, (void **) ov)

#define hashtable_put(ht, f, k, nv, ov) \
    _hashtable_put(ht, f, (ht_key_t) k, (void *) nv, (void **) ov)

#define hashtable_quick_put(ht, f, h, k, nv, ov) \
    _hashtable_quick_put(ht, f, h, (ht_key_t) k, (void *) nv, (void **) ov)

#define hashtable_direct_get(ht, h, v) \
    _hashtable_direct_get(ht, (ht_hash_t) h, (void **) v)

#define hashtable_get(ht, k, v) \
    _hashtable_get(ht, (ht_key_t) k, (void **) v)

#define hashtable_quick_get(ht, h, k, v) \
    _hashtable_quick_get(ht, h, (ht_key_t) k, (void **) v)

#define hashtable_direct_delete(ht, h, dtor) \
    _hashtable_direct_delete(ht, (ht_hash_t) h, dtor)

#define hashtable_delete(ht, k, dtor) \
    _hashtable_delete(ht, (ht_key_t) k, dtor)

#define hashtable_quick_delete(ht, h, k, dtor) \
    _hashtable_quick_delete(ht, h, (ht_key_t) k, dtor)

void *hashtable_first(HashTable *);
void hashtable_puts_keys(HashTable *);
void hashtable_to_iterator(Iterator *, HashTable *);

# ifdef DEBUG
void hashtable_print(HashTable *);
# endif /* DEBUG */

#endif /* !HASHTABLE_H */
