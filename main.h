#ifndef MAIN_H_
#define MAIN_H_

#define DA_INIT_CAP 128

#define max(a, b) ((a) > (b) ? (a) : (b))

#define da_free(da)      \
do {                     \
    assert((da)->items); \
    free((da)->items);   \
    (da)->items = NULL;  \
    (da)->size = 0;      \
    (da)->cap = 0;       \
} while (0)

#define da_append(da, item)                                      \
do {                                                             \
    assert((da)->size <= (da)->cap);                             \
    assert(sizeof(item) == sizeof(*(da)->items));                \
                                                                 \
    if ((da)->items == NULL) {                                   \
        (da)->items = realloc((da)->items, 0);                   \
    }                                                            \
                                                                 \
    if ((da)->size + 1 > (da)->cap) {                            \
        (da)->cap = max(((da)->cap + 1) * 2, DA_INIT_CAP);       \
        (da)->items = realloc((da)->items,                       \
                              sizeof(*(da)->items) * (da)->cap); \
    }                                                            \
                                                                 \
    (da)->items[(da)->size] = (item);                            \
    (da)->size += 1;                                             \
} while (0)

#define da_append_many(da, xs, n)                                \
do {                                                             \
    assert((da)->size <= (da)->cap);                             \
    assert(sizeof(*(da)->items) == sizeof(*xs));                 \
                                                                 \
    if ((da)->items == NULL) {                                   \
        (da)->items = realloc((da)->items, 0);                   \
    }                                                            \
                                                                 \
    if ((da)->size + (n) > (da)->cap) {                          \
        (da)->cap = max(((da)->cap + (n)) * 2, DA_INIT_CAP);     \
        (da)->items = realloc((da)->items,                       \
                              sizeof(*(da)->items) * (da)->cap); \
    }                                                            \
                                                                 \
    for (u32 i = 0; i < (n); ++i) {                              \
        (da)->items[(da)->size + i] = (xs)[i];                   \
    }                                                            \
    (da)->size += (n);                                           \
} while (0)


#define lines_free(lines) da_free(lines)
#define lines_append(lines, line) da_append(lines, line)

#define sb_free(sb) da_free(sb)
#define sb_append_char(sb, c) da_append(sb, (char)c)
#define sb_append_cstr(sb, cstr)  \
do {                              \
    const char *s = (cstr);       \
    u32 len = strlen(s);          \
    da_append_many((sb), s, len); \
} while (0)

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

typedef struct buffer_t {
    sb_t data;
    lines_t lines;

    u32 cursor;
    u32 row_offset;
    u32 last_visual_col;
} buffer_t;


// utility functions
u32 get_cursor_row(buffer_t *b);
u32 update_row_offset(buffer_t *b, u16 height);
u8 utf8_byte_size(char c);

// lines functions
u32 lines_tokenize(lines_t *lines, const sb_t sb);

// buffer functions
u32 buffer_from_file(buffer_t *b, const char *path);
void buffer_kill(buffer_t *b);

// editor functions
void move_down(buffer_t *b);
void move_up(buffer_t *b);
void move_right(buffer_t *b);
void move_left(buffer_t *b);

// render functions
void render(buffer_t *b);

#endif // MAIN_H_
