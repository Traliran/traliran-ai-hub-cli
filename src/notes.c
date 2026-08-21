/*
 * notes.c - notes management.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "util.h"
#include "storage.h"
#include "config.h"
#include "chat.h"
#include "rag.h"
#include "notes.h"

static note_t *note_new(void) {
    note_t *n = calloc(1, sizeof(note_t));
    n->id = xasprintf("note_%lld", (long long)now_ms());
    n->title = xstrdup("");
    n->content = xstrdup("");
    n->updated_at = now_ms();
    return n;
}

static void note_free(note_t *n) {
    free(n->id);
    free(n->title);
    free(n->content);
    for (int i = 0; i < n->ntags; i++) free(n->tags[i]);
    free(n->tags);
    free(n);
}

void notes_load(notes_t *n) {
    memset(n, 0, sizeof(*n));
    const char *raw = storage_get("gem_notes");
    if (raw[0]) {
        cJSON *arr = cJSON_Parse(raw);
        if (arr && cJSON_IsArray(arr)) {
            int count = cJSON_GetArraySize(arr);
            for (int i = 0; i < count; i++) {
                cJSON *j = cJSON_GetArrayItem(arr, i);
                cJSON *id = cJSON_GetObjectItem(j, "id");
                cJSON *title = cJSON_GetObjectItem(j, "title");
                cJSON *content = cJSON_GetObjectItem(j, "content");
                cJSON *tags = cJSON_GetObjectItem(j, "tags");
                cJSON *upd = cJSON_GetObjectItem(j, "updatedAt");
                note_t *note = note_new();
                free(note->id);
                note->id = xstrdup(id && id->valuestring ? id->valuestring : note->id);
                free(note->title);
                note->title = xstrdup(title && title->valuestring ? title->valuestring : "");
                free(note->content);
                note->content = xstrdup(content && content->valuestring ? content->valuestring : "");
                if (upd && cJSON_IsNumber(upd)) note->updated_at = (long long)upd->valuedouble;
                if (tags && cJSON_IsArray(tags)) {
                    int tn = cJSON_GetArraySize(tags);
                    for (int k = 0; k < tn; k++) {
                        cJSON *t = cJSON_GetArrayItem(tags, k);
                        if (t && cJSON_IsString(t) && t->valuestring) {
                            note->tags = realloc(note->tags, sizeof(char *) * (size_t)(note->ntags + 1));
                            note->tags[note->ntags++] = xstrdup(t->valuestring);
                        }
                    }
                }
                n->list = realloc(n->list, sizeof(note_t *) * (size_t)(n->n + 1));
                n->list[n->n++] = note;
            }
        }
        if (arr) cJSON_Delete(arr);
    }
    if (n->n == 0) {
        notes_new(n);
    } else {
        n->current = 0;
    }
}

void notes_save(notes_t *n) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n->n; i++) {
        note_t *note = n->list[i];
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "id", note->id);
        cJSON_AddStringToObject(j, "title", note->title);
        cJSON_AddStringToObject(j, "content", note->content);
        cJSON_AddNumberToObject(j, "updatedAt", (double)note->updated_at);
        cJSON *tags = cJSON_CreateArray();
        for (int k = 0; k < note->ntags; k++)
            cJSON_AddItemToArray(tags, cJSON_CreateString(note->tags[k]));
        cJSON_AddItemToObject(j, "tags", tags);
        cJSON_AddItemToArray(arr, j);
    }
    char *json = cJSON_PrintUnformatted(arr);
    if (json) {
        storage_set("gem_notes", json);
        free(json);
    }
    cJSON_Delete(arr);
    storage_save();
}

void notes_new(notes_t *n) {
    note_t *note = note_new();
    n->list = realloc(n->list, sizeof(note_t *) * (size_t)(n->n + 1));
    for (int i = n->n; i > 0; i--) n->list[i] = n->list[i - 1];
    n->list[0] = note;
    n->n++;
    n->current = 0;
}

void notes_delete(notes_t *n, int idx) {
    if (idx < 0 || idx >= n->n) return;
    note_free(n->list[idx]);
    for (int i = idx; i < n->n - 1; i++) n->list[i] = n->list[i + 1];
    n->n--;
    if (n->n == 0) {
        notes_new(n);
    } else if (n->current >= n->n) {
        n->current = n->n - 1;
    }
}

note_t *notes_current(notes_t *n) {
    if (n->n == 0) return NULL;
    if (n->current < 0 || n->current >= n->n) n->current = 0;
    return n->list[n->current];
}

void notes_select(notes_t *n, int idx) {
    if (idx >= 0 && idx < n->n) n->current = idx;
}

void notes_set_current_fields(note_t *note, const char *title, const char *content) {
    if (!note) return;
    free(note->title);
    note->title = xstrdup(title ? title : "");
    free(note->content);
    note->content = xstrdup(content ? content : "");
    note->updated_at = now_ms();
    notes_recompute_tags(note);
}

void notes_recompute_tags(note_t *note) {
    for (int i = 0; i < note->ntags; i++) free(note->tags[i]);
    free(note->tags);
    note->tags = NULL;
    note->ntags = 0;

    const char *p = note->content;
    while ((p = strchr(p, '#')) != NULL) {
        p++;
        const char *start = p;
        while (*p && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                      (*p >= '0' && *p <= '9') || *p == '_')) p++;
        if (p > start) {
            char *tag = xstrndup(start, (size_t)(p - start));
            bool exists = false;
            for (int i = 0; i < note->ntags; i++)
                if (!strcmp(note->tags[i], tag)) { exists = true; break; }
            if (!exists) {
                note->tags = realloc(note->tags, sizeof(char *) * (size_t)(note->ntags + 1));
                note->tags[note->ntags++] = tag;
            } else {
                free(tag);
            }
        }
    }
}

char *notes_as_markdown(const note_t *note) {
    sbuf_t b;
    sbuf_init(&b);
    sbuf_appendf(&b, "# %s\n\n%s", note->title, note->content);
    return sbuf_detach(&b);
}

void notes_import_md(notes_t *n, const char *path) {
    char *content = read_file(path);
    if (!content) return;
    note_t *note = note_new();
    free(note->title);
    note->title = xstrdup("");
    if (str_has_suffix(path, ".md")) {
        const char *slash = strrchr(path, '/');
        const char *base = slash ? slash + 1 : path;
        char *name = xstrdup(base);
        if (strlen(name) > 3) name[strlen(name) - 3] = '\0';
        note->title = name;
    }
    free(note->content);
    note->content = content;
    notes_recompute_tags(note);
    n->list = realloc(n->list, sizeof(note_t *) * (size_t)(n->n + 1));
    for (int i = n->n; i > 0; i--) n->list[i] = n->list[i - 1];
    n->list[0] = note;
    n->n++;
    n->current = 0;
}

bool notes_export_md_to(note_t *note, const char *path) {
    char *md = notes_as_markdown(note);
    bool ok = write_file(path, md);
    free(md);
    return ok;
}

bool notes_export_rag(notes_t *n) {
    note_t *note = notes_current(n);
    if (!note) return false;
    char *content = str_dup_trim(note->content);
    if (!content[0]) { free(content); return false; }
    free(content);
    /* Export the full note as a Markdown document (title header + body) straight
     * into the RAG knowledge base - no file is written to disk. */
    char *name = str_dup_trim(note->title);
    if (!name[0]) {
        free(name);
        name = xstrdup("Untitled Note");
    }
    if (!str_has_suffix(name, ".md") && !str_has_suffix(name, ".txt")) {
        char *tmp = xasprintf("%s.md", name);
        free(name);
        name = tmp;
    }
    char *md = notes_as_markdown(note);
    kb_t kb;
    kb_load(&kb);
    kb_add(&kb, name, md);
    kb_save(&kb);
    kb_clear(&kb);
    free(name);
    free(md);
    return true;
}

void notes_refresh_list(notes_t *n) { (void)n; }

/* =================== AI complement =================== */

void *complement_worker(void *arg) {
    complement_arg_t *a = arg;
    const char *sys = "You are a helpful note-completion assistant. Your task is to expand and complement the current note. Use Markdown formatting. Keep the tone consistent with the original text. Do not repeat the existing text, just add meaningful continuation or detailed expansion. Answer in the same language as the note content.";
    cJSON *arr = cJSON_CreateArray();
    cJSON *sysm = cJSON_CreateObject();
    cJSON_AddStringToObject(sysm, "role", "system");
    cJSON_AddStringToObject(sysm, "content", sys);
    cJSON_AddItemToArray(arr, sysm);
    cJSON *userm = cJSON_CreateObject();
    cJSON_AddStringToObject(userm, "role", "user");
    cJSON_AddStringToObject(userm, "content", xasprintf("Please complement and expand this note:\n\n%s", a->note->content));
    cJSON_AddItemToArray(arr, userm);
    char *msgs = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    api_result_t res;
    int rc = api_complete(a->model, msgs, 0.7, 1.0, 5000, &res);
    free(msgs);
    if (rc != 0) {
        a->error = xstrdup(res.content ? res.content : "AI returned an empty response");
        api_result_free(&res);
        return NULL;
    }
    if (!res.content[0]) {
        a->error = xstrdup("AI returned an empty response");
    } else {
        a->result = xstrdup(res.content);
    }
    api_result_free(&res);
    return NULL;
}