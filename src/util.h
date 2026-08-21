/*
 * util.h - shared helpers for Traliran AI Hub CLI
 * C11 / KISS
 */
#ifndef HUB_UTIL_H
#define HUB_UTIL_H

#include <stdbool.h>
#include <stddef.h>

/* growable string buffer */
typedef struct {
    char *s;
    size_t len;
    size_t cap;
} sbuf_t;

void   sbuf_init(sbuf_t *b);
void   sbuf_free(sbuf_t *b);
void   sbuf_append(sbuf_t *b, const char *s);
void   sbuf_append_n(sbuf_t *b, const char *s, size_t n);
void   sbuf_appendf(sbuf_t *b, const char *fmt, ...);
char  *sbuf_detach(sbuf_t *b);          /* transfers ownership, frees buffer struct contents */

char  *xstrdup(const char *s);
char  *xstrndup(const char *s, size_t n);
char  *xasprintf(const char *fmt, ...);

bool   str_has_prefix(const char *s, const char *pre);
bool   str_has_suffix(const char *s, const char *suf);
char  *str_trim(char *s);               /* trims whitespace in place, returns s */
void   str_tolower(char *s);            /* in place */
char  *str_dup_trim(const char *s);     /* malloc'd trimmed copy */
bool   str_empty(const char *s);
bool   str_ieq(const char *a, const char *b); /* case-insensitive equality */

char  *read_file(const char *path);     /* malloc'd content or NULL */
bool   write_file(const char *path, const char *data);
bool   file_exists(const char *path);
bool   mkdir_p(const char *path);

size_t estimate_tokens(const char *text);
char  *format_usd(double value);
char  *format_ts(long long ms);         /* "YYYY-MM-DD HH:MM" */
long long now_ms(void);

/* data directories: ~/.cache/traliran-cache */
const char *hub_dir(void);
const char *hub_workspace_dir(void);    /* ~/.cache/traliran-cache/workspace - IDE VFS on disk */
bool        ensure_dirs(void);

void   open_in_browser(const char *target); /* xdg-open fallback to sensible-browser */

#endif /* HUB_UTIL_H */
