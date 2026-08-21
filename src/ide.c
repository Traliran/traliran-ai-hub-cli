/*
 * ide.c - AI IDE: VFS on disk, version control, custom bots, agent tools.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <ctype.h>

#include "cJSON.h"
#include "util.h"
#include "storage.h"
#include "config.h"
#include "chat.h"
#include "ide.h"

/* =================== VFS =================== */

static char *ws_path(const char *rel) {
    /* returns malloc'd full path; NULL if unsafe */
    if (!rel) return NULL;
    if (strstr(rel, "..") || rel[0] == '/') return NULL;
    if (rel[0] == '\0') return NULL;
    return xasprintf("%s/%s", hub_workspace_dir(), rel);
}

void ide_init(void) {
    ensure_dirs();
    int n = 0;
    char **list = ide_list_files(&n);
    if (n == 0) {
        ide_write_file("README.md", "# Welcome to Traliran AI IDE\n\nStart coding! The AI agent can help you.\n");
        ide_write_file("index.html", "<!DOCTYPE html>\n<html>\n<head>\n    <title>My App</title>\n</head>\n<body>\n    <h1>Hello World</h1>\n    <script src=\"app.js\"></script>\n</body>\n</html>\n");
        ide_write_file("app.js", "// Your JavaScript code here\nconsole.log(\"Hello from AI IDE!\");\n");
        ide_write_file("styles.css", "/* Your styles here */\nbody {\n    font-family: system-ui;\n    margin: 2rem;\n}\n");
    }
    ide_free_list(list, n);
}

char **ide_list_files(int *n_out) {
    /* recursive walk */
    char **files = NULL;
    int n = 0;
    /* simple stack-free recursion via helper */
    /* collect via opendir walk */
    /* use a helper that appends relative paths */
    /* simpler: walk with recursion using index-based approach */
    const char *base = hub_workspace_dir();
    DIR *d = opendir(base);
    if (!d) { *n_out = 0; return NULL; }
    struct dirent *de;
    while ((de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        char full[4200];
        snprintf(full, sizeof(full), "%s/%s", base, de->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            DIR *sub = opendir(full);
            if (sub) {
                struct dirent *sde;
                while ((sde = readdir(sub))) {
                    if (!strcmp(sde->d_name, ".") || !strcmp(sde->d_name, "..")) continue;
                    char subfull[4600];
                    snprintf(subfull, sizeof(subfull), "%.*s/%.*s",
                             (int)sizeof(subfull) - 1, full,
                             (int)sizeof(subfull) - 1, sde->d_name);
                    struct stat sst;
                    if (stat(subfull, &sst) == 0 && S_ISREG(sst.st_mode)) {
                        files = realloc(files, sizeof(char *) * (size_t)(n + 1));
                        files[n++] = xasprintf("%s/%s", de->d_name, sde->d_name);
                    }
                }
                closedir(sub);
            }
        } else if (S_ISREG(st.st_mode)) {
            files = realloc(files, sizeof(char *) * (size_t)(n + 1));
            files[n++] = xstrdup(de->d_name);
        }
    }
    closedir(d);
    /* sort */
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (strcmp(files[i], files[j]) > 0) {
                char *t = files[i]; files[i] = files[j]; files[j] = t;
            }
    *n_out = n;
    return files;
}

void ide_free_list(char **list, int n) {
    for (int i = 0; i < n; i++) free(list[i]);
    free(list);
}

char *ide_read_file(const char *path) {
    char *full = ws_path(path);
    if (!full) return NULL;
    char *content = read_file(full);
    free(full);
    return content;
}

bool ide_write_file(const char *path, const char *content) {
    char *full = ws_path(path);
    if (!full) return false;
    /* ensure parent dirs exist */
    char *slash = strrchr(full, '/');
    if (slash && slash != full) {
        *slash = '\0';
        mkdir_p(full);
        *slash = '/';
    }
    bool ok = write_file(full, content);
    free(full);
    return ok;
}

bool ide_delete_file(const char *path) {
    char *full = ws_path(path);
    if (!full) return false;
    bool ok = (remove(full) == 0);
    free(full);
    return ok;
}

bool ide_delete_folder(const char *path) {
    /* delete every file whose path starts with path/ */
    int n = 0;
    char **list = ide_list_files(&n);
    bool any = false;
    size_t plen = strlen(path);
    for (int i = 0; i < n; i++) {
        if (strncmp(list[i], path, plen) == 0 && (list[i][plen] == '/' || list[i][plen] == '\0')) {
            ide_delete_file(list[i]);
            any = true;
        }
    }
    ide_free_list(list, n);
    return any;
}

const char *ide_language(const char *path) {
    if (!path) return "plaintext";
    const char *dot = strrchr(path, '.');
    if (!dot) return "plaintext";
    const char *ext = dot + 1;
    struct { const char *ext; const char *lang; } map[] = {
        { "js", "javascript" }, { "mjs", "javascript" },
        { "ts", "typescript" }, { "tsx", "typescript" },
        { "html", "html" }, { "htm", "html" },
        { "css", "css" }, { "scss", "scss" }, { "sass", "scss" },
        { "json", "json" }, { "md", "markdown" },
        { "py", "python" }, { "java", "java" },
        { "c", "c" }, { "cpp", "cpp" }, { "h", "c" }, { "hpp", "cpp" },
        { "cs", "csharp" }, { "go", "go" }, { "rs", "rust" },
        { "php", "php" }, { "rb", "ruby" }, { "swift", "swift" },
        { "kt", "kotlin" }, { "sql", "sql" },
        { "sh", "shell" }, { "bash", "shell" },
        { "yaml", "yaml" }, { "yml", "yaml" },
        { "xml", "xml" }, { "vue", "vue" }, { "svelte", "svelte" },
        { NULL, NULL }
    };
    for (int i = 0; map[i].ext; i++)
        if (!strcasecmp(ext, map[i].ext)) return map[i].lang;
    return "plaintext";
}

bool ide_new_file(const char *path) {
    char *full = ws_path(path);
    if (!full) return false;
    if (file_exists(full)) { free(full); return false; }
    bool ok = write_file(full, "");
    free(full);
    return ok;
}

char *ide_file_tree_text(void) {
    sbuf_t b;
    sbuf_init(&b);
    int n = 0;
    char **list = ide_list_files(&n);
    sbuf_append(&b, "{\n");
    for (int i = 0; i < n; i++) {
        sbuf_appendf(&b, "  \"%s\": \"%s\",\n", list[i], ide_language(list[i]));
    }
    sbuf_append(&b, "}\n");
    ide_free_list(list, n);
    return sbuf_detach(&b);
}

static bool run_cmd(const char *fmt, ...) {
    char cmd[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);
    int rc = system(cmd);
    return rc == 0;
}

bool ide_export_project(const char *path) {
    return run_cmd("tar -czf \"%s\" -C \"%s\" .", path, hub_workspace_dir());
}

bool ide_import_project(const char *path) {
    /* clean workspace then extract */
    run_cmd("find \"%s\" -mindepth 1 -delete", hub_workspace_dir());
    return run_cmd("tar -xzf \"%s\" -C \"%s\"", path, hub_workspace_dir());
}

/* =================== version control =================== */

typedef struct {
    char   *id;
    char   *message;
    long long ts;
    cJSON  *snapshot;
} commit_t;

static commit_t *g_commits = NULL;
static int g_ncommits = 0;
static pthread_mutex_t g_vc_mtx = PTHREAD_MUTEX_INITIALIZER;

static void vc_save_storage(void) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < g_ncommits; i++) {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "id", g_commits[i].id);
        cJSON_AddStringToObject(j, "message", g_commits[i].message);
        cJSON_AddNumberToObject(j, "timestamp", (double)g_commits[i].ts);
        cJSON_AddItemToObject(j, "snapshot", cJSON_Duplicate(g_commits[i].snapshot, 1));
        cJSON_AddItemToArray(arr, j);
    }
    char *json = cJSON_PrintUnformatted(arr);
    if (json) {
        storage_set("ide_vfs_commits", json);
        free(json);
    }
    cJSON_Delete(arr);
}

void vc_load(void) {
    pthread_mutex_lock(&g_vc_mtx);
    for (int i = 0; i < g_ncommits; i++) {
        free(g_commits[i].id);
        free(g_commits[i].message);
        cJSON_Delete(g_commits[i].snapshot);
    }
    free(g_commits);
    g_commits = NULL;
    g_ncommits = 0;

    const char *raw = storage_get("ide_vfs_commits");
    if (raw[0]) {
        cJSON *arr = cJSON_Parse(raw);
        if (arr && cJSON_IsArray(arr)) {
            int n = cJSON_GetArraySize(arr);
            for (int i = 0; i < n; i++) {
                cJSON *j = cJSON_GetArrayItem(arr, i);
                cJSON *id = cJSON_GetObjectItem(j, "id");
                cJSON *msg = cJSON_GetObjectItem(j, "message");
                cJSON *ts = cJSON_GetObjectItem(j, "timestamp");
                cJSON *snap = cJSON_GetObjectItem(j, "snapshot");
                g_commits = realloc(g_commits, sizeof(commit_t) * (size_t)(g_ncommits + 1));
                g_commits[g_ncommits].id = xstrdup(id && id->valuestring ? id->valuestring : "commit");
                g_commits[g_ncommits].message = xstrdup(msg && msg->valuestring ? msg->valuestring : "Untitled commit");
                g_commits[g_ncommits].ts = ts && cJSON_IsNumber(ts) ? (long long)ts->valuedouble : now_ms();
                g_commits[g_ncommits].snapshot = snap ? cJSON_Duplicate(snap, 1) : cJSON_CreateObject();
                g_ncommits++;
            }
        }
        if (arr) cJSON_Delete(arr);
    }
    pthread_mutex_unlock(&g_vc_mtx);
}

void vc_create_commit(const char *message) {
    pthread_mutex_lock(&g_vc_mtx);
    cJSON *snap = cJSON_CreateObject();
    int n = 0;
    char **files = ide_list_files(&n);
    for (int i = 0; i < n; i++) {
        char *content = ide_read_file(files[i]);
        if (content) {
            cJSON_AddStringToObject(snap, files[i], content);
            free(content);
        }
    }
    ide_free_list(files, n);

    g_commits = realloc(g_commits, sizeof(commit_t) * (size_t)(g_ncommits + 1));
    for (int i = g_ncommits; i > 0; i--) g_commits[i] = g_commits[i - 1];
    g_commits[0].id = xasprintf("commit_%lld", (long long)now_ms());
    g_commits[0].message = xstrdup(message && message[0] ? message : "Untitled commit");
    g_commits[0].ts = now_ms();
    g_commits[0].snapshot = snap;
    g_ncommits++;
    vc_save_storage();
    pthread_mutex_unlock(&g_vc_mtx);
}

bool vc_revert_to(const char *id) {
    pthread_mutex_lock(&g_vc_mtx);
    commit_t *target = NULL;
    for (int i = 0; i < g_ncommits; i++)
        if (!strcmp(g_commits[i].id, id)) { target = &g_commits[i]; break; }
    if (!target) { pthread_mutex_unlock(&g_vc_mtx); return false; }

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, target->snapshot) {
        if (cJSON_IsString(item) && item->valuestring) {
            ide_write_file(item->string, item->valuestring);
        }
    }
    pthread_mutex_unlock(&g_vc_mtx);
    return true;
}

int vc_count(void) { return g_ncommits; }

char *vc_id(int idx) {
    if (idx < 0 || idx >= g_ncommits) return NULL;
    return xstrdup(g_commits[idx].id);
}

char *vc_summary(int idx) {
    if (idx < 0 || idx >= g_ncommits) return NULL;
    commit_t *c = &g_commits[idx];
    int nfiles = c->snapshot ? cJSON_GetArraySize(c->snapshot) : 0;
    char *ts = format_ts(c->ts);
    char *out = xasprintf("%s (%d files) %s", c->message, nfiles, ts);
    free(ts);
    return out;
}

void vc_clear(void) {
    pthread_mutex_lock(&g_vc_mtx);
    for (int i = 0; i < g_ncommits; i++) {
        free(g_commits[i].id);
        free(g_commits[i].message);
        cJSON_Delete(g_commits[i].snapshot);
    }
    free(g_commits);
    g_commits = NULL;
    g_ncommits = 0;
    vc_save_storage();
    pthread_mutex_unlock(&g_vc_mtx);
}

/* =================== custom bots =================== */

static ide_bot_t *g_bots = NULL;
static int g_nbots = 0;
static char g_active_bot_id[128] = "";

static void bots_free(void) {
    for (int i = 0; i < g_nbots; i++) {
        free(g_bots[i].id);
        free(g_bots[i].name);
        free(g_bots[i].prompt);
        free(g_bots[i].model);
    }
    free(g_bots);
    g_bots = NULL;
    g_nbots = 0;
}

void ide_bots_reload(void) {
    bots_free();
    snprintf(g_active_bot_id, sizeof(g_active_bot_id), "%s", storage_get("ide_active_bot_id"));
    const char *raw = storage_get("ide_custom_bots");
    if (raw[0]) {
        cJSON *arr = cJSON_Parse(raw);
        if (arr && cJSON_IsArray(arr)) {
            int n = cJSON_GetArraySize(arr);
            for (int i = 0; i < n; i++) {
                cJSON *j = cJSON_GetArrayItem(arr, i);
                cJSON *id = cJSON_GetObjectItem(j, "id");
                cJSON *name = cJSON_GetObjectItem(j, "name");
                cJSON *prompt = cJSON_GetObjectItem(j, "prompt");
                cJSON *model = cJSON_GetObjectItem(j, "model");
                cJSON *temp = cJSON_GetObjectItem(j, "temp");
                g_bots = realloc(g_bots, sizeof(ide_bot_t) * (size_t)(g_nbots + 1));
                g_bots[g_nbots].id = xstrdup(id && id->valuestring ? id->valuestring : "bot");
                g_bots[g_nbots].name = xstrdup(name && name->valuestring ? name->valuestring : "Bot");
                g_bots[g_nbots].prompt = xstrdup(prompt && prompt->valuestring ? prompt->valuestring : "");
                g_bots[g_nbots].model = xstrdup(model && model->valuestring ? model->valuestring : "");
                g_bots[g_nbots].temp = temp && cJSON_IsNumber(temp) ? temp->valuedouble : 0.7;
                g_nbots++;
            }
        }
        if (arr) cJSON_Delete(arr);
    }
}

int ide_bots_count(void) { return g_nbots; }
ide_bot_t *ide_bots_get(int idx) { return (idx >= 0 && idx < g_nbots) ? &g_bots[idx] : NULL; }

ide_bot_t *ide_bots_active(void) {
    for (int i = 0; i < g_nbots; i++)
        if (!strcmp(g_bots[i].id, g_active_bot_id)) return &g_bots[i];
    return NULL;
}
const char *ide_bot_active_id(void) { return g_active_bot_id; }

void ide_bot_save(ide_bot_t *bot) {
    if (bot->id && bot->id[0]) {
        for (int i = 0; i < g_nbots; i++) {
            if (!strcmp(g_bots[i].id, bot->id)) {
                free(g_bots[i].name);
                free(g_bots[i].prompt);
                free(g_bots[i].model);
                g_bots[i].name = xstrdup(bot->name);
                g_bots[i].prompt = xstrdup(bot->prompt);
                g_bots[i].model = xstrdup(bot->model);
                g_bots[i].temp = bot->temp;
                break;
            }
        }
    } else {
        g_bots = realloc(g_bots, sizeof(ide_bot_t) * (size_t)(g_nbots + 1));
        g_bots[g_nbots].id = xasprintf("bot_%lld", (long long)now_ms());
        g_bots[g_nbots].name = xstrdup(bot->name);
        g_bots[g_nbots].prompt = xstrdup(bot->prompt);
        g_bots[g_nbots].model = xstrdup(bot->model);
        g_bots[g_nbots].temp = bot->temp;
        g_nbots++;
    }
    /* persist */
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < g_nbots; i++) {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "id", g_bots[i].id);
        cJSON_AddStringToObject(j, "name", g_bots[i].name);
        cJSON_AddStringToObject(j, "prompt", g_bots[i].prompt);
        cJSON_AddStringToObject(j, "model", g_bots[i].model);
        cJSON_AddNumberToObject(j, "temp", g_bots[i].temp);
        cJSON_AddItemToArray(arr, j);
    }
    char *json = cJSON_PrintUnformatted(arr);
    if (json) {
        storage_set("ide_custom_bots", json);
        free(json);
    }
    cJSON_Delete(arr);
    storage_save();
}

void ide_bot_delete(int idx) {
    if (idx < 0 || idx >= g_nbots) return;
    if (!strcmp(g_bots[idx].id, g_active_bot_id)) {
        g_active_bot_id[0] = '\0';
        storage_remove("ide_active_bot_id");
    }
    free(g_bots[idx].id);
    free(g_bots[idx].name);
    free(g_bots[idx].prompt);
    free(g_bots[idx].model);
    for (int i = idx; i < g_nbots - 1; i++) g_bots[i] = g_bots[i + 1];
    g_nbots--;
    ide_bot_save(NULL); /* persist (no-op for NULL id) */
    /* persist again with full list */
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < g_nbots; i++) {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "id", g_bots[i].id);
        cJSON_AddStringToObject(j, "name", g_bots[i].name);
        cJSON_AddStringToObject(j, "prompt", g_bots[i].prompt);
        cJSON_AddStringToObject(j, "model", g_bots[i].model);
        cJSON_AddNumberToObject(j, "temp", g_bots[i].temp);
        cJSON_AddItemToArray(arr, j);
    }
    char *json = cJSON_PrintUnformatted(arr);
    if (json) {
        storage_set("ide_custom_bots", json);
        free(json);
    }
    cJSON_Delete(arr);
    storage_save();
}

void ide_bot_set_active(int idx) {
    if (idx < 0) {
        g_active_bot_id[0] = '\0';
        storage_remove("ide_active_bot_id");
        return;
    }
    if (idx >= 0 && idx < g_nbots) {
        snprintf(g_active_bot_id, sizeof(g_active_bot_id), "%s", g_bots[idx].id);
        storage_set("ide_active_bot_id", g_active_bot_id);
    }
}

char *ide_bots_export_json(void) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < g_nbots; i++) {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "id", g_bots[i].id);
        cJSON_AddStringToObject(j, "name", g_bots[i].name);
        cJSON_AddStringToObject(j, "prompt", g_bots[i].prompt);
        cJSON_AddStringToObject(j, "model", g_bots[i].model);
        cJSON_AddNumberToObject(j, "temp", g_bots[i].temp);
        cJSON_AddItemToArray(arr, j);
    }
    char *json = cJSON_Print(arr);
    cJSON_Delete(arr);
    return json;
}

bool ide_bots_import_json(const char *json) {
    cJSON *arr = cJSON_Parse(json);
    if (!arr || !cJSON_IsArray(arr)) { if (arr) cJSON_Delete(arr); return false; }
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n; i++) {
        cJSON *j = cJSON_GetArrayItem(arr, i);
        cJSON *name = cJSON_GetObjectItem(j, "name");
        cJSON *prompt = cJSON_GetObjectItem(j, "prompt");
        cJSON *model = cJSON_GetObjectItem(j, "model");
        cJSON *temp = cJSON_GetObjectItem(j, "temp");
        if (!name || !prompt || !name->valuestring || !prompt->valuestring) continue;
        ide_bot_t bot = {0};
        bot.name = name->valuestring;
        bot.prompt = prompt->valuestring;
        bot.model = model && model->valuestring ? model->valuestring : "";
        bot.temp = temp && cJSON_IsNumber(temp) ? temp->valuedouble : 0.7;
        ide_bot_save(&bot);
    }
    cJSON_Delete(arr);
    return n > 0;
}

/* =================== agent chat =================== */

void agent_chat_init(agent_chat_t *c) {
    memset(c, 0, sizeof(*c));
    pthread_mutex_init(&c->mtx, NULL);
}
void agent_chat_free(agent_chat_t *c) {
    pthread_mutex_destroy(&c->mtx);
    agent_chat_clear(c);
}
void agent_chat_clear(agent_chat_t *c) {
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
int agent_chat_count(agent_chat_t *c) {
    pthread_mutex_lock(&c->mtx);
    int n = c->n;
    pthread_mutex_unlock(&c->mtx);
    return n;
}

static void agent_chat_push(agent_chat_t *c, const char *role, const char *content) {
    pthread_mutex_lock(&c->mtx);
    c->msgs = realloc(c->msgs, sizeof(agent_msg_t) * (size_t)(c->n + 1));
    c->msgs[c->n].role = xstrdup(role);
    c->msgs[c->n].content = xstrdup(content);
    c->n++;
    pthread_mutex_unlock(&c->mtx);
}

char *agent_chat_snapshot(agent_chat_t *c) {
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

/* =================== agent tools =================== */

static char *json_err(const char *msg) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "success", 0);
    cJSON_AddStringToObject(o, "error", msg);
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return s;
}

static char *json_errf(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return json_err(buf);
}

static char *json_okf(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "success", 1);
    cJSON_AddStringToObject(o, "message", buf);
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return s;
}

static char *tool_list_files(cJSON *args) {
    (void)args;
    cJSON *out = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    int n = 0;
    char **files = ide_list_files(&n);
    for (int i = 0; i < n; i++) {
        cJSON *f = cJSON_CreateObject();
        cJSON_AddStringToObject(f, "path", files[i]);
        cJSON_AddStringToObject(f, "language", ide_language(files[i]));
        cJSON_AddItemToArray(arr, f);
    }
    ide_free_list(files, n);
    cJSON_AddBoolToObject(out, "success", 1);
    cJSON_AddItemToObject(out, "files", arr);
    char *s = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    return s;
}

static char *tool_read_file(cJSON *args) {
    cJSON *path = cJSON_GetObjectItem(args, "path");
    if (!path || !cJSON_IsString(path) || !path->valuestring)
        return json_err("path required");
    char *content = ide_read_file(path->valuestring);
    if (!content)
        return json_errf("File not found: %s", path->valuestring);
    cJSON *out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "success", 1);
    cJSON_AddStringToObject(out, "path", path->valuestring);
    cJSON *lines = cJSON_CreateArray();
    const char *p = content;
    int ln = 1;
    if (!p[0]) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "line", 1);
        cJSON_AddStringToObject(item, "text", "");
        cJSON_AddItemToArray(lines, item);
    } else {
        while (*p) {
            const char *nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            char *line = xstrndup(p, len);
            cJSON *item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "line", ln);
            cJSON_AddStringToObject(item, "text", line);
            cJSON_AddItemToArray(lines, item);
            free(line);
            if (!nl) break;
            p = nl + 1;
            ln++;
        }
    }
    cJSON_AddItemToObject(out, "lines", lines);
    char *s = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    free(content);
    return s;
}

static char *tool_create_file(cJSON *args) {
    cJSON *path = cJSON_GetObjectItem(args, "path");
    if (!path || !cJSON_IsString(path) || !path->valuestring)
        return json_err("path required");
    if (ide_new_file(path->valuestring))
        return json_okf("File created: %s", path->valuestring);
    return json_err("Cannot create file (invalid path or it already exists)");
}

static char *tool_write_file(cJSON *args) {
    cJSON *path = cJSON_GetObjectItem(args, "path");
    cJSON *content = cJSON_GetObjectItem(args, "content");
    if (!path || !cJSON_IsString(path) || !path->valuestring)
        return json_err("path required");
    const char *c = content && cJSON_IsString(content) && content->valuestring ? content->valuestring : "";
    if (ide_write_file(path->valuestring, c))
        return json_okf("File written (created or fully overwritten): %s", path->valuestring);
    return json_err("Cannot write file");
}

static char *tool_edit_file(cJSON *args) {
    cJSON *path = cJSON_GetObjectItem(args, "path");
    cJSON *start = cJSON_GetObjectItem(args, "start_line");
    cJSON *end = cJSON_GetObjectItem(args, "end_line");
    cJSON *content = cJSON_GetObjectItem(args, "content");
    if (!path || !cJSON_IsString(path) || !path->valuestring || !start || !cJSON_IsNumber(start))
        return json_err("path (string) and start_line (number) required");
    int s = (int)start->valuedouble;
    int e = end && cJSON_IsNumber(end) ? (int)end->valuedouble : s;
    if (s < 1 || e < s)
        return json_err("invalid line range: start_line must be >= 1 and end_line must be >= start_line");
    char *body = ide_read_file(path->valuestring);
    if (!body)
        return json_errf("File not found: %s", path->valuestring);
    const char *repl = content && cJSON_IsString(content) && content->valuestring ? content->valuestring : "";

    /* split body into 1-based numbered lines */
    char **lines = NULL;
    int nlines = 0;
    const char *p = body;
    if (p[0]) {
        while (*p) {
            const char *nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            lines = realloc(lines, sizeof(char *) * (size_t)(nlines + 1));
            lines[nlines++] = xstrndup(p, len);
            if (!nl) break;
            p = nl + 1;
        }
    }

    sbuf_t out;
    sbuf_init(&out);
    int inserted = 0;
    for (int i = 1; i <= nlines; i++) {
        if (i >= s && i <= e) {
            if (!inserted && repl[0]) {
                if (out.len) sbuf_append(&out, "\n");
                sbuf_append(&out, repl);
            }
            inserted = 1;
            continue;
        }
        if (out.len) sbuf_append(&out, "\n");
        sbuf_append(&out, lines[i - 1]);
    }
    if (s > nlines && repl[0]) {
        /* appending new lines at the end of the file */
        if (out.len) sbuf_append(&out, "\n");
        sbuf_append(&out, repl);
    }

    for (int i = 0; i < nlines; i++) free(lines[i]);
    free(lines);
    free(body);

    bool ok = ide_write_file(path->valuestring, out.s);
    sbuf_free(&out);
    if (ok)
        return json_okf("Lines %d..%d updated in %s", s, e, path->valuestring);
    return json_err("Write failed");
}

static char *tool_delete_file(cJSON *args) {
    cJSON *path = cJSON_GetObjectItem(args, "path");
    if (!path || !cJSON_IsString(path) || !path->valuestring)
        return json_err("path required");
    if (ide_delete_file(path->valuestring))
        return json_okf("File deleted: %s", path->valuestring);
    return json_err("Cannot delete file (not found?)");
}

static char *parse_tool_call(const char *text) {
    /* find first '{' and matching balanced JSON */
    const char *open = strchr(text, '{');
    if (!open) return NULL;
    int depth = 0;
    bool in_str = false;
    const char *p = open;
    for (; *p; p++) {
        if (*p == '"' && (p == open || p[-1] != '\\')) in_str = !in_str;
        if (!in_str) {
            if (*p == '{') depth++;
            else if (*p == '}') {
                depth--;
                if (depth == 0) {
                    cJSON *j = cJSON_ParseWithLength(open, (size_t)(p - open) + 1);
                    if (!j) return NULL;
                    cJSON *tool = cJSON_GetObjectItem(j, "tool");
                    cJSON *args = cJSON_GetObjectItem(j, "arguments");
                    if (tool && cJSON_IsString(tool) && args && cJSON_IsObject(args)) {
                        cJSON_Delete(j);
                        char *out = xstrndup(open, (size_t)(p - open) + 1);
                        return out;
                    }
                    cJSON_Delete(j);
                    return NULL;
                }
            }
        }
    }
    return NULL;
}

static char *execute_tool(const char *tool_call) {
    cJSON *j = cJSON_Parse(tool_call);
    if (!j) return xstrdup("{\"error\": \"invalid tool call\"}");
    cJSON *tool = cJSON_GetObjectItem(j, "tool");
    cJSON *args = cJSON_GetObjectItem(j, "arguments");
    const char *name = tool && cJSON_IsString(tool) ? tool->valuestring : "";
    char *result = NULL;
    if (!strcmp(name, "list_files")) result = tool_list_files(args);
    else if (!strcmp(name, "read_file")) result = tool_read_file(args);
    else if (!strcmp(name, "create_file")) result = tool_create_file(args);
    else if (!strcmp(name, "write_file")) result = tool_write_file(args);
    else if (!strcmp(name, "edit_file")) result = tool_edit_file(args);
    else if (!strcmp(name, "delete_file")) result = tool_delete_file(args);
    else result = json_errf("unknown tool: %s", name);
    cJSON_Delete(j);
    return result;
}

/* Build the OpenAI/Anthropic function-tool schemas for the six IDE tools. These are
 * registered with the API so tool-capable backends (e.g. Groq) do not reject the
 * request with "Tool choice is none, but model called a tool" when the model emits
 * a native tool call. The local parser still accepts the text-JSON protocol too. */
static void add_tool(cJSON *arr, const char *name, const char *desc, cJSON *params) {
    cJSON *t = cJSON_CreateObject();
    cJSON_AddStringToObject(t, "type", "function");
    cJSON *fn = cJSON_CreateObject();
    cJSON_AddStringToObject(fn, "name", name);
    cJSON_AddStringToObject(fn, "description", desc);
    cJSON_AddItemToObject(fn, "parameters", params);
    cJSON_AddItemToObject(t, "function", fn);
    cJSON_AddItemToArray(arr, t);
}

static char *agent_tools_json(void) {
    cJSON *arr = cJSON_CreateArray();

    cJSON *p = cJSON_CreateObject();
    cJSON *props = cJSON_CreateObject();
    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "type", "string");
    cJSON_AddStringToObject(d, "description", "directory to list (empty string = workspace root)");
    cJSON_AddItemToObject(props, "directory", d);
    cJSON_AddItemToObject(p, "properties", props);
    add_tool(arr, "list_files", "List the files in the user's workspace (paths + languages).", p);

    p = cJSON_CreateObject();
    props = cJSON_CreateObject();
    d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "type", "string");
    cJSON_AddStringToObject(d, "description", "relative file path");
    cJSON_AddItemToObject(props, "path", d);
    cJSON_AddItemToObject(p, "properties", props);
    cJSON_AddItemToObject(p, "required", cJSON_CreateStringArray(&(const char *){"path"}, 1));
    add_tool(arr, "read_file", "Read a file; returns its content as an array of numbered lines.", p);

    p = cJSON_CreateObject();
    props = cJSON_CreateObject();
    d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "type", "string");
    cJSON_AddStringToObject(d, "description", "relative file path");
    cJSON_AddItemToObject(props, "path", d);
    cJSON_AddItemToObject(p, "properties", props);
    cJSON_AddItemToObject(p, "required", cJSON_CreateStringArray(&(const char *){"path"}, 1));
    add_tool(arr, "create_file", "Create a new empty file. Fails if the file already exists.", p);

    p = cJSON_CreateObject();
    props = cJSON_CreateObject();
    d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "type", "string");
    cJSON_AddStringToObject(d, "description", "relative file path");
    cJSON_AddItemToObject(props, "path", d);
    cJSON *dc = cJSON_CreateObject();
    cJSON_AddStringToObject(dc, "type", "string");
    cJSON_AddStringToObject(dc, "description", "full new content");
    cJSON_AddItemToObject(props, "content", dc);
    cJSON_AddItemToObject(p, "properties", props);
    const char *req[] = {"path", "content"};
    cJSON_AddItemToObject(p, "required", cJSON_CreateStringArray(req, 2));
    add_tool(arr, "write_file", "Create a file or fully overwrite it with new content.", p);

    p = cJSON_CreateObject();
    props = cJSON_CreateObject();
    d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "type", "string");
    cJSON_AddStringToObject(d, "description", "relative file path");
    cJSON_AddItemToObject(props, "path", d);
    d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "type", "integer");
    cJSON_AddStringToObject(d, "description", "first line to replace (1-based)");
    cJSON_AddItemToObject(props, "start_line", d);
    d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "type", "integer");
    cJSON_AddStringToObject(d, "description", "last line to replace (inclusive, 1-based)");
    cJSON_AddItemToObject(props, "end_line", d);
    dc = cJSON_CreateObject();
    cJSON_AddStringToObject(dc, "type", "string");
    cJSON_AddStringToObject(dc, "description", "replacement lines (empty string deletes the lines)");
    cJSON_AddItemToObject(props, "content", dc);
    cJSON_AddItemToObject(p, "properties", props);
    const char *req2[] = {"path", "start_line"};
    cJSON_AddItemToObject(p, "required", cJSON_CreateStringArray(req2, 2));
    add_tool(arr, "edit_file", "Replace a range of lines in a file.", p);

    p = cJSON_CreateObject();
    props = cJSON_CreateObject();
    d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "type", "string");
    cJSON_AddStringToObject(d, "description", "relative file path");
    cJSON_AddItemToObject(props, "path", d);
    cJSON_AddItemToObject(p, "properties", props);
    cJSON_AddItemToObject(p, "required", cJSON_CreateStringArray(&(const char *){"path"}, 1));
    add_tool(arr, "delete_file", "Delete a file.", p);

    char *s = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return s;
}

/* Absolute system prompt for the IDE agent. It defines the JSON-based
 * tool-calling protocol used instead of native (API-level) tool support.
 * The model always responds either with a pure JSON tool call (nothing else)
 * or, when finished, with a normal text answer. */
static const char *BASE_AGENT_SYSTEM =
    "You are an autonomous AI coding agent running inside the Traliran AI IDE. "
    "You can read, create, edit, and delete files in the user's workspace. "
    "There are NO native function tools in the API - the IDE parses a JSON tool call "
    "directly from your reply text, so you must follow the protocol below exactly.\n\n"
    "RESPONSE FORMAT - use ONE of two mechanisms per reply (the IDE supports both):\n"
    "1) NATIVE tool calling: the functions list_files, read_file, create_file, write_file, "
    "edit_file, delete_file are registered with the API. If you support native "
    "function/tool calls, call them directly (the IDE executes them and returns the result).\n"
    "2) TEXT JSON (works on every model): output ONLY a single JSON object, nothing before "
    "or after it (no markdown fences, no prose):\n"
    "  {\"tool\": \"<tool_name>\", \"arguments\": {<arguments>}}\n"
    "- Never mix a tool call and your summary in one reply, and never nest one mechanism "
    "inside the other (do not put a native tool call inside a JSON argument, etc.).\n"
    "- The IDE executes the tool and returns the result to you as the next message. "
    "Then you continue: read files, edit them, verify, and repeat until the task is done.\n"
    "- When the task is fully complete or no tool is needed, reply with a normal text "
    "message that answers the user / summarizes the changes.\n\n"
    "WORKFLOW (iteration system):\n"
    "1. The first turn gives you ONLY the list of files in the workspace - no contents.\n"
    "2. Call list_files to inspect, then read_file on every file whose content you need "
    "(contents are returned with 1-based line numbers).\n"
    "3. Plan your changes, then apply them with create_file / write_file / edit_file / delete_file.\n"
    "4. After editing, re-read the changed files to verify, then give your final summary.\n\n"
    "AVAILABLE TOOLS:\n"
    "1. {\"tool\": \"list_files\", \"arguments\": {\"directory\": \"\"}}\n"
    "   -> Returns the workspace file list (paths + languages).\n"
    "2. {\"tool\": \"read_file\", \"arguments\": {\"path\": \"<relative path>\"}}\n"
    "   -> Returns the file content as an array of numbered lines (line, text).\n"
    "3. {\"tool\": \"create_file\", \"arguments\": {\"path\": \"<relative path>\"}}\n"
    "   -> Creates a new empty file. Fails if the file already exists.\n"
    "4. {\"tool\": \"write_file\", \"arguments\": {\"path\": \"<relative path>\", \"content\": \"<full new content>\"}}\n"
    "   -> Creates a file or fully overwrites it. Use only for new files or complete rewrites.\n"
    "5. {\"tool\": \"edit_file\", \"arguments\": {\"path\": \"<relative path>\", \"start_line\": <n>, \"end_line\": <m>, \"content\": \"<replacement lines>\"}}\n"
    "   -> Replaces lines <n>..<m> (inclusive, 1-based, as shown by read_file) with the "
    "given content. Leave other lines untouched. Set content to an empty string to simply "
    "delete those lines. Example: start_line 5, end_line 10 replaces lines 5 through 10.\n"
    "6. {\"tool\": \"delete_file\", \"arguments\": {\"path\": \"<relative path>\"}}\n"
    "   -> Deletes a file.\n\n"
    "RULES:\n"
    "- Never guess file contents - read the file first with read_file.\n"
    "- Always read a file before editing it so edit_file line numbers match.\n"
    "- Prefer edit_file for targeted changes instead of rewriting whole files.\n"
    "- Do not claim files were created/modified unless a tool returned success.\n"
    "- If a tool returns an error, adjust your arguments and retry.\n"
    "- Work step by step - exactly one tool call per reply.\n"
    "- Communicate with the user in the same language they used.";

static char *agent_system_prompt(void) {
    ide_bot_t *bot = ide_bots_active();
    sbuf_t p;
    sbuf_init(&p);
    sbuf_append(&p, BASE_AGENT_SYSTEM);
    if (bot && bot->prompt[0]) {
        sbuf_append(&p, "\n\n=== CUSTOM BOT PROMPT (your task-specific instructions, follow them) ===\n");
        sbuf_append(&p, bot->prompt);
        sbuf_append(&p, "\n=== END CUSTOM BOT PROMPT ===\n");
    }
    return sbuf_detach(&p);
}

/* keep the first `prefix` messages (system + initial context) and the last `keep`,
 * dropping the middle so the context window does not grow unboundedly. */
static void arr_trim(cJSON *arr, int prefix, int keep) {
    int n = cJSON_GetArraySize(arr);
    if (n <= prefix + keep) return;
    int drop = n - (prefix + keep);
    for (int i = 0; i < drop; i++) {
        cJSON *item = cJSON_DetachItemFromArray(arr, prefix);
        if (item) cJSON_Delete(item);
    }
}

static void agent_progress(void *p, const char *text) {
    progress_t *pr = p;
    if (!pr) return;
    pthread_mutex_lock(&pr->mtx);
    snprintf(pr->text, sizeof(pr->text), "%s", text);
    pthread_mutex_unlock(&pr->mtx);
}

void *agent_worker(void *arg) {
    agent_arg_t *a = arg;
    agent_chat_push(a->chat, "user", a->user_message);

    char *sys = agent_system_prompt();

    /* iteration 1: only the file list (names), no contents */
    char *tree = ide_file_tree_text();
    char *initial_ctx = xasprintf(
        "User request: %s\n\nAvailable files in the workspace (names only; "
        "use read_file to get their line-numbered contents):\n%s",
        a->user_message, tree);
    free(tree);

    cJSON *arr = cJSON_CreateArray();
    cJSON *s = cJSON_CreateObject();
    cJSON_AddStringToObject(s, "role", "system");
    cJSON_AddStringToObject(s, "content", sys);
    cJSON_AddItemToArray(arr, s);
    cJSON *u = cJSON_CreateObject();
    cJSON_AddStringToObject(u, "role", "user");
    cJSON_AddStringToObject(u, "content", initial_ctx);
    cJSON_AddItemToArray(arr, u);

    /* model + parameters: from global settings by default, overridden by the
     * applied bot (if any). */
    const char *model = a->model && a->model[0] ? a->model : g_cfg.selected_model;
    double temp = g_cfg.temperature;
    double top_p = g_cfg.top_p;
    int max_tokens = g_cfg.max_tokens;
    ide_bot_t *bot = ide_bots_active();
    if (bot) {
        if (bot->model && bot->model[0]) model = bot->model;
        if (bot->temp > 0.0) temp = bot->temp;
    }

    int iterations = 0;
    while (iterations < 15 && !(a->stop && *a->stop)) {
        iterations++;
        agent_progress(a->progress_ptr, "Thinking...");
        char *messages_json = cJSON_PrintUnformatted(arr);
        char *tools_json = agent_tools_json();
        api_result_t res;
        int rc = api_complete_agent(model, messages_json, tools_json, temp, top_p, max_tokens, &res);
        free(tools_json);
        free(messages_json);
        if (rc != 0) {
            agent_chat_push(a->chat, "assistant", xasprintf("Error: %s", res.content ? res.content : "API error"));
            api_result_free(&res);
            break;
        }

        char *tool_call = parse_tool_call(res.content);
        if (tool_call) {
            cJSON *tj = cJSON_Parse(tool_call);
            cJSON *tn = cJSON_GetObjectItem(tj, "tool");
            char *result = execute_tool(tool_call);
            char *note = xasprintf("[Tool result from %s]:\n%s",
                                   tn && tn->valuestring ? tn->valuestring : "tool", result);
            /* Feed the result back as a user message. The assistant's raw tool JSON is
             * intentionally NOT echoed back: native tool calls from a previous turn are
             * not replayed, so the next request carries clean plain-text history. */
            cJSON *m = cJSON_CreateObject();
            cJSON_AddStringToObject(m, "role", "user");
            cJSON_AddStringToObject(m, "content", note);
            cJSON_AddItemToArray(arr, m);
            agent_chat_push(a->chat, "assistant", xasprintf("🔧 %s", note));
            free(note);
            free(result);
            cJSON_Delete(tj);
            free(tool_call);
            agent_progress(a->progress_ptr, "Executing tool...");
        } else {
            if (res.content && res.content[0]) {
                agent_chat_push(a->chat, "assistant", res.content);
            }
            api_result_free(&res);
            break;
        }
        api_result_free(&res);
        arr_trim(arr, 2, 30);
    }
    if (iterations >= 15) {
        agent_chat_push(a->chat, "assistant", "Reached maximum iteration limit. Please refine your request.");
    }
    cJSON_Delete(arr);
    free(sys);
    free(initial_ctx);
    return NULL;
}