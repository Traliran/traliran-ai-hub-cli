/*
 * ide.h - AI IDE: VFS on disk, version control, custom bots, agent tools.
 */
#ifndef HUB_IDE_H
#define HUB_IDE_H

#include <pthread.h>
#include <stdbool.h>

/* ---------- VFS ---------- */
void        ide_init(void);                    /* ensure workspace + defaults */
char      **ide_list_files(int *n);            /* sorted full paths (malloc'd list) */
void        ide_free_list(char **list, int n);
char       *ide_read_file(const char *path);   /* malloc'd or NULL */
bool        ide_write_file(const char *path, const char *content);
bool        ide_delete_file(const char *path);
bool        ide_delete_folder(const char *path);
const char *ide_language(const char *path);
bool        ide_new_file(const char *path);    /* refuse overwrite unless empty */
char       *ide_file_tree_text(void);          /* compact tree for agent context */
bool        ide_export_project(const char *path);
bool        ide_import_project(const char *path);

/* ---------- version control ---------- */
void   vc_load(void);
void   vc_create_commit(const char *message);
bool   vc_revert_to(const char *id);
int    vc_count(void);
char  *vc_summary(int idx);                    /* malloc'd "message (files) YYYY-MM-DD" */
char  *vc_id(int idx);
void   vc_clear(void);

/* ---------- custom bots ---------- */
typedef struct {
    char *id;
    char *name;
    char *prompt;
    char *model;
    double temp;
} ide_bot_t;

int        ide_bots_count(void);
ide_bot_t *ide_bots_get(int idx);
ide_bot_t *ide_bots_active(void);
void       ide_bots_reload(void);
void       ide_bot_save(ide_bot_t *bot);       /* NULL -> create new */
void       ide_bot_delete(int idx);
void       ide_bot_set_active(int idx);        /* -1 to clear */
const char *ide_bot_active_id(void);
char      *ide_bots_export_json(void);
bool       ide_bots_import_json(const char *json);

/* ---------- agent chat ---------- */
typedef struct {
    char *role;
    char *content;
} agent_msg_t;

typedef struct {
    agent_msg_t  *msgs;
    int           n;
    pthread_mutex_t mtx;
} agent_chat_t;

void  agent_chat_init(agent_chat_t *c);
void  agent_chat_free(agent_chat_t *c);
void  agent_chat_clear(agent_chat_t *c);
char *agent_chat_snapshot(agent_chat_t *c);    /* rendered-ish plain text */
int   agent_chat_count(agent_chat_t *c);

/* run the agent tool-loop (blocking; call from worker thread) */
typedef struct {
    char        *model;
    char        *user_message;
    char        *progress;      /* progress_t* cast via helper */
    void        *progress_ptr;
    agent_chat_t *chat;
    volatile int *stop;
} agent_arg_t;

void *agent_worker(void *arg);

#endif /* HUB_IDE_H */