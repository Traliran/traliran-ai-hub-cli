/*
 * app.h - Traliran AI Hub CLI: top-level screen management and main loop.
 */
#ifndef HUB_APP_H
#define HUB_APP_H

/* top-level screens (order matters: Tab cycles 0..5, Ctrl+1..6 jumps) */
typedef enum {
    SCREEN_CHAT = 0,
    SCREEN_NOTES,
    SCREEN_STORE,
    SCREEN_IDE,
    SCREEN_SETTINGS,
    SCREEN_RAG
} screen_t;

extern volatile int app_quit;

void app_run(void);
void draw_all(void);

#endif /* HUB_APP_H */
