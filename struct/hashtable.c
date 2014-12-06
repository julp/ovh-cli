#include <string.h>
#include <ctype.h>

#include "hashtable.h"

typedef struct _HashNode {
    hash_t hash;
    void *key;
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
    hash_t mask;
};

#define MIN_SIZE 8

// static const HashNode NullHashNode = { 0, NULL, NULL, NULL, NULL, NULL, NULL };

static inline uint32_t nearest_power(size_t requested, size_t minimal)
{
    if (requested > 0x80000000) {
        return UINT32_MAX;
    } else {
        int i = 1;
        requested = MAX(requested, minimal);
        while ((1U << i) < requested) {
            i++;
        }

        return (1U << i);
    }
}

hash_t value_hash(const void *k)
{
    return (hash_t) k;
}

bool value_equal(const void *k1, const void *k2)
{
    return k1 == k2;
}

bool ascii_equal_cs(const void *k1, const void *k2)
{
    const char *string1 = k1;
    const char *string2 = k2;

    if (NULL == k1 || NULL == k2) {
        return k1 == k2;
    }

    return 0 == strcmp(string1, string2);
}

hash_t ascii_hash_cs(const void *k)
{
    hash_t h = 5381;
    const char *str = k;

    while (0 != *str) {
        h += (h << 5);
        h ^= (unsigned long) *str++;
    }

    return h;
}

bool ascii_equal_ci(const void *k1, const void *k2)
{
    const char *string1 = k1;
    const char *string2 = k2;

    if (NULL == k1 || NULL == k2) {
        return k1 == k2;
    }

    return 0 == strcasecmp(string1, string2);
}

hash_t ascii_hash_ci(const void *k)
{
    hash_t h = 5381;
    const char *str = k;

    while (0 != *str) {
        h += (h << 5);
        h ^= (unsigned long) toupper((unsigned char) *str++);
    }

    return h;
}

HashTable *hashtable_sized_new(
    size_t capacity,
    HashFunc hf,
    EqualFunc ef,
    DupFunc key_duper,
    DtorFunc key_dtor,
    DtorFunc value_dtor
) {
    HashTable *this;

    // allow NULL for hf for "numeric" keys by using directly the hash (key member point to hash member)
#if 0
    assert(NULL != hf);
#ifndef WITH_SORTED_LIST
    assert(NULL != ef);
#else
    assert(NULL != cf);
#endif /* !WITH_SORTED_LIST */
#endif

    this = mem_new(*this);
    this->count = 0;
    this->gHead = NULL;
    this->gTail = NULL;
    this->capacity = nearest_power(capacity, MIN_SIZE);
    this->mask = this->capacity - 1;
    this->hf = hf;
    this->ef = ef;
    this->key_duper = key_duper;
    this->key_dtor = key_dtor;
    this->value_dtor = value_dtor;
    this->nodes = mem_new_n(*this->nodes, this->capacity);
    //mem_cpy_n(this->nodes, &NullHashNode, /***/*this->nodes, this->capacity);
    memset(this->nodes, 0, this->capacity * sizeof(*this->nodes));

    return this;
}

HashTable *hashtable_new(HashFunc hf, EqualFunc ef, DupFunc key_duper, DtorFunc key_dtor, DtorFunc value_dtor)
{
    return hashtable_sized_new(MIN_SIZE, hf, ef, key_duper, key_dtor, value_dtor);
}

HashTable *hashtable_ascii_cs_new(DupFunc key_duper, DtorFunc key_dtor, DtorFunc value_dtor)
{
    return hashtable_sized_new(MIN_SIZE, ascii_hash_cs, ascii_equal_cs, key_duper, key_dtor, value_dtor);
}

static inline void hashtable_rehash(HashTable *this)
{
    HashNode *n;
    uint32_t index;

    if (this->count < 1) {
        return;
    }
    //mem_cpy_n(this->nodes, &NullHashNode, *this->nodes, this->capacity);
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
    if (this->count < this->capacity) {
        return;
    }
    if ((this->capacity << 1) > 0) {
        this->nodes = mem_renew(this->nodes, *this->nodes, this->capacity << 1);
        this->capacity <<= 1;
        this->mask = this->capacity - 1;
        hashtable_rehash(this);
    }
}

hash_t hashtable_hash(HashTable *this, const void *key)
{
    assert(NULL != this);

    return this->hf(key);
}

size_t hashtable_size(HashTable *this)
{
    assert(NULL != this);

    return this->count;
}

static bool hashtable_put_real(HashTable *this, uint32_t flags, hash_t h, void *key, void *value, void **oldvalue)
{
    HashNode *n;
    uint32_t index;

    assert(NULL != this);

    index = h & this->mask;
    n = this->nodes[index];
    if (NULL == this->hf) {
        key = &h;
    }
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
                return TRUE;
            }
            return FALSE;
        }
        n = n->nNext;
    }
    n = mem_new(*n);
    if (NULL == this->hf) {
        n->key = &n->hash;
    } else if (NULL == this->key_duper) {
        n->key = key;
    } else {
        n->key = this->key_duper(key);
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

void hashtable_put(HashTable *this, void *key, void *value, void **oldvalue)
{
    hashtable_put_real(this, 0, this->hf(key), key, value, oldvalue);
}

bool hashtable_quick_put_ex(HashTable *this, uint32_t flags, hash_t h, void *key, void *value, void **oldvalue)
{
    return hashtable_put_real(this, flags, h, key, value, oldvalue);
}

bool hashtable_put_ex(HashTable *this, uint32_t flags, void *key, void *value, void **oldvalue)
{
    return hashtable_put_real(this, flags, this->hf(key), key, value, oldvalue);
}

bool hashtable_quick_get(HashTable *this, hash_t h, const void *key, void **value)
{
    HashNode *n;
    uint32_t index;

    assert(NULL != this);
    assert(NULL != value);

    index = h & this->mask;
    n = this->nodes[index];
    if (NULL == this->hf) {
        key = &h;
    }
    while (NULL != n) {
        if (n->hash == h && this->ef(key, n->key)) {
            *value = n->data;
            return TRUE;
        }
        n = n->nNext;
    }

    return FALSE;
}

bool hashtable_get(HashTable *this, const void *key, void **value)
{
    return hashtable_quick_get(this, this->hf(key), key, value);
}

bool hashtable_quick_contains(HashTable *this, hash_t h, const void *key)
{
    HashNode *n;
    uint32_t index;

    assert(NULL != this);

    index = h & this->mask;
    n = this->nodes[index];
    if (NULL == this->hf) {
        key = &h;
    }
    while (NULL != n) {
        if (n->hash == h && this->ef(key, n->key)) {
            return TRUE;
        }
        n = n->nNext;
    }

    return FALSE;
}

bool hashtable_contains(HashTable *this, const void *key)
{
    return hashtable_quick_contains(this, this->hf(key), key);
}

static bool hashtable_delete_real(HashTable *this, hash_t h, const void *key, bool call_dtor)
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
                this->key_dtor(n->key);
            }
            free(n);
            --this->count;
            return TRUE;
        }
        n = n->nNext;
    }

    return FALSE;
}

bool hashtable_quick_delete(HashTable *this, hash_t h, const void *key, bool call_dtor)
{
    return hashtable_delete_real(this, h, key, call_dtor);
}

bool hashtable_delete(HashTable *this, const void *key, bool call_dtor)
{
    return hashtable_delete_real(this, this->hf(key), key, call_dtor);
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
            this->key_dtor(tmp->key);
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

#if 0
static HashNode *hashtable_delete_node(HashTable *this, HashNode *n)
{
    HashNode *ret;

    if (NULL != n->nPrev) {
        n->nPrev->nNext = n->nNext;
    } else {
        uint32_t index;

        index = n->hash & this->mask;
        this->nodes[index] = n->nNext;
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
    --this->count;
    if (NULL != this->value_dtor) {
        this->value_dtor(n->data);
    }
    if (NULL != this->key_dtor) {
        this->key_dtor(n->key);
    }
    ret = n->gNext;
    free(n);

    return ret;
}

void hashtable_foreach(HashTable *this, ForeachFunc ff)
{
    int ret;
    HashNode *n;

    n = this->gHead;
    while (NULL != n) {
        ret = ff(n->key, n->data);
        if (ret & HT_FOREACH_DELETE) {
            n = hashtable_delete_node(this, n);
        } else {
            n = n->gNext;
        }
        if (ret & HT_FOREACH_STOP) {
            break;
        }
    }
}

void hashtable_foreach_reverse(HashTable *this, ForeachFunc ff)
{
    int ret;
    HashNode *n, *tmp;

    n = this->gTail;
    while (NULL != n) {
        ret = ff(n->key, n->data);
        tmp = n;
        n = n->gPrev;
        if (ret & HT_FOREACH_DELETE) {
            n = hashtable_delete_node(this, tmp);
        } else {
            n = n->gPrev;
        }
        if (ret & HT_FOREACH_STOP) {
            break;
        }
    }
}

void hashtable_foreach_with_arg(HashTable *this, ForeachFunc ff, void *arg)
{
    int ret;
    HashNode *n;

    n = this->gHead;
    while (NULL != n) {
        ret = ff(n->key, n->data, arg);
        if (ret & HT_FOREACH_DELETE) {
            n = hashtable_delete_node(this, n);
        } else {
            n = n->gNext;
        }
        if (ret & HT_FOREACH_STOP) {
            break;
        }
    }
}

void hashtable_foreach_reverse_with_arg(HashTable *this, ForeachFunc ff, void *arg)
{
    int ret;
    HashNode *n, *tmp;

    n = this->gTail;
    while (NULL != n) {
        ret = ff(n->key, n->data, arg);
        tmp = n;
        n = n->gPrev;
        if (ret & HT_FOREACH_DELETE) {
            n = hashtable_delete_node(this, tmp);
        } else {
            n = n->gPrev;
        }
        if (ret & HT_FOREACH_STOP) {
            break;
        }
    }
}

/*void hashtable_foreach_with_args(HashTable *this, ForeachFunc ff, int argc, ...)
{
    //
}

void hashtable_foreach_reverse_with_args(HashTable *this, ForeachFunc ff, int argc, ...)
{
    //
}*/
#endif

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
        *key = n->key;
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
            printf("    %p <==> %p (%" PRIuPTR ")\n", n->key, n->data, n->hash);
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
