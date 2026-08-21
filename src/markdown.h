/*
 * markdown.h - minimal markdown → colored display-lines renderer.
 */
#ifndef HUB_MARKDOWN_H
#define HUB_MARKDOWN_H

#include <stdbool.h>

/* color ids (map to ncurses color pairs in tui) */
enum {
    MD_DEFAULT = 0,
    MD_ACCENT = 1,   /* cyan/violet */
    MD_GREEN = 2,    /* user / success */
    MD_HEADING = 3,  /* blue bold */
    MD_DIM = 4,      /* gray */
    MD_CODE = 5,     /* yellow-ish */
    MD_QUOTE = 6,
    MD_WARN = 7,     /* amber */
    MD_ERR = 8,      /* red */
    MD_AMBER = 9,
};

typedef struct {
    char *text;
    int   color;
} md_line_t;

typedef struct {
    md_line_t *lines;
    int        n;
    int        cap;
} md_doc_t;

void md_init(md_doc_t *d);
void md_free(md_doc_t *d);
void md_reset(md_doc_t *d);
void md_add(md_doc_t *d, const char *text, int color);
void md_addf(md_doc_t *d, int color, const char *fmt, ...);

/* render markdown text into doc. is_user => plain, colored, no markdown. */
void md_render(md_doc_t *d, const char *text, bool is_user);

/* helper: find/extract thinking block; returns malloc'd thinking text or NULL
 * and strips it from `out` (malloc'd cleaned text). */
char *md_split_thinking(const char *text, char **clean_out);

#endif /* HUB_MARKDOWN_H */