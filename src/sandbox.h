/*
 * sandbox.h - run generated HTML/JS/CSS snippets in the system browser.
 */
#ifndef HUB_SANDBOX_H
#define HUB_SANDBOX_H

#include <stdbool.h>

/* Writes the snippet to a temp HTML file and opens it in the browser. */
bool sandbox_run(const char *code);

#endif /* HUB_SANDBOX_H */