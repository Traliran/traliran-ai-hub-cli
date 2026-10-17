/*
 * sessions.c - reusable multi-chat session store (IDE agent & RAG).
 * Persistence format matches the web app so chats survive reloads.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "util.h"
#include "storage.h"
#include "sessions.h"

/* =================== chat log =================== */

void chatlog_init(chatlog_t *c) {
    memset(c, 0, sizeof(*c));
    pthread_mutex_init(&c->mtx, NULL);
}

void chatlog_clear(chatlog_t *c) {
    pthread_mutex_lock(&c->mtx);
    for (int i = 0; i < c->n; i++) {
        free(c->msgs[i].role);
        free(c->msgs[i].content);
    }
    free(c->msgs);
    c->msgs = NULL;
    c->n = 0;
    pthread_mutex_unlock(&c->mtx);
}

void chatlog_free(chatlog_t *c) {
    chatlog_clear(c);
    pthread_mutex_destroy(&c->mtx);
}

void chatlog_push(chatlog_t *c, const char *role, const char *content) {
    pthread_mutex_lock(&c->mtx);
    c->msgs = realloc(c->msgs, sizeof(chat_msg_t) * (size_t)(c->n + 1));
    c->msgs[c->n].role = xstrdup(role ? role : "assistant");
    c->msgs[c->n].content = xstrdup(content ? content : "");
    c->n++;
    pthread_mutex_unlock(&c->mtx);
}

int chatlog_count(chatlog_t *c) {
    pthread_mutex_lock(&c->mtx);
    int n = c->n;
    pthread_mutex_unlock(&c->mtx);
    return n;
}

char *chatlog_snapshot(chatlog_t *c) {
    pthread_mutex_lock(&c->mtx);
    sbuf_t b;
    sbuf_init(&b);
    for (int i = 0; i < c->n; i++) {
        sbuf_appendf(&b, "[%s] %s\n", c->msgs[i].role, c->msgs[i].content);
        sbuf_append(&b, "\n");
    }
    pthread_mutex_unlock(&c->mtx);
    char *out = sbuf_detach(&b);
    sbuf_free(&b);
    return out;
}

/* =================== session store =================== */

static void session_free(chatlog_session_t *s) {
    free(s->id);
    free(s->name);
    chatlog_free(&s->log);
}

void session_store_init(session_store_t *s, const char *storage_key) {
    memset(s, 0, sizeof(*s));
    s->key = storage_key;
}

void session_store_free(session_store_t *s) {
    for (int i = 0; i < s->n; i++) session_free(&s->list[i]);
    free(s->list);
    s->list = NULL;
    s->n = 0;
    s->current = 0;
}

void session_store_new(session_store_t *s) {
    s->list = realloc(s->list, sizeof(chatlog_session_t) * (size_t)(s->n + 1));
    /* newest sessions live at index 0 */
    for (int i = s->n; i > 0; i--) s->list[i] = s->list[i - 1];
    chatlog_session_t *ns = &s->list[0];
    ns->id = xasprintf("chat_%lld_%d", (long long)now_ms(), s->n + 1);
    ns->name = xasprintf("Chat #%d", s->n + 1);
    ns->created = now_ms();
    chatlog_init(&ns->log);
    s->n++;
    s->current = 0;
    session_store_save(s);
}

void session_store_load(session_store_t *s) {
    session_store_free(s);
    const char *raw = storage_get(s->key);
    if (raw[0]) {
        cJSON *arr = cJSON_Parse(raw);
        if (arr && cJSON_IsArray(arr)) {
            int n = cJSON_GetArraySize(arr);
            for (int i = 0; i < n; i++) {
                cJSON *j = cJSON_GetArrayItem(arr, i);
                cJSON *id = cJSON_GetObjectItem(j, "id");
                cJSON *name = cJSON_GetObjectItem(j, "name");
                cJSON *created = cJSON_GetObjectItem(j, "createdAt");
                cJSON *msgs = cJSON_GetObjectItem(j, "messages");
                s->list = realloc(s->list, sizeof(chatlog_session_t) * (size_t)(s->n + 1));
                chatlog_session_t *ns = &s->list[s->n];
                ns->id = xstrdup(id && id->valuestring ? id->valuestring : "chat");
                ns->name = xstrdup(name && name->valuestring ? name->valuestring : "Chat");
                ns->created = created && cJSON_IsNumber(created) ? (long long)created->valuedouble : now_ms();
                chatlog_init(&ns->log);
                if (msgs && cJSON_IsArray(msgs)) {
                    int mn = cJSON_GetArraySize(msgs);
                    for (int k = 0; k < mn; k++) {
                        cJSON *m = cJSON_GetArrayItem(msgs, k);
                        cJSON *role = cJSON_GetObjectItem(m, "role");
                        cJSON *content = cJSON_GetObjectItem(m, "content");
                        if (!role || !role->valuestring) continue;
                        chatlog_push(&ns->log, role->valuestring,
                                     content && content->valuestring ? content->valuestring : "");
                    }
                }
                s->n++;
            }
        }
        if (arr) cJSON_Delete(arr);
    }
    if (s->n == 0) {
        session_store_new(s);
        return;
    }
    s->current = 0;
}

void session_store_save(session_store_t *s) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < s->n; i++) {
        chatlog_session_t *sess = &s->list[i];
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "id", sess->id);
        cJSON_AddStringToObject(j, "name", sess->name);
        cJSON_AddNumberToObject(j, "createdAt", (double)sess->created);
        cJSON *msgs = cJSON_CreateArray();
        pthread_mutex_lock(&sess->log.mtx);
        for (int k = 0; k < sess->log.n; k++) {
            cJSON *m = cJSON_CreateObject();
            cJSON_AddStringToObject(m, "role", sess->log.msgs[k].role);
            cJSON_AddStringToObject(m, "content", sess->log.msgs[k].content);
            cJSON_AddItemToArray(msgs, m);
        }
        pthread_mutex_unlock(&sess->log.mtx);
        cJSON_AddItemToObject(j, "messages", msgs);
        cJSON_AddItemToArray(arr, j);
    }
    char *json = cJSON_PrintUnformatted(arr);
    if (json) {
        storage_set(s->key, json);
        free(json);
    }
    cJSON_Delete(arr);
    storage_save();
}

int session_store_count(session_store_t *s) { return s->n; }

const char *session_store_name(session_store_t *s, int idx) {
    if (idx < 0 || idx >= s->n) return "";
    return s->list[idx].name;
}

int session_store_current(session_store_t *s) { return s->current; }

chatlog_t *session_store_chat(session_store_t *s) {
    if (s->current < 0 || s->current >= s->n) return NULL;
    return &s->list[s->current].log;
}

void session_store_select(session_store_t *s, int idx) {
    if (idx >= 0 && idx < s->n) s->current = idx;
}

void session_store_rename(session_store_t *s, int idx, const char *name) {
    if (idx < 0 || idx >= s->n || !name || !name[0]) return;
    free(s->list[idx].name);
    s->list[idx].name = xstrdup(name);
    session_store_save(s);
}

void session_store_delete(session_store_t *s, int idx) {
    if (idx < 0 || idx >= s->n) return;
    session_free(&s->list[idx]);
    for (int i = idx; i < s->n - 1; i++) s->list[i] = s->list[i + 1];
    s->n--;
    if (s->n == 0) {
        s->list = realloc(s->list, 0);
        session_store_new(s);
        return;
    }
    if (s->current >= s->n) s->current = s->n - 1;
    if (s->current < 0) s->current = 0;
    session_store_save(s);
}

void session_store_clear(session_store_t *s) {
    chatlog_t *log = session_store_chat(s);
    if (!log) return;
    chatlog_clear(log);
    session_store_save(s);
}

void session_store_autoname(session_store_t *s, const char *text) {
    if (s->current < 0 || s->current >= s->n || !text || !text[0]) return;
    const char *name = s->list[s->current].name;
    if (strncmp(name, "Chat #", 6) != 0) return;
    size_t len = strlen(text);
    char buf[64];
    size_t keep = len > 28 ? 28 : len;
    memcpy(buf, text, keep);
    buf[keep] = '\0';
    if (len > 28) {
        strncat(buf, "...", sizeof(buf) - strlen(buf) - 1);
    }
    free(s->list[s->current].name);
    s->list[s->current].name = xstrdup(buf);
    session_store_save(s);
}
