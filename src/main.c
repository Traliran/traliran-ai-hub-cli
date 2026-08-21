/*
 * main.c - Traliran AI Hub CLI entry point.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>

#include "http.h"
#include "tui.h"
#include "app.h"

int main(void) {
    http_global_init();
    tui_init();
    app_run();
    tui_end();
    http_global_cleanup();
    return 0;
}
