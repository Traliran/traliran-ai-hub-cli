/*
 * notes.h - notes management (CRUD, tags, AI complement, RAG export).
 */
#ifndef HUB_NOTES_H
#define HUB_NOTES_H

#include <stdbool.h>

typedef struct {
    char  *id;
    char  *title;
    char  *content;
    char **tags;
    int    ntags;
    long long updated_at;
} note_t;

typedef struct {
    note_t **list;
    int     n;
    int     current;
    char    search[256];
} notes_t;

void   notes_load(notes_t *n);
void   notes_save(notes_t *n);
void   notes_new(notes_t *n);
void   notes_delete(notes_t *n, int idx);
note_t *notes_current(notes_t *n);
void   notes_select(notes_t *n, int idx);
void   notes_set_current_fields(note_t *note, const char *title, const char *content);
void   notes_recompute_tags(note_t *note);           /* #hashtags -> tags[] */
char  *notes_as_markdown(const note_t *note);        /* "# title\n\ncontent" */
void   notes_import_md(notes_t *n, const char *path);
bool   notes_export_md_to(note_t *note, const char *path);
bool   notes_export_rag(notes_t *n);                 /* true if exported */
void   notes_refresh_list(notes_t *n);               /* nothing needed, kept for clarity */

/* AI complement worker */
typedef struct {
    note_t   *note;
    char     *model;
    char     *error;     /* set by worker */
    char     *result;    /* appended text */
} complement_arg_t;

void *complement_worker(void *arg);

#endif /* HUB_NOTES_H */