#define _XOPEN_SOURCE_EXTENDED

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <locale.h>

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
} buffer_t;


u32 get_cursor_row(buffer_t *b);
u32 update_row_offset(buffer_t *b, u16 height);

u32 lines_tokenize(lines_t *lines, const sb_t sb);

u32 buffer_from_file(buffer_t *b, const char *path);
void buffer_kill(buffer_t *b);

void move_down(buffer_t *b);
void move_up(buffer_t *b);

void render(buffer_t *b);

u32 get_cursor_row(buffer_t *b)
{
    for (u32 i = 0; i < b->lines.size; ++i) {
        line_t line = b->lines.items[i];
        if (b->cursor >= line.begin && b->cursor <= line.end) {
            return i;
        }
    }
    assert(0 && "unreachable");
}

u32 update_row_offset(buffer_t *b, u16 height)
{
    s32 absolute_row = get_cursor_row(b);
    s32 relative_row = absolute_row - b->row_offset;

    if (relative_row < 0) {
        b->row_offset += relative_row;
    } else if (relative_row > height - 1) {
        b->row_offset += relative_row - (height - 1);
    }

    return absolute_row;
}

u32 lines_tokenize(lines_t *lines, const sb_t sb)
{
    lines->size = 0;

    line_t line = {0};

    for (u32 i = 0; i < sb.size; ++i) {
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

u32 buffer_from_file(buffer_t *b, const char *path)
{
    memset(b, 0, sizeof(buffer_t));

    FILE *fp = fopen(path, "r");
    if (fp == NULL) return 0;

    fseek(fp, 0, SEEK_END);
    u32 file_size = ftell(fp);
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

void move_down(buffer_t *b)
{
    u32 cursor_row = get_cursor_row(b);
    if (cursor_row + 1 < b->lines.size) {
        line_t next_line = b->lines.items[cursor_row + 1];
        b->cursor = next_line.begin;
    }
}

void move_up(buffer_t *b) {
    u32 cursor_row = get_cursor_row(b);
    if (cursor_row > 0) {
        line_t prev_line = b->lines.items[cursor_row - 1];
        b->cursor = prev_line.begin;
    }
}

void render(buffer_t *b)
{
    u32 cursor_row = update_row_offset(b, LINES);

    erase();
    curs_set(0);

    u16 cursor_visual_col = 0;

    for (u32 i = 0; i < (u32)LINES && i < b->data.size; ++i) {
        line_t line = b->lines.items[i + b->row_offset];
        u16 y = 0;
        for (u32 j = 0; j < line.end - line.begin; ) {
            wchar_t wch[2] = {0};
            int ret = mbtowc(wch, &b->data.items[line.begin + j], 4);

            assert(ret != -1);
            mvaddwstr(i, y, wch);

            if (i == (u32)cursor_row &&
                line.begin + j < b->cursor)
            {
                cursor_visual_col++;
            }
            y++;
            j += ret;
        }
    }

    move(cursor_row - b->row_offset, cursor_visual_col);

    curs_set(1);
    refresh();
}

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "en_US.utf-8");

    if (argc != 2) return 1;

    buffer_t b = {0};
    if (buffer_from_file(&b, argv[1]) == 0) return 1;

    initscr();
    noecho();
    nl();
    cbreak();

    bool should_close = false;
    while (!should_close) {
        render(&b);

        int c = getch();

        switch (c) {
        case 'q':
            should_close = true;
            break;
        case 'j':
            move_down(&b);
            break;
        case 'k':
            move_up(&b);
            break;
        }
    }

    endwin();
    buffer_kill(&b);
    return 0;
}
