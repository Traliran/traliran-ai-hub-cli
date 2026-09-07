/*
 * config.h - app settings (provider, keys, parameters). Mirrors web app's
 * loadApiSettings()/saveApiSettings() on the gem_* storage keys.
 */
#ifndef HUB_CONFIG_H
#define HUB_CONFIG_H

#include <stdbool.h>

#define CFG_MAX_PROMPT 16384

typedef struct {
    char    provider[64];
    char    api_key[2048];
    char    endpoint[1024];
    char    bot_name[256];
    char    system_prompt[CFG_MAX_PROMPT];
    char    personal_info[CFG_MAX_PROMPT];
    double  temperature;
    double  top_p;
    int     max_tokens;
    char    theme[32];
    char    selected_model[1024];
} cfg_t;

extern cfg_t g_cfg;

void config_load(void);
void config_save(void);

const char *config_provider_id(void);
const char *config_endpoint(void);      /* provider default url if custom empty */
const char *config_api_key(void);
bool        config_has_key(void);       /* true when a key is expected but missing */
bool        config_key_required(void);

/* per-provider helpers for cross-provider multi-model */
void   config_get_key_for(const char *provider, char *out, size_t outsz);
void   config_get_endpoint_for(const char *provider, char *out, size_t outsz);
bool   config_has_key_for(const char *provider);
void   config_switch_provider(const char *new_provider); /* preserves old provider's key/endpoint/model and restores new provider's */

#endif /* HUB_CONFIG_H */