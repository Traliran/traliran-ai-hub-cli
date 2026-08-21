/*
 * chat.c - sessions, messages, and LLM API calls.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <pthread.h>

#include "cJSON.h"
#include "util.h"
#include "storage.h"
#include "providers.h"
#include "config.h"
#include "http.h"
#include "chat.h"

/* =================== sessions =================== */

static msg_t *msg_new(const char *role, const char *content) {
    msg_t *m = calloc(1, sizeof(msg_t));
    m->role = xstrdup(role);
    m->content = xstrdup(content ? content : "");
    return m;
}

static void msg_free(msg_t *m) {
    free(m->role);
    free(m->content);
    free(m);
}

static void session_free(session_t *s) {
    free(s->id);
    free(s->name);
    free(s->system_prompt);
    free(s->bot_name);
    for (int i = 0; i < s->n; i++) msg_free(&s->messages[i]);
    free(s->messages);
    pthread_mutex_destroy(&s->mtx);
    free(s);
}

static session_t *session_new(const char *name, const char *bot_name) {
    session_t *s = calloc(1, sizeof(session_t));
    pthread_mutex_init(&s->mtx, NULL);
    s->id = xasprintf("session_%lld", (long long)now_ms());
    s->name = xstrdup(name);
    s->bot_name = xstrdup(bot_name && bot_name[0] ? bot_name : "Default AI");
    s->system_prompt = xstrdup("");
    return s;
}

void chat_msg_push(session_t *s, const char *role, const char *content) {
    pthread_mutex_lock(&s->mtx);
    if (s->n >= s->cap) {
        s->cap = s->cap ? s->cap * 2 : 16;
        s->messages = realloc(s->messages, sizeof(msg_t) * (size_t)s->cap);
    }
    s->messages[s->n++] = *msg_new(role, content);
    pthread_mutex_unlock(&s->mtx);
}

int chat_count_assistant(const session_t *s) {
    int n = 0;
    for (int i = 0; i < s->n; i++)
        if (!strcmp(s->messages[i].role, "assistant")) n++;
    return n;
}

void chat_load(chat_t *c) {
    memset(c, 0, sizeof(*c));
    const char *raw = storage_get("gem_sessions");
    if (raw[0]) {
        cJSON *arr = cJSON_Parse(raw);
        if (arr && cJSON_IsArray(arr)) {
            int n = cJSON_GetArraySize(arr);
            for (int i = 0; i < n; i++) {
                cJSON *j = cJSON_GetArrayItem(arr, i);
                cJSON *id = cJSON_GetObjectItem(j, "id");
                cJSON *name = cJSON_GetObjectItem(j, "name");
                cJSON *sysp = cJSON_GetObjectItem(j, "systemPrompt");
                cJSON *botn = cJSON_GetObjectItem(j, "botName");
                cJSON *msgs = cJSON_GetObjectItem(j, "messages");
                session_t *s = session_new(
                    name && name->valuestring ? name->valuestring : "Chat",
                    botn && botn->valuestring ? botn->valuestring : "Default AI");
                free(s->id);
                s->id = xstrdup(id && id->valuestring ? id->valuestring : s->id);
                if (sysp && sysp->valuestring) {
                    free(s->system_prompt);
                    s->system_prompt = xstrdup(sysp->valuestring);
                }
                if (msgs && cJSON_IsArray(msgs)) {
                    int mn = cJSON_GetArraySize(msgs);
                    for (int k = 0; k < mn; k++) {
                        cJSON *jm = cJSON_GetArrayItem(msgs, k);
                        cJSON *role = cJSON_GetObjectItem(jm, "role");
                        cJSON *content = cJSON_GetObjectItem(jm, "content");
                        if (role && role->valuestring && content && content->valuestring)
                            chat_msg_push(s, role->valuestring, content->valuestring);
                    }
                }
                c->list = realloc(c->list, sizeof(session_t *) * (size_t)(c->n + 1));
                c->list[c->n++] = s;
            }
        }
        if (arr) cJSON_Delete(arr);
    }
    if (c->n == 0) {
        chat_new(c);
    } else {
        c->current = 0;
    }
}

void chat_save(chat_t *c) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < c->n; i++) {
        session_t *s = c->list[i];
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "id", s->id);
        cJSON_AddStringToObject(j, "name", s->name);
        cJSON_AddStringToObject(j, "systemPrompt", s->system_prompt);
        cJSON_AddStringToObject(j, "botName", s->bot_name);
        cJSON *msgs = cJSON_CreateArray();
        for (int k = 0; k < s->n; k++) {
            cJSON *jm = cJSON_CreateObject();
            cJSON_AddStringToObject(jm, "role", s->messages[k].role);
            cJSON_AddStringToObject(jm, "content", s->messages[k].content);
            cJSON_AddItemToArray(msgs, jm);
        }
        cJSON_AddItemToObject(j, "messages", msgs);
        cJSON_AddItemToArray(arr, j);
    }
    char *json = cJSON_PrintUnformatted(arr);
    if (json) {
        storage_set("gem_sessions", json);
        free(json);
    }
    cJSON_Delete(arr);
    storage_save();
}

session_t *chat_current(chat_t *c) {
    if (c->n == 0) return NULL;
    if (c->current < 0 || c->current >= c->n) c->current = 0;
    return c->list[c->current];
}

void chat_new(chat_t *c) {
    session_t *s = session_new("", "Default AI");
    free(s->name);
    s->name = xasprintf("Chat #%d", c->n + 1);
    c->list = realloc(c->list, sizeof(session_t *) * (size_t)(c->n + 1));
    for (int i = c->n; i > 0; i--) c->list[i] = c->list[i - 1];
    c->list[0] = s;
    c->n++;
    c->current = 0;
}

void chat_delete(chat_t *c, int idx) {
    if (idx < 0 || idx >= c->n) return;
    session_free(c->list[idx]);
    for (int i = idx; i < c->n - 1; i++) c->list[i] = c->list[i + 1];
    c->n--;
    if (c->n == 0) {
        chat_new(c);
    } else {
        if (c->current >= c->n) c->current = c->n - 1;
    }
}

void chat_rename(chat_t *c, int idx, const char *name) {
    if (idx < 0 || idx >= c->n) return;
    free(c->list[idx]->name);
    c->list[idx]->name = xstrdup(name);
}

void chat_select(chat_t *c, int idx) {
    if (idx >= 0 && idx < c->n) c->current = idx;
}

char *chat_session_name(session_t *s) {
    if (!s) return NULL;
    if (str_has_prefix(s->name, "Chat #") && s->n > 0) {
        const char *first = s->messages[0].content;
        if (first && *first) {
            size_t n = strlen(first);
            if (n > 24) n = 24;
            return xstrndup(first, n);
        }
    }
    return xstrdup(s->name);
}

void chat_set_session_prompt(session_t *s, const char *system_prompt, const char *bot_name) {
    free(s->system_prompt);
    s->system_prompt = xstrdup(system_prompt ? system_prompt : "");
    if (bot_name && bot_name[0]) {
        free(s->bot_name);
        s->bot_name = xstrdup(bot_name);
    }
}

/* =================== stream buffer =================== */

void stream_init(stream_t *s) {
    memset(s, 0, sizeof(*s));
    pthread_mutex_init(&s->mtx, NULL);
}
void stream_free(stream_t *s) {
    pthread_mutex_destroy(&s->mtx);
    free(s->content);
    free(s->reasoning);
    free(s->raw);
    free(s->linebuf);
    free(s->error);
}

static void stream_append(char **dst, size_t *len, size_t *cap, const char *data, size_t n) {
    if (*len + n + 1 > *cap) {
        *cap = *cap ? *cap : 256;
        while (*cap < *len + n + 1) *cap *= 2;
        *dst = realloc(*dst, *cap);
    }
    memcpy(*dst + *len, data, n);
    *len += n;
    (*dst)[*len] = '\0';
}

/* simple append for content/reasoning: length derived from strlen */
static void stream_append_simple(char **dst, const char *data, size_t n) {
    size_t len = *dst ? strlen(*dst) : 0;
    size_t cap = len + n + 1;
    *dst = realloc(*dst, cap);
    memcpy(*dst + len, data, n);
    (*dst)[len + n] = '\0';
}

char *stream_snapshot_content(stream_t *s) {
    pthread_mutex_lock(&s->mtx);
    char *out = xstrdup(s->content ? s->content : "");
    pthread_mutex_unlock(&s->mtx);
    return out;
}
char *stream_snapshot_reasoning(stream_t *s) {
    pthread_mutex_lock(&s->mtx);
    char *out = xstrdup(s->reasoning ? s->reasoning : "");
    pthread_mutex_unlock(&s->mtx);
    return out;
}

/* =================== SSE parsing =================== */

static void sse_process_line(stream_t *st, char *line) {
    char *trimmed = str_trim(line);
    if (trimmed[0] == '\0') return;
    if (str_has_prefix(trimmed, "data:")) {
        char *payload = str_trim(trimmed + 5);
        if (!strcmp(payload, "[DONE]")) return;
        cJSON *j = cJSON_Parse(payload);
        if (!j) return;
        const char *delta_text = NULL;
        const char *delta_reasoning = NULL;

        /* OpenAI-compatible */
        cJSON *choices = cJSON_GetObjectItem(j, "choices");
        if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
            cJSON *c0 = cJSON_GetArrayItem(choices, 0);
            cJSON *delta = cJSON_GetObjectItem(c0, "delta");
            if (delta) {
                cJSON *content = cJSON_GetObjectItem(delta, "content");
                cJSON *reasoning = cJSON_GetObjectItem(delta, "reasoning_content");
                cJSON *reasoning2 = cJSON_GetObjectItem(delta, "thinking_content");
                if (content && cJSON_IsString(content)) delta_text = content->valuestring;
                if (reasoning && cJSON_IsString(reasoning)) delta_reasoning = reasoning->valuestring;
                if (!delta_reasoning && reasoning2 && cJSON_IsString(reasoning2)) delta_reasoning = reasoning2->valuestring;
            }
        }
        /* Anthropic streaming */
        if (!delta_text) {
            cJSON *d = cJSON_GetObjectItem(j, "delta");
            if (d) {
                cJSON *t = cJSON_GetObjectItem(d, "text");
                if (t && cJSON_IsString(t)) delta_text = t->valuestring;
                cJSON *r = cJSON_GetObjectItem(d, "thinking");
                if (r && cJSON_IsString(r)) delta_reasoning = r->valuestring;
            }
        }
        if (delta_text && *delta_text)
            stream_append_simple(&st->content, delta_text, strlen(delta_text));
        if (delta_reasoning && *delta_reasoning)
            stream_append_simple(&st->reasoning, delta_reasoning, strlen(delta_reasoning));
        cJSON_Delete(j);
    }
}

static int sse_cb(const char *data, size_t len, void *ud) {
    stream_t *st = ud;
    pthread_mutex_lock(&st->mtx);
    if (st->stop) {
        pthread_mutex_unlock(&st->mtx);
        return 1;
    }
    /* raw accumulation */
    stream_append(&st->raw, &st->raw_len, &st->raw_cap, data, len);
    /* line buffer parsing */
    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if (c == '\n') {
            if (st->linebuf_len) {
                st->linebuf[st->linebuf_len] = '\0';
                sse_process_line(st, st->linebuf);
                st->linebuf_len = 0;
            }
        } else {
            stream_append(&st->linebuf, &st->linebuf_len, &st->linebuf_cap, &c, 1);
        }
    }
    pthread_mutex_unlock(&st->mtx);
    return 0;
}

/* =================== API calls =================== */

static char *extract_error_message(const char *body) {
    if (!body) return xstrdup("Unknown error");
    cJSON *j = cJSON_Parse(body);
    if (j) {
        cJSON *err = cJSON_GetObjectItem(j, "error");
        char *msg = NULL;
        if (err && cJSON_IsObject(err)) {
            cJSON *m = cJSON_GetObjectItem(err, "message");
            if (m && cJSON_IsString(m) && m->valuestring) msg = xstrdup(m->valuestring);
        } else if (err && cJSON_IsString(err) && err->valuestring) {
            msg = xstrdup(err->valuestring);
        }
        cJSON_Delete(j);
        if (msg) return msg;
    }
    return xasprintf("HTTP error: %.*s", 200, body ? body : "");
}

static char *build_openai_payload(const char *model, const char *messages_json,
                                  const char *tools_json,
                                  double temperature, double top_p, int max_tokens,
                                  bool stream) {
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "model", model);
    cJSON *msgs = cJSON_Parse(messages_json);
    if (msgs && cJSON_IsArray(msgs)) {
        cJSON_AddItemToObject(j, "messages", msgs);
    } else {
        if (msgs) cJSON_Delete(msgs);
        cJSON_AddItemToObject(j, "messages", cJSON_CreateArray());
    }
    cJSON_AddNumberToObject(j, "temperature", temperature);
    cJSON_AddNumberToObject(j, "top_p", top_p);
    cJSON_AddNumberToObject(j, "max_tokens", max_tokens);
    /* Register tools when provided. Some OpenAI-compatible backends (e.g. Groq)
     * treat the absence of `tools` as tool_choice "none" and reject a response in
     * which the model emits a native tool call with "Tool choice is none, but
     * model called a tool". Declaring the tools lets the model call them natively. */
    if (tools_json && tools_json[0]) {
        cJSON *tools = cJSON_Parse(tools_json);
        if (tools && cJSON_IsArray(tools)) {
            cJSON_AddItemToObject(j, "tools", tools);
            cJSON_AddStringToObject(j, "tool_choice", "auto");
        } else if (tools) {
            cJSON_Delete(tools);
        }
    }
    if (stream) cJSON_AddBoolToObject(j, "stream", 1);
    char *out = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    return out;
}

/* Convert a native OpenAI `tool_calls` array into the IDE's plain-text JSON protocol
 * ({"tool": "...", "arguments": {...}}). Used only as a fallback: some tool-capable
 * models emit native tool calls even though no `tools` field is sent in the request. */
static char *native_tool_calls_to_json(cJSON *tc) {
    if (!tc || !cJSON_IsArray(tc) || cJSON_GetArraySize(tc) == 0) return NULL;
    cJSON *call = cJSON_GetArrayItem(tc, 0);
    cJSON *fn = cJSON_GetObjectItem(call, "function");
    cJSON *nm = fn ? cJSON_GetObjectItem(fn, "name") : NULL;
    cJSON *args = fn ? cJSON_GetObjectItem(fn, "arguments") : NULL;
    const char *name = nm && nm->valuestring ? nm->valuestring : "";
    if (!name[0]) return NULL;
    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "tool", name);
    cJSON *args_obj = NULL;
    if (args && args->valuestring) {
        cJSON *parsed = cJSON_Parse(args->valuestring);
        if (parsed && cJSON_IsObject(parsed)) args_obj = parsed;
        else if (parsed) cJSON_Delete(parsed);
    }
    if (!args_obj) {
        args_obj = cJSON_CreateObject();
        if (args && args->valuestring)
            cJSON_AddStringToObject(args_obj, "raw", args->valuestring);
    }
    cJSON_AddItemToObject(out, "arguments", args_obj);
    char *s = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    return s;
}

/* prepend a native tool call JSON to the content so the local JSON parser finds it first.
 * Consumes `ntc` (ownership transferred); returns malloc'd string. */
static char *merge_native_call(const char *content, char *ntc) {
    if (!ntc) return xstrdup(content ? content : "");
    if (content && content[0]) {
        char *merged = xasprintf("%s\n%s", ntc, content);
        free(ntc);
        return merged;
    }
    return ntc;
}

static int api_complete_impl(const char *model, const char *messages_json,
                             const char *tools_json,
                             double temperature, double top_p, int max_tokens,
                             api_result_t *out) {
    memset(out, 0, sizeof(*out));
    const provider_t *pr = providers_get(g_cfg.provider);
    const char *ep = config_endpoint();
    const char *key = g_cfg.api_key;

    if (!strcmp(pr->type, "anthropic")) {
        /* convert to anthropic /messages payload */
        cJSON *msgs = cJSON_Parse(messages_json);
        cJSON *payload = cJSON_CreateObject();
        cJSON_AddStringToObject(payload, "model", model);
        cJSON_AddNumberToObject(payload, "max_tokens", max_tokens > 0 ? max_tokens : 1024);
        cJSON_AddNumberToObject(payload, "temperature", temperature);
        cJSON *mlist = cJSON_CreateArray();
        if (msgs && cJSON_IsArray(msgs)) {
            int n = cJSON_GetArraySize(msgs);
            for (int i = 0; i < n; i++) {
                cJSON *m = cJSON_GetArrayItem(msgs, i);
                cJSON *role = cJSON_GetObjectItem(m, "role");
                cJSON *content = cJSON_GetObjectItem(m, "content");
                if (!role || !role->valuestring) continue;
                if (!strcmp(role->valuestring, "system")) {
                    if (content && content->valuestring) {
                        cJSON *sys = cJSON_CreateString(content->valuestring);
                        cJSON_AddItemToObject(payload, "system", sys);
                    }
                    continue;
                }
                cJSON *mm = cJSON_CreateObject();
                cJSON_AddStringToObject(mm, "role",
                    !strcmp(role->valuestring, "assistant") ? "assistant" : "user");
                cJSON_AddStringToObject(mm, "content", content && content->valuestring ? content->valuestring : "");
                cJSON_AddItemToArray(mlist, mm);
            }
        }
        cJSON_AddItemToObject(payload, "messages", mlist);
        /* Register tools (same rationale as the OpenAI-compatible path above). */
        if (tools_json && tools_json[0]) {
            cJSON *tools = cJSON_Parse(tools_json);
            if (tools && cJSON_IsArray(tools)) {
                cJSON_AddItemToObject(payload, "tools", tools);
                cJSON *tc = cJSON_CreateObject();
                cJSON_AddStringToObject(tc, "type", "auto");
                cJSON_AddItemToObject(payload, "tool_choice", tc);
            } else if (tools) {
                cJSON_Delete(tools);
            }
        }
        char *body = cJSON_PrintUnformatted(payload);
        cJSON_Delete(payload);
        if (msgs) cJSON_Delete(msgs);

        char url[2048];
        snprintf(url, sizeof(url), "%s/messages", ep);
        http_res_t *r = http_post_json(url, NULL, key, body);
        free(body);
        if (!r) return -1;
        if (!r->ok) {
            char *msg = extract_error_message(r->body);
            out->content = msg;
            out->reasoning = NULL;
            http_res_free(r);
            return -1;
        }
        cJSON *j = cJSON_Parse(r->body);
        http_res_free(r);
        if (!j) return -1;
        cJSON *carr = cJSON_GetObjectItem(j, "content");
        sbuf_t text;
        sbuf_init(&text);
        if (carr && cJSON_IsArray(carr)) {
            int n = cJSON_GetArraySize(carr);
            for (int i = 0; i < n; i++) {
                cJSON *part = cJSON_GetArrayItem(carr, i);
                cJSON *t = cJSON_GetObjectItem(part, "text");
                if (t && cJSON_IsString(t) && t->valuestring)
                    sbuf_append(&text, t->valuestring);
                cJSON *ptype = cJSON_GetObjectItem(part, "type");
                if (ptype && ptype->valuestring && !strcmp(ptype->valuestring, "tool_use")) {
                    cJSON *pnm = cJSON_GetObjectItem(part, "name");
                    cJSON *pin = cJSON_GetObjectItem(part, "input");
                    if (pnm && pnm->valuestring) {
                        if (text.len) sbuf_append(&text, "\n");
                        cJSON *call = cJSON_CreateObject();
                        cJSON_AddStringToObject(call, "tool", pnm->valuestring);
                        cJSON_AddItemToObject(call, "arguments",
                                              pin ? cJSON_Duplicate(pin, 1) : cJSON_CreateObject());
                        char *cs = cJSON_PrintUnformatted(call);
                        sbuf_append(&text, cs);
                        free(cs);
                        cJSON_Delete(call);
                    }
                }
            }
        }
        out->content = sbuf_detach(&text);
        sbuf_free(&text);
        cJSON_Delete(j);
        return 0;
    }

    /* OpenAI-compatible */
    char *payload = build_openai_payload(model, messages_json, tools_json, temperature, top_p, max_tokens, false);
    char url[2048];
    snprintf(url, sizeof(url), "%s/chat/completions", ep);
    http_res_t *r = http_post_json(url, key, NULL, payload);
    free(payload);
    if (!r) return -1;
    if (!r->ok) {
        char *msg = extract_error_message(r->body);
        out->content = msg;
        out->reasoning = NULL;
        http_res_free(r);
        return -1;
    }
    cJSON *j = cJSON_Parse(r->body);
    http_res_free(r);
    if (!j) return -1;
    cJSON *choices = cJSON_GetObjectItem(j, "choices");
    if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *c0 = cJSON_GetArrayItem(choices, 0);
        cJSON *message = cJSON_GetObjectItem(c0, "message");
        cJSON *content = cJSON_GetObjectItem(message, "content");
        cJSON *reasoning = cJSON_GetObjectItem(message, "reasoning_content");
        cJSON *thinking = cJSON_GetObjectItem(message, "thinking_content");
        out->content = xstrdup(content && content->valuestring ? content->valuestring : "");
        if (reasoning && cJSON_IsString(reasoning)) out->reasoning = xstrdup(reasoning->valuestring);
        else if (thinking && cJSON_IsString(thinking)) out->reasoning = xstrdup(thinking->valuestring);
        else out->reasoning = xstrdup("");
        char *ntc = native_tool_calls_to_json(cJSON_GetObjectItem(message, "tool_calls"));
        if (ntc) {
            char *merged = merge_native_call(out->content, ntc);
            free(out->content);
            out->content = merged;
        }
    } else {
        out->content = xstrdup("");
        out->reasoning = xstrdup("");
    }
    cJSON_Delete(j);
    return 0;
}

int api_complete(const char *model, const char *messages_json,
                 double temperature, double top_p, int max_tokens,
                 api_result_t *out) {
    return api_complete_impl(model, messages_json, NULL, temperature, top_p, max_tokens, out);
}

int api_complete_agent(const char *model, const char *messages_json, const char *tools_json,
                       double temperature, double top_p, int max_tokens,
                       api_result_t *out) {
    return api_complete_impl(model, messages_json, tools_json, temperature, top_p, max_tokens, out);
}

void api_result_free(api_result_t *r) {
    free(r->content);
    free(r->reasoning);
}

int api_stream(const char *model, const char *messages_json,
               double temperature, double top_p, int max_tokens,
               stream_t *st) {
    const char *ep = config_endpoint();
    const char *key = g_cfg.api_key;

    char *payload = build_openai_payload(model, messages_json, NULL, temperature, top_p, max_tokens, true);
    char url[2048];
    snprintf(url, sizeof(url), "%s/chat/completions", ep);
    int rc = http_post_json_stream(url, key, NULL, payload, sse_cb, st);
    free(payload);
    if (rc != 0) {
        /* rc>0 => http status; rc<0 => transfer error */
        pthread_mutex_lock(&st->mtx);
        if (!st->stop) {
            if (rc > 0) {
                char *msg = extract_error_message(st->raw);
                st->error = xasprintf("HTTP %d: %s", rc, msg);
                free(msg);
            } else {
                st->error = xasprintf("Network error (%d)", rc);
            }
        }
        pthread_mutex_unlock(&st->mtx);
        return -1;
    }
    return 0;
}

/* =================== system prompt =================== */

char *chat_build_system(const char *user_text) {
    sbuf_t p;
    sbuf_init(&p);
    char *base = str_dup_trim(g_cfg.system_prompt);
    if (base && base[0]) sbuf_append(&p, base);
    free(base);
    char *pi = str_dup_trim(g_cfg.personal_info);
    if (pi && pi[0]) {
        if (p.len) sbuf_append(&p, "\n\n");
        sbuf_appendf(&p, "[About the user]:\n%s", pi);
    }
    free(pi);
    bool cyr = false;
    if (user_text) {
        for (const unsigned char *c = (const unsigned char *)user_text; *c; c++) {
            if (*c >= 0xC0) { cyr = true; break; }
        }
    }
    const char *hint = cyr
        ? "Ответь на русском языке. Answer in the same language as the user's prompt and keep the response complete, without cutting off the answer mid-sentence."
        : "Answer in the same language as the user's prompt and keep the response complete, without cutting off the answer mid-sentence.";
    if (p.len) sbuf_append(&p, "\n\n");
    sbuf_append(&p, hint);
    return sbuf_detach(&p);
}

/* build messages JSON from a session (system + all messages) */
static char *session_messages_json(session_t *s, const char *extra_system) {
    cJSON *arr = cJSON_CreateArray();
    if (s->system_prompt && s->system_prompt[0]) {
        cJSON *sys = cJSON_CreateObject();
        cJSON_AddStringToObject(sys, "role", "system");
        cJSON_AddStringToObject(sys, "content", s->system_prompt);
        cJSON_AddItemToArray(arr, sys);
    } else if (extra_system) {
        cJSON *sys = cJSON_CreateObject();
        cJSON_AddStringToObject(sys, "role", "system");
        cJSON_AddStringToObject(sys, "content", extra_system);
        cJSON_AddItemToArray(arr, sys);
    }
    for (int i = 0; i < s->n; i++) {
        cJSON *m = cJSON_CreateObject();
        cJSON_AddStringToObject(m, "role", s->messages[i].role);
        cJSON_AddStringToObject(m, "content", s->messages[i].content);
        cJSON_AddItemToArray(arr, m);
    }
    char *out = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return out;
}

/* =================== send worker =================== */

void *send_worker(void *arg) {
    send_arg_t *a = arg;
    char *msgs = session_messages_json(a->session, NULL);
    api_stream(a->model, msgs, g_cfg.temperature, g_cfg.top_p, g_cfg.max_tokens, &a->stream);
    free(msgs);
    return NULL;
}

/* =================== multi-model worker =================== */

typedef struct {
    mm_arg_t  *all;
    int        idx;
} mm_one_arg_t;

static void *mm_one_t(void *arg) {
    mm_one_arg_t *a = arg;
    mm_arg_t *all = a->all;
    int idx = a->idx;
    mm_result_t *r = &all->results[idx];
    if (all->stop) return NULL;
    int rc = api_complete(all->models[idx], all->messages_json,
                          all->temperature, all->top_p, all->max_tokens, &r->res);
    if (rc != 0) {
        r->err = 1;
        r->errmsg = xstrdup(r->res.content ? r->res.content : "API error");
        free(r->res.content);
        r->res.content = NULL;
    }
    free(a);
    return NULL;
}

void *multi_worker(void *arg) {
    mm_arg_t *all = arg;
    pthread_t *tids = calloc((size_t)all->nmodels, sizeof(pthread_t));
    for (int i = 0; i < all->nmodels; i++) {
        mm_one_arg_t *a = calloc(1, sizeof(*a));
        a->all = all;
        a->idx = i;
        pthread_create(&tids[i], NULL, mm_one_t, a);
    }
    for (int i = 0; i < all->nmodels; i++) pthread_join(tids[i], NULL);
    free(tids);
    return NULL;
}

/* =================== debate worker =================== */

void progress_set(progress_t *p, const char *fmt, ...) {
    pthread_mutex_lock(&p->mtx);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(p->text, sizeof(p->text), fmt, ap);
    va_end(ap);
    pthread_mutex_unlock(&p->mtx);
}

void *debate_worker(void *arg) {
    debate_arg_t *d = arg;
    const char *lang_hint = "Answer in the same language as the user's proposition. If the proposition is in Russian, respond in Russian; if it is in English, respond in English. Do not switch languages and keep your output complete, avoiding cut-off fragments.";
    const char *agents[3][2] = {
        { "Agent Optimist", "You are an optimistic market strategist. Analyze the given idea, highlight its strongest disruptive potentials, hidden opportunities, and scalable micro-advantages. Keep your response brief, targeted, and focused entirely on potential success vectors. %s" },
        { "Agent Critic", "You are a ruthless risk analyst and security architect. Deconstruct the user's idea to find conceptual faults, operational vulnerabilities, security pitfalls, and hidden execution expenses. Be brutally honest. %s" },
        { "Agent Technologist", "You are a pragmatic solutions engineer. Evaluate the architectural feasibility of the idea, map out a realistic software/hardware stack layout, data handling structures, and step-by-step developer pipeline roadmap. %s" },
    };

    /* push user proposition */
    chat_msg_push(d->session, "user", xasprintf("Proposition for debate:\n%s", d->idea));

    int max_tokens = g_cfg.max_tokens < 1500 ? 1500 : g_cfg.max_tokens;
    for (int round = 1; round <= 2; round++) {
        for (int i = 0; i < 3; i++) {
            progress_set(d->prog, "%s evaluating (Round %d/2)...", agents[i][0], round);
            char *sys = xasprintf(agents[i][1], lang_hint);
            /* build messages: system + session */
            cJSON *arr = cJSON_CreateArray();
            cJSON *sysm = cJSON_CreateObject();
            cJSON_AddStringToObject(sysm, "role", "system");
            cJSON_AddStringToObject(sysm, "content", sys);
            cJSON_AddItemToArray(arr, sysm);
            for (int k = 0; k < d->session->n; k++) {
                cJSON *m = cJSON_CreateObject();
                cJSON_AddStringToObject(m, "role", d->session->messages[k].role);
                cJSON_AddStringToObject(m, "content", d->session->messages[k].content);
                cJSON_AddItemToArray(arr, m);
            }
            char *msgs = cJSON_PrintUnformatted(arr);
            cJSON_Delete(arr);
            free(sys);

            api_result_t res;
            if (api_complete(d->model, msgs, 0.8, 0.95, max_tokens, &res) == 0) {
                char *final = NULL;
                if (res.reasoning && res.reasoning[0]) {
                    final = xasprintf(" thinking%s response\n%s", res.reasoning, res.content ? res.content : "");
                } else {
                    final = xstrdup(res.content ? res.content : "");
                }
                chat_msg_push(d->session, "assistant", xasprintf("**[%s]**\n\n%s", agents[i][0], final));
                free(final);
                api_result_free(&res);
            }
            free(msgs);
        }
    }
    progress_set(d->prog, "Debate finished.");
    return NULL;
}

/* =================== generic non-streaming job =================== */

void *simple_complete_worker(void *arg) {
    simple_arg_t *a = arg;
    api_complete(a->model, a->messages_json, a->temperature, a->top_p, a->max_tokens, &a->res);
    return NULL;
}