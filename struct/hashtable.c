#include <string.h>
#include <ctype.h>

#include "hashtable.h"
#include "nearest_power.h"

#define HASHTABLE_MIN_SIZE 8

typedef struct _HashNode {
    ht_hash_t hash;
    ht_key_t key;
    void *data;
    struct _HashNode *nNext; /* n = node */
    struct _HashNode *nPrev;
    struct _HashNode *gNext; /* g = global */
    struct _HashNode *gPrev;
} HashNode;

struct _HashTable {
    HashNode **nodes;
    HashNode *gHead;
    HashNode *gTail;
    HashFunc hf;
    EqualFunc ef;
    DupFunc key_duper;
    DtorFunc key_dtor;
    DtorFunc value_dtor;
    size_t capacity;
    size_t count;
    ht_hash_t mask;
};

static bool hashtable_put_real(HashTable *, uint32_t, ht_hash_t, ht_key_t, void *, void **);

ht_hash_t value_hash(ht_key_t k)
{
    return (ht_hash_t) k;
}

bool value_equal(ht_key_t k1, ht_key_t k2)
{
    return k1 == k2;
}

bool ascii_equal_cs(ht_key_t k1, ht_key_t k2)
{
    const char *string1 = (const char *) k1;
    const char *string2 = (const char *) k2;

    if (NULL == string1 || NULL == string2) {
        return string1 == string2;
    }

    return 0 == strcmp(string1, string2);
}

ht_hash_t ascii_hash_cs(ht_key_t k)
{
    ht_hash_t h = 5381;
    const char *str = (const char *) k;

    while (0 != *str) {
        h += (h << 5);
        h ^= (unsigned long) *str++;
    }

    return h;
}

bool ascii_equal_ci(ht_key_t k1, ht_key_t k2)
{
    const char *string1 = (const char *) k1;
    const char *string2 = (const char *) k2;

    if (NULL == string1 || NULL == string2) {
        return string1 == string2;
    }

    return 0 == strcasecmp(string1, string2);
}

ht_hash_t ascii_hash_ci(ht_key_t k)
{
    ht_hash_t h = 5381;
    const char *str = (const char *) k;

    while (0 != *str) {
        h += (h << 5);
        h ^= (unsigned long) toupper((unsigned char) *str++);
    }

    return h;
}

static inline void hashtable_rehash(HashTable *this)
{
    HashNode *n;
    uint32_t index;

    if (UNEXPECTED(this->count < 1)) {
        return;
    }
    memset(this->nodes, 0, this->capacity * sizeof(*this->nodes));
    n = this->gHead;
    while (NULL != n) {
        index = n->hash & this->mask;
        n->nNext = this->nodes[index];
        n->nPrev = NULL;
        if (NULL != n->nNext) {
            n->nNext->nPrev = n;
        }
        this->nodes[index] = n;
        n = n->gNext;
    }
}

static inline void hashtable_maybe_resize(HashTable *this)
{
    if (UNEXPECTED(this->count < this->capacity)) {
        return;
    }
    if (EXPECTED(this->capacity << 1) > 0) {
        this->nodes = mem_renew(this->nodes, *this->nodes, this->capacity << 1);
        this->capacity <<= 1;
        this->mask = this->capacity - 1;
        hashtable_rehash(this);
    }
}

HashTable *hashtable_sized_new(
    size_t capacity,
    HashFunc hf,
    EqualFunc ef,
    DupFunc key_duper,
    DtorFunc key_dtor,
    DtorFunc value_dtor
)
{
    HashTable *this;

    this = mem_new(*this);
    this->count = 0;
    this->gHead = NULL;
    this->gTail = NULL;
    this->capacity = nearest_power(capacity, HASHTABLE_MIN_SIZE);
    this->mask = this->capacity - 1;
    this->hf = hf;
    if (NULL == ef) {
        this->ef = value_equal;
    } else {
        this->ef = ef;
    }
    this->key_duper = key_duper;
    this->key_dtor = key_dtor;
    this->value_dtor = value_dtor;
    this->nodes = mem_new_n(*this->nodes, this->capacity);
    memset(this->nodes, 0, this->capacity * sizeof(*this->nodes));

    return this;
}

HashTable *hashtable_new(
    HashFunc hf,
    EqualFunc ef,
    DupFunc key_duper,
    DtorFunc key_dtor,
    DtorFunc value_dtor
) {
    return hashtable_sized_new(HASHTABLE_MIN_SIZE, hf, ef, key_duper, key_dtor, value_dtor);
}

HashTable *hashtable_copy(HashTable *ht, DupFunc key_duper_override, DupFunc value_duper)
{
    HashNode *n;
    HashTable *copy;
    DupFunc key_duper;

    assert(NULL != ht);

    copy = hashtable_sized_new(ht->capacity, ht->hf, ht->ef, ht->key_duper, ht->key_dtor, ht->value_dtor);
    key_duper = NULL == key_duper_override ? ht->key_duper : key_duper_override;

    for (n = ht->gHead; NULL != n; n = n->gNext) {
        hashtable_put_real(copy, 0, n->hash, NULL == key_duper ? n->key : (ht_key_t) key_duper((void *) n->key), NULL == value_duper ? n->data : value_duper((void *) n->data), NULL);
    }

    return copy;
}

HashTable *hashtable_ascii_cs_new(DupFunc key_duper, DtorFunc key_dtor, DtorFunc value_dtor)
{
    return hashtable_new(/*HASHTABLE_MIN_SIZE, */ascii_hash_cs, ascii_equal_cs, key_duper, key_dtor, value_dtor);
}

HashTable *hashtable_ascii_ci_new(DupFunc key_duper, DtorFunc key_dtor, DtorFunc value_dtor)
{
    return hashtable_new(/*HASHTABLE_MIN_SIZE, */ascii_hash_ci, ascii_equal_ci, key_duper, key_dtor, value_dtor);
}

ht_hash_t _hashtable_hash(HashTable *this, ht_key_t key)
{
    assert(NULL != this);

    return NULL == this->hf ? key : this->hf(key);
}

size_t hashtable_size(HashTable *this)
{
    assert(NULL != this);

    return this->count;
}

static bool hashtable_put_real(HashTable *this, uint32_t flags, ht_hash_t h, ht_key_t key, void *value, void **oldvalue)
{
    HashNode *n;
    uint32_t index;

    assert(NULL != this);

    index = h & this->mask;
    n = this->nodes[index];
    while (NULL != n) {
        if (n->hash == h && this->ef(key, n->key)) {
            if (NULL != oldvalue) {
                *oldvalue = n->data;
            }
            if (!HAS_FLAG(flags, HT_PUT_ON_DUP_KEY_PRESERVE)) {
                if (NULL != this->value_dtor/* && !HAS_FLAG(flags, HT_PUT_ON_DUP_KEY_NO_DTOR)*/) {
                    this->value_dtor(n->data);
                }
                n->data = value;
                if (NULL == this->key_duper) {
                    n->key = key;
                } else {
                    n->key = (ht_key_t) this->key_duper((void *) key);
                }
                return TRUE;
            }
            return FALSE;
        }
        n = n->nNext;
    }
    n = mem_new(*n);
    if (NULL == this->key_duper) {
        n->key = key;
    } else {
        n->key = (ht_key_t) this->key_duper((void *) key);
    }
    n->hash = h;
    n->data = value;
    // Bucket: prepend
    n->nNext = this->nodes[index];
    n->nPrev = NULL;
    if (NULL != n->nNext) {
        n->nNext->nPrev = n;
    }
    // Global
    n->gPrev = this->gTail;
    this->gTail = n;
    n->gNext = NULL;
    if (NULL != n->gPrev) {
        n->gPrev->gNext = n;
    }
    if (NULL == this->gHead) {
        this->gHead = n;
    }
    this->nodes[index] = n;
    ++this->count;
    hashtable_maybe_resize(this);

    return TRUE;
}

bool _hashtable_quick_put(HashTable *this, uint32_t flags, ht_hash_t h, ht_key_t key, void *value, void **oldvalue)
{
    return hashtable_put_real(this, flags, h, key, value, oldvalue);
}

bool _hashtable_put(HashTable *this, uint32_t flags, ht_key_t key, void *value, void **oldvalue)
{
    return hashtable_put_real(this, flags, NULL == this->hf ? key : this->hf(key), key, value, oldvalue);
}

bool _hashtable_direct_put(HashTable *this, uint32_t flags, ht_hash_t h, void *value, void **oldvalue)
{
    return hashtable_put_real(this, flags, h, (ht_key_t) h, value, oldvalue);
}

bool _hashtable_quick_contains(HashTable *this, ht_hash_t h, ht_key_t key)
{
    HashNode *n;
    uint32_t index;

    assert(NULL != this);

    index = h & this->mask;
    n = this->nodes[index];
    while (NULL != n) {
        if (n->hash == h && this->ef(key, n->key)) {
            return TRUE;
        }
        n = n->nNext;
    }

    return FALSE;
}

bool _hashtable_contains(HashTable *this, ht_key_t key)
{
    return _hashtable_quick_contains(this, NULL == this->hf ? key : this->hf(key), key);
}

bool _hashtable_direct_contains(HashTable *this, ht_hash_t h)
{
    return _hashtable_quick_contains(this, h, (ht_key_t) h);
}

bool _hashtable_quick_get(HashTable *this, ht_hash_t h, ht_key_t key, void **value)
{
    HashNode *n;
    uint32_t index;

    assert(NULL != this);
    assert(NULL != value);

    index = h & this->mask;
    n = this->nodes[index];
    while (NULL != n) {
        if (n->hash == h && this->ef(key, n->key)) {
            *value = n->data;
            return TRUE;
        }
        n = n->nNext;
    }

    return FALSE;
}

bool _hashtable_get(HashTable *this, ht_key_t key, void **value)
{
    return _hashtable_quick_get(this, NULL == this->hf ? key : this->hf(key), key, value);
}

bool _hashtable_direct_get(HashTable *this, ht_hash_t h, void **value)
{
    return _hashtable_quick_get(this, h, h, value);
}

static bool hashtable_delete_real(HashTable *this, ht_hash_t h, ht_key_t key, bool call_dtor)
{
    HashNode *n;
    uint32_t index;

    assert(NULL != this);

    index = h & this->mask;
    n = this->nodes[index];
    while (NULL != n) {
        if (n->hash == h && this->ef(key, n->key)) {
            if (n == this->nodes[index]) {
                this->nodes[index] = n->nNext;
            } else {
                n->nPrev->nNext = n->nNext;
            }
            if (NULL != n->nNext) {
                n->nNext->nPrev = n->nPrev;
            }
            if (NULL != n->gPrev) {
                n->gPrev->gNext = n->gNext;
            } else {
                this->gHead = n->gNext;
            }
            if (NULL != n->gNext) {
                n->gNext->gPrev = n->gPrev;
            } else {
                this->gTail = n->gPrev;
            }
            if (call_dtor && NULL != this->value_dtor) {
                this->value_dtor(n->data);
            }
            if (call_dtor && NULL != this->key_dtor) {
                this->key_dtor((void *) n->key);
            }
            free(n);
            --this->count;
            return TRUE;
        }
        n = n->nNext;
    }

    return FALSE;
}

bool _hashtable_quick_delete(HashTable *this, ht_hash_t h, ht_key_t key, bool call_dtor)
{
    return hashtable_delete_real(this, h, key, call_dtor);
}

bool _hashtable_delete(HashTable *this, ht_key_t key, bool call_dtor)
{
    return hashtable_delete_real(this, NULL == this->hf ? key : this->hf(key), key, call_dtor);
}

bool _hashtable_direct_delete(HashTable *this, ht_hash_t h, bool call_dtor)
{
    return hashtable_delete_real(this, h, h, call_dtor);
}

static void hashtable_clear_real(HashTable *this)
{
    HashNode *n, *tmp;

    n = this->gHead;
    this->count = 0;
    this->gHead = NULL;
    this->gTail = NULL;
    while (NULL != n) {
        tmp = n;
        n = n->gNext;
        if (NULL != this->value_dtor) {
            this->value_dtor(tmp->data);
        }
        if (NULL != this->key_dtor) {
            this->key_dtor((void *) tmp->key);
        }
        free(tmp);
    }
    memset(this->nodes, 0, this->capacity * sizeof(*this->nodes));
}

void hashtable_clear(HashTable *this)
{
    assert(NULL != this);

    hashtable_clear_real(this);
}

void hashtable_destroy(HashTable *this)
{
    assert(NULL != this);

    hashtable_clear_real(this);
    free(this->nodes);
    free(this);
}

void *hashtable_first(HashTable *this)
{
    if (NULL == this->gHead) {
        return NULL;
    } else {
        return this->gHead->data;
    }
}

static void hashtable_iterator_first(const void *collection, void **state)
{
    assert(NULL != collection);
    assert(NULL != state);

    *state = ((HashTable *) collection)->gHead;
}

static void hashtable_iterator_last(const void *collection, void **state)
{
    assert(NULL != collection);
    assert(NULL != state);

    *state = ((HashTable *) collection)->gTail;
}

static bool hashtable_iterator_is_valid(const void *UNUSED(collection), void **state)
{
    assert(NULL != state);

    return NULL != *state;
}

static void hashtable_iterator_current(const void *UNUSED(collection), void **state, void **value, void **key)
{
    HashNode *n;

    assert(NULL != state);
    assert(NULL != value);

    n = (HashNode *) *state;
    if (NULL != key) {
        *((ht_key_t *) key) = n->key;
    }
    *value = n->data;
}

static void hashtable_iterator_next(const void *UNUSED(collection), void **state)
{
    assert(NULL != state);

    *state = ((HashNode *) *state)->gNext;
}

static void hashtable_iterator_previous(const void *UNUSED(collection), void **state)
{
    assert(NULL != state);

    *state = ((HashNode *) *state)->gPrev;
}

void hashtable_to_iterator(Iterator *it, HashTable *this)
{
    iterator_init(
        it, this, NULL,
        hashtable_iterator_first, hashtable_iterator_last,
        hashtable_iterator_current,
        hashtable_iterator_next, hashtable_iterator_previous,
        hashtable_iterator_is_valid,
        NULL
    );
}

#ifdef DEBUG
# include <stdio.h>
# include <inttypes.h>
void hashtable_print(HashTable *this)
{
    size_t i;
    HashNode *n;

    for (i = 0; i < this->capacity; i++) {
        n = this->nodes[i];
        printf("%zu/%zu:\n", i, this->capacity);
        while (NULL != n) {
            printf("    %" PRIXPTR " <==> %p (%" PRIuPTR ")\n", n->key, n->data, n->hash);
            n = n->nNext;
        }
    }
    printf("\n");
}
#endif /* DEBUG */

void hashtable_puts_keys(HashTable *this)
{
    HashNode *n;

    assert(NULL != this);

    for (n = this->gHead; NULL != n; n = n->gNext) {
        puts((const char *) n->key);
    }
}
