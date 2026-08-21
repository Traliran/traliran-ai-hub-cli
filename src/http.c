/*
 * http.c - libcurl HTTP helpers (GET / POST JSON / SSE streaming).
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#include "util.h"
#include "http.h"

void http_global_init(void)    { curl_global_init(CURL_GLOBAL_DEFAULT); }
void http_global_cleanup(void) { curl_global_cleanup(); }

void http_res_free(http_res_t *r) {
    if (!r) return;
    free(r->body);
    free(r);
}

struct mem {
    char  *data;
    size_t len;
};

static size_t mem_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    struct mem *m = userdata;
    char *nd = realloc(m->data, m->len + total + 1);
    if (!nd) return 0;
    m->data = nd;
    memcpy(m->data + m->len, ptr, total);
    m->len += total;
    m->data[m->len] = '\0';
    return total;
}

struct stream_ctx {
    http_stream_cb cb;
    void          *ud;
    int            aborted;
};

static size_t stream_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    struct stream_ctx *sc = userdata;
    size_t total = size * nmemb;
    if (sc->aborted) return 0;
    if (sc->cb && sc->cb(ptr, total, sc->ud) != 0) {
        sc->aborted = 1;
        return 0;   /* abort curl transfer */
    }
    return total;
}

static void setup_common(CURL *h, const char *url, const char *bearer,
                         const char *x_api_key) {
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    if (bearer && bearer[0]) {
        char *auth = xasprintf("Authorization: Bearer %s", bearer);
        headers = curl_slist_append(headers, auth);
        free(auth);
    }
    if (x_api_key && x_api_key[0]) {
        char *auth = xasprintf("x-api-key: %s", x_api_key);
        headers = curl_slist_append(headers, auth);
        headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
        free(auth);
    }
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(h, CURLOPT_USERAGENT, "traliran-ai-hub-cli/1.0");
}

static http_res_t *run_post(const char *url, const char *bearer,
                            const char *x_api_key, const char *json_body) {
    CURL *h = curl_easy_init();
    if (!h) return NULL;
    setup_common(h, url, bearer, x_api_key);
    curl_easy_setopt(h, CURLOPT_POSTFIELDS, json_body);
    curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)strlen(json_body));

    struct mem m = {0};
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, mem_cb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &m);

    CURLcode rc = curl_easy_perform(h);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(h);

    http_res_t *r = calloc(1, sizeof(*r));
    if (rc != CURLE_OK) {
        r->status = status;
        r->ok = false;
        r->body = xasprintf("{\"error\":{\"message\":\"%s\"}}", curl_easy_strerror(rc));
        free(m.data);
        return r;
    }
    r->status = status;
    r->ok = status >= 200 && status < 300;
    r->body = m.data ? m.data : xstrdup("");
    return r;
}

http_res_t *http_get(const char *url, const char *bearer) {
    CURL *h = curl_easy_init();
    if (!h) return NULL;
    setup_common(h, url, bearer, NULL);
    curl_easy_setopt(h, CURLOPT_HTTPGET, 1L);

    struct mem m = {0};
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, mem_cb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &m);

    CURLcode rc = curl_easy_perform(h);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(h);

    http_res_t *r = calloc(1, sizeof(*r));
    if (rc != CURLE_OK) {
        r->status = status;
        r->ok = false;
        r->body = xasprintf("{\"error\":{\"message\":\"%s\"}}", curl_easy_strerror(rc));
        free(m.data);
        return r;
    }
    r->status = status;
    r->ok = status >= 200 && status < 300;
    r->body = m.data ? m.data : xstrdup("");
    return r;
}

http_res_t *http_post_json(const char *url, const char *bearer,
                           const char *x_api_key, const char *json_body) {
    return run_post(url, bearer, x_api_key, json_body);
}

int http_post_json_stream(const char *url, const char *bearer,
                          const char *x_api_key, const char *json_body,
                          http_stream_cb cb, void *ud) {
    CURL *h = curl_easy_init();
    if (!h) return -1;
    setup_common(h, url, bearer, x_api_key);
    curl_easy_setopt(h, CURLOPT_POSTFIELDS, json_body);
    curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)strlen(json_body));
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);

    struct stream_ctx sc = { cb, ud, 0 };
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, stream_cb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &sc);

    CURLcode rc = curl_easy_perform(h);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(h);

    if (rc == CURLE_ABORTED_BY_CALLBACK || sc.aborted) return 1; /* user abort */
    if (rc != CURLE_OK) return -2;
    if (status < 200 || status >= 300) return (int)status;
    return 0;
}