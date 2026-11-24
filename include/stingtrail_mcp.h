#ifndef STINGTRAIL_MCP_H
#define STINGTRAIL_MCP_H

#include "stingtrail.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STINGTRAIL_MCP_PROTOCOL_VERSION "2024-11-05"

/* Format ASCII mission board into string buffer */
void sting_mcp_format_board(const sting_state_t *st, sting_buf_t *b);

/* Dispatch a single JSON-RPC request line and write response to out_buf.
 * Returns:
 *   > 0: Length of response written to out_buf (response should be sent)
 *     0: Success, but no response needed (e.g. notification)
 *   < 0: Internal failure
 */
int sting_mcp_dispatch_message(sting_state_t *st, const char *msg, char *out_buf, size_t out_cap);

/* Run MCP server on standard input/output until EOF */
int sting_mcp_run_stdio(sting_state_t *st);

#ifdef __cplusplus
}
#endif

#endif /* STINGTRAIL_MCP_H */
