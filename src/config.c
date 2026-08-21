/*
 * config.c - app settings (provider, keys, parameters).
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "storage.h"
#include "providers.h"
#include "config.h"

cfg_t g_cfg;

static const char *model_key(const char *provider) {
    static char buf[128];
    snprintf(buf, sizeof(buf), "gem_selected_model_%s", provider);
    return buf;
}

/* parse a number that may have been written with a locale comma decimal point */
static double parse_num(const char *s) {
    char tmp[64];
    size_t n = strlen(s);
    if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
    memcpy(tmp, s, n);
    tmp[n] = '\0';
    for (char *p = tmp; *p; p++)
        if (*p == ',') *p = '.';
    return atof(tmp);
}

void config_load(void) {
    memset(&g_cfg, 0, sizeof(g_cfg));

    const char *provider = storage_get("gem_provider");
    const provider_t *p = providers_find(provider);
    snprintf(g_cfg.provider, sizeof(g_cfg.provider), "%s", (p || provider[0]) ? provider : "groq");

    const char *key_prefix = "gem_key_";
    char keyname[128];
    snprintf(keyname, sizeof(keyname), "%s%s", key_prefix, g_cfg.provider);
    snprintf(g_cfg.api_key, sizeof(g_cfg.api_key), "%s", storage_get(keyname));

    const char *endpoint = storage_get("gem_endpoint_");
    snprintf(keyname, sizeof(keyname), "gem_endpoint_%s", g_cfg.provider);
    endpoint = storage_get(keyname);
    if (endpoint[0]) {
        snprintf(g_cfg.endpoint, sizeof(g_cfg.endpoint), "%s", endpoint);
    } else {
        snprintf(g_cfg.endpoint, sizeof(g_cfg.endpoint), "%s", providers_get(g_cfg.provider)->url);
    }

    snprintf(g_cfg.bot_name, sizeof(g_cfg.bot_name), "%s",
             storage_get("gem_bot_name")[0] ? storage_get("gem_bot_name") : "System AI");
    snprintf(g_cfg.system_prompt, sizeof(g_cfg.system_prompt), "%s", storage_get("gem_system_prompt"));
    snprintf(g_cfg.personal_info, sizeof(g_cfg.personal_info), "%s", storage_get("gem_personal_info"));

    const char *t = storage_get("gem_temp");
    g_cfg.temperature = t[0] ? parse_num(t) : 0.7;
    t = storage_get("gem_topp");
    g_cfg.top_p = t[0] ? parse_num(t) : 1.0;
    t = storage_get("gem_tokens");
    g_cfg.max_tokens = t[0] ? atoi(t) : 2048;

    snprintf(g_cfg.theme, sizeof(g_cfg.theme), "%s",
             storage_get("gem_theme")[0] ? storage_get("gem_theme") : "default");

    snprintf(g_cfg.selected_model, sizeof(g_cfg.selected_model), "%s",
             storage_get(model_key(g_cfg.provider)));
}

void config_save(void) {
    storage_set("gem_provider", g_cfg.provider);
    char keyname[128];
    snprintf(keyname, sizeof(keyname), "gem_key_%s", g_cfg.provider);
    storage_set(keyname, g_cfg.api_key);
    snprintf(keyname, sizeof(keyname), "gem_endpoint_%s", g_cfg.provider);
    storage_set(keyname, g_cfg.endpoint);
    storage_set("gem_bot_name", g_cfg.bot_name);
    storage_set("gem_system_prompt", g_cfg.system_prompt);
    storage_set("gem_personal_info", g_cfg.personal_info);
    char num[64];
    snprintf(num, sizeof(num), "%.2f", g_cfg.temperature);
    storage_set("gem_temp", num);
    snprintf(num, sizeof(num), "%.2f", g_cfg.top_p);
    storage_set("gem_topp", num);
    snprintf(num, sizeof(num), "%d", g_cfg.max_tokens);
    storage_set("gem_tokens", num);
    storage_set("gem_theme", g_cfg.theme);
    storage_set(model_key(g_cfg.provider), g_cfg.selected_model);
    storage_save();
}

const char *config_provider_id(void) { return g_cfg.provider; }

const char *config_endpoint(void) {
    if (g_cfg.endpoint[0]) return g_cfg.endpoint;
    return providers_get(g_cfg.provider)->url;
}

const char *config_api_key(void) { return g_cfg.api_key; }

bool config_key_required(void) {
    return providers_get(g_cfg.provider)->has_key;
}

bool config_has_key(void) {
    if (!config_key_required()) return true;   /* local providers need no key */
    return g_cfg.api_key[0] != '\0';
}