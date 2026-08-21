/*
 * rag.c - knowledge base (RAG) management + RAG agent (tools over uploaded docs).
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "cJSON.h"
#include "util.h"
#include "storage.h"
#include "config.h"
#include "chat.h"
#include "notes.h"
#include "rag.h"

void kb_clear(kb_t *kb) {
    for (int i = 0; i < kb->n; i++) {
        free(kb->items[i].name);
        free(kb->items[i].content);
    }
    free(kb->items);
    kb->items = NULL;
    kb->n = 0;
}

void kb_load(kb_t *kb) {
    memset(kb, 0, sizeof(*kb));
    const char *raw = storage_get("gem_rag_kb");
    if (!raw[0]) return;
    cJSON *arr = cJSON_Parse(raw);
    if (!arr || !cJSON_IsArray(arr)) { if (arr) cJSON_Delete(arr); return; }
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        cJSON *name = item ? cJSON_GetObjectItem(item, "name") : NULL;
        cJSON *content = item ? cJSON_GetObjectItem(item, "content") : NULL;
        if (!name || !cJSON_IsString(name) || !name->valuestring) continue;
        kb->items = realloc(kb->items, sizeof(kb_entry_t) * (size_t)(kb->n + 1));
        kb->items[kb->n].name = xstrdup(name->valuestring);
        kb->items[kb->n].content = xstrdup(content && content->valuestring ? content->valuestring : "");
        kb->n++;
    }
    cJSON_Delete(arr);
}

void kb_save(const kb_t *kb) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < kb->n; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", kb->items[i].name);
        cJSON_AddStringToObject(item, "content", kb->items[i].content);
        cJSON_AddStringToObject(item, "source", "notes");
        cJSON_AddNumberToObject(item, "updatedAt", (double)now_ms());
        cJSON_AddItemToArray(arr, item);
    }
    char *json = cJSON_PrintUnformatted(arr);
    if (json) {
        storage_set("gem_rag_kb", json);
        free(json);
    }
    cJSON_Delete(arr);
}

int kb_find(const kb_t *kb, const char *name) {
    for (int i = 0; i < kb->n; i++) {
        if (!strcmp(kb->items[i].name, name)) return i;
    }
    return -1;
}

void kb_add(kb_t *kb, const char *name, const char *content) {
    int idx = kb_find(kb, name);
    if (idx >= 0) {
        free(kb->items[idx].content);
        kb->items[idx].content = xstrdup(content);
        return;
    }
    kb->items = realloc(kb->items, sizeof(kb_entry_t) * (size_t)(kb->n + 1));
    kb->items[kb->n].name = xstrdup(name);
    kb->items[kb->n].content = xstrdup(content);
    kb->n++;
}

void kb_remove(kb_t *kb, int idx) {
    if (idx < 0 || idx >= kb->n) return;
    free(kb->items[idx].name);
    free(kb->items[idx].content);
    for (int i = idx; i < kb->n - 1; i++) kb->items[i] = kb->items[i + 1];
    kb->n--;
}

char *kb_build_context(const kb_t *kb) {
    sbuf_t b;
    sbuf_init(&b);
    if (kb->n == 0) {
        sbuf_append(&b, "No files uploaded yet.");
        return sbuf_detach(&b);
    }
    for (int i = 0; i < kb->n; i++) {
        if (i) sbuf_append(&b, "\n\n---\n\n");
        sbuf_appendf(&b, "File: %s\nContent:\n%s", kb->items[i].name, kb->items[i].content);
    }
    return sbuf_detach(&b);
}

/* Read a .md/.txt file from disk into the knowledge base. Returns false when the
 * extension is not supported or the file cannot be read. */
bool kb_add_file(kb_t *kb, const char *path) {
    if (!path) return false;
    char lower[4096];
    snprintf(lower, sizeof(lower), "%s", path);
    str_tolower(lower);
    if (!str_has_suffix(lower, ".md") && !str_has_suffix(lower, ".txt")) return false;
    char *content = read_file(path);
    if (!content) return false;
    const char *slash = strrchr(path, '/');
    kb_add(kb, slash ? slash + 1 : path, content);
    free(content);
    return true;
}

/* =================== RAG agent chat =================== */

void rag_chat_init(rag_chat_t *c) {
    memset(c, 0, sizeof(*c));
    pthread_mutex_init(&c->mtx, NULL);
}

void rag_chat_free(rag_chat_t *c) {
    pthread_mutex_destroy(&c->mtx);
    rag_chat_clear(c);
}

void rag_chat_clear(rag_chat_t *c) {
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

static void rag_chat_push(rag_chat_t *c, const char *role, const char *content) {
    pthread_mutex_lock(&c->mtx);
    c->msgs = realloc(c->msgs, sizeof(rag_msg_t) * (size_t)(c->n + 1));
    c->msgs[c->n].role = xstrdup(role);
    c->msgs[c->n].content = xstrdup(content);
    c->n++;
    pthread_mutex_unlock(&c->mtx);
}

char *rag_chat_snapshot(rag_chat_t *c) {
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

/* =================== RAG agent tools =================== */

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

/* case-insensitive substring (ASCII-folded, byte-exact for non-ASCII/UTF-8) */
static const char *str_ifind(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return hay;
    size_t nl = strlen(needle);
    size_t hl = strlen(hay);
    if (nl > hl) return NULL;
    for (size_t i = 0; i + nl <= hl; i++) {
        size_t k;
        for (k = 0; k < nl; k++) {
            unsigned char h = (unsigned char)hay[i + k];
            unsigned char n = (unsigned char)needle[k];
            if (h >= 'A' && h <= 'Z') h = (unsigned char)(h - 'A' + 'a');
            if (n >= 'A' && n <= 'Z') n = (unsigned char)(n - 'A' + 'a');
            if (h != n) break;
        }
        if (k == nl) return hay + i;
    }
    return NULL;
}

static char *snippet_around(const char *text, const char *at, int radius) {
    const char *start = at - radius;
    if (start < text) start = text;
    const char *end = at + radius;
    if (end > text + strlen(text)) end = text + strlen(text);
    sbuf_t b;
    sbuf_init(&b);
    if (start > text) sbuf_append(&b, "...");
    for (const char *p = start; p < end; p++) {
        sbuf_append_n(&b, *p == '\n' || *p == '\r' || *p == '\t' ? " " : p, 1);
    }
    if (end < text + strlen(text)) sbuf_append(&b, "...");
    return sbuf_detach(&b);
}

static char *tool_list_files(cJSON *args, const kb_t *kb) {
    (void)args;
    cJSON *out = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < kb->n; i++) {
        cJSON *f = cJSON_CreateObject();
        cJSON_AddStringToObject(f, "name", kb->items[i].name);
        cJSON_AddNumberToObject(f, "size_chars", (double)strlen(kb->items[i].content));
        cJSON_AddItemToArray(arr, f);
    }
    cJSON_AddBoolToObject(out, "success", 1);
    cJSON_AddItemToObject(out, "files", arr);
    char *s = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    return s;
}

static char *tool_read_file(cJSON *args, const kb_t *kb) {
    cJSON *name = cJSON_GetObjectItem(args, "name");
    if (!name || !cJSON_IsString(name) || !name->valuestring)
        return json_err("name required");
    int idx = kb_find(kb, name->valuestring);
    if (idx < 0)
        return json_errf("Document not found: %s", name->valuestring);
    cJSON *out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "success", 1);
    cJSON_AddStringToObject(out, "name", kb->items[idx].name);
    cJSON_AddStringToObject(out, "content", kb->items[idx].content);
    char *s = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    return s;
}

static char *tool_search_files(cJSON *args, const kb_t *kb) {
    cJSON *query = cJSON_GetObjectItem(args, "query");
    if (!query || !cJSON_IsString(query) || !query->valuestring)
        return json_err("query required");
    cJSON *out = cJSON_CreateObject();
    cJSON *results = cJSON_CreateArray();
    int total = 0;
    for (int i = 0; i < kb->n; i++) {
        const char *hit = str_ifind(kb->items[i].content, query->valuestring);
        if (!hit) continue;
        total++;
        char *snip = snippet_around(kb->items[i].content, hit, 140);
        cJSON *r = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "name", kb->items[i].name);
        cJSON_AddStringToObject(r, "snippet", snip);
        cJSON_AddItemToArray(results, r);
        free(snip);
    }
    cJSON_AddBoolToObject(out, "success", 1);
    cJSON_AddNumberToObject(out, "matches", total);
    cJSON_AddItemToObject(out, "results", results);
    char *s = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    return s;
}

/* parse the plain-text JSON tool call the model emits, mirroring the IDE agent */
static char *parse_tool_call(const char *text) {
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

static char *execute_tool(const char *tool_call, const kb_t *kb) {
    cJSON *j = cJSON_Parse(tool_call);
    if (!j) return xstrdup("{\"error\": \"invalid tool call\"}");
    cJSON *tool = cJSON_GetObjectItem(j, "tool");
    cJSON *args = cJSON_GetObjectItem(j, "arguments");
    const char *name = tool && cJSON_IsString(tool) ? tool->valuestring : "";
    char *result = NULL;
    if (!strcmp(name, "list_files")) result = tool_list_files(args, kb);
    else if (!strcmp(name, "read_file")) result = tool_read_file(args, kb);
    else if (!strcmp(name, "search_files")) result = tool_search_files(args, kb);
    else result = json_errf("unknown tool: %s", name);
    cJSON_Delete(j);
    return result;
}

/* OpenAI/Anthropic tool schemas registered on the request so tool-capable backends
 * accept native calls; the local JSON parser also supports the text-JSON protocol. */
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

static char *rag_tools_json(void) {
    cJSON *arr = cJSON_CreateArray();

    cJSON *p = cJSON_CreateObject();
    cJSON *props = cJSON_CreateObject();
    cJSON_AddItemToObject(p, "properties", props);
    cJSON_AddItemToObject(p, "type", cJSON_CreateString("object"));
    add_tool(arr, "list_files", "List the documents uploaded to the knowledge base (names + sizes).", p);

    p = cJSON_CreateObject();
    props = cJSON_CreateObject();
    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "type", "string");
    cJSON_AddStringToObject(d, "description", "document name (as returned by list_files)");
    cJSON_AddItemToObject(props, "name", d);
    cJSON_AddItemToObject(p, "properties", props);
    cJSON_AddItemToObject(p, "required", cJSON_CreateStringArray(&(const char *){"name"}, 1));
    cJSON_AddItemToObject(p, "type", cJSON_CreateString("object"));
    add_tool(arr, "read_file", "Read a document from the knowledge base; returns its full content.", p);

    p = cJSON_CreateObject();
    props = cJSON_CreateObject();
    d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "type", "string");
    cJSON_AddStringToObject(d, "description", "search text");
    cJSON_AddItemToObject(props, "query", d);
    cJSON_AddItemToObject(p, "properties", props);
    cJSON_AddItemToObject(p, "required", cJSON_CreateStringArray(&(const char *){"query"}, 1));
    cJSON_AddItemToObject(p, "type", cJSON_CreateString("object"));
    add_tool(arr, "search_files", "Search all documents for a text query; returns matching documents with snippets.", p);

    char *s = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return s;
}

/* Absolute system prompt for the RAG agent. Same JSON-based tool-calling protocol
 * as the IDE agent: no native tools are assumed, the reply text is parsed. */
static const char *RAG_SYSTEM_PROMPT =
    "You are an AI assistant with access to a personal knowledge base (RAG) that "
    "contains documents uploaded by the user (Markdown and text files). "
    "You answer the user's questions based on these documents.\n"
    "There are NO native function tools in the API - the client parses a JSON tool call "
    "directly from your reply text, so you must follow the protocol below exactly.\n\n"
    "RESPONSE FORMAT - use ONE of two mechanisms per reply (both are supported):\n"
    "1) NATIVE tool calling: the functions list_files, read_file, search_files are "
    "registered with the API. If you support native function/tool calls, call them "
    "directly (the client executes them and returns the result).\n"
    "2) TEXT JSON (works on every model): output ONLY a single JSON object, nothing before "
    "or after it (no markdown fences, no prose):\n"
    "  {\"tool\": \"<tool_name>\", \"arguments\": {<arguments>}}\n"
    "- Never mix a tool call and a summary in one reply, and never nest one mechanism "
    "inside the other.\n"
    "- The client executes the tool and returns the result to you as the next message. "
    "Then you continue: inspect documents, search for relevant passages, and repeat "
    "until you can answer.\n"
    "- When you have gathered all the information you need, reply with a normal text "
    "message that answers the user based on the documents.\n\n"
    "AVAILABLE TOOLS:\n"
    "1. {\"tool\": \"list_files\", \"arguments\": {}}\n"
    "   -> Returns the list of documents in the knowledge base (name + size).\n"
    "2. {\"tool\": \"read_file\", \"arguments\": {\"name\": \"<document name>\"}}\n"
    "   -> Returns the full content of the named document.\n"
    "3. {\"tool\": \"search_files\", \"arguments\": {\"query\": \"<search text>\"}}\n"
    "   -> Returns which documents contain the query and a short snippet from each.\n\n"
    "RULES:\n"
    "- Always base your answers on the uploaded documents; use list_files / read_file / "
    "search_files to retrieve the relevant information.\n"
    "- Never invent content: if the knowledge base does not contain the answer, say so.\n"
    "- If a tool returns an error, adjust your arguments and retry.\n"
    "- Work step by step - exactly one tool call per reply.\n"
    "- Communicate with the user in the same language they used.";

static void rag_progress(void *p, const char *text) {
    progress_t *pr = p;
    if (!pr) return;
    progress_set(pr, "%s", text);
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

void *rag_worker(void *arg) {
    rag_arg_t *a = arg;
    rag_chat_push(a->chat, "user", a->user_message);

    /* iteration 1: only the document list (names), no contents - the model must
     * call read_file / search_files to inspect the actual documents. The KB is
     * loaded once on the main thread (a->kb), so no storage access here. */
    sbuf_t blist;
    sbuf_init(&blist);
    if (a->kb.n == 0) {
        sbuf_append(&blist, "The knowledge base is empty - no documents uploaded yet.");
    } else {
        for (int i = 0; i < a->kb.n; i++) {
            if (i) sbuf_append(&blist, "\n");
            sbuf_appendf(&blist, "- %s (%d characters)", a->kb.items[i].name,
                         (int)strlen(a->kb.items[i].content));
        }
    }
    char *initial_ctx = xasprintf(
        "User request: %s\n\nDocuments in the knowledge base (names only; use "
        "read_file or search_files to inspect their content):\n%s",
        a->user_message, blist.s ? blist.s : "");
    sbuf_free(&blist);

    cJSON *arr = cJSON_CreateArray();
    cJSON *s = cJSON_CreateObject();
    cJSON_AddStringToObject(s, "role", "system");
    cJSON_AddStringToObject(s, "content", RAG_SYSTEM_PROMPT);
    cJSON_AddItemToArray(arr, s);
    cJSON *u = cJSON_CreateObject();
    cJSON_AddStringToObject(u, "role", "user");
    cJSON_AddStringToObject(u, "content", initial_ctx);
    cJSON_AddItemToArray(arr, u);

    const char *model = a->model && a->model[0] ? a->model : g_cfg.selected_model;

    int iterations = 0;
    while (iterations < 15 && !(a->stop && *a->stop)) {
        iterations++;
        rag_progress(a->progress_ptr, "Thinking...");
        char *messages_json = cJSON_PrintUnformatted(arr);
        char *tools_json = rag_tools_json();
        api_result_t res;
        int rc = api_complete_agent(model, messages_json, tools_json,
                                    g_cfg.temperature, g_cfg.top_p, g_cfg.max_tokens, &res);
        free(tools_json);
        free(messages_json);
        if (rc != 0) {
            rag_chat_push(a->chat, "assistant", xasprintf("Error: %s", res.content ? res.content : "API error"));
            api_result_free(&res);
            break;
        }

        char *tool_call = parse_tool_call(res.content);
        if (tool_call) {
            cJSON *tj = cJSON_Parse(tool_call);
            cJSON *tn = cJSON_GetObjectItem(tj, "tool");
            char *result = execute_tool(tool_call, &a->kb);
            char *note = xasprintf("[Tool result from %s]:\n%s",
                                   tn && tn->valuestring ? tn->valuestring : "tool", result);
            /* Feed the result back as a user message. The assistant's raw tool JSON is
             * intentionally NOT echoed back: native tool calls from a previous turn are
             * not replayed, so the next request carries clean plain-text history. */
            cJSON *m = cJSON_CreateObject();
            cJSON_AddStringToObject(m, "role", "user");
            cJSON_AddStringToObject(m, "content", note);
            cJSON_AddItemToArray(arr, m);
            rag_chat_push(a->chat, "assistant", xasprintf("🔧 %s", note));
            free(note);
            free(result);
            cJSON_Delete(tj);
            free(tool_call);
            rag_progress(a->progress_ptr, "Executing tool...");
        } else {
            if (res.content && res.content[0]) {
                rag_chat_push(a->chat, "assistant", res.content);
            }
            api_result_free(&res);
            break;
        }
        api_result_free(&res);
        arr_trim(arr, 2, 30);
    }
    if (iterations >= 15) {
        rag_chat_push(a->chat, "assistant", "Reached maximum iteration limit. Please refine your request.");
    }
    cJSON_Delete(arr);
    free(initial_ctx);
    return NULL;
}

bool rag_export_to_notes(const kb_t *kb, int idx, notes_t *notes) {
    if (!kb || !notes || idx < 0 || idx >= kb->n) return false;
    notes_new(notes);
    note_t *note = notes_current(notes);
    char *title = xstrdup(kb->items[idx].name);
    if (str_has_suffix(title, ".md")) {
        title[strlen(title) - 3] = '\0';
    } else if (str_has_suffix(title, ".txt")) {
        title[strlen(title) - 4] = '\0';
    }
    notes_set_current_fields(note, title, kb->items[idx].content);
    free(title);
    return true;
}
