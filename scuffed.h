#define INDENT_SPACES 4

#define DA_INIT_CAP 128
#define MAX_WIDTH 256
#define MAX_HEIGHT 256
#define TEMP_BUF_SIZE 1024

#define SC_ASSERT assert

#define max(a, b) ((a) > (b) ? (a) : (b))

#define da_free(da)         \
do {                        \
    SC_ASSERT((da)->items); \
    free((da)->items);      \
    (da)->items = NULL;     \
    (da)->size = 0;         \
    (da)->cap = 0;          \
} while (0)

#define da_grow(da, n)                                              \
do {                                                                \
    SC_ASSERT((da)->size <= (da)->cap);                             \
                                                                    \
    if ((da)->items == NULL) {                                      \
        (da)->items = realloc((da)->items, 0);                      \
    }                                                               \
                                                                    \
    if ((da)->size + (n) > (da)->cap) {                             \
        (da)->cap = max(((da)->size + (n)) * 2, DA_INIT_CAP);       \
        (da)->items = realloc((da)->items,                          \
                              sizeof(*(da)->items) * (da)->cap);    \
    }                                                               \
                                                                    \
    (da)->size += (n);                                              \
} while (0)

#define da_shrink(da, n)                                            \
do {                                                                \
    SC_ASSERT((da)->size <= (da)->cap);                             \
    SC_ASSERT((n) <= (da)->size);                                   \
                                                                    \
    if ((da)->size - (n) <= (da)->cap / 4) {                        \
        (da)->cap = max(((da)->size - (n)) * 2, DA_INIT_CAP);       \
        (da)->items = realloc((da)->items,                          \
                              sizeof(*(da)->items) * (da)->cap);    \
    }                                                               \
                                                                    \
    (da)->size -= (n);                                              \
} while (0)

#define da_append(da, item)                             \
do {                                                    \
    SC_ASSERT(sizeof(item) == sizeof(*(da)->items));    \
    da_grow(da, 1);                                     \
    (da)->items[(da)->size - 1] = (item);               \
} while (0)

#define da_append_many(da, xs, n)                   \
do {                                                \
    SC_ASSERT(sizeof(*xs) == sizeof(*(da)->items)); \
    da_grow(da, n);                                 \
    memcpy(&(da)->items[(da)->size - (n)], (xs),    \
           sizeof(*(da)->items) * (n));             \
} while (0)

#define da_insert_many(da, xs, n, pos)                      \
do {                                                        \
    SC_ASSERT((pos) <= (da)->size);                         \
    SC_ASSERT(sizeof(*xs) == sizeof(*(da)->items));         \
                                                            \
    da_grow(da, n);                                         \
                                                            \
    memmove(&(da)->items[(pos) + (n)],                      \
            &(da)->items[(pos)],                            \
            sizeof(*(da)->items) * ((da)->size - (pos)));   \
    memcpy(&(da)->items[(pos)], (xs),                       \
           sizeof(*(da)->items) * (n));                     \
} while (0)

#define da_delete_many(da, pos, n)                              \
do {                                                            \
    SC_ASSERT((pos) + (n) <= (da)->size);                       \
                                                                \
    memmove(&(da)->items[(pos)],                                \
            &(da)->items[(pos) + (n)],                          \
            sizeof(*(da)->items) * ((da)->size - (pos) - (n))); \
                                                                \
    da_shrink(da, n);                                           \
} while (0)

#define lines_free da_free
#define lines_append da_append

#define sb_free da_free
#define sb_insert_buf da_insert_many
#define sb_delete_substr da_delete_many

#define sb_append_cstr(sb, cstr)    \
do {                                \
    const char *s = (cstr);         \
    u32 len = strlen(s);            \
    da_append_many(sb, s, len);     \
} while (0)

// #########################################################################
// Type defenitions
// #########################################################################

typedef signed char s8;
typedef unsigned char u8;
typedef signed short s16;
typedef unsigned short u16;
typedef signed int s32;
typedef unsigned int u32;
typedef signed long s64;
typedef unsigned long u64;

typedef struct line_t {
    u32 begin;
    u32 end;
} line_t;

typedef struct lines_t {
    line_t *items;
    u32 size;
    u32 cap;
} lines_t;

typedef struct sb_t {
    char *items;
    u32 size;
    u32 cap;
} sb_t;

typedef enum buffer_mode_t {
    NORMAL_MODE = 0,
    INSERT_MODE = 1
} buffer_mode_t;

typedef struct buffer_t {
    sb_t data;
    sb_t path;
    lines_t line_tokens;
    buffer_mode_t mode;

    u32 cursor;
    u32 row_offset;
    u32 last_visual_col;

    u16 contents_width;
    u16 contents_height;

    bool saved;
} buffer_t;

typedef union utf8_char_t {
    char arr[4];
    u32 abs;
} utf8_char_t;
