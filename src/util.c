/*
 * util.c - shared helpers for Traliran AI Hub CLI
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include "util.h"

#define SB_GROW 64

static void sbuf_ensure(sbuf_t *b, size_t extra) {
    if (b->len + extra + 1 > b->cap) {
        size_t ncap = b->cap ? b->cap : SB_GROW;
        while (ncap < b->len + extra + 1) ncap *= 2;
        b->s = realloc(b->s, ncap);
        b->cap = ncap;
    }
}

void sbuf_init(sbuf_t *b) { b->s = NULL; b->len = 0; b->cap = 0; }
void sbuf_free(sbuf_t *b) { free(b->s); b->s = NULL; b->len = b->cap = 0; }

void sbuf_append_n(sbuf_t *b, const char *s, size_t n) {
    if (!s || n == 0) return;
    sbuf_ensure(b, n);
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = '\0';
}

void sbuf_append(sbuf_t *b, const char *s) {
    if (!s) return;
    sbuf_append_n(b, s, strlen(s));
}

void sbuf_appendf(sbuf_t *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return; }
    sbuf_ensure(b, (size_t)n);
    vsnprintf(b->s + b->len, b->cap - b->len, fmt, ap2);
    va_end(ap2);
    b->len += (size_t)n;
}

char *sbuf_detach(sbuf_t *b) {
    char *out = b->s ? b->s : xstrdup("");
    b->s = NULL; b->len = b->cap = 0;
    return out;
}

char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

char *xstrndup(const char *s, size_t n) {
    char *p = malloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char *xasprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return xstrdup(""); }
    char *out = malloc((size_t)n + 1);
    vsnprintf(out, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return out;
}

bool str_has_prefix(const char *s, const char *pre) {
    if (!s || !pre) return false;
    return strncmp(s, pre, strlen(pre)) == 0;
}

bool str_has_suffix(const char *s, const char *suf) {
    if (!s || !suf) return false;
    size_t ls = strlen(s), lf = strlen(suf);
    return ls >= lf && memcmp(s + ls - lf, suf, lf) == 0;
}

static bool is_space_char(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

char *str_trim(char *s) {
    if (!s) return s;
    char *end = s + strlen(s);
    while (end > s && is_space_char((unsigned char)end[-1])) end--;
    *end = '\0';
    char *start = s;
    while (*start && is_space_char((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    return s;
}

void str_tolower(char *s) {
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

char *str_dup_trim(const char *s) {
    if (!s) return NULL;
    char *dup = xstrdup(s);
    return str_trim(dup);
}

bool str_empty(const char *s) { return !s || *s == '\0'; }

bool str_ieq(const char *a, const char *b) {
    if (!a || !b) return a == b;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)sz + 1);
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

bool write_file(const char *path, const char *data) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    size_t n = data ? strlen(data) : 0;
    size_t w = fwrite(data ? data : "", 1, n, f);
    fclose(f);
    return w == n;
}

bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

bool mkdir_p(const char *path) {
    if (!path || !*path) return false;
    char tmp[4096];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return false;
    memcpy(tmp, path, len + 1);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return false;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return false;
    return true;
}

size_t estimate_tokens(const char *text) {
    if (!text) return 0;
    size_t n = strlen(text);
    size_t t = n / 4 + (n % 4 ? 1 : 0);
    return t < 1 ? 1 : t;
}

char *format_usd(double value) {
    return xasprintf("$%.4f", value);
}

char *format_ts(long long ms) {
    time_t sec = (time_t)(ms / 1000);
    struct tm tm;
    localtime_r(&sec, &tm);
    return xasprintf("%04d-%02d-%02d %02d:%02d",
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                     tm.tm_hour, tm.tm_min);
}

long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static char g_hub_dir[4096];
static char g_ws_dir[4096];

const char *hub_dir(void) {
    if (g_hub_dir[0]) return g_hub_dir;
    const char *xdg = getenv("XDG_CACHE_HOME");
    if (xdg && *xdg) {
        snprintf(g_hub_dir, sizeof(g_hub_dir), "%s/traliran-cache", xdg);
    } else {
        const char *home = getenv("HOME");
        snprintf(g_hub_dir, sizeof(g_hub_dir), "%s/.cache/traliran-cache", home ? home : ".");
    }
    return g_hub_dir;
}

const char *hub_workspace_dir(void) {
    if (g_ws_dir[0]) return g_ws_dir;
    snprintf(g_ws_dir, sizeof(g_ws_dir), "%s/workspace", hub_dir());
    return g_ws_dir;
}

bool ensure_dirs(void) {
    return mkdir_p(hub_dir()) && mkdir_p(hub_workspace_dir());
}

void open_in_browser(const char *target) {
    /* fork & exec, best effort */
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        freopen("/dev/null", "r", stdin);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        execlp("xdg-open", "xdg-open", target, (char *)NULL);
        execlp("sensible-browser", "sensible-browser", target, (char *)NULL);
        execlp("firefox", "firefox", target, (char *)NULL);
        _exit(1);
    }
}
