#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

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
    for (size_t i = 0; i < (n); ++i) {                           \
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
    size_t len = strlen(s);       \
    da_append_many((sb), s, len); \
} while (0)

typedef struct line_t {
    size_t begin;
    size_t end;
} line_t;

typedef struct lines_t {
    line_t *items;
    size_t size;
    size_t cap;
} lines_t;

typedef struct sb_t {
    char *items;
    size_t size;
    size_t cap;
} sb_t;

typedef struct buffer_t {
    sb_t data;
    lines_t lines;
} buffer_t;

size_t lines_tokenize(lines_t    *lines,
                      const sb_t  sb);
size_t buffer_from_file(buffer_t   *b,
                        const char *path);
void buffer_kill(buffer_t *b);

size_t lines_tokenize(lines_t    *lines,
                      const sb_t  sb)
{
    lines->size = 0;

    line_t line = {0};

    for (size_t i = 0; i < sb.size; ++i) {
        if (sb.items[i] == '\n') {
            line.end = i;
            lines_append(lines, line);
            line.begin = i + 1;
            line.end = 0;
        }
    }
    line.end = sb.size;
    lines_append(lines, line);

    return lines->size;
}

size_t buffer_from_file(buffer_t   *b,
                        const char *path)
{
    memset(b, 0, sizeof(buffer_t));

    FILE *fp = fopen(path, "r");
    if (fp == NULL) return 0;

    fseek(fp, 0, SEEK_END);
    size_t file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    b->data.items = malloc(file_size);
    b->data.size = file_size;
    b->data.cap = file_size;
    fread(b->data.items, 1, file_size, fp);

    lines_tokenize(&b->lines, b->data);

    fclose(fp);
    return b->lines.size;
}

void buffer_kill(buffer_t *b)
{
    sb_free(&b->data);
    lines_free(&b->lines);
    memset(b, 0, sizeof(buffer_t));
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;

    buffer_t b = {0};
    size_t lines_read = buffer_from_file(&b, argv[1]);

    printf("lines read: %lu\n", lines_read);

/*
    printf("size: %lu, cap: %lu\n", b.lines.size, b.lines.cap);
    for (size_t i = 0; i < b.lines.size; ++i) {
        line_t line = b.lines.items[i];
        printf("begin: %lu, end: %lu: ", line.begin,
                                         line.end);

        for (size_t j = line.begin; j < line.end; ++j) {
            putchar(b.data.items[j]);
        }
        putchar('\n');
    }
*/

    printf("sb size: %lu, sb cap: %lu\n", b.data.size, b.data.cap);

    buffer_kill(&b);
    return 0;
}
