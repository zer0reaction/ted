/*
 * Dynamic array header - last edited by zer0 on 13 Jun 2025
 * Written in C99.
 * 
 * Usage:
 * 
 * 1.  DA_TYPEDEF(type, name) to define a struct with the specified name
 * 2.  name_create() to allocate memory and return struct by value
 * ... Do whatever
 * n.  name_destroy(type *) to free memory
 *
 * All allocated memory is not initialized.
 * 
 * DA_INIT_CAP sets the initial capacity of the array. Capacity can't go below
 * it by using generated functions (is this the right way to do this?).
 *
 * malloc and realloc were used, so calculated values are not protected from
 * overflow (you shouldn't allocate that much memory anyway).
 */

#ifndef DA_H_
#define DA_H_

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define DA_INIT_CAP 128

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define DA_TYPEDEF(T, name) \
typedef struct name {       \
    T* data;                \
    size_t size;            \
    size_t cap;             \
} name;                     \
\
static name name##_create(void)             \
{                                           \
    name a;                                 \
                                            \
    a.cap = DA_INIT_CAP;                    \
    a.data = malloc(sizeof(T) * a.cap);     \
    a.size = 0;                             \
                                            \
    return a;                               \
}                                           \
\
static void name##_destroy(name *a) \
{                                   \
    free(a->data);                  \
    a->data = NULL;                 \
    a->size = a->cap = 0;           \
}                                   \
static T name##_at(const name *a, size_t i)             \
{                                                       \
    assert(i < a->size && "Array index out of bounds"); \
    return a->data[i];                                  \
}                                                       \
\
static void name##_push_back(name *a, T item)           \
{                                                       \
    if (a->size + 1 > a->cap) {                         \
        a->cap = MAX(DA_INIT_CAP, a->cap * 2);          \
        a->data = realloc(a->data, sizeof(T) * a->cap); \
    }                                                   \
                                                        \
    a->data[a->size] = item;                            \
    a->size += 1;                                       \
}                                                       \
\
static void name##_push_back_many(name *a, const T* items, size_t n)    \
{                                                                       \
    if (a->size + n > a->cap) {                                         \
        a->cap = MAX(DA_INIT_CAP, (a->size + n) * 2);                   \
        a->data = realloc(a->data, sizeof(T) * a->cap);                 \
    }                                                                   \
    memcpy(&a->data[a->size], items, sizeof(T) * n);                    \
    a->size += n;                                                       \
}                                                                       \
\
static T name##_pop_back(name *a)                           \
{                                                           \
    assert(a->size > 0 && "Can't pop from empty array");    \
                                                            \
    T item = a->data[a->size - 1];                          \
    a->size--;                                              \
                                                            \
    if (a->size <= a->cap / 4) {                            \
        a->cap = MAX(DA_INIT_CAP, a->cap / 2);              \
        a->data = realloc(a->data, sizeof(T) * a->cap);     \
    }                                                       \
                                                            \
    return item;                                            \
}                                                           \
\
static void name##_push(name    *a,                                         \
                        size_t   pos,                                       \
                        const T *items,                                     \
                        size_t   n)                                         \
{                                                                           \
    assert(pos <= a->size && "Can't insert at this position");              \
                                                                            \
    if (a->size + n > a->cap) {                                             \
        a->cap = MAX(DA_INIT_CAP, (a->size + n) * 2);                       \
        a->data = realloc(a->data, sizeof(T) * a->cap);                     \
    }                                                                       \
                                                                            \
    memmove(&a->data[pos + n], &a->data[pos], sizeof(T) * (a->size - pos)); \
    memcpy(&a->data[pos], items, sizeof(T) * n);                            \
    a->size += n;                                                           \
}                                                                           \
\
static void name##_delete_many(name *a, size_t pos, size_t n)       \
{                                                                   \
    assert(pos < a->size && "Array index out of bounds");           \
    assert(pos + n <= a->size && "Can't delete this many items");   \
                                                                    \
    memmove(&a->data[pos],                                          \
            &a->data[pos + n],                                      \
            sizeof(T) * (a->size - (pos + n)));                     \
                                                                    \
    a->size -= n;                                                   \
    if (a->size <= a->cap / 4) {                                    \
        a->cap = MAX(DA_INIT_CAP, a->cap / 2);                      \
        a->data = realloc(a->data, sizeof(T) * a->cap);             \
    }                                                               \
}

#endif // DA_H_
