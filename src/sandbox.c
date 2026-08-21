/*
 * sandbox.c - run generated HTML/JS/CSS snippets in the system browser.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"
#include "sandbox.h"

bool sandbox_run(const char *code) {
    if (!code) return false;

    sbuf_t page;
    sbuf_init(&page);

    const char *c = code;
    /* skip leading whitespace */
    while (*c && (*c == ' ' || *c == '\n' || *c == '\r' || *c == '\t')) c++;

    if (str_has_prefix(c, "<!doctype") || str_has_prefix(c, "<html") ||
        strstr(c, "<body") || strstr(c, "<style") || strstr(c, "<script")) {
        sbuf_append(&page, c);
    } else {
        /* treat as JS (or CSS) snippet, wrap in a minimal page */
        sbuf_append(&page,
                    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
                    "<style>body{font-family:system-ui,sans-serif;margin:2rem;}</style>"
                    "</head><body>\n");
        sbuf_append(&page,
                    "<pre id=\"out\" style=\"white-space:pre-wrap;background:#111;"
                    "color:#7CFC00;padding:1rem;border-radius:8px;min-height:40px;\">console output</pre>\n");
        sbuf_append(&page, "<script>\n");
        sbuf_append(&page,
                    "try{\n(function(){\n");
        sbuf_append(&page, c);
        sbuf_append(&page,
                    "\n}).call(this);\n}catch(e){\n"
                    "var out=document.getElementById('out');\n"
                    "out.textContent='Error: '+e.message;\n"
                    "out.style.color='#ff5555';\n}\n"
                    "</script></body></html>\n");
    }

    char path[256];
    snprintf(path, sizeof(path), "/tmp/traliran-sandbox-%d.html", (int)getpid());
    bool ok = write_file(path, page.s);
    sbuf_free(&page);
    if (ok) open_in_browser(path);
    return ok;
}