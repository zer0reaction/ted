#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <locale.h>

#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

#include "scuffed.h"

// terminal functions
void term_set_char(utf8_char_t c, u16 row_i, u16 col_i);
void term_clear(u16 term_width, u16 term_height);
void term_display(u16 term_width, u16 term_height);
void term_move_cursor(u16 row, u16 col);
void render(buffer_t *b, u16 term_width, u16 term_height);

// helper functions
u32 get_cursor_row(buffer_t *b);
u32 update_row_offset(buffer_t *b);
u32 update_last_visual_col(buffer_t *b);
u8 utf8_byte_size(char c);
void set_cursor_col_after_vertical_move(buffer_t *b, line_t next_line);

// lines functions
u32 lines_tokenize(lines_t *line_tokens, const sb_t sb);

// buffer functions
u32 buffer_from_file(buffer_t *b, const char *path);
void buffer_save(buffer_t *b);
void buffer_kill(buffer_t *b);

// editor functions
void move_down(buffer_t *b);
void move_up(buffer_t *b);
void move_right(buffer_t *b);
void move_left(buffer_t *b);
void move_down_page(buffer_t *b);
void move_up_page(buffer_t *b);
void center_cursor_line(buffer_t *b);
void insert_char_at_cursor(buffer_t *b, char c);
void backspace(buffer_t *b);

utf8_char_t display_buffer[MAX_HEIGHT][MAX_WIDTH] = {0};
bool dirty_buffer[MAX_HEIGHT][MAX_WIDTH] = {0};

int main(int argc, char **argv)
{
    SC_ASSERT(sizeof(u8) == 1);
    SC_ASSERT(sizeof(s8) == 1);
    SC_ASSERT(sizeof(u16) == 2);
    SC_ASSERT(sizeof(s16) == 2);
    SC_ASSERT(sizeof(u32) == 4);
    SC_ASSERT(sizeof(s32) == 4);
    SC_ASSERT(sizeof(u64) == 8);
    SC_ASSERT(sizeof(s64) == 8);

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

    struct termios original_settings = {0};
    SC_ASSERT(tcgetattr(STDIN_FILENO, &original_settings) != -1);

    struct termios raw_mode = original_settings;
    raw_mode.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw_mode.c_iflag &= ~(IXON) | ICRNL;
    raw_mode.c_oflag &= ~(OPOST);
    raw_mode.c_cc[VMIN] = 1;
    raw_mode.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_mode);

    // TODO handle window resize
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)) {
        printf("ioctl failed\n");
        return 1;
    }
    u16 term_width = ws.ws_col;
    u16 term_height = ws.ws_row;
    b.contents_width = term_width;
    b.contents_height = term_height - 1;

    if (term_width > MAX_WIDTH || term_height > MAX_HEIGHT) {
        printf("terminal resolution is too high\n");
        return 1;
    }

    printf("\033[?1049h"); // enable alternative buffer

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
            case 's':
                buffer_save(&b);
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
            case 'n':
                move_down_page(&b);
                break;
            case 'p':
                move_up_page(&b);
                break;
            case 'f':
                center_cursor_line(&b);
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

// #########################################################################
// Render functions
// #########################################################################

void term_set_char(utf8_char_t c, u16 row_i, u16 col_i)
{
    if (display_buffer[row_i][col_i].abs == c.abs) return;

    display_buffer[row_i][col_i] = c;
    dirty_buffer[row_i][col_i] = true;
}

void term_clear(u16 term_width, u16 term_height)
{
    for (u16 row_i = 0; row_i < term_height; ++row_i) {
        for (u16 col_i = 0; col_i < term_width; ++col_i) {
            term_set_char((utf8_char_t){ .abs = 0 }, row_i, col_i);
        }
    }
}

void term_display(u16 term_width, u16 term_height)
{
    for (u16 row_i = 0; row_i < term_height; ++row_i) {
        for (u16 col_i = 0; col_i < term_width; ++col_i) {
            if (!dirty_buffer[row_i][col_i]) continue;

            // TODO DSA mode?
            term_move_cursor(row_i + 1, col_i + 1);

            if (display_buffer[row_i][col_i].abs == 0) {
                putchar(' ');
            } else {
                printf("%s", display_buffer[row_i][col_i].arr);
            }
        }
    }
}

// row and col start with 1
void term_move_cursor(u16 row, u16 col)
{
    printf("\033[%d;%dH", row, col);
}

void render(buffer_t *b, u16 term_width, u16 term_height)
{
    term_clear(term_width, term_height);

    u16 cursor_visual_col = 1;
    u32 cursor_row = update_row_offset(b);

    for (u32 row_i = 0; row_i + 1 < term_height; ++row_i) {
        utf8_char_t c = {0};

        if (b->row_offset + row_i >= b->line_tokens.size) {
            c.arr[0] = '~';
            term_set_char(c, row_i, 0);
            continue;
        }

        line_t line = b->line_tokens.items[b->row_offset + row_i];
        u16 col_i = 0;

        for (u32 char_i = 0; char_i < line.end - line.begin;) {
            u8 size = utf8_byte_size(b->data.items[line.begin + char_i]);

            c.abs = 0;
            memcpy(c.arr, &b->data.items[line.begin + char_i], size);
            term_set_char(c, row_i, col_i);

            col_i++;
            char_i += size;
        }

        if (b->row_offset + row_i == cursor_row) {
            for (u32 k = line.begin; k < b->cursor;) {
                cursor_visual_col++;
                k += utf8_byte_size(b->data.items[k]);
            }
        }
    }

    char status[TEMP_BUF_SIZE] = {0};

    if (!b->saved) {
        strcat(status, "*");
    }

    strncat(status, b->path.items, b->path.size);
    sprintf(&status[strlen(status)], ":%u:%u",
            cursor_row + 1, cursor_visual_col);

    if (b->mode == INSERT_MODE) {
        strcat(status, " [insert]");
    }

    utf8_char_t c = {0};
    u16 col_i = 0;

    for (u16 i = 0; i < strlen(status) && i < term_width;) {
        u8 size = utf8_byte_size(status[i]);

        c.abs = 0;
        memcpy(c.arr, &status[i], size);

        term_set_char(c, term_height - 1, col_i);
        i += size;
        col_i++;
    }

    printf("\033[?25l"); // hide cursor
    term_display(term_width, term_height);
    printf("\033[?25h"); // show cursor

    term_move_cursor(cursor_row - b->row_offset + 1, cursor_visual_col);
    fflush(stdout);
}

// #########################################################################
// Utility functions
// #########################################################################

u32 get_cursor_row(buffer_t *b)
{
    for (u32 i = 0; i < b->line_tokens.size; ++i) {
        line_t line = b->line_tokens.items[i];
        if (b->cursor >= line.begin && b->cursor <= line.end) {
            return i;
        }
    }
    SC_ASSERT(0 && "unreachable");
}

u32 update_row_offset(buffer_t *b)
{
    s32 absolute_row = get_cursor_row(b);
    s32 relative_row = absolute_row - b->row_offset;

    if (relative_row < 0) {
        b->row_offset += relative_row;
    } else if (relative_row + 1 > b->contents_height) {
        b->row_offset += relative_row - (b->contents_height - 1);
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

    SC_ASSERT(0 && "unreachable");
}

void set_cursor_col_after_vertical_move(buffer_t *b, line_t next_line)
{
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

// #########################################################################
// Lines functions
// #########################################################################

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

// #########################################################################
// Buffer functions
// #########################################################################

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

    sb_append_cstr(&b->path, path);
    b->saved = true;

    fclose(fp);
    return b->line_tokens.size;
}

void buffer_save(buffer_t *b)
{
    char path[TEMP_BUF_SIZE] = {0};
    strncpy(path, b->path.items, b->path.size);

    FILE *fp = fopen(path, "w");
    SC_ASSERT(fp);

    fwrite(b->data.items, 1, b->data.size, fp);

    fclose(fp);
    b->saved = true;
}

void buffer_kill(buffer_t *b)
{
    sb_free(&b->data);
    sb_free(&b->path);
    lines_free(&b->line_tokens);
    memset(b, 0, sizeof(buffer_t));
}

// #########################################################################
// Editor functions
// #########################################################################

void move_down(buffer_t *b)
{
    u32 cursor_row = get_cursor_row(b);
    if (cursor_row + 1 == b->line_tokens.size) return;

    line_t next_line = b->line_tokens.items[cursor_row + 1];
    set_cursor_col_after_vertical_move(b, next_line);
}

void move_up(buffer_t *b)
{
    u32 cursor_row = get_cursor_row(b);
    if (cursor_row == 0) return;

    line_t next_line = b->line_tokens.items[cursor_row - 1];
    set_cursor_col_after_vertical_move(b, next_line);
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

void move_down_page(buffer_t *b)
{
    u32 cursor_row = get_cursor_row(b);

    line_t next_line = {0};
    if (cursor_row + b->contents_height / 2 >= b->line_tokens.size) {
        next_line = b->line_tokens.items[b->line_tokens.size - 1];
    } else {
        next_line = b->line_tokens.items[cursor_row + b->contents_height / 2];
    }

    set_cursor_col_after_vertical_move(b, next_line);
}

void move_up_page(buffer_t *b)
{
    u32 cursor_row = get_cursor_row(b);

    line_t next_line = {0};
    if (cursor_row < b->contents_height / 2) {
        next_line = b->line_tokens.items[0];
    } else {
        next_line = b->line_tokens.items[cursor_row - b->contents_height / 2];
    }

    set_cursor_col_after_vertical_move(b, next_line);
}

void center_cursor_line(buffer_t *b)
{
    u32 cursor_row = get_cursor_row(b);

    if (cursor_row <= b->contents_height / 2) {
        b->row_offset = 0;
    } else {
        b->row_offset = cursor_row - b->contents_height / 2;
    }
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

        b->saved = false;
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

    b->saved = false;
    lines_tokenize(&b->line_tokens, b->data);
    update_last_visual_col(b);
}
