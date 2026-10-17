/*
 * ide.h - AI IDE: VFS on disk, multi-repository workspaces, real-git version
 * control, custom bots, agent tools, and multi-chat agent sessions.
 */
#ifndef HUB_IDE_H
#define HUB_IDE_H

#include <pthread.h>
#include <stdbool.h>

#include "sessions.h"

/* ---------- repositories (each one is a real folder on disk) ---------- */
void        ide_repos_load(void);              /* load index, migrate legacy workspace */
int         ide_repos_count(void);
const char *ide_repo_name(int idx);            /* "" when out of range */
const char *ide_repo_path(int idx);            /* "" when out of range */
int         ide_repo_active_index(void);
const char *ide_repo_active_path(void);        /* active repo folder (never NULL) */
void        ide_repo_create(const char *name, const char *path);
void        ide_repo_switch(int idx);
void        ide_repo_remove(int idx);          /* forget repo; files on disk are kept */

/* ---------- branches (real git under the active repo) ---------- */
bool   ide_git_available(void);
char  *ide_branch_current(void);               /* malloc'd ("" if unknown) */
char **ide_branch_list(int *n);                /* malloc'd list of names */
void   ide_branch_free_list(char **list, int n);
bool   ide_branch_create(const char *name);
bool   ide_branch_switch(const char *name);
bool   ide_branch_delete(const char *name);    /* also drops that branch's stored commits */

/* ---------- VFS ---------- */
void        ide_init(void);                    /* ensure workspace + defaults */
char      **ide_list_files(int *n);            /* sorted full paths (malloc'd list) */
void        ide_free_list(char **list, int n);
char       *ide_read_file(const char *path);   /* malloc'd or NULL */
bool        ide_write_file(const char *path, const char *content);
bool        ide_delete_file(const char *path);
bool        ide_delete_folder(const char *path);
bool        ide_new_file(const char *path);    /* refuse overwrite */
bool        ide_new_folder(const char *path);  /* mkdir -p; empty folders show once they hold files */
bool        ide_rename_path(const char *old_path, const char *new_path);
const char *ide_language(const char *path);
char       *ide_file_tree_text(void);          /* compact tree for agent context */
bool        ide_export_project(const char *path);
bool        ide_import_project(const char *path);

/* ---------- version control (git-backed snapshots) ---------- */
void   vc_load(void);
void   vc_create_commit(const char *message);
bool   vc_revert_to(const char *id);
int    vc_count(void);                          /* commits on the active branch */
char  *vc_summary(int idx);                     /* malloc'd "message (sha · branch · files) date" */
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
typedef chatlog_t agent_chat_t;

/* ---------- agent multi-chat sessions ---------- */
void         agent_sessions_load(void);
void         agent_sessions_save(void);
int          agent_sessions_count(void);
const char  *agent_session_name(int idx);
int          agent_session_current(void);
void         agent_session_new(void);
void         agent_session_select(int idx);
void         agent_session_rename(int idx, const char *name);
void         agent_session_delete(int idx);
void         agent_session_autoname(const char *text);
void         agent_session_clear(void);
agent_chat_t *agent_session_chat(void);        /* active log or NULL */

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
