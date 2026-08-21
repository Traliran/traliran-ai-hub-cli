/*
 * chat.h - sessions, messages, and LLM API calls (streaming/multi/debate).
 */
#ifndef HUB_CHAT_H
#define HUB_CHAT_H

#include <pthread.h>
#include <stdbool.h>

typedef struct {
    char *role;      /* "user" | "assistant" */
    char *content;
} msg_t;

typedef struct {
    char *id;
    char *name;
    msg_t *messages;
    int    n, cap;
    char  *system_prompt;
    char  *bot_name;
    pthread_mutex_t mtx;   /* guards messages while a worker may push */
} session_t;

typedef struct {
    session_t **list;
    int        n;
    int        current;   /* index of active session */
} chat_t;

void        chat_load(chat_t *c);
void        chat_save(chat_t *c);
session_t  *chat_current(chat_t *c);
void        chat_new(chat_t *c);
void        chat_delete(chat_t *c, int idx);
void        chat_rename(chat_t *c, int idx, const char *name);
void        chat_select(chat_t *c, int idx);
void        chat_msg_push(session_t *s, const char *role, const char *content);
int         chat_count_assistant(const session_t *s);
char       *chat_session_name(session_t *s);
void        chat_set_session_prompt(session_t *s, const char *system_prompt, const char *bot_name);

/* API result */
typedef struct {
    char *content;
    char *reasoning;
} api_result_t;

void  api_result_free(api_result_t *r);

/* shared streaming buffer */
typedef struct {
    char          *content;    /* accumulated assistant text */
    char          *reasoning;
    pthread_mutex_t mtx;
    volatile int    stop;
    char          *raw;        /* all raw bytes (for error diagnostics) */
    size_t          raw_len;
    size_t          raw_cap;
    char          *linebuf;    /* SSE line buffer */
    size_t          linebuf_len;
    size_t          linebuf_cap;
    char          *error;
    int             errstatus;
} stream_t;

void  stream_init(stream_t *s);
void  stream_free(stream_t *s);
char *stream_snapshot_content(stream_t *s);
char *stream_snapshot_reasoning(stream_t *s);

int   api_complete(const char *model, const char *messages_json,
                   double temperature, double top_p, int max_tokens,
                   api_result_t *out);
/* same as api_complete, but registers the given tools array (JSON) on the request
 * so tool-capable models may call them natively instead of via text JSON */
int   api_complete_agent(const char *model, const char *messages_json, const char *tools_json,
                         double temperature, double top_p, int max_tokens,
                         api_result_t *out);
int   api_stream(const char *model, const char *messages_json,
                 double temperature, double top_p, int max_tokens,
                 stream_t *st);

/* build system prompt mirroring the web app (personal info + language hint) */
char *chat_build_system(const char *user_text);

/* single-model send worker */
typedef struct {
    session_t *session;
    char      *user_text;
    char      *model;
    stream_t   stream;
} send_arg_t;

void *send_worker(void *arg);

/* multi-model parallel worker */
typedef struct {
    char        *model;
    api_result_t res;
    int          err;
    char        *errmsg;
} mm_result_t;

typedef struct {
    char        **models;
    int           nmodels;
    char         *messages_json;
    double        temperature;
    double        top_p;
    int           max_tokens;
    mm_result_t  *results;
    volatile int  stop;
} mm_arg_t;

void *multi_worker(void *arg);

/* progress indicator helper */
typedef struct {
    char             text[256];
    pthread_mutex_t  mtx;
} progress_t;

void progress_set(progress_t *p, const char *fmt, ...);

/* debate worker (3 agents x 2 rounds) */
typedef struct {
    char        *idea;
    char        *model;
    session_t   *session;
    progress_t  *prog;
} debate_arg_t;

void *debate_worker(void *arg);

/* generic non-streaming completion job (summarize, etc.) */
typedef struct {
    char        *model;
    char        *messages_json;
    double       temperature;
    double       top_p;
    int          max_tokens;
    api_result_t res;
} simple_arg_t;

void *simple_complete_worker(void *arg);

#endif /* HUB_CHAT_H */