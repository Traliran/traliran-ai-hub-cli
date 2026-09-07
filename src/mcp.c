/*
 * mcp.c - Model Context Protocol client (Streamable HTTP + JSON-RPC 2.0).
 * Ports mcp.js and app.js agent integration to C11 + libcurl + cJSON.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <pthread.h>
#include <curl/curl.h>

#include "cJSON.h"
#include "util.h"
#include "storage.h"
#include "mcp.h"

/* =================== internal state =================== */

static mcp_server_t g_servers[MCP_MAX_SERVERS];
static int g_nservers = 0;

static mcp_client_t g_clients[MCP_MAX_SERVERS];
static int g_nclients = 0;

static mcp_registry_entry_t g_registry[MCP_MAX_REGISTRY];
static int g_nregistry = 0;

static pthread_mutex_t g_mcp_mtx = PTHREAD_MUTEX_INITIALIZER;

/* =================== helpers =================== */

char *mcp_sanitize_name(const char *name) {
    if (!name || !name[0]) return xstrdup("server");
    size_t len = strlen(name);
    char *tmp = malloc(len * 2 + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_') {
            tmp[j++] = c;
        } else {
            tmp[j++] = '_';
        }
    }
    tmp[j] = '\0';
    /* collapse consecutive '_' */
    char *out = malloc(j + 1);
    size_t k = 0;
    bool last_us = false;
    for (size_t i = 0; i < j; i++) {
        if (tmp[i] == '_') {
            if (!last_us) out[k++] = '_';
            last_us = true;
        } else {
            out[k++] = tmp[i];
            last_us = false;
        }
    }
    out[k] = '\0';
    free(tmp);
    /* trim leading/trailing '_' */
    size_t start = 0;
    while (out[start] == '_') start++;
    size_t end = k;
    while (end > start && out[end - 1] == '_') end--;
    size_t slen = end - start;
    if (slen > 40) slen = 40;
    char *final = malloc(slen + 1);
    if (slen > 0) memcpy(final, out + start, slen);
    final[slen] = '\0';
    free(out);
    if (final[0] == '\0') {
        free(final);
        return xstrdup("server");
    }
    return final;
}

char *mcp_format_result(cJSON *result) {
    if (!result || cJSON_IsNull(result)) return xstrdup("(no result)");
    cJSON *isError = cJSON_GetObjectItem(result, "isError");
    if (cJSON_IsTrue(isError)) {
        cJSON *content = cJSON_GetObjectItem(result, "content");
        sbuf_t b;
        sbuf_init(&b);
        sbuf_append(&b, "Error: ");
        if (content && cJSON_IsArray(content)) {
            int n = cJSON_GetArraySize(content);
            for (int i = 0; i < n; i++) {
                cJSON *it = cJSON_GetArrayItem(content, i);
                cJSON *txt = cJSON_GetObjectItem(it, "text");
                if (txt && cJSON_IsString(txt) && txt->valuestring) {
                    if (i) sbuf_append(&b, "\n");
                    sbuf_append(&b, txt->valuestring);
                } else {
                    char *js = cJSON_PrintUnformatted(it);
                    if (js) { if (i) sbuf_append(&b, "\n"); sbuf_append(&b, js); free(js); }
                }
            }
        } else if (content) {
            char *js = cJSON_PrintUnformatted(content);
            if (js) { sbuf_append(&b, js); free(js); }
        } else {
            char *js = cJSON_PrintUnformatted(result);
            if (js) { sbuf_append(&b, js); free(js); }
        }
        return sbuf_detach(&b);
    }
    cJSON *content = cJSON_GetObjectItem(result, "content");
    if (content && cJSON_IsArray(content)) {
        sbuf_t b;
        sbuf_init(&b);
        int n = cJSON_GetArraySize(content);
        for (int i = 0; i < n; i++) {
            cJSON *it = cJSON_GetArrayItem(content, i);
            cJSON *type = cJSON_GetObjectItem(it, "type");
            const char *t = type && cJSON_IsString(type) ? type->valuestring : "";
            if (!strcmp(t, "text")) {
                cJSON *txt = cJSON_GetObjectItem(it, "text");
                if (txt && cJSON_IsString(txt) && txt->valuestring) {
                    if (b.len) sbuf_append(&b, "\n");
                    sbuf_append(&b, txt->valuestring);
                }
            } else if (!strcmp(t, "resource")) {
                cJSON *res = cJSON_GetObjectItem(it, "resource");
                char *js = res ? cJSON_PrintUnformatted(res) : NULL;
                if (js) { if (b.len) sbuf_append(&b, "\n"); sbuf_append(&b, js); free(js); }
                else {
                    char *js2 = cJSON_PrintUnformatted(it);
                    if (js2) { if (b.len) sbuf_append(&b, "\n"); sbuf_append(&b, js2); free(js2); }
                }
            } else {
                cJSON *txt = cJSON_GetObjectItem(it, "text");
                if (txt && cJSON_IsString(txt) && txt->valuestring) {
                    if (b.len) sbuf_append(&b, "\n");
                    sbuf_append(&b, txt->valuestring);
                } else {
                    char *js = cJSON_PrintUnformatted(it);
                    if (js) { if (b.len) sbuf_append(&b, "\n"); sbuf_append(&b, js); free(js); }
                }
            }
        }
        if (b.len) return sbuf_detach(&b);
        sbuf_free(&b);
    }
    char *js = cJSON_PrintUnformatted(result);
    if (js) return js;
    return xstrdup("(no result)");
}

/* =================== persistence =================== */

void mcp_load(void) {
    pthread_mutex_lock(&g_mcp_mtx);
    g_nservers = 0;
    const char *raw = storage_get(MCP_STORAGE_KEY);
    if (!raw || !raw[0]) {
        pthread_mutex_unlock(&g_mcp_mtx);
        return;
    }
    cJSON *arr = cJSON_Parse(raw);
    if (!arr || !cJSON_IsArray(arr)) {
        if (arr) cJSON_Delete(arr);
        pthread_mutex_unlock(&g_mcp_mtx);
        return;
    }
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n && g_nservers < MCP_MAX_SERVERS; i++) {
        cJSON *j = cJSON_GetArrayItem(arr, i);
        cJSON *jid = cJSON_GetObjectItem(j, "id");
        cJSON *jname = cJSON_GetObjectItem(j, "name");
        cJSON *jurl = cJSON_GetObjectItem(j, "url");
        cJSON *jauth = cJSON_GetObjectItem(j, "authHeader");
        if (!jid || !cJSON_IsString(jid) || !jid->valuestring) continue;
        if (!jurl || !cJSON_IsString(jurl) || !jurl->valuestring) continue;
        mcp_server_t *s = &g_servers[g_nservers++];
        snprintf(s->id, sizeof(s->id), "%s", jid->valuestring);
        snprintf(s->name, sizeof(s->name), "%s", jname && jname->valuestring ? jname->valuestring : "");
        snprintf(s->url, sizeof(s->url), "%s", jurl->valuestring);
        snprintf(s->authHeader, sizeof(s->authHeader), "%s", jauth && jauth->valuestring ? jauth->valuestring : "");
    }
    cJSON_Delete(arr);
    pthread_mutex_unlock(&g_mcp_mtx);
}

void mcp_save(void) {
    pthread_mutex_lock(&g_mcp_mtx);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < g_nservers; i++) {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "id", g_servers[i].id);
        cJSON_AddStringToObject(j, "name", g_servers[i].name);
        cJSON_AddStringToObject(j, "url", g_servers[i].url);
        cJSON_AddStringToObject(j, "authHeader", g_servers[i].authHeader);
        cJSON_AddItemToArray(arr, j);
    }
    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    pthread_mutex_unlock(&g_mcp_mtx);
    if (json) {
        storage_set(MCP_STORAGE_KEY, json);
        storage_save();
        free(json);
    }
}

/* =================== registry =================== */

static void mcp_free_client_tools(mcp_client_t *c) {
    for (int i = 0; i < c->ntools; i++) {
        if (c->tools[i].inputSchema) cJSON_Delete(c->tools[i].inputSchema);
    }
    free(c->tools);
    c->tools = NULL;
    c->ntools = 0;
    c->cap_tools = 0;
}

static int find_server_idx(const char *id) {
    for (int i = 0; i < g_nservers; i++) if (!strcmp(g_servers[i].id, id)) return i;
    return -1;
}

static int find_client_idx(const char *id) {
    for (int i = 0; i < g_nclients; i++) if (!strcmp(g_clients[i].id, id)) return i;
    return -1;
}

void mcp_rebuild_registry(void) {
    g_nregistry = 0;
    /* Build a set of used names for dedup */
    char used[MCP_MAX_REGISTRY][MCP_MAX_FN];
    int nused = 0;
    for (int ci = 0; ci < g_nclients; ci++) {
        mcp_client_t *cl = &g_clients[ci];
        if (!cl->connected) continue;
        char *prefix = mcp_sanitize_name(cl->name);
        for (int ti = 0; ti < cl->ntools; ti++) {
            if (g_nregistry >= MCP_MAX_REGISTRY) break;
            char base[MCP_MAX_FN];
            snprintf(base, sizeof(base), "%s_%s", prefix, cl->tools[ti].name);
            if (strlen(base) > 64) base[64] = '\0';
            char fn[MCP_MAX_FN];
            snprintf(fn, sizeof(fn), "%s", base);
            /* dedup */
            int attempts = 0;
            while (attempts < 10) {
                bool exists = false;
                for (int k = 0; k < nused; k++) if (!strcmp(used[k], fn)) { exists = true; break; }
                for (int k = 0; k < g_nregistry; k++) if (!strcmp(g_registry[k].fnName, fn)) { exists = true; break; }
                if (!exists) break;
                /* append random suffix 3 chars */
                char suffix[8];
                const char *chars = "abcdefghijklmnopqrstuvwxyz0123456789";
                for (int s = 0; s < 3; s++) suffix[s] = chars[rand() % 36];
                suffix[3] = '\0';
                char trunc[61];
                snprintf(trunc, sizeof(trunc), "%.*s", 60, base);
                snprintf(fn, sizeof(fn), "%s_%s", trunc, suffix);
                attempts++;
            }
            mcp_registry_entry_t *e = &g_registry[g_nregistry++];
            snprintf(e->fnName, sizeof(e->fnName), "%s", fn);
            snprintf(e->clientId, sizeof(e->clientId), "%s", cl->id);
            snprintf(e->toolName, sizeof(e->toolName), "%s", cl->tools[ti].name);
            snprintf(used[nused++], sizeof(used[0]), "%s", fn);
        }
        free(prefix);
    }
}

/* =================== HTTP transport =================== */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    char *header_session; /* captured Mcp-Session-Id */
    char *content_type;   /* captured Content-Type */
    long status;
} mcp_http_res_t;

static size_t mcp_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    mcp_http_res_t *r = userdata;
    if (r->len + total + 1 > r->cap) {
        size_t ncap = r->cap ? r->cap * 2 : 1024;
        while (ncap < r->len + total + 1) ncap *= 2;
        r->data = realloc(r->data, ncap);
        r->cap = ncap;
    }
    memcpy(r->data + r->len, ptr, total);
    r->len += total;
    r->data[r->len] = '\0';
    return total;
}

static size_t mcp_header_cb(char *buffer, size_t size, size_t nitems, void *userdata) {
    size_t total = size * nitems;
    mcp_http_res_t *r = userdata;
    /* header line like "Mcp-Session-Id: abc123\r\n" */
    char *line = xstrndup(buffer, total);
    char *colon = strchr(line, ':');
    if (colon) {
        *colon = '\0';
        char *key = str_trim(line);
        char *val = str_trim(colon + 1);
        /* strip trailing \r\n already trimmed */
        if (str_ieq(key, "mcp-session-id")) {
            free(r->header_session);
            r->header_session = xstrdup(val);
        } else if (str_ieq(key, "content-type")) {
            free(r->content_type);
            r->content_type = xstrdup(val);
        }
    }
    free(line);
    return total;
}

static void mcp_http_res_free(mcp_http_res_t *r) {
    if (!r) return;
    free(r->data);
    free(r->header_session);
    free(r->content_type);
    free(r);
}

/* Parse SSE body: extract last JSON with result or error */
static cJSON *mcp_parse_sse(const char *body, size_t len) {
    cJSON *last = NULL;
    char *copy = xstrndup(body, len);
    char *p = copy;
    char *data_acc = NULL;
    size_t data_len = 0, data_cap = 0;
    while (p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = '\0';
        char *trimmed = str_trim(p);
        if (trimmed[0] == '\0') {
            /* empty line = dispatch */
            if (data_acc && data_len) {
                data_acc[data_len] = '\0';
                cJSON *j = cJSON_Parse(data_acc);
                if (j) {
                    if (cJSON_GetObjectItem(j, "result") || cJSON_GetObjectItem(j, "error")) {
                        if (last) cJSON_Delete(last);
                        last = j;
                    } else {
                        cJSON_Delete(j);
                    }
                }
                data_len = 0;
            }
        } else if (str_has_prefix(trimmed, "data:")) {
            char *payload = str_trim(trimmed + 5);
            size_t pl = strlen(payload);
            if (data_len + pl + 2 > data_cap) {
                data_cap = data_cap ? data_cap * 2 : 256;
                while (data_cap < data_len + pl + 2) data_cap *= 2;
                data_acc = realloc(data_acc, data_cap);
            }
            if (data_len) data_acc[data_len++] = '\n';
            memcpy(data_acc + data_len, payload, pl);
            data_len += pl;
            data_acc[data_len] = '\0';
        }
        if (!nl) break;
        p = nl + 1;
    }
    /* trailing data without empty line */
    if (data_acc && data_len) {
        data_acc[data_len] = '\0';
        cJSON *j = cJSON_Parse(data_acc);
        if (j) {
            if (cJSON_GetObjectItem(j, "result") || cJSON_GetObjectItem(j, "error")) {
                if (last) cJSON_Delete(last);
                last = j;
            } else {
                cJSON_Delete(j);
            }
        }
    }
    free(data_acc);
    free(copy);
    return last;
}

static cJSON *mcp_parse_response(mcp_http_res_t *res) {
    if (!res || !res->data) return NULL;
    bool is_sse = false;
    if (res->content_type && strstr(res->content_type, "text/event-stream")) is_sse = true;
    /* fallback: body starts with "data:" or "event:" */
    if (!is_sse && res->data && str_has_prefix(res->data, "data:")) is_sse = true;
    if (is_sse) {
        cJSON *sse = mcp_parse_sse(res->data, res->len);
        if (sse) return sse;
        /* fall through to json if SSE empty */
    }
    cJSON *j = cJSON_Parse(res->data);
    return j;
}

/* Internal RPC: performs POST JSON-RPC, captures session id, returns parsed JSON.
 * If notification==true, expects 200/202 and returns NULL on success.
 * Caller must cJSON_Delete returned value. */
static cJSON *mcp_rpc(mcp_client_t *client, const char *method, cJSON *params,
                      bool notification, char *errbuf, size_t errsz) {
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "jsonrpc", "2.0");
    cJSON_AddStringToObject(payload, "method", method);
    if (params) cJSON_AddItemToObject(payload, "params", cJSON_Duplicate(params, 1));
    else cJSON_AddItemToObject(payload, "params", cJSON_CreateObject());
    if (!notification) {
        char idbuf[32];
        snprintf(idbuf, sizeof(idbuf), "%d", ++client->id_counter);
        cJSON_AddStringToObject(payload, "id", idbuf);
    }
    char *body = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    if (!body) {
        if (errbuf) snprintf(errbuf, errsz, "Failed to serialize RPC payload");
        return NULL;
    }

    CURL *h = curl_easy_init();
    if (!h) {
        free(body);
        if (errbuf) snprintf(errbuf, errsz, "curl_easy_init failed");
        return NULL;
    }
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json, text/event-stream");
    if (client->authHeader[0]) {
        char *ah = NULL;
        /* authHeader may already contain "Bearer " or "Authorization: ..." prefix.
         * In web code it's passed as full header value. Here we ensure correct header. */
        if (str_has_prefix(client->authHeader, "Authorization:") ||
            str_has_prefix(client->authHeader, "authorization:")) {
            ah = xstrdup(client->authHeader);
        } else if (str_has_prefix(client->authHeader, "Bearer ") ||
                   str_has_prefix(client->authHeader, "bearer ")) {
            ah = xasprintf("Authorization: %s", client->authHeader);
        } else {
            /* assume raw token or "Bearer xxx" without prefix check - use as Authorization */
            if (strchr(client->authHeader, ' ')) {
                ah = xasprintf("Authorization: %s", client->authHeader);
            } else {
                ah = xasprintf("Authorization: Bearer %s", client->authHeader);
            }
        }
        headers = curl_slist_append(headers, ah);
        free(ah);
    }
    if (client->sessionId[0]) {
        char *sh = xasprintf("Mcp-Session-Id: %s", client->sessionId);
        headers = curl_slist_append(headers, sh);
        free(sh);
    }
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(h, CURLOPT_URL, client->url);
    curl_easy_setopt(h, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_USERAGENT, "traliran-ai-hub-cli/" HUB_VERSION);

    mcp_http_res_t *res = calloc(1, sizeof(*res));
    res->data = malloc(1);
    res->data[0] = '\0';
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, mcp_write_cb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, res);
    curl_easy_setopt(h, CURLOPT_HEADERFUNCTION, mcp_header_cb);
    curl_easy_setopt(h, CURLOPT_HEADERDATA, res);

    CURLcode rc = curl_easy_perform(h);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    res->status = status;

    curl_slist_free_all(headers);
    curl_easy_cleanup(h);
    free(body);

    if (rc != CURLE_OK) {
        if (errbuf) snprintf(errbuf, errsz, "Network error: %s", curl_easy_strerror(rc));
        mcp_http_res_free(res);
        return NULL;
    }
    if (res->header_session && res->header_session[0]) {
        snprintf(client->sessionId, sizeof(client->sessionId), "%s", res->header_session);
    }
    if (notification) {
        if (status != 200 && status != 202) {
            if (errbuf) snprintf(errbuf, errsz, "Notification failed: HTTP %ld %s", status, res->data ? res->data : "");
            mcp_http_res_free(res);
            return NULL;
        }
        mcp_http_res_free(res);
        /* return dummy non-NULL to signal success for notification path */
        return (cJSON *)1;
    }
    if (status < 200 || status >= 300) {
        cJSON *j = res->data ? cJSON_Parse(res->data) : NULL;
        char *msg = NULL;
        if (j) {
            cJSON *err = cJSON_GetObjectItem(j, "error");
            if (err) {
                cJSON *m = cJSON_GetObjectItem(err, "message");
                if (m && cJSON_IsString(m) && m->valuestring) msg = xstrdup(m->valuestring);
                else {
                    char *js = cJSON_PrintUnformatted(err);
                    msg = js ? js : xstrdup("Unknown error");
                }
            }
            cJSON_Delete(j);
        }
        if (errbuf) {
            if (msg) snprintf(errbuf, errsz, "HTTP %ld: %s", status, msg);
            else snprintf(errbuf, errsz, "HTTP %ld: %s", status, res->data ? res->data : "Unknown error");
        }
        free(msg);
        mcp_http_res_free(res);
        return NULL;
    }
    cJSON *parsed = mcp_parse_response(res);
    if (!parsed) {
        if (errbuf) snprintf(errbuf, errsz, "Empty or invalid JSON-RPC response: %s", res->data ? res->data : "");
        mcp_http_res_free(res);
        return NULL;
    }
    /* Check JSON-RPC error field */
    cJSON *err = cJSON_GetObjectItem(parsed, "error");
    if (err) {
        cJSON *m = cJSON_GetObjectItem(err, "message");
        const char *em = m && cJSON_IsString(m) ? m->valuestring : NULL;
        if (errbuf) {
            if (em) snprintf(errbuf, errsz, "RPC error: %s", em);
            else {
                char *js = cJSON_PrintUnformatted(err);
                snprintf(errbuf, errsz, "RPC error: %s", js ? js : "unknown");
                free(js);
            }
        }
        cJSON_Delete(parsed);
        mcp_http_res_free(res);
        return NULL;
    }
    mcp_http_res_free(res);
    return parsed;
}

/* =================== client operations =================== */

static int mcp_client_connect(mcp_client_t *client, char *errbuf, size_t errsz) {
    client->id_counter = 0;
    client->sessionId[0] = '\0';
    snprintf(client->protocolVersion, sizeof(client->protocolVersion), "2024-11-05");

    /* 1. initialize */
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "protocolVersion", "2024-11-05");
    cJSON_AddItemToObject(params, "capabilities", cJSON_CreateObject());
    cJSON *ci = cJSON_CreateObject();
    cJSON_AddStringToObject(ci, "name", "TraliranAIHub");
    cJSON_AddStringToObject(ci, "version", HUB_VERSION);
    cJSON_AddItemToObject(params, "clientInfo", ci);
    cJSON *resp = mcp_rpc(client, "initialize", params, false, errbuf, errsz);
    cJSON_Delete(params);
    if (!resp) return -1;
    cJSON *result = cJSON_GetObjectItem(resp, "result");
    if (result) {
        cJSON *pv = cJSON_GetObjectItem(result, "protocolVersion");
        if (pv && cJSON_IsString(pv) && pv->valuestring) {
            snprintf(client->protocolVersion, sizeof(client->protocolVersion), "%s", pv->valuestring);
        }
    }
    cJSON_Delete(resp);

    /* 2. notifications/initialized */
    cJSON *nparams = cJSON_CreateObject();
    cJSON *nresp = mcp_rpc(client, "notifications/initialized", nparams, true, errbuf, errsz);
    cJSON_Delete(nparams);
    if (!nresp) return -1;
    /* nresp is dummy 1 for success */
    /* 3. tools/list */
    cJSON *lparams = cJSON_CreateObject();
    cJSON *lresp = mcp_rpc(client, "tools/list", lparams, false, errbuf, errsz);
    cJSON_Delete(lparams);
    if (!lresp) return -1;
    cJSON *lresult = cJSON_GetObjectItem(lresp, "result");
    mcp_free_client_tools(client);
    if (lresult) {
        cJSON *tools = cJSON_GetObjectItem(lresult, "tools");
        if (tools && cJSON_IsArray(tools)) {
            int n = cJSON_GetArraySize(tools);
            for (int i = 0; i < n; i++) {
                cJSON *t = cJSON_GetArrayItem(tools, i);
                cJSON *jname = cJSON_GetObjectItem(t, "name");
                if (!jname || !cJSON_IsString(jname) || !jname->valuestring) continue;
                if (client->ntools >= client->cap_tools) {
                    client->cap_tools = client->cap_tools ? client->cap_tools * 2 : 8;
                    client->tools = realloc(client->tools, sizeof(mcp_tool_t) * (size_t)client->cap_tools);
                }
                mcp_tool_t *tool = &client->tools[client->ntools++];
                memset(tool, 0, sizeof(*tool));
                snprintf(tool->name, sizeof(tool->name), "%s", jname->valuestring);
                cJSON *jdesc = cJSON_GetObjectItem(t, "description");
                if (jdesc && cJSON_IsString(jdesc) && jdesc->valuestring)
                    snprintf(tool->description, sizeof(tool->description), "%s", jdesc->valuestring);
                cJSON *jschema = cJSON_GetObjectItem(t, "inputSchema");
                if (jschema) tool->inputSchema = cJSON_Duplicate(jschema, 1);
            }
        }
    }
    cJSON_Delete(lresp);
    client->connected = true;
    return 0;
}

/* =================== public operations =================== */

int mcp_add(const char *name, const char *url, const char *authHeader,
            char *out_id, size_t out_sz) {
    if (!name || !name[0] || !url || !url[0]) return -1;
    pthread_mutex_lock(&g_mcp_mtx);
    if (g_nservers >= MCP_MAX_SERVERS) {
        pthread_mutex_unlock(&g_mcp_mtx);
        return -1;
    }
    char idbuf[MCP_MAX_ID];
    snprintf(idbuf, sizeof(idbuf), "mcp_%lld_%03d", (long long)now_ms(), rand() % 1000);
    /* ensure uniqueness */
    for (int i = 0; i < g_nservers; i++) if (!strcmp(g_servers[i].id, idbuf)) {
        snprintf(idbuf, sizeof(idbuf), "mcp_%lld_%03d_%d", (long long)now_ms(), rand() % 1000, i);
        break;
    }
    mcp_server_t *s = &g_servers[g_nservers++];
    snprintf(s->id, sizeof(s->id), "%s", idbuf);
    snprintf(s->name, sizeof(s->name), "%s", name);
    /* trim whitespace */
    str_trim(s->name);
    snprintf(s->url, sizeof(s->url), "%s", url);
    str_trim(s->url);
    snprintf(s->authHeader, sizeof(s->authHeader), "%s", authHeader ? authHeader : "");
    str_trim(s->authHeader);
    pthread_mutex_unlock(&g_mcp_mtx);
    mcp_save();
    if (out_id && out_sz) snprintf(out_id, out_sz, "%s", idbuf);
    return 0;
}

int mcp_remove(const char *id) {
    if (!id) return -1;
    pthread_mutex_lock(&g_mcp_mtx);
    int idx = find_server_idx(id);
    if (idx < 0) { pthread_mutex_unlock(&g_mcp_mtx); return -1; }
    for (int i = idx; i < g_nservers - 1; i++) g_servers[i] = g_servers[i + 1];
    g_nservers--;
    int ci = find_client_idx(id);
    if (ci >= 0) {
        mcp_free_client_tools(&g_clients[ci]);
        for (int i = ci; i < g_nclients - 1; i++) g_clients[i] = g_clients[i + 1];
        g_nclients--;
        mcp_rebuild_registry();
    }
    pthread_mutex_unlock(&g_mcp_mtx);
    mcp_save();
    return 0;
}

void mcp_disconnect_one(const char *id) {
    if (!id) return;
    pthread_mutex_lock(&g_mcp_mtx);
    int ci = find_client_idx(id);
    if (ci >= 0) {
        mcp_free_client_tools(&g_clients[ci]);
        for (int i = ci; i < g_nclients - 1; i++) g_clients[i] = g_clients[i + 1];
        g_nclients--;
        mcp_rebuild_registry();
    }
    pthread_mutex_unlock(&g_mcp_mtx);
}

void mcp_disconnect_all(void) {
    pthread_mutex_lock(&g_mcp_mtx);
    for (int i = 0; i < g_nclients; i++) mcp_free_client_tools(&g_clients[i]);
    g_nclients = 0;
    g_nregistry = 0;
    pthread_mutex_unlock(&g_mcp_mtx);
}

void mcp_cleanup(void) {
    mcp_disconnect_all();
}

int mcp_connect_one(const char *id, char *errbuf, size_t errsz) {
    if (!id) return -1;
    /* copy server config under lock */
    mcp_server_t cfg;
    pthread_mutex_lock(&g_mcp_mtx);
    int si = find_server_idx(id);
    if (si < 0) { pthread_mutex_unlock(&g_mcp_mtx); if (errbuf) snprintf(errbuf, errsz, "Server not found"); return -1; }
    cfg = g_servers[si];
    /* remove existing client if any */
    int ci = find_client_idx(id);
    if (ci >= 0) {
        mcp_free_client_tools(&g_clients[ci]);
        for (int i = ci; i < g_nclients - 1; i++) g_clients[i] = g_clients[i + 1];
        g_nclients--;
    }
    pthread_mutex_unlock(&g_mcp_mtx);

    mcp_client_t client;
    memset(&client, 0, sizeof(client));
    snprintf(client.id, sizeof(client.id), "%s", cfg.id);
    snprintf(client.name, sizeof(client.name), "%s", cfg.name);
    snprintf(client.url, sizeof(client.url), "%s", cfg.url);
    snprintf(client.authHeader, sizeof(client.authHeader), "%s", cfg.authHeader);
    snprintf(client.protocolVersion, sizeof(client.protocolVersion), "2024-11-05");
    client.connected = false;
    client.id_counter = 0;

    char local_err[512] = {0};
    int rc = mcp_client_connect(&client, local_err, sizeof(local_err));
    if (rc != 0) {
        mcp_free_client_tools(&client);
        if (errbuf) snprintf(errbuf, errsz, "%s", local_err[0] ? local_err : "Connection failed");
        return -1;
    }
    pthread_mutex_lock(&g_mcp_mtx);
    if (g_nclients < MCP_MAX_SERVERS) {
        g_clients[g_nclients++] = client;
        mcp_rebuild_registry();
    } else {
        mcp_free_client_tools(&client);
        pthread_mutex_unlock(&g_mcp_mtx);
        if (errbuf) snprintf(errbuf, errsz, "Too many clients");
        return -1;
    }
    pthread_mutex_unlock(&g_mcp_mtx);
    return 0;
}

int mcp_connect_all(void) {
    /* copy ids to avoid holding lock during network */
    char ids[MCP_MAX_SERVERS][MCP_MAX_ID];
    int n = 0;
    pthread_mutex_lock(&g_mcp_mtx);
    for (int i = 0; i < g_nservers; i++) snprintf(ids[n++], sizeof(ids[0]), "%s", g_servers[i].id);
    pthread_mutex_unlock(&g_mcp_mtx);
    int ok = 0;
    for (int i = 0; i < n; i++) {
        char err[512];
        int rc = mcp_connect_one(ids[i], err, sizeof(err));
        if (rc == 0) ok++;
        /* best-effort, log to stderr for debugging */
        if (rc != 0) fprintf(stderr, "[mcp] connect %s failed: %s\n", ids[i], err);
    }
    pthread_mutex_lock(&g_mcp_mtx);
    mcp_rebuild_registry();
    pthread_mutex_unlock(&g_mcp_mtx);
    return ok;
}

int mcp_connected_count(void) {
    pthread_mutex_lock(&g_mcp_mtx);
    int cnt = 0;
    for (int i = 0; i < g_nclients; i++) if (g_clients[i].connected && g_clients[i].ntools > 0) cnt++;
    pthread_mutex_unlock(&g_mcp_mtx);
    return cnt;
}

int mcp_tool_count(void) {
    pthread_mutex_lock(&g_mcp_mtx);
    int sum = 0;
    for (int i = 0; i < g_nclients; i++) if (g_clients[i].connected) sum += g_clients[i].ntools;
    pthread_mutex_unlock(&g_mcp_mtx);
    return sum;
}

bool mcp_is_ready(void) {
    return mcp_connected_count() > 0 && mcp_tool_count() > 0;
}

cJSON *mcp_build_toolset(void) {
    pthread_mutex_lock(&g_mcp_mtx);
    /* ensure registry is fresh */
    mcp_rebuild_registry();
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < g_nregistry; i++) {
        mcp_registry_entry_t *e = &g_registry[i];
        int ci = find_client_idx(e->clientId);
        if (ci < 0) continue;
        mcp_client_t *cl = &g_clients[ci];
        mcp_tool_t *tool = NULL;
        for (int t = 0; t < cl->ntools; t++) if (!strcmp(cl->tools[t].name, e->toolName)) { tool = &cl->tools[t]; break; }
        if (!tool) continue;
        cJSON *tj = cJSON_CreateObject();
        cJSON_AddStringToObject(tj, "type", "function");
        cJSON *fn = cJSON_CreateObject();
        cJSON_AddStringToObject(fn, "name", e->fnName);
        char desc[1024];
        snprintf(desc, sizeof(desc), "[MCP:%s] %s", cl->name, tool->description[0] ? tool->description : tool->name);
        cJSON_AddStringToObject(fn, "description", desc);
        if (tool->inputSchema) cJSON_AddItemToObject(fn, "parameters", cJSON_Duplicate(tool->inputSchema, 1));
        else {
            cJSON *ps = cJSON_CreateObject();
            cJSON_AddStringToObject(ps, "type", "object");
            cJSON_AddItemToObject(ps, "properties", cJSON_CreateObject());
            cJSON_AddItemToObject(fn, "parameters", ps);
        }
        cJSON_AddItemToObject(tj, "function", fn);
        cJSON_AddItemToArray(arr, tj);
    }
    pthread_mutex_unlock(&g_mcp_mtx);
    return arr;
}

char *mcp_build_toolset_json(void) {
    cJSON *arr = mcp_build_toolset();
    char *js = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!js) return xstrdup("[]");
    return js;
}

char *mcp_build_system_note(void) {
    pthread_mutex_lock(&g_mcp_mtx);
    bool has = false;
    for (int i = 0; i < g_nclients; i++) if (g_clients[i].connected && g_clients[i].ntools) { has = true; break; }
    if (!has) { pthread_mutex_unlock(&g_mcp_mtx); return xstrdup(""); }
    sbuf_t b;
    sbuf_init(&b);
    /* Pure text-JSON protocol: NO native function tools are sent via the API.
     * The model must emit a single JSON object per turn which is parsed locally. */
    sbuf_append(&b,
        "[Available MCP tools]\n"
        "You have access to external tools via MCP (Model Context Protocol).\n"
        "There are NO native function tools in the API - the client parses a JSON tool call\n"
        "directly from your reply text, so you must follow the protocol below exactly.\n\n"
        "RESPONSE FORMAT - use ONE of two options per reply:\n"
        "1) TEXT JSON (tool call): output ONLY a single JSON object, nothing before or after it\n"
        "   (no markdown fences, no prose): {\"tool\": \"<tool_name>\", \"arguments\": {<args>}}\n"
        "2) FINAL ANSWER: when no tool is needed or the task is complete, reply with normal text.\n"
        "- Never mix a tool call and a summary in one reply.\n"
        "- Never fabricate tool results; wait for the tool result message.\n"
        "- Work step by step - exactly one tool call per reply. The client executes the tool\n"
        "  and returns the result as the next message. Then continue or summarize.\n"
        "- Communicate with the user in the same language they used.\n\n"
        "AVAILABLE TOOLS:\n");
    /* Ensure registry is fresh so fnName is what the model must emit */
    mcp_rebuild_registry();
    for (int i = 0; i < g_nregistry; i++) {
        mcp_registry_entry_t *e = &g_registry[i];
        int ci = find_client_idx(e->clientId);
        if (ci < 0) continue;
        mcp_client_t *cl = &g_clients[ci];
        mcp_tool_t *tool = NULL;
        for (int t = 0; t < cl->ntools; t++) if (!strcmp(cl->tools[t].name, e->toolName)) { tool = &cl->tools[t]; break; }
        if (!tool) continue;
        sbuf_appendf(&b, "%d. {\"tool\": \"%s\", \"arguments\": ", i + 1, e->fnName);
        if (tool->inputSchema) {
            char *schema = cJSON_PrintUnformatted(tool->inputSchema);
            sbuf_append(&b, schema ? schema : "{}");
            free(schema);
        } else {
            sbuf_append(&b, "{}");
        }
        sbuf_appendf(&b, "} -> %s [%s: %s]\n",
            tool->description[0] ? tool->description : tool->name,
            cl->name, tool->name);
    }
    pthread_mutex_unlock(&g_mcp_mtx);
    return sbuf_detach(&b);
}

int mcp_call_tool(const char *fnName, const char *args_json, char **out_result) {
    if (!fnName || !out_result) return -1;
    char clientId[MCP_MAX_ID] = {0};
    char toolName[MCP_MAX_TOOL_NAME] = {0};
    pthread_mutex_lock(&g_mcp_mtx);
    bool found = false;
    for (int i = 0; i < g_nregistry; i++) if (!strcmp(g_registry[i].fnName, fnName)) {
        snprintf(clientId, sizeof(clientId), "%s", g_registry[i].clientId);
        snprintf(toolName, sizeof(toolName), "%s", g_registry[i].toolName);
        found = true; break;
    }
    pthread_mutex_unlock(&g_mcp_mtx);
    if (!found) {
        *out_result = xasprintf("Unknown MCP tool: %s", fnName);
        return -1;
    }
    /* locate client */
    pthread_mutex_lock(&g_mcp_mtx);
    int ci = find_client_idx(clientId);
    if (ci < 0) { pthread_mutex_unlock(&g_mcp_mtx); *out_result = xasprintf("MCP server \"%s\" is not connected", clientId); return -2; }
    mcp_client_t *cl = &g_clients[ci];
    if (!cl->connected) { pthread_mutex_unlock(&g_mcp_mtx); *out_result = xasprintf("MCP server \"%s\" is not connected", cl->name); return -2; }
    /* need to copy client pointer? we will use it outside lock, but ensure it stays.
     * We will perform RPC without holding lock (to allow concurrency).
     * Copy relevant fields for RPC. */
    mcp_client_t tmp = *cl;
    /* shallow copy tools pointer - but we only need id/name/url/auth/session/counter.
     * Use the live client for RPC (need to update sessionId/id_counter).
     * Instead operate on the actual client under lock per RPC call.
     * Simpler: do RPC while still holding reference but unlock during network?
     * We'll do RPC on the real client with no lock - not ideal but small cli is single-threaded for chat.
     */
    pthread_mutex_unlock(&g_mcp_mtx);

    cJSON *args = NULL;
    if (args_json && args_json[0]) {
        args = cJSON_Parse(args_json);
        if (!args) {
            /* try to wrap as string */
            args = cJSON_CreateObject();
            cJSON_AddStringToObject(args, "raw", args_json);
        }
    } else {
        args = cJSON_CreateObject();
    }
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", toolName);
    cJSON_AddItemToObject(params, "arguments", args);

    char err[512] = {0};
    /* we need to find the live client again for mutation */
    pthread_mutex_lock(&g_mcp_mtx);
    ci = find_client_idx(clientId);
    if (ci < 0) { pthread_mutex_unlock(&g_mcp_mtx); cJSON_Delete(params); *out_result = xstrdup("MCP server disconnected"); return -2; }
    cl = &g_clients[ci];
    pthread_mutex_unlock(&g_mcp_mtx);

    cJSON *resp = mcp_rpc(cl, "tools/call", params, false, err, sizeof(err));
    cJSON_Delete(params);
    if (!resp) {
        *out_result = xasprintf("MCP tool error: %s", err[0] ? err : "RPC failed");
        return -3;
    }
    cJSON *result = cJSON_GetObjectItem(resp, "result");
    char *formatted = NULL;
    if (result) formatted = mcp_format_result(result);
    else {
        char *js = cJSON_PrintUnformatted(resp);
        formatted = js ? js : xstrdup("(no result)");
    }
    cJSON_Delete(resp);
    *out_result = formatted;
    /* copy back mutation? sessionId and id_counter already mutated in cl */
    (void)tmp;
    return 0;
}

/* =================== introspection =================== */

int mcp_server_count(void) {
    pthread_mutex_lock(&g_mcp_mtx);
    int n = g_nservers;
    pthread_mutex_unlock(&g_mcp_mtx);
    return n;
}

const mcp_server_t *mcp_get_server(int idx) {
    pthread_mutex_lock(&g_mcp_mtx);
    const mcp_server_t *s = (idx >= 0 && idx < g_nservers) ? &g_servers[idx] : NULL;
    pthread_mutex_unlock(&g_mcp_mtx);
    return s;
}

const mcp_client_t *mcp_get_client(const char *id) {
    pthread_mutex_lock(&g_mcp_mtx);
    int ci = find_client_idx(id);
    const mcp_client_t *c = ci >= 0 ? &g_clients[ci] : NULL;
    pthread_mutex_unlock(&g_mcp_mtx);
    return c;
}

int mcp_registry_count(void) {
    pthread_mutex_lock(&g_mcp_mtx);
    int n = g_nregistry;
    pthread_mutex_unlock(&g_mcp_mtx);
    return n;
}

const mcp_registry_entry_t *mcp_get_registry(int idx) {
    pthread_mutex_lock(&g_mcp_mtx);
    const mcp_registry_entry_t *e = (idx >= 0 && idx < g_nregistry) ? &g_registry[idx] : NULL;
    pthread_mutex_unlock(&g_mcp_mtx);
    return e;
}
