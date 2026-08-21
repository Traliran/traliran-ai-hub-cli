/*
 * rag.h - knowledge base (RAG) management, mirrors web gem_rag_kb store.
 */
#ifndef HUB_RAG_H
#define HUB_RAG_H

#include <pthread.h>
#include <stdbool.h>

#include "notes.h"

typedef struct {
    char *name;
    char *content;
} kb_entry_t;

typedef struct {
    kb_entry_t *items;
    int         n;
} kb_t;

void   kb_load(kb_t *kb);
void   kb_save(const kb_t *kb);
void   kb_clear(kb_t *kb);
int    kb_find(const kb_t *kb, const char *name);
void   kb_add(kb_t *kb, const char *name, const char *content); /* replace by name */
void   kb_remove(kb_t *kb, int idx);
char  *kb_build_context(const kb_t *kb);   /* markdown context for system prompt */
bool   kb_add_file(kb_t *kb, const char *path);                 /* .md/.txt only */

/* ---------- RAG agent chat ---------- */
typedef struct {
    char *role;
    char *content;
} rag_msg_t;

typedef struct {
    rag_msg_t     *msgs;
    int            n;
    pthread_mutex_t mtx;
} rag_chat_t;

void  rag_chat_init(rag_chat_t *c);
void  rag_chat_free(rag_chat_t *c);
void  rag_chat_clear(rag_chat_t *c);
char *rag_chat_snapshot(rag_chat_t *c);    /* rendered-ish plain text */

/* run the RAG agent tool-loop (blocking; call from worker thread) */
typedef struct {
    char          *model;
    char          *user_message;
    rag_chat_t    *chat;
    volatile int  *stop;
    void          *progress_ptr;           /* progress_t* */
    kb_t           kb;                     /* KB loaded on the main thread */
} rag_arg_t;

void *rag_worker(void *arg);

/* export a knowledge base entry into the notes list (prepends a new note) */
bool rag_export_to_notes(const kb_t *kb, int idx, notes_t *notes);

#endif /* HUB_RAG_H */
