/*
 * providers.c - supported AI providers table (mirrors web PROVIDERS).
 */
#include <stddef.h>
#include "providers.h"

static const provider_t PROVIDERS[] = {
    { "groq",      "https://api.groq.com/openai/v1",                       true,  "openai" },
    { "google",    "https://generativelanguage.googleapis.com/v1beta/openai", true, "openai" },
    { "openrouter","https://openrouter.ai/api/v1",                          true,  "openai" },
    { "openai",    "https://api.openai.com/v1",                             true,  "openai" },
    { "deepseek",  "https://api.deepseek.com/v1",                           true,  "openai" },
    { "qwen",      "https://dashscope.aliyuncs.com/compatible-mode/v1",     true,  "openai" },
    { "glm",       "https://open.bigmodel.cn/api/paas/v4",                  true,  "openai" },
    { "claude",    "https://api.anthropic.com/v1",                          true,  "anthropic" },
    { "ollama",    "http://localhost:11434/v1",                             false, "openai" },
    { "llamacpp",  "http://localhost:8080/v1",                              false, "openai" },
};

#define NPROVIDERS ((int)(sizeof(PROVIDERS) / sizeof(PROVIDERS[0])))

const provider_t *providers_find(const char *id) {
    if (!id) return NULL;
    for (int i = 0; i < NPROVIDERS; i++) {
        const provider_t *p = &PROVIDERS[i];
        const char *a = id, *b = p->id;
        while (*a && *b) {
            if (*a != *b) break;
            a++; b++;
        }
        if (*a == '\0' && *b == '\0') return p;
    }
    return NULL;
}

const provider_t *providers_get(const char *id) {
    const provider_t *p = providers_find(id);
    return p ? p : &PROVIDERS[0];
}

const provider_t **providers_all(void) {
    static const provider_t *list[NPROVIDERS + 1];
    if (!list[0]) {
        for (int i = 0; i < NPROVIDERS; i++) list[i] = &PROVIDERS[i];
        list[NPROVIDERS] = NULL;
    }
    return list;
}