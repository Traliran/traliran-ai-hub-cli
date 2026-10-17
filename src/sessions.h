/*
 * sessions.h - reusable multi-chat session store shared by the IDE agent and RAG.
 * Each session owns a chat log; the whole list is persisted as a JSON array
 * under a storage key. Format mirrors the web app's ide_agent_sessions /
 * gem_rag_sessions entries: [{id, name, createdAt, messages:[{role, content}]}].
 */
#ifndef HUB_SESSIONS_H
#define HUB_SESSIONS_H

#include <pthread.h>
#include <stdbool.h>

/* ---------- chat log (thread-safe message list) ---------- */
typedef struct {
    char *role;
    char *content;
} chat_msg_t;

typedef struct {
    chat_msg_t     *msgs;
    int             n;
    pthread_mutex_t mtx;
} chatlog_t;

void  chatlog_init(chatlog_t *c);
void  chatlog_free(chatlog_t *c);
void  chatlog_clear(chatlog_t *c);
void  chatlog_push(chatlog_t *c, const char *role, const char *content);
char *chatlog_snapshot(chatlog_t *c);          /* rendered plain text (malloc'd) */
int   chatlog_count(chatlog_t *c);

/* ---------- session store (newest first) ---------- */
typedef struct {
    char      *id;
    char      *name;
    long long  created;
    chatlog_t  log;
} chatlog_session_t;

typedef struct {
    chatlog_session_t *list;
    int                n;
    int                current;
    const char        *key;      /* storage key, owned by the caller */
} session_store_t;

void        session_store_init(session_store_t *s, const char *storage_key);
void        session_store_load(session_store_t *s);   /* (re)load; always leaves >=1 session */
void        session_store_free(session_store_t *s);
void        session_store_save(session_store_t *s);

int         session_store_count(session_store_t *s);
const char *session_store_name(session_store_t *s, int idx);
int         session_store_current(session_store_t *s);
chatlog_t  *session_store_chat(session_store_t *s);   /* active log or NULL */

void        session_store_new(session_store_t *s);
void        session_store_select(session_store_t *s, int idx);
void        session_store_rename(session_store_t *s, int idx, const char *name);
void        session_store_delete(session_store_t *s, int idx);
void        session_store_clear(session_store_t *s);  /* clear the active log */
void        session_store_autoname(session_store_t *s, const char *text);

#endif /* HUB_SESSIONS_H */
