#include <term.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <locale.h>
#include <unistd.h>

#include "main.h"

int main(int argc, char **argv)
{
    assert(sizeof(u8) == 1);
    assert(sizeof(s8) == 1);
    assert(sizeof(u16) == 2);
    assert(sizeof(s16) == 2);
    assert(sizeof(u32) == 4);
    assert(sizeof(s32) == 4);
    assert(sizeof(u64) == 8);
    assert(sizeof(s64) == 8);

    setlocale(LC_ALL, "en_US.utf-8");

    if (argc != 2) {
        printf("specify a file\n");
        return 1;
    }

    buffer_t b = {0};
    if (buffer_from_file(&b, argv[1]) == 0) {
        printf("no file found\n");
        return 1;
    }

    char *term = getenv("TERM");
    if (!term) {
        printf("TERM environment variable not found\n");
        return 1;
    }

    int ret = tgetent(NULL, term);
    if (ret != 1) {
        printf("terminal is not in terminfo database\n");
        return 2;
    }

    struct termios original_settings = {0};
    assert(tcgetattr(STDIN_FILENO, &original_settings) != -1);

    struct termios raw_mode = original_settings;
    raw_mode.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw_mode.c_iflag &= ~(IXON) | ICRNL;
    raw_mode.c_oflag &= ~(OPOST);
    raw_mode.c_cc[VMIN] = 1;
    raw_mode.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_mode);

    printf("\033[?1049h"); // enable alternative buffer

    s16 term_width = tgetnum("co");
    s16 term_height = tgetnum("li");
    assert(term_width != -1 && term_height != -1);

    bool should_close = false;
    while (!should_close) {
        render(&b, term_width, term_height);

        char c;
        if (read(STDIN_FILENO, &c, 1) != 1) break;

        if (b.mode == NORMAL_MODE) {
            switch (c) {
            case 'q':
                should_close = true;
                break;
            case 'i':
                b.mode = INSERT_MODE;
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
        } else if (b.mode == INSERT_MODE) {
            switch (c) {
            case 033:
                b.mode = NORMAL_MODE;
                break;
            case 127: // backspace
                backspace(&b);
                break;
            default:
                insert_char_at_cursor(&b, c);
            }
        }
    }

    buffer_kill(&b);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_settings);
    printf("\033[?1049l"); // disable alternative buffer
    return 0;
}

// render functions

// row and col start with 1
void term_move_cursor(u16 row, u16 col)
{
    printf("\033[%d;%dH", row, col);
}

void render(buffer_t *b, u16 term_width, u16 term_height)
{
    printf("\033[?25l"); // hide cursor
    printf("\033[2J"); // erase screen (bad)

    u16 cursor_visual_col = 1;
    u32 cursor_row = update_row_offset(b, term_height);

    for (u32 row_i = 0; row_i < term_height; ++row_i) {
        if (b->row_offset + row_i >= b->line_tokens.size) break;

        line_t line = b->line_tokens.items[b->row_offset + row_i];
        u16 y = 1;

        term_move_cursor(row_i + 1, 1);

        for (u32 char_i = 0; char_i < line.end - line.begin;) {
            u8 size = utf8_byte_size(b->data.items[line.begin + char_i]);
            for (u8 i = 0; i < size; ++i) {
                putchar(b->data.items[line.begin + char_i + i]);
            }

            y++;
            char_i += size;
        }

        for (; y <= term_width; ++y) {
            putchar(' ');
        }

        if (b->row_offset + row_i == cursor_row) {
            for (u32 k = line.begin; k < b->cursor;) {
                cursor_visual_col++;
                k += utf8_byte_size(b->data.items[k]);
            }
        }
    }

    printf("\033[?25h"); // show cursor
    term_move_cursor(cursor_row - b->row_offset + 1, cursor_visual_col);
    fflush(stdout);
}

// utility functions

u32 get_cursor_row(buffer_t *b)
{
    for (u32 i = 0; i < b->line_tokens.size; ++i) {
        line_t line = b->line_tokens.items[i];
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

u32 update_last_visual_col(buffer_t *b)
{
    u32 cursor_row = get_cursor_row(b);
    line_t cursor_line = b->line_tokens.items[cursor_row];

    b->last_visual_col = 0;
    for (u32 i = cursor_line.begin; i < b->cursor; ) {
        b->last_visual_col++;
        i += utf8_byte_size(b->data.items[i]);
    }

    return cursor_row;
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

u32 lines_tokenize(lines_t *line_tokens, const sb_t sb)
{
    line_tokens->size = 0;

    line_t line = {0};

    for (u32 i = 0; i < sb.size; ++i) {
        if (sb.items[i] == '\n') {
            line.end = i;
            lines_append(line_tokens, line);
            line.begin = i + 1;
            line.end = 0;
        }
    }
    line.end = sb.size;
    lines_append(line_tokens, line);

    return line_tokens->size;
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

    lines_tokenize(&b->line_tokens, b->data);

    fclose(fp);
    return b->line_tokens.size;
}

void buffer_kill(buffer_t *b)
{
    sb_free(&b->data);
    lines_free(&b->line_tokens);
    memset(b, 0, sizeof(buffer_t));
}

// editor functions

void move_down(buffer_t *b)
{
    u32 cursor_row = get_cursor_row(b);

    if (cursor_row + 1 == b->line_tokens.size) return;

    line_t next_line = b->line_tokens.items[cursor_row + 1];

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

void move_up(buffer_t *b)
{
    u32 cursor_row = get_cursor_row(b);

    if (cursor_row == 0) return;

    line_t next_line = b->line_tokens.items[cursor_row - 1];

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

void move_right(buffer_t *b)
{
    if (b->cursor == b->data.size) return;

    b->cursor += utf8_byte_size(b->data.items[b->cursor]);

    update_last_visual_col(b);
}

void move_left(buffer_t *b)
{
    if (b->cursor == 0) return;

    b->cursor--;
    while (utf8_byte_size(b->data.items[b->cursor]) == 0) {
        b->cursor--;
    }

    update_last_visual_col(b);
}

void insert_char_at_cursor(buffer_t *b, char c)
{
    static u8 size = 0;
    static u8 accum = 0;
    static char buf[4] = {0};

    if (utf8_byte_size(c) > 0) {
        size = utf8_byte_size(c);
        accum = 0;
        memset(buf, 0, 4);
    }

    buf[accum++] = c;

    if (accum == size && accum != 0) {
        sb_insert_buf(&b->data, buf, size, b->cursor);

        b->cursor += size;

        if (buf[0] == '\n') {
            b->last_visual_col = 0;
        } else {
            b->last_visual_col += 1;
        }

        lines_tokenize(&b->line_tokens, b->data);
    }
}

void backspace(buffer_t *b)
{
    if (b->cursor == 0) return;

    b->cursor--;
    while (utf8_byte_size(b->data.items[b->cursor]) == 0) {
        b->cursor--;
    }

    u8 size = utf8_byte_size(b->data.items[b->cursor]);
    sb_delete_substr(&b->data, b->cursor, size);

    lines_tokenize(&b->line_tokens, b->data);
    update_last_visual_col(b);
}
