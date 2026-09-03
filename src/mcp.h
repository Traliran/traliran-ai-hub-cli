/*
 * mcp.h - Model Context Protocol client (Streamable HTTP + JSON-RPC 2.0).
 * Mirrors web implementation from mcp.js and app.js agent loop.
 */
#ifndef HUB_MCP_H
#define HUB_MCP_H

#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"

#define MCP_STORAGE_KEY "gem_mcp_servers"
#define MCP_MAX_SERVERS 32
#define MCP_MAX_TOOLS_PER_SERVER 128
#define MCP_MAX_REGISTRY 512
#define MCP_MAX_ID 64
#define MCP_MAX_NAME 128
#define MCP_MAX_URL 512
#define MCP_MAX_AUTH 512
#define MCP_MAX_SESSION 128
#define MCP_MAX_PROTO 32
#define MCP_MAX_FN 65
#define MCP_MAX_TOOL_NAME 128

/* Persistent server entry (stored in storage.json) */
typedef struct {
    char id[MCP_MAX_ID];
    char name[MCP_MAX_NAME];
    char url[MCP_MAX_URL];
    char authHeader[MCP_MAX_AUTH];
} mcp_server_t;

/* Tool description as returned by MCP server */
typedef struct {
    char name[MCP_MAX_TOOL_NAME];
    char description[512];
    cJSON *inputSchema; /* owned, may be NULL */
} mcp_tool_t;

/* Live MCP client */
typedef struct {
    char id[MCP_MAX_ID];
    char name[MCP_MAX_NAME];
    char url[MCP_MAX_URL];
    char authHeader[MCP_MAX_AUTH];
    char sessionId[MCP_MAX_SESSION];
    char protocolVersion[MCP_MAX_PROTO];
    mcp_tool_t *tools;
    int ntools;
    int cap_tools;
    bool connected;
    int id_counter;
} mcp_client_t;

/* Registry entry mapping OpenAI function name -> (clientId, toolName) */
typedef struct {
    char fnName[MCP_MAX_FN];
    char clientId[MCP_MAX_ID];
    char toolName[MCP_MAX_TOOL_NAME];
} mcp_registry_entry_t;

/* Public API */
void mcp_load(void);
void mcp_save(void);

/* Add a new server config. Returns 0 on success, out_id filled if not NULL. */
int mcp_add(const char *name, const char *url, const char *authHeader,
            char *out_id, size_t out_sz);

/* Remove server and disconnect if needed. Returns 0 on success. */
int mcp_remove(const char *id);

/* Connect a single server by id. Returns 0 on success, negative on error.
 * On failure stores human-readable message in errbuf if provided. */
int mcp_connect_one(const char *id, char *errbuf, size_t errsz);

/* Connect all configured servers (best-effort, continues on error). */
int mcp_connect_all(void);

/* Disconnect a single client without removing the config. */
void mcp_disconnect_one(const char *id);

/* Disconnect all clients. */
void mcp_disconnect_all(void);

/* Free all heap resources. */
void mcp_cleanup(void);

int mcp_connected_count(void);
int mcp_tool_count(void);

/* Build OpenAI-compatible tools array (caller must cJSON_Delete). */
cJSON *mcp_build_toolset(void);

/* Build tools array as JSON string (malloc'd, caller free). */
char *mcp_build_toolset_json(void);

/* Build system note listing available tools (malloc'd, caller free).
 * Returns empty string if no tools connected. */
char *mcp_build_system_note(void);

/* Call a tool by its OpenAI function name.
 * args_json is a JSON object string (may be NULL/empty -> "{}").
 * On success *out_result is malloc'd string (formatted via formatMcpResult).
 * Returns 0 on success, -1 if tool not found, -2 if server not connected,
 * -3 on RPC/transport error. */
int mcp_call_tool(const char *fnName, const char *args_json, char **out_result);

/* Rebuild registry after clients change (internal but exposed for testing). */
void mcp_rebuild_registry(void);

/* Helpers */
char *mcp_sanitize_name(const char *name); /* malloc'd, caller free */
char *mcp_format_result(cJSON *result);    /* malloc'd, caller free */

/* Introspection for UI */
int mcp_server_count(void);
const mcp_server_t *mcp_get_server(int idx);
const mcp_client_t *mcp_get_client(const char *id); /* NULL if not connected */
int mcp_registry_count(void);
const mcp_registry_entry_t *mcp_get_registry(int idx);

/* Header helpers for status badge */
bool mcp_is_ready(void); /* connectedCount>0 && toolCount>0 */

#endif /* HUB_MCP_H */
