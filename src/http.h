/*
 * http.h - libcurl HTTP helpers (GET / POST JSON / SSE streaming).
 */
#ifndef HUB_HTTP_H
#define HUB_HTTP_H

#include <stddef.h>
#include <stdbool.h>

typedef struct {
    long   status;
    char  *body;
    bool   ok;      /* status in 200..299 */
} http_res_t;

/* stream callback. return non-zero to abort the transfer. */
typedef int (*http_stream_cb)(const char *data, size_t len, void *ud);

void         http_global_init(void);
void         http_global_cleanup(void);

http_res_t  *http_get(const char *url, const char *bearer);
http_res_t  *http_post_json(const char *url, const char *bearer,
                            const char *x_api_key, const char *json_body);
int          http_post_json_stream(const char *url, const char *bearer,
                                   const char *x_api_key, const char *json_body,
                                   http_stream_cb cb, void *ud);
void         http_res_free(http_res_t *r);

#endif /* HUB_HTTP_H */