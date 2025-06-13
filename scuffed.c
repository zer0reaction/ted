#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <locale.h>

#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

#include "da.h"

// #########################################################################
// Editor settings
// #########################################################################

#define INDENT_SPACES 4

// #########################################################################
// Constants
// #########################################################################

#define MAX_WIDTH 256
#define MAX_HEIGHT 256
#define TEMP_BUF_SIZE 1024

// #########################################################################
// Utility macros
// #########################################################################

#define UTF8_BYTE_SIZE(c) (assert((u8)(c) <= 247), utf8_cache[(u8)(c)])

#define TERM_SET_CHAR(c, row_i, col_i)              \
if (display_buffer[row_i][col_i].abs != (c).abs) {  \
    display_buffer[row_i][col_i] = c;               \
    dirty_buffer[row_i][col_i] = true;              \
}

#define TERM_MOVE_CURSOR(row, col) printf("\033[%d;%dH", row, col)

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

typedef enum buffer_mode_t {
    NORMAL_MODE = 0,
    INSERT_MODE = 1
} buffer_mode_t;

DA_TYPEDEF(char, sb_t)
DA_TYPEDEF(line_t, lines_t)

typedef struct buffer_t {
    sb_t data;
    sb_t path;
    lines_t lines;

    buffer_mode_t mode;

    u32 cursor;
    u32 row_offset;
    u32 last_visual_col;

    u16 contents_width;
    u16 contents_height;

    bool saved;
} buffer_t;

typedef union utf8_char_t {
    char arr[5]; // 5th for null byte for printf
    u32 abs;
} utf8_char_t;

// #########################################################################
// Render functions
// #########################################################################

void term_clear(u16 term_width, u16 term_height);
void term_display(u16 term_width, u16 term_height);
void render(buffer_t *b, u16 term_width, u16 term_height);

// #########################################################################
// Utility functions
// #########################################################################

u32 get_cursor_row(buffer_t *b);
u32 update_row_offset(buffer_t *b);
u32 update_last_visual_col(buffer_t *b);
void cache_utf8_byte_size(void);
void set_cursor_col_after_vertical_move(buffer_t *b, line_t next_line);

// #########################################################################
// Lines functions
// #########################################################################

u32 tokenize_lines(lines_t *lines, sb_t *sb);

// #########################################################################
// Buffer functions
// #########################################################################

u32 buffer_create_from_file(buffer_t *b, const char *path);
void buffer_save(buffer_t *b);
void buffer_kill(buffer_t *b);

// #########################################################################
// Editor functions
// #########################################################################

void move_down(buffer_t *b);
void move_up(buffer_t *b);
void move_right(buffer_t *b);
void move_left(buffer_t *b);
void move_down_page(buffer_t *b);
void move_up_page(buffer_t *b);
void move_line_first_char(buffer_t *b);
void move_line_begin(buffer_t *b);
void move_line_end(buffer_t *b);
void move_top(buffer_t *b);
void move_bottom(buffer_t *b);
void center_cursor_line(buffer_t *b);
void insert_char_at_cursor(buffer_t *b, char c);
void insert_indent_spaces_at_cursor(buffer_t *b);
void backspace(buffer_t *b);

u8 utf8_cache[256] = {0};
utf8_char_t display_buffer[MAX_HEIGHT][MAX_WIDTH] = {0};
bool dirty_buffer[MAX_HEIGHT][MAX_WIDTH] = {0};

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
    if (buffer_create_from_file(&b, argv[1]) == 0) {
        printf("no file found\n");
        return 1;
    }

    cache_utf8_byte_size();

    char stdout_buf[1024 * 256] = {0};
    setvbuf(stdout, stdout_buf, _IOFBF, 1024 * 256);

    struct termios original_settings = {0};
    assert(tcgetattr(STDIN_FILENO, &original_settings) != -1);

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

    memset(dirty_buffer, 1, MAX_WIDTH * MAX_HEIGHT);

    bool should_close = false;
    while (!should_close) {
        render(&b, term_width, term_height);

        char c;
        if (read(STDIN_FILENO, &c, 1) != 1) break;

        if (b.mode == NORMAL_MODE) {
            switch (c) {
            // basic commands
            case 'q':
                should_close = true;
                break;
            case 's':
                buffer_save(&b);
                break;

            // enterning insert mode
            case 'i':
                b.mode = INSERT_MODE;
                break;
            case 'A':
                move_line_end(&b);
                b.mode = INSERT_MODE;
                break;
            case 'I':
                move_line_begin(&b);
                b.mode = INSERT_MODE;
                break;

            // movement
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
                center_cursor_line(&b);
                break;
            case 'p':
                move_up_page(&b);
                center_cursor_line(&b);
                break;
            case '0':
                move_line_first_char(&b);
                break;
            case '^':
                move_line_begin(&b);
                break;
            case '$':
                move_line_end(&b);
                break;
            case 'g':
                move_top(&b);
                break;
            case 'G':
                move_bottom(&b);
                break;

            // screen operations
            case 'f':
                center_cursor_line(&b);
                break;

            // TODO skip rendering on 'default'
            }
        } else if (b.mode == INSERT_MODE) {
            switch (c) {
            case 033:
                b.mode = NORMAL_MODE;
                break;
            case 127: // backspace
                backspace(&b);
                break;
            case '\t':
                insert_indent_spaces_at_cursor(&b);
                break;
            default:
                insert_char_at_cursor(&b, c);
            }
        }
    }

    buffer_kill(&b);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_settings);
    TERM_MOVE_CURSOR(term_height, 1);
    printf("\033[2K"); // erase entire line
    return 0;
}

// #########################################################################
// Render functions
// #########################################################################

void term_clear(u16 term_width, u16 term_height)
{
    for (u16 row_i = 0; row_i < term_height; ++row_i) {
        for (u16 col_i = 0; col_i < term_width; ++col_i) {
            TERM_SET_CHAR((utf8_char_t){ .abs = 0 }, row_i, col_i);
        }
    }
}

void term_display(u16 term_width, u16 term_height)
{
    u16 row = 1;

    for (u16 row_i = 0; row_i < term_height; ++row_i) {
        u16 col = 1;
        TERM_MOVE_CURSOR(row, col);

        for (u16 col_i = 0; col_i < term_width; ++col_i) {
            if (!dirty_buffer[row_i][col_i]) continue;

            if (row != row_i + 1 || col != col_i + 1) {
                TERM_MOVE_CURSOR(row_i + 1, col_i + 1);
                row = row_i + 1;
                col = col_i + 1;
            }

            if (display_buffer[row_i][col_i].abs == 0)
                putchar(' ');
            else
                printf("%s", display_buffer[row_i][col_i].arr);

            dirty_buffer[row_i][col_i] = false;
            col++;
        }
        row++;
    }
}

void render(buffer_t *b, u16 term_width, u16 term_height)
{
    term_clear(term_width, term_height);

    u16 cursor_visual_col = 1;
    u32 cursor_row = update_row_offset(b);

    utf8_char_t c = {0};

    for (u32 row_i = 0; row_i + 1 < term_height; ++row_i) {
        if (b->row_offset + row_i >= b->lines.size) {
            c.abs = 0;
            c.arr[0] = '~';
            TERM_SET_CHAR(c, row_i, 0);
            continue;
        }

        line_t line = lines_t_at(&b->lines, b->row_offset + row_i);
        u16 col_i = 0;

        for (u32 char_i = 0; char_i < line.end - line.begin;) {
            u8 size = UTF8_BYTE_SIZE(sb_t_at(&b->data, line.begin + char_i));

            c.abs = 0;
            memcpy(c.arr, &b->data.data[line.begin + char_i], size);
            TERM_SET_CHAR(c, row_i, col_i);

            col_i++;
            char_i += size;
        }

        if (b->row_offset + row_i == cursor_row) {
            for (u32 k = line.begin; k < b->cursor;) {
                cursor_visual_col++;
                k += UTF8_BYTE_SIZE(sb_t_at(&b->data, k));
            }
        }
    }

    char status[TEMP_BUF_SIZE] = {0};

    if (!b->saved) {
        strcat(status, "*");
    }

    strncat(status, b->path.data, b->path.size);
    sprintf(&status[strlen(status)], ":%u:%u",
            cursor_row + 1, cursor_visual_col);

    if (b->mode == INSERT_MODE) {
        strcat(status, " [insert]");
    }

    u16 col_i = 0;

    for (u16 i = 0; i < strlen(status) && i < term_width;) {
        u8 size = UTF8_BYTE_SIZE(status[i]);

        c.abs = 0;
        memcpy(c.arr, &status[i], size);

        TERM_SET_CHAR(c, term_height - 1, col_i);
        i += size;
        col_i++;
    }

    printf("\033[?25l"); // hide cursor
    term_display(term_width, term_height);
    printf("\033[?25h"); // show cursor

    TERM_MOVE_CURSOR(cursor_row - b->row_offset + 1, cursor_visual_col);
    fflush(stdout);
}

// #########################################################################
// Utility functions
// #########################################################################

u32 get_cursor_row(buffer_t *b)
{
    for (u32 i = 0; i < b->lines.size; ++i) {
        line_t line = lines_t_at(&b->lines, i);
        if (b->cursor >= line.begin && b->cursor <= line.end) {
            return i;
        }
    }
    assert(0 && "unreachable");
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
    line_t cursor_line = lines_t_at(&b->lines, cursor_row);

    b->last_visual_col = 0;
    for (u32 i = cursor_line.begin; i < b->cursor; ) {
        b->last_visual_col++;
        i += UTF8_BYTE_SIZE(sb_t_at(&b->data, i));
    }

    return cursor_row;
}

void cache_utf8_byte_size(void)
{
    for (u32 i = 0; i < 256; ++i) {
        if      (i <= 127)             utf8_cache[i] =  1;
        else if (i >= 128 && i <= 191) utf8_cache[i] =  0;
        else if (i >= 192 && i <= 223) utf8_cache[i] =  2;
        else if (i >= 224 && i <= 239) utf8_cache[i] =  3;
        else if (i >= 240 && i <= 247) utf8_cache[i] =  4;
        // > 247 should be handled in macro
    }
}

void set_cursor_col_after_vertical_move(buffer_t *b, line_t next_line)
{
    u32 next_line_visual_len = 0;
    for (u32 i = next_line.begin; i < next_line.end; ) {
        next_line_visual_len += 1;
        i += UTF8_BYTE_SIZE(sb_t_at(&b->data, i));
    }

    if (b->last_visual_col > next_line_visual_len) {
        b->cursor = next_line.end;
    } else {
        b->cursor = next_line.begin;
        for (u32 i = 0; i < b->last_visual_col; ++i) {
            b->cursor += UTF8_BYTE_SIZE(sb_t_at(&b->data, b->cursor));
        }
    }
}

// #########################################################################
// Lines functions
// #########################################################################

u32 tokenize_lines(lines_t *lines, sb_t *sb)
{
    lines->size = 0;

    line_t line = {0};

    for (u32 i = 0; i < sb->size; ++i) {
        if (sb_t_at(sb, i) == '\n') {
            line.end = i;
            lines_t_push_back(lines, line);
            line.begin = i + 1;
            line.end = 0;
        }
    }
    line.end = sb->size;
    lines_t_push_back(lines, line);

    // TODO resize if needed

    return lines->size;
}

// #########################################################################
// Buffer functions
// #########################################################################

u32 buffer_create_from_file(buffer_t *b, const char *path)
{
    memset(b, 0, sizeof(buffer_t));

    FILE *fp = fopen(path, "r");
    if (fp == NULL) return 0;

    fseek(fp, 0, SEEK_END);
    u32 file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    b->data.data = malloc(file_size);
    b->data.size = file_size;
    b->data.cap = file_size;
    fread(b->data.data, 1, file_size, fp);

    b->lines = lines_t_create();
    tokenize_lines(&b->lines, &b->data);

    b->path = sb_t_create();
    sb_t_push_back_many(&b->path, path, strlen(path));

    b->saved = true;

    fclose(fp);
    return b->lines.size;
}

void buffer_save(buffer_t *b)
{
    char path[TEMP_BUF_SIZE] = {0};
    strncpy(path, b->path.data, b->path.size);

    FILE *fp = fopen(path, "w");
    assert(fp);

    fwrite(b->data.data, 1, b->data.size, fp);

    fclose(fp);
    b->saved = true;
}

void buffer_kill(buffer_t *b)
{
    sb_t_destroy(&b->data);
    sb_t_destroy(&b->path);
    lines_t_destroy(&b->lines);
    memset(b, 0, sizeof(buffer_t));
}

// #########################################################################
// Editor functions
// #########################################################################

void move_down(buffer_t *b)
{
    u32 cursor_row = get_cursor_row(b);
    if (cursor_row + 1 == b->lines.size) return;

    line_t next_line = lines_t_at(&b->lines, cursor_row + 1);
    set_cursor_col_after_vertical_move(b, next_line);
}

void move_up(buffer_t *b)
{
    u32 cursor_row = get_cursor_row(b);
    if (cursor_row == 0) return;

    line_t next_line = lines_t_at(&b->lines, cursor_row - 1);
    set_cursor_col_after_vertical_move(b, next_line);
}

void move_right(buffer_t *b)
{
    if (b->cursor == b->data.size) return;

    b->cursor += UTF8_BYTE_SIZE(sb_t_at(&b->data, b->cursor));
    update_last_visual_col(b);
}

void move_left(buffer_t *b)
{
    if (b->cursor == 0) return;

    b->cursor--;
    while (UTF8_BYTE_SIZE(sb_t_at(&b->data, b->cursor)) == 0) {
        b->cursor--;
    }

    update_last_visual_col(b);
}

void move_down_page(buffer_t *b)
{
    u32 cursor_row = get_cursor_row(b);

    line_t next_line = {0};
    if (cursor_row + b->contents_height / 2 >= b->lines.size) {
        next_line = lines_t_at(&b->lines, b->lines.size - 1);
    } else {
        next_line = lines_t_at(&b->lines, cursor_row + b->contents_height / 2);
    }

    set_cursor_col_after_vertical_move(b, next_line);
}

void move_up_page(buffer_t *b)
{
    u32 cursor_row = get_cursor_row(b);

    line_t next_line = {0};
    if (cursor_row < b->contents_height / 2) {
        next_line = lines_t_at(&b->lines, 0);
    } else {
        next_line = lines_t_at(&b->lines, cursor_row - b->contents_height / 2);
    }

    set_cursor_col_after_vertical_move(b, next_line);
}

void move_line_first_char(buffer_t *b)
{
    u32 cursor_row = get_cursor_row(b);
    line_t cursor_line = lines_t_at(&b->lines, cursor_row);

    b->cursor = cursor_line.begin;

    b->last_visual_col = 0;
}

void move_line_begin(buffer_t *b)
{
    u32 cursor_row = get_cursor_row(b);
    line_t cursor_line = lines_t_at(&b->lines, cursor_row);

    b->cursor = cursor_line.begin;
    while (sb_t_at(&b->data, b->cursor) == ' ') {
        b->cursor += 1;
    }

    update_last_visual_col(b);
}

void move_line_end(buffer_t *b)
{
    u32 cursor_row = get_cursor_row(b);
    line_t cursor_line = lines_t_at(&b->lines, cursor_row);

    b->cursor = cursor_line.end;

    update_last_visual_col(b);
}

void move_top(buffer_t *b)
{
    b->cursor = 0;
    update_last_visual_col(b);
}

void move_bottom(buffer_t *b)
{
    line_t bottom_line = lines_t_at(&b->lines, b->lines.size - 1);
    b->cursor = bottom_line.begin;
    update_last_visual_col(b);
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

    if (UTF8_BYTE_SIZE(c) > 0) {
        size = UTF8_BYTE_SIZE(c);
        accum = 0;
        memset(buf, 0, 4);
    }

    buf[accum++] = c;

    if (accum == size && accum != 0) {
        sb_t_push(&b->data, b->cursor, buf, size);

        b->cursor += size;

        if (buf[0] == '\n') {
            b->last_visual_col = 0;
        } else {
            b->last_visual_col += 1;
        }

        b->saved = false;
        tokenize_lines(&b->lines, &b->data);
    }
}

void insert_indent_spaces_at_cursor(buffer_t *b)
{
    const char buf[9] = "        "; // 8 spaces maximum
    sb_t_push(&b->data, b->cursor, buf, INDENT_SPACES);
    b->cursor += INDENT_SPACES;

    tokenize_lines(&b->lines, &b->data);
    update_last_visual_col(b);
    b->saved = false;
}

void backspace(buffer_t *b)
{
    if (b->cursor == 0) return;

    b->cursor--;
    while (UTF8_BYTE_SIZE(sb_t_at(&b->data, b->cursor)) == 0) {
        b->cursor--;
    }

    u8 size = UTF8_BYTE_SIZE(sb_t_at(&b->data, b->cursor));
    sb_t_delete_many(&b->data, b->cursor, size);

    b->saved = false;
    tokenize_lines(&b->lines, &b->data);
    update_last_visual_col(b);
}
