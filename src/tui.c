/*
 * tui.c - ncurses UI toolkit implementation.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <locale.h>
#include <wchar.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util.h"
#include "tui.h"

/* color mapping: md color id -> (fg, attr) */
static const int fg_table[10] = {
    -1,            /* default */
    COLOR_CYAN,    /* accent */
    COLOR_GREEN,   /* green */
    COLOR_BLUE,    /* heading */
    COLOR_WHITE,   /* dim */
    COLOR_YELLOW,  /* code */
    COLOR_CYAN,    /* quote */
    COLOR_YELLOW,  /* warn */
    COLOR_RED,     /* err */
    COLOR_YELLOW,  /* amber */
};
static const int attr_table[10] = {
    A_NORMAL,
    0,            /* accent */
    0,            /* green */
    A_BOLD,       /* heading */
    A_DIM,        /* dim */
    A_BOLD,       /* code */
    A_DIM,        /* quote */
    A_BOLD,       /* warn */
    A_BOLD,       /* err */
    A_BOLD,       /* amber */
};

void tui_init(void) {
    setlocale(LC_ALL, "");
    setlocale(LC_NUMERIC, "C");
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    if (has_colors()) {
        start_color();
        bool have_defaults = use_default_colors() == OK;
        short bg = have_defaults ? -1 : (short)COLOR_BLACK;
        for (int i = 1; i <= 9; i++) {
            init_pair((short)i, (short)fg_table[i], bg);
        }
    }
    clear();
    refresh();
}

void tui_end(void) {
    endwin();
}

int tui_width(void)  { return COLS; }
int tui_height(void) { return LINES; }

WINDOW *tui_win(int h, int w, int y, int x) {
    if (h < 1) h = 1;
    if (w < 1) w = 1;
    if (y < 0) y = 0;
    if (x < 0) x = 0;
    return newwin(h, w, y, x);
}

void tui_box(WINDOW *w, const char *title) {
    box(w, 0, 0);
    if (title && *title) {
        int x = 2;
        wattrset(w, COLOR_PAIR(MD_DIM) | A_DIM);
        mvwprintw(w, 0, x, " %s ", title);
    }
    wattrset(w, 0);
}

void tui_mvline(WINDOW *w, int y, int x, int ch, int n) {
    mvwhline(w, y, x, ch, n);
}

void tui_attr(int color) {
    if (color <= 0) {
        wattrset(stdscr, A_NORMAL);
        return;
    }
    int attr = attr_table[color];
    if (color == MD_DIM) attr |= A_DIM;
    wattrset(stdscr, COLOR_PAIR(color) | attr);
}

void tui_clear_line(WINDOW *w, int y) {
    int width = getmaxx(w);
    for (int x = 0; x < width; x++) mvwaddch(w, y, x, ' ');
}

/* =================== UTF-8 helpers =================== */

/* returns byte length and terminal width of next utf8 char at s (<= end) */
static int utf8_char(const char *s, const char *end, int *width_out) {
    unsigned char c = (unsigned char)*s;
    int cw;
    if (c < 0x80) cw = 1;
    else if ((c & 0xE0) == 0xC0) cw = 2;
    else if ((c & 0xF0) == 0xE0) cw = 3;
    else if ((c & 0xF8) == 0xF0) cw = 4;
    else cw = 1;
    if (s + cw > end) cw = (int)(end - s);
    int width = 1;
    if (cw > 1) {
        char tmp[8];
        memcpy(tmp, s, (size_t)cw);
        tmp[cw] = '\0';
        mbstate_t st = {0};
        wchar_t wc = 0;
        size_t r = mbrtowc(&wc, tmp, (size_t)cw, &st);
        if (r != (size_t)-1 && r != (size_t)-2 && wc != 0) {
            int w = wcwidth(wc);
            if (w > 0) width = w;
        }
    }
    *width_out = width;
    return cw;
}

/* move column left/right by one utf8 char within str of length len */
static int utf8_prev(const char *s, int col) {
    if (col <= 0) return 0;
    int pos = col - 1;
    while (pos > 0 && ((unsigned char)s[pos] & 0xC0) == 0x80) pos--;
    return pos;
}
static int utf8_next(const char *s, int len, int col) {
    if (col >= len) return len;
    int w;
    int cw = utf8_char(s + col, s + len, &w);
    return col + cw;
}

/* ---------- UTF-8 aware key reading ---------- */

static char g_utf8_pending[8];

int tui_getkey(void) {
    wint_t wc;
    int rc = get_wch(&wc);
    if (rc == ERR) return ERR;
    if (rc == KEY_CODE_YES) return (int)wc;   /* function/arrow key etc. */
    if (wc < 0x80) return (int)wc;            /* ASCII or control char */
    /* wide char: encode back to UTF-8 */
    mbstate_t st = {0};
    size_t n = wcrtomb(g_utf8_pending, (wchar_t)wc, &st);
    if (n == (size_t)-1) return (int)wc;
    g_utf8_pending[n] = '\0';
    return TUI_UTF8;
}

const char *tui_utf8(void) { return g_utf8_pending; }
int tui_utf8_len(void) { return (int)strlen(g_utf8_pending); }

/* =================== message view =================== */

void mv_init(mv_t *v) {
    memset(v, 0, sizeof(*v));
    v->width = -1;
    v->follow = 1;
}
void mv_free(mv_t *v) {
    for (int i = 0; i < v->nrow; i++) free(v->rows[i].text);
    free(v->rows);
    memset(v, 0, sizeof(*v));
}
void mv_clear(mv_t *v) {
    for (int i = 0; i < v->nrow; i++) free(v->rows[i].text);
    v->nrow = 0;
}

static void mv_push_row(mv_t *v, const char *text, int color) {
    if (v->nrow >= v->caprow) {
        v->caprow = v->caprow ? v->caprow * 2 : 32;
        v->rows = realloc(v->rows, sizeof(md_line_t) * (size_t)v->caprow);
    }
    v->rows[v->nrow].text = xstrdup(text);
    v->rows[v->nrow].color = color;
    v->nrow++;
}

void mv_add_sender(mv_t *v, const char *name, int color) {
    sbuf_t b;
    sbuf_init(&b);
    sbuf_appendf(&b, "%s", name);
    mv_push_row(v, sbuf_detach(&b), color);
    sbuf_free(&b);
}

void mv_add_md(mv_t *v, const char *text, bool is_user) {
    md_doc_t doc;
    md_init(&doc);
    md_render(&doc, text, is_user);
    for (int i = 0; i < doc.n; i++) {
        mv_push_row(v, doc.lines[i].text, doc.lines[i].color);
    }
    mv_push_row(v, "", MD_DEFAULT);
    md_free(&doc);
}

void mv_add_plain(mv_t *v, const char *text, int color) {
    mv_push_row(v, text, color);
}

void mv_scroll(mv_t *v, int delta) {
    v->top += delta;
    if (v->top < 0) v->top = 0;
    if (v->nrow > 0 && v->top >= v->nrow) v->top = v->nrow;
    v->follow = (v->top >= v->nrow);
}

void mv_scroll_to_bottom(mv_t *v) {
    v->top = v->nrow;
    v->follow = 1;
}

/* Wrap one logical row's text into display rows that fit `width` columns.
 * Breaks at spaces when possible, hard-breaks long words, and never splits a
 * UTF-8 sequence. Empty text yields one empty display row so blank separators
 * between messages stay visible. Appends pieces to *disp / *dcol. */
static void mv_wrap_row(mv_t *v, int i, int width,
                        char ***disp, int **dcol, int *ndisp, int *capdisp) {
    const char *text = v->rows[i].text;
    const char *s = text;
    int color = v->rows[i].color;
    for (;;) {
        const char *end = s + strlen(s);
        const char *q = s;
        const char *bp = NULL;   /* last fitting space */
        int w = 0;
        bool fits = true;
        while (q < end) {
            int wdt;
            int c = utf8_char(q, end, &wdt);
            if (w + wdt > width) { fits = false; break; }
            if (*q == ' ') bp = q;
            w += wdt;
            q += c;
        }
        char *piece;
        size_t plen;
        if (fits) {
            plen = (size_t)(end - s);
            piece = xstrndup(s, plen);
            s = end;
        } else if (bp && bp > s) {
            plen = (size_t)(bp - s);
            piece = xstrndup(s, plen);
            s = bp + 1;
            while (*s == ' ') s++;
        } else {
            plen = (size_t)(q - s);
            if (plen == 0) {
                int wdt;
                plen = (size_t)utf8_char(s, end, &wdt);
            }
            piece = xstrndup(s, plen);
            s += plen;
        }
        while (plen > 0 && piece[plen - 1] == ' ') piece[--plen] = '\0';
        if (plen == 0 && text[0] != '\0') {
            free(piece);
        } else {
            if (*ndisp >= *capdisp) {
                *capdisp = *capdisp ? *capdisp * 2 : 64;
                *disp = realloc(*disp, sizeof(char *) * (size_t)*capdisp);
                *dcol = realloc(*dcol, sizeof(int) * (size_t)*capdisp);
            }
            (*disp)[*ndisp] = piece;
            (*dcol)[*ndisp] = color;
            (*ndisp)++;
        }
        if (fits || s[0] == '\0') break;
    }
}

void mv_draw(mv_t *v, WINDOW *win, int y, int h) {
    int width = getmaxx(win);
    if (width < 1) width = 1;
    int height = h;
    if (height < 0) height = 0;
    int max_rows = (int)(v->nrow ? v->nrow : 0);
    if (v->follow) v->top = max_rows;
    if (v->top > max_rows) v->top = max_rows;

    /* reflow logical rows into display rows that fit the window width */
    int ndisp = 0, capdisp = 0;
    char **disp = NULL;
    int  *dcol  = NULL;
    int  *off   = malloc(sizeof(int) * (size_t)(v->nrow + 1));
    for (int i = 0; i < v->nrow; i++) {
        off[i] = ndisp;
        mv_wrap_row(v, i, width, &disp, &dcol, &ndisp, &capdisp);
    }
    off[v->nrow] = ndisp;

    int top_disp;
    if (v->follow) {
        /* stick to the bottom: show the last `height` display rows */
        top_disp = ndisp > height ? ndisp - height : 0;
    } else {
        /* map the logical scroll offset to display rows */
        top_disp = off[v->top];
        if (ndisp - top_disp < height) {
            int nb = ndisp > height ? ndisp - height : 0;
            if (top_disp > nb) top_disp = nb;
        }
    }

    werase(win);
    for (int i = 0; i < height; i++) {
        int idx = top_disp + i;
        if (idx >= ndisp) break;
        int color = dcol[idx];
        int attr = A_NORMAL;
        if (color > 0) {
            attr = attr_table[color];
            if (color == MD_DIM) attr |= A_DIM;
            wattrset(win, COLOR_PAIR(color) | attr);
        } else {
            wattrset(win, A_NORMAL);
        }
        mvwaddnstr(win, y + i, 0, disp[idx], (int)strlen(disp[idx]));
    }
    wattrset(win, A_NORMAL);

    for (int i = 0; i < ndisp; i++) free(disp[i]);
    free(disp);
    free(dcol);
    free(off);
}

/* =================== editor =================== */

void ed_init(editor_t *e) {
    memset(e, 0, sizeof(*e));
    ed_set_text(e, "");
}
void ed_free(editor_t *e) {
    for (int i = 0; i < e->n; i++) free(e->lines[i]);
    free(e->lines);
    memset(e, 0, sizeof(*e));
}

static void ed_insert_line_at(editor_t *e, int idx, char *line) {
    if (e->n >= e->cap) {
        e->cap = e->cap ? e->cap * 2 : 32;
        e->lines = realloc(e->lines, sizeof(char *) * (size_t)e->cap);
    }
    for (int i = e->n; i > idx; i--) e->lines[i] = e->lines[i - 1];
    e->lines[idx] = line;
    e->n++;
}

void ed_set_text(editor_t *e, const char *text) {
    for (int i = 0; i < e->n; i++) free(e->lines[i]);
    free(e->lines);
    e->lines = NULL;
    e->n = e->cap = 0;
    if (!text) text = "";
    ed_insert_line_at(e, 0, xstrdup(""));
    const char *p = text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        if (nl) {
            ed_insert_line_at(e, e->n, xstrndup(p, (size_t)(nl - p)));
            p = nl + 1;
        } else {
            ed_insert_line_at(e, e->n, xstrdup(p));
            break;
        }
    }
    if (e->n == 1 && e->lines[0][0] == '\0' && text[0] != '\0') {
        /* keep single trailing empty line out if content non-empty */
    }
    e->row = 0;
    e->col = 0;
    e->top = 0;
    e->dirty = 0;
}

char *ed_get_text(editor_t *e) {
    sbuf_t b;
    sbuf_init(&b);
    for (int i = 0; i < e->n; i++) {
        if (i) sbuf_append(&b, "\n");
        sbuf_append(&b, e->lines[i]);
    }
    return sbuf_detach(&b);
}

static void ed_clamp_col(editor_t *e) {
    int len = (int)strlen(e->lines[e->row]);
    if (e->col > len) e->col = len;
    if (e->col < 0) e->col = 0;
}

static void ed_insert_char(editor_t *e, int ch) {
    char *line = e->lines[e->row];
    e->lines[e->row] = xasprintf("%.*s%c%s", e->col, line, ch, line + e->col);
    free(line);
    e->col++;
    e->dirty = 1;
}

static void ed_insert_text(editor_t *e, const char *s) {
    char *line = e->lines[e->row];
    e->lines[e->row] = xasprintf("%.*s%s%s", e->col, line, s, line + e->col);
    free(line);
    e->col += (int)strlen(s);
    e->dirty = 1;
}

void ed_draw(editor_t *e, WINDOW *win) {
    int h = getmaxy(win);
    int w = getmaxx(win);
    int cur_y = e->row - e->top;
    if (cur_y < 0) { e->top = e->row; cur_y = 0; }
    if (cur_y >= h) { e->top = e->row - (h - 1); cur_y = h - 1; }
    if (e->top < 0) e->top = 0;

    werase(win);
    for (int i = 0; i < h; i++) {
        int idx = e->top + i;
        if (idx >= e->n) break;
        const char *line = e->lines[idx];
        mvwaddnstr(win, i, 0, line, w);
    }
    /* cursor */
    ed_clamp_col(e);
    int cur_x = 0;
    const char *line = e->lines[e->row];
    int len = (int)strlen(line);
    int target = e->col;
    int i = 0;
    while (i < target && i < len) {
        int wdt;
        int c = utf8_char(line + i, line + len, &wdt);
        cur_x += wdt;
        i += c;
    }
    if (cur_x >= w) cur_x = w - 1;
    int sy = e->row - e->top;
    if (sy >= 0 && sy < h) {
        wmove(win, sy, cur_x);
        wattrset(win, COLOR_PAIR(MD_DIM));
        waddch(win, ACS_CKBOARD | A_BOLD);
        wattrset(win, A_NORMAL);
    }
}

int ed_cursor_y(editor_t *e) {
    return e->row - e->top;
}

bool ed_key(editor_t *e, int ch) {
    if (e->readonly) {
        /* allow navigation only */
        switch (ch) {
            case KEY_UP:    if (e->row > 0) { e->row--; e->col = 0; } return true;
            case KEY_DOWN:  if (e->row < e->n - 1) { e->row++; e->col = 0; } return true;
            case KEY_LEFT:  if (e->col > 0) e->col = utf8_prev(e->lines[e->row], e->col); return true;
            case KEY_RIGHT: if (e->col < (int)strlen(e->lines[e->row])) e->col = utf8_next(e->lines[e->row], (int)strlen(e->lines[e->row]), e->col); return true;
            case KEY_HOME:  e->col = 0; return true;
            case KEY_END:   e->col = (int)strlen(e->lines[e->row]); return true;
            case KEY_NPAGE: e->top += 10; return true;
            case KEY_PPAGE: e->top -= 10; if (e->top < 0) e->top = 0; return true;
        }
        return false;
    }

    switch (ch) {
        case KEY_UP:
            if (e->row > 0) { e->row--; ed_clamp_col(e); }
            return true;
        case KEY_DOWN:
            if (e->row < e->n - 1) { e->row++; ed_clamp_col(e); }
            return true;
        case KEY_LEFT:
            if (e->col > 0) {
                e->col = utf8_prev(e->lines[e->row], e->col);
            } else if (e->row > 0) {
                e->row--;
                e->col = (int)strlen(e->lines[e->row]);
            }
            return true;
        case KEY_RIGHT:
            if (e->col < (int)strlen(e->lines[e->row])) {
                e->col = utf8_next(e->lines[e->row], (int)strlen(e->lines[e->row]), e->col);
            } else if (e->row < e->n - 1) {
                e->row++;
                e->col = 0;
            }
            return true;
        case KEY_HOME: e->col = 0; return true;
        case KEY_END:  e->col = (int)strlen(e->lines[e->row]); return true;
        case KEY_NPAGE: e->top += 10; return true;
        case KEY_PPAGE: e->top -= 10; if (e->top < 0) e->top = 0; return true;
        case '\n':
        case '\r':
        case KEY_ENTER: {
            char *line = e->lines[e->row];
            char *rest = xstrdup(line + e->col);
            line[e->col] = '\0';
            ed_insert_line_at(e, e->row + 1, rest);
            e->row++;
            e->col = 0;
            e->dirty = 1;
            return true;
        }
        case KEY_BACKSPACE:
        case 127:
        case 8: {
            if (e->col > 0) {
                int prev = utf8_prev(e->lines[e->row], e->col);
                char *line = e->lines[e->row];
                e->lines[e->row] = xasprintf("%.*s%s", prev, line, line + e->col);
                free(line);
                e->col = prev;
                e->dirty = 1;
            } else if (e->row > 0) {
                char *prevline = e->lines[e->row - 1];
                int plen = (int)strlen(prevline);
                char *joined = xasprintf("%s%s", prevline, e->lines[e->row]);
                free(e->lines[e->row - 1]);
                free(e->lines[e->row]);
                e->lines[e->row - 1] = joined;
                for (int i = e->row; i < e->n - 1; i++) e->lines[i] = e->lines[i + 1];
                e->n--;
                e->row--;
                e->col = plen;
                e->dirty = 1;
            }
            return true;
        }
        case KEY_DC: { /* delete */
            char *line = e->lines[e->row];
            int len = (int)strlen(line);
            if (e->col < len) {
                int next = utf8_next(line, len, e->col);
                e->lines[e->row] = xasprintf("%.*s%s", e->col, line, line + next);
                free(line);
                e->dirty = 1;
            } else if (e->row < e->n - 1) {
                char *nextline = e->lines[e->row + 1];
                char *joined = xasprintf("%s%s", line, nextline);
                free(line);
                free(nextline);
                e->lines[e->row] = joined;
                for (int i = e->row + 1; i < e->n - 1; i++) e->lines[i] = e->lines[i + 1];
                e->n--;
                e->dirty = 1;
            }
            return true;
        }
        case '\t': {
            ed_insert_text(e, "    ");
            return true;
        }
        default:
            if (ch == TUI_UTF8) {
                ed_insert_text(e, tui_utf8());
                return true;
            }
            if (ch >= 32 && ch < 127) {
                ed_insert_char(e, ch);
                return true;
            }
            return false;
    }
}

/* =================== input line =================== */

void in_init(input_t *in) { memset(in, 0, sizeof(*in)); }
void in_set(input_t *in, const char *s) {
    memset(in, 0, sizeof(*in));
    if (s) {
        snprintf(in->buf, sizeof(in->buf), "%.*s", (int)sizeof(in->buf) - 1, s);
        in->len = (int)strlen(in->buf);
        in->cursor = in->len;
    }
}
const char *in_get(input_t *in) { return in->buf; }

void in_key(input_t *in, int ch) {
    switch (ch) {
        case KEY_LEFT:
            if (in->cursor > 0) in->cursor = utf8_prev(in->buf, in->cursor);
            break;
        case KEY_RIGHT:
            if (in->cursor < in->len) in->cursor = utf8_next(in->buf, in->len, in->cursor);
            break;
        case KEY_HOME: in->cursor = 0; break;
        case KEY_END: in->cursor = in->len; break;
        case KEY_BACKSPACE:
        case 127:
        case 8:
            if (in->cursor > 0) {
                int prev = utf8_prev(in->buf, in->cursor);
                memmove(in->buf + prev, in->buf + in->cursor, (size_t)(in->len - in->cursor) + 1);
                in->len -= in->cursor - prev;
                in->cursor = prev;
            }
            break;
        case KEY_DC:
            if (in->cursor < in->len) {
                int next = utf8_next(in->buf, in->len, in->cursor);
                memmove(in->buf + in->cursor, in->buf + next, (size_t)(in->len - next) + 1);
                in->len -= next - in->cursor;
            }
            break;
        default:
            if (ch == TUI_UTF8) {
                const char *u = tui_utf8();
                int n = (int)strlen(u);
                if (in->len + n < (int)sizeof(in->buf)) {
                    memmove(in->buf + in->cursor + n, in->buf + in->cursor, (size_t)(in->len - in->cursor) + 1);
                    memcpy(in->buf + in->cursor, u, (size_t)n);
                    in->cursor += n;
                    in->len += n;
                }
            } else if (ch >= 32 && ch < 127 && in->len < (int)sizeof(in->buf) - 1) {
                memmove(in->buf + in->cursor + 1, in->buf + in->cursor, (size_t)(in->len - in->cursor) + 1);
                in->buf[in->cursor] = (char)ch;
                in->cursor++;
                in->len++;
            }
            break;
    }
}

void in_draw(input_t *in, WINDOW *win, int y, int x, int w) {
    werase(win);
    if (w < 1) return;
    /* display column of the cursor */
    int cur_w = 0;
    int i = 0;
    while (i < in->cursor && i < in->len) {
        int wdt;
        int c = utf8_char(in->buf + i, in->buf + in->len, &wdt);
        cur_w += wdt;
        i += c;
    }
    /* scroll so the cursor stays visible */
    int start = 0;
    int vis_cursor = cur_w;
    if (cur_w >= w) {
        int s_w = 0;
        int k = 0;
        while (k < in->len) {
            int wdt;
            int c = utf8_char(in->buf + k, in->buf + in->len, &wdt);
            if (s_w + wdt > cur_w - (w - 1)) break;
            s_w += wdt;
            k += c;
        }
        start = k;
        vis_cursor = cur_w - s_w;
        if (vis_cursor >= w) vis_cursor = w - 1;
    }
    /* draw text but reserve the last column for the cursor block when the
       cursor sits at the right edge, so the last character stays visible */
    int draw_w = 0;
    int n = 0;
    i = start;
    while (i < in->len) {
        int wdt;
        int c = utf8_char(in->buf + i, in->buf + in->len, &wdt);
        if (draw_w + wdt > w) break;
        if (vis_cursor == w - 1 && draw_w >= w - 1) break;
        draw_w += wdt;
        n += c;
        i += c;
    }
    mvwaddnstr(win, y, x, in->buf + start, n);
    if (vis_cursor < w) {
        wmove(win, y, x + vis_cursor);
        wattrset(win, A_REVERSE);
        waddch(win, ' ');
        wattrset(win, A_NORMAL);
    }
}

/* =================== dialogs =================== */

bool tui_confirm(const char *title) {
    int h = 5, w = strlen(title) + 12;
    if (w > COLS - 4) w = COLS - 4;
    if (w < 24) w = 24;
    WINDOW *win = tui_win(h, w, (LINES - h) / 2, (COLS - w) / 2);
    tui_box(win, title);
    mvwprintw(win, 2, 2, "   [y] Yes   [n] No / Esc");
    wrefresh(win);
    int done = 0;
    bool result = false;
    while (!done) {
        int ch = getch();
        if (ch == 'y' || ch == 'Y') { result = true; done = 1; }
        else if (ch == 'n' || ch == 'N' || ch == 27 || ch == ERR) { result = false; done = 1; }
    }
    delwin(win);
    return result;
}

void tui_alert(const char *title) {
    int h = 5, w = strlen(title) + 8;
    if (w > COLS - 4) w = COLS - 4;
    if (w < 20) w = 20;
    WINDOW *win = tui_win(h, w, (LINES - h) / 2, (COLS - w) / 2);
    tui_box(win, "Notice");
    mvwprintw(win, 2, 1, "%.*s", w - 2, title);
    mvwprintw(win, 3, 2, "[Enter / Esc to close]");
    wrefresh(win);
    int ch;
    do { ch = getch(); } while (ch != '\n' && ch != '\r' && ch != KEY_ENTER && ch != 27 && ch != ERR);
    delwin(win);
}

bool tui_prompt(const char *title, const char *initial, char *out, size_t outsz) {
    int h = 6, w = 64;
    if (w > COLS - 4) w = COLS - 4;
    if (w < 32) w = 32;
    WINDOW *win = tui_win(h, w, (LINES - h) / 2, (COLS - w) / 2);
    tui_box(win, title);
    input_t in;
    in_init(&in);
    in_set(&in, initial);
    WINDOW *inwin = tui_win(1, w - 4, (LINES - h) / 2 + 2, (COLS - w) / 2 + 2);
    bool done = false;
    bool ok = false;
    while (!done) {
        in_draw(&in, inwin, 0, 0, w - 4);
        wrefresh(inwin);
        wrefresh(win);
        int ch = tui_getkey();
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            ok = true;
            done = true;
        } else if (ch == 27 || ch == ERR) {
            ok = false;
            done = true;
        } else if (ch == KEY_F(5)) {   /* clear */
            in_set(&in, "");
        } else {
            in_key(&in, ch);
        }
    }
    if (ok) {
        strncpy(out, in.buf, outsz - 1);
        out[outsz - 1] = '\0';
    }
    delwin(inwin);
    delwin(win);
    return ok;
}

/* ---------- file picker ---------- */

bool tui_pick_file(const char *title, char *out, size_t outsz) {
    int h = LINES - 8, w = 72;
    if (w > COLS - 4) w = COLS - 4;
    if (h < 8) h = 8;
    int y0 = (LINES - h) / 2, x0 = (COLS - w) / 2;

    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, "/");

    WINDOW *win = tui_win(h, w, y0, x0);
    tui_box(win, title);
    WINDOW *listwin = tui_win(h - 4, w - 2, y0 + 2, x0 + 1);
    WINDOW *pathwin = tui_win(1, w - 2, y0 + h - 2, x0 + 1);

    input_t in;
    in_init(&in);

    char **entries = NULL;
    int nentries = 0;
    int sel = 0;
    bool done = false;
    bool result = false;
    bool in_path = false;

    while (!done) {
        /* read dir */
        for (int i = 0; i < nentries; i++) free(entries[i]);
        free(entries);
        entries = NULL;
        nentries = 0;
        int cap = 0;
        DIR *d = opendir(cwd);
        if (d) {
            struct dirent *de;
            while ((de = readdir(d))) {
                if (!strcmp(de->d_name, ".")) continue;
                char full[4200];
                snprintf(full, sizeof(full), "%s/%s", cwd, de->d_name);
                struct stat st;
                bool isdir = (stat(full, &st) == 0 && S_ISDIR(st.st_mode));
                char *label = xasprintf("%s%s", de->d_name, isdir ? "/" : "");
                if (nentries >= cap) {
                    cap = cap ? cap * 2 : 64;
                    entries = realloc(entries, sizeof(char *) * (size_t)cap);
                }
                entries[nentries++] = label;
            }
            closedir(d);
        }
        /* sort: dirs first */
        for (int i = 0; i < nentries; i++) {
            for (int j = i + 1; j < nentries; j++) {
                bool di = entries[i][strlen(entries[i]) - 1] == '/';
                bool dj = entries[j][strlen(entries[j]) - 1] == '/';
                int cmp;
                if (di == dj) cmp = strcmp(entries[i], entries[j]);
                else cmp = di ? -1 : 1;
                if (cmp > 0) {
                    char *t = entries[i]; entries[i] = entries[j]; entries[j] = t;
                }
            }
        }
        if (sel >= nentries) sel = nentries > 0 ? nentries - 1 : 0;

        werase(listwin);
        int lh = getmaxy(listwin);
        int top = sel - lh / 2;
        if (top < 0) top = 0;
        for (int i = 0; i < lh && top + i < nentries; i++) {
            int idx = top + i;
            if (idx == sel) {
                wattron(listwin, A_REVERSE);
                mvwaddnstr(listwin, i, 0, entries[idx], w - 2);
                wattroff(listwin, A_REVERSE);
            } else {
                mvwaddnstr(listwin, i, 0, entries[idx], w - 2);
            }
        }
        wattrset(win, A_NORMAL);
        mvwprintw(win, 1, 1, "%.*s", w - 2, cwd);
        wrefresh(listwin);

        if (in_path) {
            in_draw(&in, pathwin, 0, 0, w - 2);
        } else {
            werase(pathwin);
            mvwprintw(pathwin, 0, 0, "%.*s", w - 2, "Enter: open/select   [d]: type path   Esc: cancel");
        }
        wrefresh(pathwin);
        wrefresh(win);

        int ch = tui_getkey();
        if (in_path) {
            if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                /* try as path */
                char *p = str_trim(in.buf);
                if (p[0] == '/') {
                    struct stat st;
                    if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) {
                        snprintf(cwd, sizeof(cwd), "%s", p);
                        in_set(&in, "");
                    } else if (stat(p, &st) == 0 && S_ISREG(st.st_mode)) {
                        snprintf(out, outsz, "%s", p);
                        result = true;
                        done = true;
                    }
                }
                in_path = false;
            } else if (ch == 27) {
                in_path = false;
            } else {
                in_key(&in, ch);
            }
            continue;
        }

        if (ch == 27 || ch == ERR) { done = true; result = false; }
        else if (ch == KEY_UP) { if (sel > 0) sel--; }
        else if (ch == KEY_DOWN) { if (sel < nentries - 1) sel++; }
        else if (ch == KEY_NPAGE) { sel += 10; if (sel >= nentries) sel = nentries - 1; }
        else if (ch == KEY_PPAGE) { sel -= 10; if (sel < 0) sel = 0; }
        else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            if (nentries == 0) continue;
            char *label = entries[sel];
            bool isdir = label[strlen(label) - 1] == '/';
            char full[4600];
            snprintf(full, sizeof(full), "%s/%s", cwd, label);
            full[sizeof(full) - 1] = '\0';
            if (isdir) {
                snprintf(cwd, sizeof(cwd), "%.*s", (int)sizeof(cwd) - 1, full);
                sel = 0;
            } else {
                snprintf(out, outsz, "%.*s", (int)outsz - 1, full);
                result = true;
                done = true;
            }
        } else if (ch == 'd' || ch == '/') {
            in_path = true;
            in_set(&in, cwd);
        }
    }
    for (int i = 0; i < nentries; i++) free(entries[i]);
    free(entries);
    delwin(listwin);
    delwin(pathwin);
    delwin(win);
    return result;
}