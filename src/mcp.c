#define _POSIX_C_SOURCE 200809L
#include "stingtrail_mcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void skip_ws(const char **p) {
    while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n') {
        (*p)++;
    }
}

static bool skip_json_value(const char **p) {
    skip_ws(p);
    if (!**p) return false;
    if (**p == '"') {
        (*p)++;
        while (**p && **p != '"') {
            if (**p == '\\' && *(*p + 1)) (*p) += 2;
            else (*p)++;
        }
        if (**p == '"') (*p)++;
        return true;
    }
    if (**p == '{' || **p == '[') {
        char open = **p;
        char close = (open == '{') ? '}' : ']';
        int depth = 1;
        (*p)++;
        while (**p && depth > 0) {
            if (**p == '"') {
                (*p)++;
                while (**p && **p != '"') {
                    if (**p == '\\' && *(*p + 1)) (*p) += 2;
                    else (*p)++;
                }
                if (**p == '"') (*p)++;
            } else {
                if (**p == open) depth++;
                else if (**p == close) depth--;
                (*p)++;
            }
        }
        return depth == 0;
    }
    /* Number, boolean, null */
    while (**p && **p != ',' && **p != '}' && **p != ']' &&
           **p != ' ' && **p != '\t' && **p != '\r' && **p != '\n') {
        (*p)++;
    }
    return true;
}

static const char *find_field_in_obj(const char *json_obj, const char *key) {
    if (!json_obj) return NULL;
    const char *p = json_obj;
    skip_ws(&p);
    if (*p != '{') return NULL;
    p++;
    while (*p && *p != '}') {
        skip_ws(&p);
        if (*p == '}') break;
        if (*p != '"') return NULL;
        char kbuf[64];
        if (!sting_json_parse_string(&p, kbuf, sizeof(kbuf))) return NULL;
        skip_ws(&p);
        if (*p != ':') return NULL;
        p++;
        skip_ws(&p);
        if (strcmp(kbuf, key) == 0) {
            return p; /* pointer to start of value */
        }
        if (!skip_json_value(&p)) return NULL;
        skip_ws(&p);
        if (*p == ',') p++;
    }
    return NULL;
}

static bool extract_str_field(const char *json_obj, const char *key, char *out, size_t out_cap) {
    if (!out || out_cap == 0) return false;
    out[0] = '\0';
    const char *p = find_field_in_obj(json_obj, key);
    if (!p) return false;
    skip_ws(&p);
    if (*p != '"') return false;
    return sting_json_parse_string(&p, out, out_cap);
}

static long long extract_int_field(const char *json_obj, const char *key, long long def_val) {
    const char *p = find_field_in_obj(json_obj, key);
    if (!p) return def_val;
    long long v = def_val;
    if (sting_json_parse_int(&p, &v)) {
        return v;
    }
    return def_val;
}

static bool extract_bool_field(const char *json_obj, const char *key, bool def_val) {
    const char *p = find_field_in_obj(json_obj, key);
    if (!p) return def_val;
    skip_ws(&p);
    if (strncmp(p, "true", 4) == 0) return true;
    if (strncmp(p, "false", 5) == 0) return false;
    return def_val;
}

static bool extract_id(const char *json_req, char *out_id, size_t out_cap) {
    if (!out_id || out_cap == 0) return false;
    out_id[0] = '\0';
    const char *p = find_field_in_obj(json_req, "id");
    if (!p) return false;
    skip_ws(&p);
    if (*p == '"') {
        const char *start = p;
        if (!skip_json_value(&p)) return false;
        size_t len = (size_t)(p - start);
        if (len >= out_cap) len = out_cap - 1;
        memcpy(out_id, start, len);
        out_id[len] = '\0';
        return true;
    } else {
        size_t len = 0;
        while (*p && *p != ',' && *p != '}' && *p != ']' &&
               *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
            if (len + 1 < out_cap) out_id[len++] = *p;
            p++;
        }
        out_id[len] = '\0';
        return len > 0;
    }
}

void sting_mcp_format_board(const sting_state_t *st, sting_buf_t *b) {
    if (!st || !b) return;
    sting_buf_append(b, "=== STINGTRAIL // CRITICAL THINKING BOARD ===\nNodes (");
    sting_buf_append_int(b, (long long)st->node_count);
    sting_buf_append(b, "):\n");
    if (st->node_count == 0) {
        sting_buf_append(b, "  (none)\n");
    } else {
        for (size_t i = 0; i < st->node_count; i++) {
            size_t n_total = 0, n_inc = 0;
            sting_state_plan_counts(st, st->nodes[i].id, &n_total, &n_inc);
            sting_buf_append(b, "  [#");
            sting_buf_append_int(b, (long long)st->nodes[i].id);
            sting_buf_append(b, "] (");
            sting_buf_append(b, st->nodes[i].kind);
            sting_buf_append(b, ") ");
            sting_buf_append(b, st->nodes[i].name);
            if (st->nodes[i].detail[0]) {
                sting_buf_append(b, " \xe2\x80\x94 "); /* utf-8 em-dash */
                sting_buf_append(b, st->nodes[i].detail);
            }
            sting_buf_append(b, " [incomplete plans: ");
            sting_buf_append_int(b, (long long)n_inc);
            sting_buf_append_ch(b, '/');
            sting_buf_append_int(b, (long long)n_total);
            sting_buf_append(b, "] (created: ");
            sting_buf_append(b, st->nodes[i].created[0] ? st->nodes[i].created : "-");
            sting_buf_append(b, ")\n");
        }
    }

    sting_buf_append(b, "\nReasoning Log (");
    sting_buf_append_int(b, (long long)st->entry_count);
    sting_buf_append(b, "):\n");
    if (st->entry_count == 0) {
        sting_buf_append(b, "  (none)\n");
    } else {
        for (size_t i = 0; i < st->entry_count; i++) {
            sting_buf_append(b, "  [#");
            sting_buf_append_int(b, (long long)st->entries[i].id);
            sting_buf_append(b, " <- #");
            sting_buf_append_int(b, (long long)st->entries[i].parent_id);
            sting_buf_append(b, "] [");
            sting_buf_append(b, st->entries[i].kind);
            sting_buf_append_ch(b, '/');
            sting_buf_append(b, st->entries[i].severity);
            sting_buf_append(b, "] ");
            sting_buf_append(b, st->entries[i].summary);
            if (st->entries[i].detail[0]) {
                sting_buf_append(b, " | ");
                sting_buf_append(b, st->entries[i].detail);
            }
            if (strcmp(st->entries[i].kind, "hypothesis") == 0) {
                size_t h_total = 0, h_inc = 0;
                sting_state_plan_counts(st, st->entries[i].id, &h_total, &h_inc);
                sting_buf_append(b, " [incomplete plans: ");
                sting_buf_append_int(b, (long long)h_inc);
                sting_buf_append_ch(b, '/');
                sting_buf_append_int(b, (long long)h_total);
                sting_buf_append_ch(b, ']');
            }
            sting_buf_append(b, " (created: ");
            sting_buf_append(b, st->entries[i].created[0] ? st->entries[i].created : "-");
            sting_buf_append(b, ", modified: ");
            sting_buf_append(b, st->entries[i].modified[0] ? st->entries[i].modified : "-");
            sting_buf_append(b, ")\n");
        }
    }

    size_t all_total = 0, all_inc = 0;
    sting_state_plan_counts(st, 0, &all_total, &all_inc);
    sting_buf_append(b, "\nPlanned Next Steps (");
    sting_buf_append_int(b, (long long)all_total);
    sting_buf_append(b, ", ");
    sting_buf_append_int(b, (long long)all_inc);
    sting_buf_append(b, " incomplete):\n");
    if (st->step_count == 0) {
        sting_buf_append(b, "  (none)\n");
    } else {
        for (int p = 1; p <= 5; p++) {
            for (size_t i = 0; i < st->step_count; i++) {
                if (st->steps[i].priority != p) continue;
                sting_buf_append(b, "  [#");
                sting_buf_append_int(b, (long long)st->steps[i].id);
                sting_buf_append(b, " <- #");
                sting_buf_append_int(b, (long long)st->steps[i].parent_id);
                sting_buf_append(b, "] [P");
                sting_buf_append_int(b, st->steps[i].priority);
                sting_buf_append(b, "][");
                sting_buf_append(b, st->steps[i].status);
                sting_buf_append(b, "] ");
                sting_buf_append(b, st->steps[i].action);
                if (st->steps[i].target[0]) {
                    sting_buf_append(b, " (target: ");
                    sting_buf_append(b, st->steps[i].target);
                    sting_buf_append_ch(b, ')');
                }
                sting_buf_append(b, " (created: ");
                sting_buf_append(b, st->steps[i].created[0] ? st->steps[i].created : "-");
                sting_buf_append(b, ", modified: ");
                sting_buf_append(b, st->steps[i].modified[0] ? st->steps[i].modified : "-");
                sting_buf_append(b, ")\n");
                if (st->steps[i].rationale[0]) {
                    sting_buf_append(b, "      why: ");
                    sting_buf_append(b, st->steps[i].rationale);
                    sting_buf_append_ch(b, '\n');
                }
                if (st->steps[i].outcome[0]) {
                    sting_buf_append(b, "      outcome: ");
                    sting_buf_append(b, st->steps[i].outcome);
                    sting_buf_append_ch(b, '\n');
                }
            }
        }
    }
}

static void send_tool_result(sting_buf_t *b, const char *id, bool is_error, const char *text) {
    sting_buf_append(b, "{\"jsonrpc\":\"2.0\",\"id\":");
    sting_buf_append(b, id);
    sting_buf_append(b, ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":");
    sting_buf_append_json_str(b, text);
    sting_buf_append(b, "}],\"isError\":");
    sting_buf_append(b, is_error ? "true" : "false");
    sting_buf_append(b, "}}");
}

static void send_jsonrpc_error(sting_buf_t *b, const char *id, int code, const char *msg) {
    sting_buf_append(b, "{\"jsonrpc\":\"2.0\",\"id\":");
    sting_buf_append(b, id ? id : "null");
    sting_buf_append(b, ",\"error\":{\"code\":");
    sting_buf_append_int(b, code);
    sting_buf_append(b, ",\"message\":");
    sting_buf_append_json_str(b, msg);
    sting_buf_append(b, "}}");
}

static const char *TOOLS_LIST_JSON =
    "{\"tools\":["
      "{"
        "\"name\":\"stingtrail_node\","
        "\"description\":\"Record a conceptual DAG root node (premise, asset, context)\","
        "\"inputSchema\":{"
          "\"type\":\"object\","
          "\"properties\":{"
            "\"kind\":{\"type\":\"string\",\"enum\":[\"premise\",\"asset\",\"context\"],\"description\":\"Node kind (default: asset)\"},"
            "\"name\":{\"type\":\"string\",\"description\":\"Unique conceptual title or identifier\"},"
            "\"detail\":{\"type\":\"string\",\"description\":\"Optional detailed description or background context\"}"
          "},"
          "\"required\":[\"kind\",\"name\"]"
        "}"
      "},"
      "{"
        "\"name\":\"stingtrail_log\","
        "\"description\":\"Append epistemic reasoning log (observation, hypothesis, evidence, refutation, finding)\","
        "\"inputSchema\":{"
          "\"type\":\"object\","
          "\"properties\":{"
            "\"kind\":{\"type\":\"string\",\"enum\":[\"observation\",\"hypothesis\",\"evidence\",\"refutation\",\"finding\"],\"description\":\"Kind of reasoning entry\"},"
            "\"summary\":{\"type\":\"string\",\"description\":\"Concise summary of reasoning or observation\"},"
            "\"parent_id\":{\"type\":\"integer\",\"description\":\"Parent node or prior entry ID (0 for root)\"},"
            "\"severity\":{\"type\":\"string\",\"enum\":[\"info\",\"low\",\"medium\",\"high\",\"critical\"],\"description\":\"Severity level (default: info)\"},"
            "\"detail\":{\"type\":\"string\",\"description\":\"Optional detailed diagnostic or technical data\"}"
          "},"
          "\"required\":[\"kind\",\"summary\"]"
        "}"
      "},"
      "{"
        "\"name\":\"stingtrail_plan\","
        "\"description\":\"Propose a prioritized action step under an existing hypothesis (optional why rationale)\","
        "\"inputSchema\":{"
          "\"type\":\"object\","
          "\"properties\":{"
            "\"action\":{\"type\":\"string\",\"description\":\"Action description to execute\"},"
            "\"parent_id\":{\"type\":\"integer\",\"description\":\"Existing hypothesis ID that motivates this plan\"},"
            "\"why\":{\"type\":\"string\",\"description\":\"Optional rationale explaining why this action is proposed\"},"
            "\"priority\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":5,\"description\":\"Priority 1 (highest) to 5 (lowest). Default: 3\"},"
            "\"target\":{\"type\":\"string\",\"description\":\"Optional target host or subject\"}"
          "},"
          "\"required\":[\"action\",\"parent_id\"]"
        "}"
      "},"
      "{"
        "\"name\":\"stingtrail_step\","
        "\"description\":\"Update step lifecycle state and record concrete outcome\","
        "\"inputSchema\":{"
          "\"type\":\"object\","
          "\"properties\":{"
            "\"step_id\":{\"type\":\"integer\",\"description\":\"ID of the step to update\"},"
            "\"status\":{\"type\":\"string\",\"enum\":[\"pending\",\"in_progress\",\"done\",\"blocked\"],\"description\":\"New lifecycle state\"},"
            "\"outcome\":{\"type\":\"string\",\"description\":\"Concrete outcome, finding, or result\"}"
          "},"
          "\"required\":[\"step_id\",\"status\"]"
        "}"
      "},"
      "{"
        "\"name\":\"stingtrail_status\","
        "\"description\":\"Display mission critical-thinking board or raw JSON DAG state\","
        "\"inputSchema\":{"
          "\"type\":\"object\","
          "\"properties\":{"
            "\"json\":{\"type\":\"boolean\",\"description\":\"If true, returns state JSON instead of formatted board\"}"
          "}"
        "}"
      "},"
      "{"
        "\"name\":\"stingtrail_export\","
        "\"description\":\"Export full DAG journal as JSON containing all nodes, entries, and steps\","
        "\"inputSchema\":{"
          "\"type\":\"object\","
          "\"properties\":{}"
        "}"
      "},"
      "{"
        "\"name\":\"stingtrail_guard\","
        "\"description\":\"Check if an action command or target host violates fail-closed safety guards\","
        "\"inputSchema\":{"
          "\"type\":\"object\","
          "\"properties\":{"
            "\"action\":{\"type\":\"string\",\"description\":\"Action or command string to check against destructive patterns\"},"
            "\"target_host\":{\"type\":\"string\",\"description\":\"Target host to validate against allowed scope\"}"
          "}"
        "}"
      "},"
      "{"
        "\"name\":\"stingtrail_rebase\","
        "\"description\":\"Compact completed chain(s) into .stingtrail/*.md summary and super-premise node\","
        "\"inputSchema\":{"
          "\"type\":\"object\","
          "\"properties\":{"
            "\"target_id\":{\"type\":\"integer\",\"description\":\"Root or subtree ID to rebase\"},"
            "\"completed\":{\"type\":\"boolean\",\"description\":\"Rebase all completed chains (done/blocked only)\"},"
            "\"dry_run\":{\"type\":\"boolean\",\"description\":\"Validate and preview counts without modifying files\"},"
            "\"title\":{\"type\":\"string\",\"description\":\"Optional custom title for super-premise node\"}"
          "}"
        "}"
      "}"
    "]}";

int sting_mcp_dispatch_message(sting_state_t *st, const char *msg, char *out_buf, size_t out_cap) {
    if (!st || !msg || !out_buf || out_cap == 0) return -1;

    char id_str[64];
    bool has_id = extract_id(msg, id_str, sizeof(id_str));

    char method[64];
    if (!extract_str_field(msg, "method", method, sizeof(method))) {
        if (has_id) {
            sting_buf_t b;
            sting_buf_init(&b, out_buf, out_cap);
            send_jsonrpc_error(&b, id_str, -32600, "Invalid Request: missing method");
            return (int)b.len;
        }
        return 0;
    }

    /* Handle Notifications (no response required) */
    if (strcmp(method, "notifications/initialized") == 0 ||
        strcmp(method, "initialized") == 0) {
        return 0;
    }

    /* Handshake: initialize */
    if (strcmp(method, "initialize") == 0) {
        if (!has_id) return 0;
        sting_buf_t b;
        sting_buf_init(&b, out_buf, out_cap);
        sting_buf_append(&b, "{\"jsonrpc\":\"2.0\",\"id\":");
        sting_buf_append(&b, id_str);
        sting_buf_append(&b, ",\"result\":{"
                              "\"protocolVersion\":\"" STINGTRAIL_MCP_PROTOCOL_VERSION "\","
                              "\"capabilities\":{\"tools\":{},\"resources\":{}},"
                              "\"serverInfo\":{\"name\":\"stingtrail\",\"version\":\"" STINGTRAIL_VERSION "\"}"
                              "}}");
        return b.overflow ? -1 : (int)b.len;
    }

    /* Ping */
    if (strcmp(method, "ping") == 0) {
        if (!has_id) return 0;
        sting_buf_t b;
        sting_buf_init(&b, out_buf, out_cap);
        sting_buf_append(&b, "{\"jsonrpc\":\"2.0\",\"id\":");
        sting_buf_append(&b, id_str);
        sting_buf_append(&b, ",\"result\":{}}");
        return b.overflow ? -1 : (int)b.len;
    }

    /* Tools List */
    if (strcmp(method, "tools/list") == 0) {
        if (!has_id) return 0;
        sting_buf_t b;
        sting_buf_init(&b, out_buf, out_cap);
        sting_buf_append(&b, "{\"jsonrpc\":\"2.0\",\"id\":");
        sting_buf_append(&b, id_str);
        sting_buf_append(&b, ",\"result\":");
        sting_buf_append(&b, TOOLS_LIST_JSON);
        sting_buf_append_ch(&b, '}');
        return b.overflow ? -1 : (int)b.len;
    }

    /* Resources List */
    if (strcmp(method, "resources/list") == 0) {
        if (!has_id) return 0;
        sting_buf_t b;
        sting_buf_init(&b, out_buf, out_cap);
        sting_buf_append(&b, "{\"jsonrpc\":\"2.0\",\"id\":");
        sting_buf_append(&b, id_str);
        sting_buf_append(&b, ",\"result\":{\"resources\":["
                              "{\"uri\":\"stingtrail://board\",\"name\":\"Mission Board\",\"description\":\"ASCII Critical-Thinking Board\",\"mimeType\":\"text/plain\"},"
                              "{\"uri\":\"stingtrail://journal\",\"name\":\"DAG Journal\",\"description\":\"Complete DAG State Export\",\"mimeType\":\"application/json\"}"
                              "]}}");
        return b.overflow ? -1 : (int)b.len;
    }

    /* Resources Read */
    if (strcmp(method, "resources/read") == 0) {
        if (!has_id) return 0;
        const char *params = find_field_in_obj(msg, "params");
        char uri[128];
        extract_str_field(params ? params : msg, "uri", uri, sizeof(uri));

        sting_buf_t b;
        sting_buf_init(&b, out_buf, out_cap);

        if (strcmp(uri, "stingtrail://board") == 0) {
            char board[131072];
            sting_buf_t bb;
            sting_buf_init(&bb, board, sizeof(board));
            sting_mcp_format_board(st, &bb);

            sting_buf_append(&b, "{\"jsonrpc\":\"2.0\",\"id\":");
            sting_buf_append(&b, id_str);
            sting_buf_append(&b, ",\"result\":{\"contents\":[{\"uri\":\"stingtrail://board\",\"mimeType\":\"text/plain\",\"text\":");
            sting_buf_append_json_str(&b, board);
            sting_buf_append(&b, "}]}}");
            return b.overflow ? -1 : (int)b.len;
        } else if (strcmp(uri, "stingtrail://journal") == 0) {
            char jbuf[131072];
            sting_state_to_json(st, jbuf, sizeof(jbuf));

            sting_buf_append(&b, "{\"jsonrpc\":\"2.0\",\"id\":");
            sting_buf_append(&b, id_str);
            sting_buf_append(&b, ",\"result\":{\"contents\":[{\"uri\":\"stingtrail://journal\",\"mimeType\":\"application/json\",\"text\":");
            sting_buf_append_json_str(&b, jbuf);
            sting_buf_append(&b, "}]}}");
            return b.overflow ? -1 : (int)b.len;
        } else {
            send_jsonrpc_error(&b, id_str, -32602, "Unknown resource URI");
            return (int)b.len;
        }
    }

    /* Tool Call */
    if (strcmp(method, "tools/call") == 0) {
        if (!has_id) return 0;
        const char *params = find_field_in_obj(msg, "params");
        if (!params) {
            sting_buf_t b;
            sting_buf_init(&b, out_buf, out_cap);
            send_jsonrpc_error(&b, id_str, -32602, "Invalid params: missing params object");
            return (int)b.len;
        }

        char tool_name[64];
        if (!extract_str_field(params, "name", tool_name, sizeof(tool_name))) {
            sting_buf_t b;
            sting_buf_init(&b, out_buf, out_cap);
            send_jsonrpc_error(&b, id_str, -32602, "Invalid params: missing tool name");
            return (int)b.len;
        }

        const char *args = find_field_in_obj(params, "arguments");
        if (!args) args = "{}";

        sting_buf_t b;
        sting_buf_init(&b, out_buf, out_cap);

        if (strcmp(tool_name, "stingtrail_node") == 0) {
            char kind[32], name[128], detail[256];
            extract_str_field(args, "kind", kind, sizeof(kind));
            extract_str_field(args, "name", name, sizeof(name));
            extract_str_field(args, "detail", detail, sizeof(detail));

            if (name[0] == '\0') {
                send_tool_result(&b, id_str, true, "error: node requires 'name'");
            } else {
                uint32_t nid = sting_state_add_node(st, kind[0] ? kind : "asset", name, detail, true);
                if (nid == 0) {
                    send_tool_result(&b, id_str, true, "error: failed to add node (check node limit)");
                } else {
                    char res[256];
                    snprintf(res, sizeof(res), "node #%u recorded", nid);
                    send_tool_result(&b, id_str, false, res);
                }
            }
            return b.overflow ? -1 : (int)b.len;
        }

        if (strcmp(tool_name, "stingtrail_log") == 0) {
            char kind[32], severity[16], summary[160], detail[512];
            extract_str_field(args, "kind", kind, sizeof(kind));
            extract_str_field(args, "severity", severity, sizeof(severity));
            extract_str_field(args, "summary", summary, sizeof(summary));
            extract_str_field(args, "detail", detail, sizeof(detail));
            long long pid = extract_int_field(args, "parent_id", 0);

            if (summary[0] == '\0') {
                send_tool_result(&b, id_str, true, "error: log requires 'summary'");
            } else {
                uint32_t eid = sting_state_add_entry(st, (uint32_t)pid,
                                                     kind[0] ? kind : "observation",
                                                     severity[0] ? severity : "info",
                                                     summary, detail, true);
                if (eid == 0) {
                    send_tool_result(&b, id_str, true, "error: failed to log entry (verify parent_id exists)");
                } else {
                    char res[256];
                    snprintf(res, sizeof(res), "entry #%u logged", eid);
                    send_tool_result(&b, id_str, false, res);
                }
            }
            return b.overflow ? -1 : (int)b.len;
        }

        if (strcmp(tool_name, "stingtrail_plan") == 0) {
            char action[256], why[384], target[128];
            extract_str_field(args, "action", action, sizeof(action));
            extract_str_field(args, "why", why, sizeof(why));
            extract_str_field(args, "target", target, sizeof(target));
            long long prio = extract_int_field(args, "priority", 3);
            long long pid = extract_int_field(args, "parent_id", 0);

            if (action[0] == '\0') {
                send_tool_result(&b, id_str, true, "error: plan requires 'action'");
            } else {
                const char *err = NULL;
                uint32_t sid = sting_state_plan_step(st, (uint32_t)pid, (int)prio,
                                                     target[0] ? target : NULL,
                                                     action, why, true, &err);
                if (sid == 0) {
                    char err_msg[256];
                    snprintf(err_msg, sizeof(err_msg), "error: %s", err ? err : "failed to plan step");
                    send_tool_result(&b, id_str, true, err_msg);
                } else {
                    char res[256];
                    snprintf(res, sizeof(res), "step #%u planned", sid);
                    send_tool_result(&b, id_str, false, res);
                }
            }
            return b.overflow ? -1 : (int)b.len;
        }

        if (strcmp(tool_name, "stingtrail_step") == 0) {
            long long step_id = extract_int_field(args, "step_id", 0);
            char status[24], outcome[384];
            extract_str_field(args, "status", status, sizeof(status));
            extract_str_field(args, "outcome", outcome, sizeof(outcome));

            if (step_id <= 0 || status[0] == '\0') {
                send_tool_result(&b, id_str, true, "error: step requires 'step_id' and 'status'");
            } else {
                bool ok = sting_state_update_step(st, (uint32_t)step_id, status, outcome, true);
                if (!ok) {
                    send_tool_result(&b, id_str, true, "error: invalid step id or status (valid: pending, in_progress, done, blocked)");
                } else {
                    char res[256];
                    snprintf(res, sizeof(res), "step #%lld updated to %s", step_id, status);
                    send_tool_result(&b, id_str, false, res);
                }
            }
            return b.overflow ? -1 : (int)b.len;
        }

        if (strcmp(tool_name, "stingtrail_status") == 0) {
            bool json_mode = extract_bool_field(args, "json", false);
            if (json_mode) {
                char jbuf[131072];
                if (sting_state_to_json(st, jbuf, sizeof(jbuf))) {
                    send_tool_result(&b, id_str, false, jbuf);
                } else {
                    send_tool_result(&b, id_str, true, "error: failed to generate JSON status");
                }
            } else {
                char board[131072];
                sting_buf_t bb;
                sting_buf_init(&bb, board, sizeof(board));
                sting_mcp_format_board(st, &bb);
                send_tool_result(&b, id_str, false, board);
            }
            return b.overflow ? -1 : (int)b.len;
        }

        if (strcmp(tool_name, "stingtrail_export") == 0) {
            char jbuf[131072];
            if (sting_state_to_json(st, jbuf, sizeof(jbuf))) {
                send_tool_result(&b, id_str, false, jbuf);
            } else {
                send_tool_result(&b, id_str, true, "error: failed to export state");
            }
            return b.overflow ? -1 : (int)b.len;
        }

        if (strcmp(tool_name, "stingtrail_guard") == 0) {
            char action[256], target_host[128];
            extract_str_field(args, "action", action, sizeof(action));
            extract_str_field(args, "target_host", target_host, sizeof(target_host));

            const char *reason = NULL;
            sting_guard_verdict_t v = sting_guard_check(&st->scope,
                                                        target_host[0] ? target_host : NULL,
                                                        action[0] ? action : NULL,
                                                        &reason);
            if (v == STING_GUARD_ALLOW) {
                send_tool_result(&b, id_str, false, "{\"verdict\":\"allow\"}");
            } else {
                char res[256];
                snprintf(res, sizeof(res), "{\"verdict\":\"deny\",\"reason\":\"%s\"}",
                         reason ? reason : "blocked by guard");
                send_tool_result(&b, id_str, false, res);
            }
            return b.overflow ? -1 : (int)b.len;
        }

        if (strcmp(tool_name, "stingtrail_rebase") == 0) {
            long long tid = extract_int_field(args, "target_id", 0);
            bool completed = extract_bool_field(args, "completed", false);
            bool dry_run = extract_bool_field(args, "dry_run", false);
            char title[128];
            extract_str_field(args, "title", title, sizeof(title));

            sting_rebase_opts_t opts;
            memset(&opts, 0, sizeof(opts));
            opts.target_id = (tid > 0) ? (uint32_t)tid : 0;
            opts.all_completed = completed;
            opts.dry_run = dry_run;
            opts.title = title[0] ? title : NULL;

            const char *err = NULL;
            sting_rebase_result_t rres;
            if (!sting_state_rebase(st, &opts, &rres, &err)) {
                char err_msg[256];
                snprintf(err_msg, sizeof(err_msg), "error: %s", err ? err : "rebase failed");
                send_tool_result(&b, id_str, true, err_msg);
            } else {
                char res[768];
                snprintf(res, sizeof(res),
                         "rebased %zu chain(s) -> super-premise #%u (%zu entries, %zu steps compacted; summary: %s)",
                         rres.chains_rebased, rres.last_super_id,
                         rres.entries_removed, rres.steps_removed, rres.summary_path);
                send_tool_result(&b, id_str, false, res);
            }
            return b.overflow ? -1 : (int)b.len;
        }

        /* Unknown tool name */
        char err_msg[128];
        snprintf(err_msg, sizeof(err_msg), "Unknown tool: %s", tool_name);
        send_tool_result(&b, id_str, true, err_msg);
        return b.overflow ? -1 : (int)b.len;
    }

    /* Unhandled method */
    if (has_id) {
        sting_buf_t b;
        sting_buf_init(&b, out_buf, out_cap);
        send_jsonrpc_error(&b, id_str, -32601, "Method not found");
        return (int)b.len;
    }

    return 0;
}

int sting_mcp_run_stdio(sting_state_t *st) {
    if (!st) return 1;

    size_t cap = 262144;
    char *line = (char *)malloc(cap);
    char *resp = (char *)malloc(cap);
    if (!line || !resp) {
        free(line);
        free(resp);
        return 1;
    }

    while (fgets(line, (int)cap, stdin)) {
        if (strncmp(line, "Content-Length:", 15) == 0) {
            long clen = strtol(line + 15, NULL, 10);
            while (fgets(line, (int)cap, stdin)) {
                if (line[0] == '\r' || line[0] == '\n' || line[0] == '\0') break;
            }
            if (clen > 0 && clen < (long)cap) {
                size_t total = 0;
                while (total < (size_t)clen) {
                    size_t n = fread(line + total, 1, (size_t)clen - total, stdin);
                    if (n == 0) break;
                    total += n;
                }
                line[total] = '\0';
                int ret = sting_mcp_dispatch_message(st, line, resp, cap);
                if (ret > 0) {
                    fputs(resp, stdout);
                    fputc('\n', stdout);
                    fflush(stdout);
                }
            }
            continue;
        }

        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;

        int ret = sting_mcp_dispatch_message(st, line, resp, cap);
        if (ret > 0) {
            fputs(resp, stdout);
            fputc('\n', stdout);
            fflush(stdout);
        }
    }

    free(line);
    free(resp);
    return 0;
}
