#include "common.h"
#include "iterator.h"

static const Iterator NULL_ITERATOR = { NULL, NULL, NULL, NULL, NULL, NULL, NULL };

void iterator_init(
    Iterator *this,
    void *collection,
    void *state,
    iterator_first_t first,
    iterator_last_t last,
    iterator_current_t current,
    iterator_next_t next,
    iterator_previous_t previous,
    iterator_is_valid_t valid,
    iterator_close_t close
)
{
    this->state = state;
    this->collection = collection;
    this->first = first;
    this->last = last;
    //this->type = type;
    this->current = current;
    this->next = next;
    this->previous = previous;
    this->valid = valid;
    this->close = close;
}

void iterator_close(Iterator *this)
{
    if (NULL != this->close) {
        this->close(this->state);
    }
    *this = NULL_ITERATOR;
}

void *iterator_current(Iterator *this, void **key)
{
    void *value = NULL;

    if (NULL != this->current) {
        this->current(this->collection, &this->state, &value, key);
    }

    return value;
}

void iterator_first(Iterator *this)
{
    if (NULL != this->first) {
        this->first(this->collection, &this->state);
    }
}

void iterator_last(Iterator *this)
{
    if (NULL != this->last) {
        this->last(this->collection, &this->state);
    }
}

void iterator_next(Iterator *this)
{
    if (NULL != this->next) {
        this->next(this->collection, &this->state);
    }
}

void iterator_previous(Iterator *this)
{
    if (NULL != this->previous) {
        this->previous(this->collection, &this->state);
    }
}

bool iterator_is_valid(Iterator *this)
{
    return this->valid(this->collection, &this->state);
}
