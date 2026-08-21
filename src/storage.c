/*
 * storage.c - persistent key/value store backed by a JSON file.
 * Location: <hub_dir>/storage.json
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "cJSON.h"
#include "util.h"
#include "storage.h"

static cJSON *g_store = NULL;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *storage_path(void) {
    static char path[4096];
    if (!path[0]) snprintf(path, sizeof(path), "%s/storage.json", hub_dir());
    return path;
}

void storage_init(void) {
    ensure_dirs();
    pthread_mutex_lock(&g_lock);
    char *raw = read_file(storage_path());
    g_store = cJSON_CreateObject();
    if (raw) {
        cJSON *parsed = cJSON_Parse(raw);
        if (parsed && cJSON_IsObject(parsed)) {
            cJSON_Delete(g_store);
            g_store = parsed;
        } else if (parsed) {
            cJSON_Delete(parsed);
        }
        free(raw);
    }
    pthread_mutex_unlock(&g_lock);
}

void storage_save(void) {
    pthread_mutex_lock(&g_lock);
    if (g_store) {
        char *json = cJSON_Print(g_store);
        if (json) {
            write_file(storage_path(), json);
            free(json);
        }
    }
    pthread_mutex_unlock(&g_lock);
}

const char *storage_get(const char *key) {
    pthread_mutex_lock(&g_lock);
    static char tmp[65536];
    tmp[0] = '\0';
    if (g_store) {
        cJSON *item = cJSON_GetObjectItem(g_store, key);
        if (item && cJSON_IsString(item) && item->valuestring) {
            snprintf(tmp, sizeof(tmp), "%s", item->valuestring);
        }
    }
    pthread_mutex_unlock(&g_lock);
    return tmp;
}

void storage_set(const char *key, const char *value) {
    pthread_mutex_lock(&g_lock);
    if (!g_store) g_store = cJSON_CreateObject();
    cJSON *item = cJSON_GetObjectItem(g_store, key);
    if (item) {
        cJSON_SetValuestring(item, value ? value : "");
    } else {
        cJSON_AddStringToObject(g_store, key, value ? value : "");
    }
    pthread_mutex_unlock(&g_lock);
}

void storage_remove(const char *key) {
    pthread_mutex_lock(&g_lock);
    if (g_store) cJSON_DeleteItemFromObject(g_store, key);
    pthread_mutex_unlock(&g_lock);
}