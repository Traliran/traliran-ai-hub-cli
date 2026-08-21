/*
 * tui.h - ncurses UI toolkit: windows, message view, editor, input, dialogs.
 */
#ifndef HUB_TUI_H
#define HUB_TUI_H

#include <ncurses.h>
#include <stdbool.h>
#include <stddef.h>

#include "markdown.h"

/* ---------- lifecycle ---------- */
void tui_init(void);
void tui_end(void);
int  tui_width(void);
int  tui_height(void);

/* ---------- drawing helpers ---------- */
WINDOW *tui_win(int h, int w, int y, int x);            /* new window */
void    tui_box(WINDOW *w, const char *title);          /* bordered box + title */
void    tui_mvline(WINDOW *w, int y, int x, int ch, int n);
void    tui_attr(int color);                            /* wattrset current window */
void    tui_clear_line(WINDOW *w, int y);

/* ---------- message view (scrollable rich-text chat) ---------- */
typedef struct {
    md_line_t *rows;      /* wrapped display rows */
    int        nrow;
    int        caprow;
    int        top;       /* first visible row index */
    int        width;     /* wrap width used last reflow */
    int        follow;    /* 1 = stick to bottom (default), 0 = free scroll */
} mv_t;

void mv_init(mv_t *v);
void mv_free(mv_t *v);
void mv_clear(mv_t *v);
void mv_add_md(mv_t *v, const char *text, bool is_user);   /* renders + appends */
void mv_add_sender(mv_t *v, const char *name, int color);  /* header row */
void mv_add_plain(mv_t *v, const char *text, int color);
void mv_scroll(mv_t *v, int delta);
void mv_scroll_to_bottom(mv_t *v);
void mv_draw(mv_t *v, WINDOW *win, int y, int h);

/* ---------- text editor (multi-line) ---------- */
typedef struct {
    char **lines;
    int    n;
    int    cap;
    int    row;     /* cursor line */
    int    col;     /* cursor column (bytes) */
    int    top;     /* scroll offset (first visible line) */
    int    dirty;
    int    readonly;
} editor_t;

void   ed_init(editor_t *e);
void   ed_free(editor_t *e);
void   ed_set_text(editor_t *e, const char *text);
char  *ed_get_text(editor_t *e);           /* malloc'd, joined with '\n' */
void   ed_draw(editor_t *e, WINDOW *win);
bool   ed_key(editor_t *e, int ch);        /* returns true if consumed */
int    ed_cursor_y(editor_t *e);           /* screen y of cursor (0-based) */

/* ---------- single-line input ---------- */
typedef struct {
    char  buf[2048];
    int   len;
    int   cursor;
} input_t;

void        in_init(input_t *in);
void        in_set(input_t *in, const char *s);
const char *in_get(input_t *in);
void        in_key(input_t *in, int ch);
void        in_draw(input_t *in, WINDOW *win, int y, int x, int w);

/* ---------- input reading (UTF-8 aware) ---------- */
#define TUI_UTF8 0x1FFFFF   /* sentinel: pending UTF-8 char, see tui_utf8() */
int  tui_getkey(void);                  /* one key or TUI_UTF8 (if a wide char) */
const char *tui_utf8(void);             /* pending UTF-8 bytes (NUL-terminated) */
int  tui_utf8_len(void);

/* ---------- modal dialogs ---------- */
bool tui_confirm(const char *title);                       /* Y/N */
void tui_alert(const char *title);
bool tui_prompt(const char *title, const char *initial, char *out, size_t outsz);
bool tui_pick_file(const char *title, char *out, size_t outsz); /* path picker */

#endif /* HUB_TUI_H */