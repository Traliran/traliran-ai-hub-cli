/*
 * markdown.c - minimal markdown → colored display-lines renderer.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "util.h"
#include "markdown.h"

void md_init(md_doc_t *d) { d->lines = NULL; d->n = 0; d->cap = 0; }
void md_free(md_doc_t *d) {
    for (int i = 0; i < d->n; i++) free(d->lines[i].text);
    free(d->lines);
    d->lines = NULL; d->n = d->cap = 0;
}
void md_reset(md_doc_t *d) {
    for (int i = 0; i < d->n; i++) free(d->lines[i].text);
    d->n = 0;
}
void md_add(md_doc_t *d, const char *text, int color) {
    if (!text) return;
    if (d->n >= d->cap) {
        d->cap = d->cap ? d->cap * 2 : 16;
        d->lines = realloc(d->lines, sizeof(md_line_t) * (size_t)d->cap);
    }
    d->lines[d->n].text = xstrdup(text);
    d->lines[d->n].color = color;
    d->n++;
}
void md_addf(md_doc_t *d, int color, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return; }
    char *buf = malloc((size_t)n + 1);
    vsnprintf(buf, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    md_add(d, buf, color);
    free(buf);
}

char *md_split_thinking(const char *text, char **clean_out) {
    const char *open = strstr(text, "<think>");
    char *thinking = NULL;
    if (open) {
        const char *close = strstr(open + 7, "</think>");
        if (close) {
            size_t tl = (size_t)(close - (open + 7));
            thinking = xstrndup(open + 7, tl);
            str_trim(thinking);
            sbuf_t out;
            sbuf_init(&out);
            sbuf_append_n(&out, text, (size_t)(open - text));
            sbuf_append(&out, close + 8);
            *clean_out = sbuf_detach(&out);
            sbuf_free(&out);
            return thinking;
        }
    }
    *clean_out = xstrdup(text);
    return thinking;
}

/* ---- inline formatting helpers ---- */

typedef struct {
    sbuf_t b;
    int    color;
    int    start_color;
    bool   bold;
    bool   italic;
} inline_ctx;

static void inline_flush(inline_ctx *c, md_doc_t *d) {
    if (c->b.len == 0) return;
    char *s = sbuf_detach(&c->b);
    md_add(d, s, c->color == -1 ? c->start_color : c->color);
    free(s);
}

/* Render simple inline markdown (**bold**, *italic*, `code`) for one plain line.
 * We keep it intentionally simple: apply a single color per segment. */
static void render_inline(md_doc_t *d, const char *line, int base_color) {
    inline_ctx c = {0};
    sbuf_init(&c.b);
    c.color = -1;
    c.start_color = base_color;
    c.bold = false;
    c.italic = false;

    const char *p = line;
    while (*p) {
        if (p[0] == '`') {
            /* find closing backtick */
            const char *end = strchr(p + 1, '`');
            if (end) {
                inline_flush(&c, d);
                sbuf_append_n(&c.b, p + 1, (size_t)(end - p - 1));
                c.color = MD_CODE;
                inline_flush(&c, d);
                c.color = -1;
                p = end + 1;
                continue;
            }
        }
        if (p[0] == '*' && p[1] == '*') {
            const char *end = strstr(p + 2, "**");
            if (end) {
                inline_flush(&c, d);
                c.bold = true;
                c.color = MD_HEADING;
                sbuf_append_n(&c.b, p + 2, (size_t)(end - (p + 2)));
                inline_flush(&c, d);
                c.bold = false;
                c.color = -1;
                p = end + 2;
                continue;
            }
        }
        if (p[0] == '*' && p[1] != ' ' && p[1] != '\0') {
            const char *end = strchr(p + 1, '*');
            if (end) {
                inline_flush(&c, d);
                c.color = base_color == MD_DEFAULT ? MD_DEFAULT : base_color;
                sbuf_append_n(&c.b, p + 1, (size_t)(end - (p + 1)));
                inline_flush(&c, d);
                c.color = -1;
                p = end + 1;
                continue;
            }
        }
        if (p[0] == '[') {
            /* bare link: [text](url) -> text */
            const char *br = strchr(p + 1, ']');
            if (br && br[1] == '(') {
                const char *end = strchr(br + 2, ')');
                if (end) {
                    sbuf_append_n(&c.b, p + 1, (size_t)(br - (p + 1)));
                    p = end + 1;
                    continue;
                }
            }
        }
        sbuf_append_n(&c.b, p, 1);
        p++;
    }
    inline_flush(&c, d);
    sbuf_free(&c.b);
}

static void md_render_plain(md_doc_t *d, const char *text) {
    /* split by lines */
    const char *p = text;
    const char *nl;
    while ((nl = strchr(p, '\n')) != NULL) {
        char *line = xstrndup(p, (size_t)(nl - p));
        md_add(d, line, MD_DEFAULT);
        free(line);
        p = nl + 1;
    }
    if (*p) md_add(d, p, MD_DEFAULT);
}

void md_render(md_doc_t *d, const char *text, bool is_user) {
    if (!text) return;
    if (is_user) {
        md_render_plain(d, text);
        for (int i = 0; i < d->n; i++) {
            if (d->lines[i].color == MD_DEFAULT) d->lines[i].color = MD_GREEN;
        }
        return;
    }

    /* handle thinking block */
    char *clean = NULL;
    char *thinking = md_split_thinking(text, &clean);
    if (thinking && *thinking) {
        md_add(d, "── Model Thinking ───────────────────────────", MD_DIM);
        md_render_plain(d, thinking);
        /* color thinking lines dim */
        for (int i = d->n - 1; i >= 0; i--) {
            if (d->lines[i].color == MD_DEFAULT) d->lines[i].color = MD_DIM;
        }
        md_add(d, "──────────────────────────────────────────────", MD_DIM);
    } else {
        clean = xstrdup(text);
    }
    free(thinking);

    /* process line by line */
    const char *p = clean;
    const char *nl;
    bool in_code = false;
    char codebuf[65536];
    size_t cblen = 0;

    sbuf_t rest;
    sbuf_init(&rest);

    while (*p) {
        nl = strchr(p, '\n');
        size_t llen = nl ? (size_t)(nl - p) : strlen(p);
        char *line = xstrndup(p, llen);
        str_trim(line);

        if (!in_code) {
            /* fenced code block start */
            if (str_has_prefix(line, "```")) {
                if (cblen) {
                    md_add(d, codebuf, MD_CODE);
                    cblen = 0;
                }
                in_code = true;
                free(line);
                p = nl ? nl + 1 : p + llen;
                continue;
            }
            if (line[0] == '\0') {
                md_add(d, "", MD_DEFAULT);
                free(line);
                p = nl ? nl + 1 : p + llen;
                continue;
            }
            if (line[0] == '#') {
                int level = 0;
                while (line[level] == '#') level++;
                const char *restp = str_trim(line + level);
                md_addf(d, MD_HEADING, "%s", restp);
                free(line);
                p = nl ? nl + 1 : p + llen;
                continue;
            }
            if (str_has_prefix(line, "---") || str_has_prefix(line, "***")) {
                md_add(d, "──────────────", MD_DIM);
                free(line);
                p = nl ? nl + 1 : p + llen;
                continue;
            }
            if (line[0] == '>') {
                char *q = str_trim(line + 1);
                md_addf(d, MD_QUOTE, "▎ %s", q);
                free(line);
                p = nl ? nl + 1 : p + llen;
                continue;
            }
            if (str_has_prefix(line, "- ") || str_has_prefix(line, "* ")) {
                render_inline(d, line, MD_DEFAULT);
                free(line);
                p = nl ? nl + 1 : p + llen;
                continue;
            }
            if ((line[0] >= '0' && line[0] <= '9') && strstr(line, ". ")) {
                render_inline(d, line, MD_DEFAULT);
                free(line);
                p = nl ? nl + 1 : p + llen;
                continue;
            }
            /* heading-like bold lines `**...**` at line start */
            render_inline(d, line, MD_DEFAULT);
            free(line);
        } else {
            if (str_has_prefix(line, "```")) {
                in_code = false;
                md_add(d, codebuf, MD_CODE);
                cblen = 0;
                free(line);
                p = nl ? nl + 1 : p + llen;
                continue;
            }
            if (cblen + llen + 2 < sizeof(codebuf)) {
                if (cblen) codebuf[cblen++] = '\n';
                memcpy(codebuf + cblen, p, llen);
                cblen += llen;
                codebuf[cblen] = '\0';
            }
            free(line);
        }
        p = nl ? nl + 1 : p + llen;
    }
    if (cblen) {
        md_add(d, codebuf, MD_CODE);
    }
    free(clean);
    sbuf_free(&rest);
}