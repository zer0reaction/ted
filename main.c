#define _XOPEN_SOURCE_EXTENDED

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <locale.h>

#include "main.h"

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
        case 'l':
            move_right(&b);
            break;
        case 'h':
            move_left(&b);
            break;
        }
    }

    endwin();
    buffer_kill(&b);
    return 0;
}

// utility functions

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

u8 utf8_byte_size(char c)
{
    u8 byte = (u8)c;

    if      (byte <= 127) return 1;
    else if (byte >= 128 && byte <= 191) return 0;
    else if (byte >= 192 && byte <= 223) return 2;
    else if (byte >= 224 && byte <= 239) return 3;
    else if (byte >= 240 && byte <= 247) return 4;

    assert(0 && "unreachable");
}

// lines functions

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

// buffer functions

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

// editor functions

void move_down(buffer_t *b)
{
    u32 cursor_row = get_cursor_row(b);

    if (cursor_row + 1 < b->lines.size) {
        line_t next_line = b->lines.items[cursor_row + 1];

        u32 next_line_visual_len = 0;
        for (u32 i = next_line.begin; i < next_line.end; ) {
            next_line_visual_len += 1;
            i += utf8_byte_size(b->data.items[i]);
        }

        if (b->last_visual_col > next_line_visual_len) {
            b->cursor = next_line.end;
        } else {
            b->cursor = next_line.begin;
            for (u32 i = 0; i < b->last_visual_col; ++i) {
                b->cursor += utf8_byte_size(b->data.items[b->cursor]);
            }
        }
    }
}

void move_up(buffer_t *b) {
    u32 cursor_row = get_cursor_row(b);

    if (cursor_row > 0) {
        line_t next_line = b->lines.items[cursor_row - 1];

        u32 next_line_visual_len = 0;
        for (u32 i = next_line.begin; i < next_line.end; ) {
            next_line_visual_len += 1;
            i += utf8_byte_size(b->data.items[i]);
        }

        if (b->last_visual_col > next_line_visual_len) {
            b->cursor = next_line.end;
        } else {
            b->cursor = next_line.begin;
            for (u32 i = 0; i < b->last_visual_col; ++i) {
                b->cursor += utf8_byte_size(b->data.items[b->cursor]);
            }
        }
    }
}

void move_right(buffer_t *b)
{
    if (b->cursor < b->data.size) {
        b->cursor += utf8_byte_size(b->data.items[b->cursor]);

        u32 cursor_row = get_cursor_row(b);
        line_t cursor_line = b->lines.items[cursor_row];

        b->last_visual_col = 0;
        for (u32 i = cursor_line.begin; i < b->cursor; ) {
            b->last_visual_col++;
            i += utf8_byte_size(b->data.items[i]);
        }
    }
}

void move_left(buffer_t *b)
{
    if (b->cursor > 0) {
        b->cursor--;
        while (utf8_byte_size(b->data.items[b->cursor]) == 0) {
            b->cursor--;
        }

        u32 cursor_row = get_cursor_row(b);
        line_t cursor_line = b->lines.items[cursor_row];

        b->last_visual_col = 0;
        for (u32 i = cursor_line.begin; i < b->cursor; ) {
            b->last_visual_col++;
            i += utf8_byte_size(b->data.items[i]);
        }
    }
}

// render functions

void render(buffer_t *b)
{
    u32 cursor_row = update_row_offset(b, LINES);

    erase();
    curs_set(0);

    u16 cursor_visual_col = 0;

    for (u32 i = 0; i < (u32)LINES; ++i) {
        if (i + b->row_offset >= b->lines.size) break;

        line_t line = b->lines.items[i + b->row_offset];
        u16 y = 0;
        for (u32 j = 0; j < line.end - line.begin; ) {
            wchar_t wch[2] = {0};
            int ret = mbtowc(wch, &b->data.items[line.begin + j], 4);

            assert(ret != -1);
            mvaddwstr(i, y, wch);

            if (i + b->row_offset == (u32)cursor_row &&
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
