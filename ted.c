#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <locale.h>

#include <signal.h>
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

#define UTF8_BYTESIZE(c)            \
(                                   \
    assert((u8)(c) <= 247),         \
    utf8_bytesize_cache[(u8)(c)]    \
)

#define TERM_SET_CHAR(c, row_i, col_i)              \
if (display_buffer[row_i][col_i].abs != (c).abs) {  \
    display_buffer[row_i][col_i] = c;               \
    dirty_buffer[row_i][col_i] = true;              \
}

#define TERM_MOVE_CURSOR(row, col) printf("\033[%d;%dH", row, col)

#define CONTENTS_WIDTH (assert(term_width > 0), (u16)(term_width - 0))
#define CONTENTS_HEIGHT (assert(term_height > 0), (u16)(term_height - 1))

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

typedef struct Line {
    u32 begin;
    u32 end;
} Line;

typedef enum Mode {
    NORMAL_MODE = 0,
    INSERT_MODE = 1,
    REGION_MODE = 2
} Mode;

DA_TYPEDEF(char, SB)
DA_TYPEDEF(Line, Lines)

typedef struct Buffer {
    SB data;
    SB path;
    SB clipboard;
    Lines lines;

    Mode mode;

    u32 cursor;
    u32 row_offset; // only updated by renderer
    u32 last_visual_col;

    u32 region_begin;
    u32 region_end;

    bool saved;
} Buffer;

typedef union Utf8_Char {
    char arr[5]; // 5th for null byte for printf
    u32 abs;
} Utf8_Char;

// #########################################################################
// Render functions
// #########################################################################

static void term_clear(void);
static void term_display(void);
static void render(Buffer *b);

// #########################################################################
// Utility functions
// #########################################################################

static u32 get_cursor_row(Buffer *b);
static u32 update_row_offset(Buffer *b);
static u32 update_last_visual_col(Buffer *b);
static void set_cursor_col_after_vertical_move(Buffer *b, Line next_line);

// #########################################################################
// Misc functions
// #########################################################################

static void cache_utf8_bytesize(void);
static void signal_handler(s32 signum);

// #########################################################################
// Lines functions
// #########################################################################

static u32 tokenize_lines(Lines *lines, SB *sb);

// #########################################################################
// Buffer functions
// #########################################################################

static u32 buffer_create_from_file(Buffer *b, const char *path);
static void buffer_save(Buffer *b);
static void buffer_kill(Buffer *b);

// #########################################################################
// Editor functions
// #########################################################################

static void move_down(Buffer *b);
static void move_up(Buffer *b);
static void move_right(Buffer *b);
static void move_left(Buffer *b);
static void move_down_page(Buffer *b);
static void move_up_page(Buffer *b);
static void move_line_first_char(Buffer *b);
static void move_line_begin(Buffer *b);
static void move_line_end(Buffer *b);
static void move_top(Buffer *b);
static void move_bottom(Buffer *b);
static void center_cursor_line(Buffer *b);
static void insert_char_at_cursor(Buffer *b, char c);
static void insert_indent_spaces_at_cursor(Buffer *b);
static void backspace(Buffer *b);
static void begin_region(Buffer *b);
static void end_region(Buffer *b);
static void discard_region(Buffer *b);
static void copy_region_append(Buffer *b);
static void cut_region_append(Buffer *b);
static void delete_region(Buffer *b);
static void paste_clipboard_at_cursor(Buffer *b);
static void clear_clipboard(Buffer *b);

// #########################################################################
// Global variables
// #########################################################################

u8 utf8_bytesize_cache[256] = {0};

Utf8_Char display_buffer[MAX_HEIGHT][MAX_WIDTH] = {0};
bool dirty_buffer[MAX_HEIGHT][MAX_WIDTH] = {0};

Buffer *current_b = NULL;

u16 term_width = 0;
u16 term_height = 0;

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

    Buffer b = {0};
    if (buffer_create_from_file(&b, argv[1]) == 0) {
        printf("no file found\n");
        return 1;
    }
    current_b = &b;

    cache_utf8_bytesize();

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

    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)) {
        printf("ioctl failed\n");
        return 1;
    }
    term_width = ws.ws_col;
    term_height = ws.ws_row;

    if (term_width > MAX_WIDTH || term_height > MAX_HEIGHT) {
        printf("terminal resolution is too high\n");
        return 1;
    }

    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, NULL);

    printf("\033c"); // clear, scrollback included

    bool should_close = false;
    while (!should_close) {
        render(&b);

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
            case 'y':
                paste_clipboard_at_cursor(&b);
                break;
            case 'r':
                clear_clipboard(&b);
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
            case 'o':
                move_line_end(&b);
                insert_char_at_cursor(&b, '\n');
                b.mode = INSERT_MODE;
                break;
            case 'O':
                move_up(&b);
                move_line_end(&b);
                insert_char_at_cursor(&b, '\n');
                b.mode = INSERT_MODE;
                break;

            // entering region mode
            case 'v':
                b.mode = REGION_MODE;
                begin_region(&b);
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
        } else if (b.mode == REGION_MODE) {
            switch (c) {
            // basic commands
            case 'v':
                discard_region(&b);
                b.mode = NORMAL_MODE;
                break;
            case 'c':
                end_region(&b);
                copy_region_append(&b);
                b.mode = NORMAL_MODE;
                break;
            case 'x':
                end_region(&b);
                cut_region_append(&b);
                b.mode = NORMAL_MODE;
                break;
            case 'd':
                end_region(&b);
                delete_region(&b);
                b.mode = NORMAL_MODE;
                break;
            case 'r':
                clear_clipboard(&b);
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
    printf("\033c"); // clear, scrollback included
    return 0;
}

// #########################################################################
// Render functions
// #########################################################################

static
void term_clear(void)
{
    for (u16 row_i = 0; row_i < term_height; ++row_i) {
        for (u16 col_i = 0; col_i < term_width; ++col_i) {
            TERM_SET_CHAR((Utf8_Char){ .abs = 0 }, row_i, col_i);
        }
    }
}

static
void term_display(void)
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

static
void render(Buffer *b)
{
    term_clear();

    u16 cursor_visual_col = 1;
    u32 cursor_row = update_row_offset(b);

    Utf8_Char c = {0};

    for (u32 row_i = 0; row_i + 1 < term_height; ++row_i) {
        if (b->row_offset + row_i >= b->lines.size) {
            c.abs = 0;
            c.arr[0] = '~';
            TERM_SET_CHAR(c, row_i, 0);
            continue;
        }

        Line line = Lines_at(&b->lines, b->row_offset + row_i);
        u16 col_i = 0;

        for (u32 char_i = 0; char_i < line.end - line.begin;) {
            u8 size = UTF8_BYTESIZE(SB_at(&b->data, line.begin + char_i));

            c.abs = 0;
            memcpy(c.arr, &b->data.data[line.begin + char_i], size);
            TERM_SET_CHAR(c, row_i, col_i);

            col_i++;
            char_i += size;
        }

        if (b->row_offset + row_i == cursor_row) {
            for (u32 k = line.begin; k < b->cursor;) {
                cursor_visual_col++;
                k += UTF8_BYTESIZE(SB_at(&b->data, k));
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
    } else if (b->mode == REGION_MODE) {
        strcat(status, " [region]");
    }

    // TODO calculate length of clipboard
    sprintf(&status[strlen(status)], " [%lu]", b->clipboard.size);

    u16 col_i = 0;

    for (u16 i = 0; i < strlen(status) && i < term_width;) {
        u8 size = UTF8_BYTESIZE(status[i]);

        c.abs = 0;
        memcpy(c.arr, &status[i], size);

        TERM_SET_CHAR(c, term_height - 1, col_i);
        i += size;
        col_i++;
    }

    printf("\033[?25l"); // hide cursor
    term_display();
    printf("\033[?25h"); // show cursor

    TERM_MOVE_CURSOR(cursor_row - b->row_offset + 1, cursor_visual_col);
    fflush(stdout);
}

// #########################################################################
// Utility functions
// #########################################################################

static
u32 get_cursor_row(Buffer *b)
{
    for (u32 i = 0; i < b->lines.size; ++i) {
        Line line = Lines_at(&b->lines, i);
        if (b->cursor >= line.begin && b->cursor <= line.end) {
            return i;
        }
    }
    assert(0 && "unreachable");
}

static
u32 update_row_offset(Buffer *b)
{
    s32 absolute_row = get_cursor_row(b);
    s32 relative_row = absolute_row - b->row_offset;

    if (relative_row < 0) {
        b->row_offset += relative_row;
    } else if (relative_row + 1 > CONTENTS_HEIGHT) {
        b->row_offset += relative_row - (CONTENTS_HEIGHT - 1);
    }

    return absolute_row;
}

static
u32 update_last_visual_col(Buffer *b)
{
    u32 cursor_row = get_cursor_row(b);
    Line cursor_line = Lines_at(&b->lines, cursor_row);

    b->last_visual_col = 0;
    for (u32 i = cursor_line.begin; i < b->cursor; ) {
        b->last_visual_col++;
        i += UTF8_BYTESIZE(SB_at(&b->data, i));
    }

    return cursor_row;
}

static
void set_cursor_col_after_vertical_move(Buffer *b, Line next_line)
{
    u32 next_line_visual_len = 0;
    for (u32 i = next_line.begin; i < next_line.end; ) {
        next_line_visual_len += 1;
        i += UTF8_BYTESIZE(SB_at(&b->data, i));
    }

    if (b->last_visual_col > next_line_visual_len) {
        b->cursor = next_line.end;
    } else {
        b->cursor = next_line.begin;
        for (u32 i = 0; i < b->last_visual_col; ++i) {
            b->cursor += UTF8_BYTESIZE(SB_at(&b->data, b->cursor));
        }
    }
}

// #########################################################################
// Misc functions
// #########################################################################

static
void signal_handler(s32 signum)
{
    if (signum == SIGWINCH) {
        struct winsize ws;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws); // TODO check ioctl fail
        term_width = ws.ws_col;
        term_height = ws.ws_row;

        assert(current_b);
        memset(dirty_buffer, 1, MAX_WIDTH * MAX_HEIGHT);
        tokenize_lines(&current_b->lines, &current_b->data);
        render(current_b);
    }
}

static
void cache_utf8_bytesize(void)
{
    for (u32 i = 0; i < 256; ++i) {
        if      (i <= 127)             utf8_bytesize_cache[i] =  1;
        else if (i >= 128 && i <= 191) utf8_bytesize_cache[i] =  0;
        else if (i >= 192 && i <= 223) utf8_bytesize_cache[i] =  2;
        else if (i >= 224 && i <= 239) utf8_bytesize_cache[i] =  3;
        else if (i >= 240 && i <= 247) utf8_bytesize_cache[i] =  4;
        // > 247 should be handled in macro
    }
}

// #########################################################################
// Lines functions
// #########################################################################

static
u32 tokenize_lines(Lines *lines, SB *sb)
{
    lines->size = 0;

    Line line = {0};

    for (u32 i = 0; i < sb->size; ++i) {
        if (SB_at(sb, i) == '\n') {
            line.end = i;
            Lines_push_back(lines, line);
            line.begin = i + 1;
            line.end = 0;
        }
    }
    line.end = sb->size;
    Lines_push_back(lines, line);

    Lines_shrink_to_fit(lines);

    return lines->size;
}

// #########################################################################
// Buffer functions
// #########################################################################

static
u32 buffer_create_from_file(Buffer *b, const char *path)
{
    memset(b, 0, sizeof(Buffer));

    FILE *fp = fopen(path, "r");
    if (fp == NULL) return 0;

    fseek(fp, 0, SEEK_END);
    u32 file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    b->data.data = malloc(file_size);
    b->data.size = file_size;
    b->data.cap = file_size;
    fread(b->data.data, 1, file_size, fp);

    b->lines = Lines_create();
    tokenize_lines(&b->lines, &b->data);

    b->path = SB_create();
    SB_push_back_many(&b->path, path, strlen(path));

    b->clipboard = SB_create();

    b->saved = true;

    fclose(fp);
    return b->lines.size;
}

static
void buffer_save(Buffer *b)
{
    char path[TEMP_BUF_SIZE] = {0};
    strncpy(path, b->path.data, b->path.size);

    FILE *fp = fopen(path, "w");
    assert(fp);

    fwrite(b->data.data, 1, b->data.size, fp);

    fclose(fp);
    b->saved = true;
}

static
void buffer_kill(Buffer *b)
{
    SB_destroy(&b->data);
    SB_destroy(&b->path);
    SB_destroy(&b->clipboard);
    Lines_destroy(&b->lines);
    memset(b, 0, sizeof(Buffer));
}

// #########################################################################
// Editor functions
// #########################################################################

static
void move_down(Buffer *b)
{
    u32 cursor_row = get_cursor_row(b);
    if (cursor_row + 1 == b->lines.size) return;

    Line next_line = Lines_at(&b->lines, cursor_row + 1);
    set_cursor_col_after_vertical_move(b, next_line);
}

static
void move_up(Buffer *b)
{
    u32 cursor_row = get_cursor_row(b);
    if (cursor_row == 0) return;

    Line next_line = Lines_at(&b->lines, cursor_row - 1);
    set_cursor_col_after_vertical_move(b, next_line);
}

static
void move_right(Buffer *b)
{
    if (b->cursor == b->data.size) return;

    b->cursor += UTF8_BYTESIZE(SB_at(&b->data, b->cursor));
    update_last_visual_col(b);
}

static
void move_left(Buffer *b)
{
    if (b->cursor == 0) return;

    b->cursor--;
    while (UTF8_BYTESIZE(SB_at(&b->data, b->cursor)) == 0) {
        b->cursor--;
    }

    update_last_visual_col(b);
}

static
void move_down_page(Buffer *b)
{
    u32 cursor_row = get_cursor_row(b);

    Line next_line = {0};
    if (cursor_row + CONTENTS_HEIGHT / 2 >= b->lines.size) {
        next_line = Lines_at(&b->lines, b->lines.size - 1);
    } else {
        next_line = Lines_at(&b->lines, cursor_row + CONTENTS_HEIGHT / 2);
    }

    set_cursor_col_after_vertical_move(b, next_line);
}

static
void move_up_page(Buffer *b)
{
    u32 cursor_row = get_cursor_row(b);

    Line next_line = {0};
    if (cursor_row < CONTENTS_HEIGHT / 2) {
        next_line = Lines_at(&b->lines, 0);
    } else {
        next_line = Lines_at(&b->lines, cursor_row - CONTENTS_HEIGHT / 2);
    }

    set_cursor_col_after_vertical_move(b, next_line);
}

static
void move_line_first_char(Buffer *b)
{
    u32 cursor_row = get_cursor_row(b);
    Line cursor_line = Lines_at(&b->lines, cursor_row);

    b->cursor = cursor_line.begin;

    b->last_visual_col = 0;
}

static
void move_line_begin(Buffer *b)
{
    u32 cursor_row = get_cursor_row(b);
    Line cursor_line = Lines_at(&b->lines, cursor_row);

    b->cursor = cursor_line.begin;
    while (SB_at(&b->data, b->cursor) == ' ') {
        b->cursor += 1;
    }

    update_last_visual_col(b);
}

static
void move_line_end(Buffer *b)
{
    u32 cursor_row = get_cursor_row(b);
    Line cursor_line = Lines_at(&b->lines, cursor_row);

    b->cursor = cursor_line.end;

    update_last_visual_col(b);
}

static
void move_top(Buffer *b)
{
    b->cursor = 0;
    b->last_visual_col = 0;
}

static
void move_bottom(Buffer *b)
{
    Line bottom_line = Lines_at(&b->lines, b->lines.size - 1);
    b->cursor = bottom_line.begin;
    b->last_visual_col = 0;
}

static
void center_cursor_line(Buffer *b)
{
    u32 cursor_row = get_cursor_row(b);

    if (cursor_row <= CONTENTS_HEIGHT / 2) {
        b->row_offset = 0;
    } else {
        b->row_offset = cursor_row - CONTENTS_HEIGHT / 2;
    }
}

static
void insert_char_at_cursor(Buffer *b, char c)
{
    static u8 size = 0;
    static u8 accum = 0;
    static char buf[4] = {0};

    if (UTF8_BYTESIZE(c) > 0) {
        size = UTF8_BYTESIZE(c);
        accum = 0;
        memset(buf, 0, 4);
    }

    buf[accum++] = c;

    if (accum == size && accum != 0) {
        SB_push_many(&b->data, b->cursor, buf, size);

        b->cursor += size;

        if (buf[0] == '\n') {
            b->last_visual_col = 0;
        } else {
            b->last_visual_col += 1;
        }

        b->saved = false;
        tokenize_lines(&b->lines, &b->data);
        update_last_visual_col(b);
    }
}

static
void insert_indent_spaces_at_cursor(Buffer *b)
{
    const char buf[9] = "        "; // 8 spaces maximum
    SB_push_many(&b->data, b->cursor, buf, INDENT_SPACES);
    b->cursor += INDENT_SPACES;

    tokenize_lines(&b->lines, &b->data);
    update_last_visual_col(b);
    b->saved = false;
}

static
void backspace(Buffer *b)
{
    if (b->cursor == 0) return;

    b->cursor--;
    while (UTF8_BYTESIZE(SB_at(&b->data, b->cursor)) == 0) {
        b->cursor--;
    }

    u8 size = UTF8_BYTESIZE(SB_at(&b->data, b->cursor));
    SB_delete_many(&b->data, b->cursor, size);

    b->saved = false;
    tokenize_lines(&b->lines, &b->data);
    update_last_visual_col(b);
}

static
void begin_region(Buffer *b)
{
    b->region_begin = b->region_end = b->cursor;
}

static
void end_region(Buffer *b)
{
    if (b->cursor < b->region_begin) {
        b->region_end = b->region_begin;
        b->region_begin = b->cursor;
    } else if (b->cursor > b->region_begin) {
        b->region_end = b->cursor;
    } else {
        discard_region(b);
    }
}

static
void discard_region(Buffer *b)
{
    b->region_begin = b->region_end = 0;
}

static
void copy_region_append(Buffer *b)
{
    if (b->region_begin == b->region_end) return;
    assert(b->region_end > b->region_begin);

    SB_push_back_many(&b->clipboard,
                        &b->data.data[b->region_begin],
                        b->region_end - b->region_begin);
}

static
void cut_region_append(Buffer *b)
{
    if (b->region_begin == b->region_end) return;
    assert(b->region_end > b->region_begin);

    SB_push_back_many(&b->clipboard,
                        &b->data.data[b->region_begin],
                        b->region_end - b->region_begin);
    SB_delete_many(&b->data,
                     b->region_begin,
                     b->region_end - b->region_begin);

    b->cursor = b->region_begin;
    tokenize_lines(&b->lines, &b->data);
    update_last_visual_col(b);
    b->saved = false;
}

static
void delete_region(Buffer *b)
{
    if (b->region_begin == b->region_end) return;
    assert(b->region_end > b->region_begin);

    SB_delete_many(&b->data,
                     b->region_begin,
                     b->region_end - b->region_begin);

    b->cursor = b->region_begin;
    tokenize_lines(&b->lines, &b->data);
    update_last_visual_col(b);
    b->saved = false;
}

static
void paste_clipboard_at_cursor(Buffer *b)
{
    if (b->clipboard.size == 0) return;

    SB_push_many(&b->data,
                   b->cursor,
                   b->clipboard.data,
                   b->clipboard.size);

    b->cursor += b->clipboard.size;
    tokenize_lines(&b->lines, &b->data);
    update_last_visual_col(b);
    b->saved = false;
}

static
void clear_clipboard(Buffer *b)
{
    SB_clear(&b->clipboard);
}
