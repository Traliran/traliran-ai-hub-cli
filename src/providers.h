/*
 * providers.h - supported AI providers table (mirrors web PROVIDERS).
 */
#ifndef HUB_PROVIDERS_H
#define HUB_PROVIDERS_H

#include <stdbool.h>

typedef struct {
    const char *id;          /* "groq", "claude", ... */
    const char *url;         /* base endpoint */
    bool        has_key;     /* requires API key */
    const char *type;        /* "openai" or "anthropic" */
} provider_t;

const provider_t *providers_find(const char *id);       /* NULL if not found */
const provider_t *providers_get(const char *id);        /* falls back to groq */
const provider_t **providers_all(void);                 /* NULL-terminated */

#endif /* HUB_PROVIDERS_H */