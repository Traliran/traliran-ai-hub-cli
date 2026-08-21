/*
 * store.h - assistant store (free presets + paid marketplace links).
 */
#ifndef HUB_STORE_H
#define HUB_STORE_H

typedef struct {
    const char *name;
    const char *description;
    const char *link;    /* for paid; NULL for free */
    const char *prompt;  /* for free assistants */
} store_item_t;

typedef struct {
    store_item_t **free;
    int            nfree;
    store_item_t **paid;
    int            npaid;
} store_t;

void store_init(store_t *s);

#endif /* HUB_STORE_H */