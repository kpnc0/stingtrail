#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "stingtrail.h"
#include "stingtrail_mcp.h"

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    g_tests_run++; \
    printf("  RUN  test_%s\n", #name); \
    test_##name(); \
    g_tests_passed++; \
    printf("  PASS test_%s\n", #name); \
} while (0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERTION FAILED [%s:%d]: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ_INT(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_STR_EQ(a, b) ASSERT_TRUE(strcmp((a), (b)) == 0)

TEST(mcp_initialize_and_notifications) {
    sting_state_t st;
    sting_state_init(&st, NULL);

    char resp[4096];
    const char *init_req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2024-11-05\"}}";
    int len = sting_mcp_dispatch_message(&st, init_req, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "\"jsonrpc\":\"2.0\"") != NULL);
    ASSERT_TRUE(strstr(resp, "\"id\":1") != NULL);
    ASSERT_TRUE(strstr(resp, "\"protocolVersion\":\"2024-11-05\"") != NULL);
    ASSERT_TRUE(strstr(resp, "\"serverInfo\":{\"name\":\"stingtrail\"") != NULL);
    ASSERT_TRUE(strstr(resp, "\"tools\":{}") != NULL);
    ASSERT_TRUE(strstr(resp, "\"resources\":{}") != NULL);

    /* String id */
    const char *init_str_id = "{\"jsonrpc\":\"2.0\",\"id\":\"req-abc\",\"method\":\"initialize\"}";
    len = sting_mcp_dispatch_message(&st, init_str_id, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "\"id\":\"req-abc\"") != NULL);

    /* Notifications return 0 (no output sent) */
    const char *notif = "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}";
    len = sting_mcp_dispatch_message(&st, notif, resp, sizeof(resp));
    ASSERT_EQ_INT(len, 0);

    /* Ping returns {} */
    const char *ping_req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"ping\"}";
    len = sting_mcp_dispatch_message(&st, ping_req, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "\"id\":2") != NULL);
    ASSERT_TRUE(strstr(resp, "\"result\":{}") != NULL);
}

TEST(mcp_tools_list) {
    sting_state_t st;
    sting_state_init(&st, NULL);

    char resp[16384];
    const char *req = "{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"tools/list\"}";
    int len = sting_mcp_dispatch_message(&st, req, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "\"tools\":[") != NULL);
    ASSERT_TRUE(strstr(resp, "\"name\":\"stingtrail_node\"") != NULL);
    ASSERT_TRUE(strstr(resp, "\"name\":\"stingtrail_log\"") != NULL);
    ASSERT_TRUE(strstr(resp, "\"name\":\"stingtrail_plan\"") != NULL);
    ASSERT_TRUE(strstr(resp, "\"name\":\"stingtrail_step\"") != NULL);
    ASSERT_TRUE(strstr(resp, "\"name\":\"stingtrail_status\"") != NULL);
    ASSERT_TRUE(strstr(resp, "\"name\":\"stingtrail_export\"") != NULL);
    ASSERT_TRUE(strstr(resp, "\"name\":\"stingtrail_guard\"") != NULL);
}

TEST(mcp_tool_node_mutation) {
    sting_state_t st;
    sting_state_init(&st, NULL);

    char resp[4096];
    const char *req1 = "{\"jsonrpc\":\"2.0\",\"id\":11,\"method\":\"tools/call\",\"params\":{\"name\":\"stingtrail_node\",\"arguments\":{\"kind\":\"premise\",\"name\":\"MCP Integration Premise\",\"detail\":\"Zero-dep C server\"}}}";
    int len = sting_mcp_dispatch_message(&st, req1, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "\"isError\":false") != NULL);
    ASSERT_TRUE(strstr(resp, "node #1 recorded") != NULL);
    ASSERT_EQ_INT(st.node_count, 1);
    ASSERT_STR_EQ(st.nodes[0].name, "MCP Integration Premise");

    /* Deduplication check: same kind & name returns existing ID */
    const char *req2 = "{\"jsonrpc\":\"2.0\",\"id\":12,\"method\":\"tools/call\",\"params\":{\"name\":\"stingtrail_node\",\"arguments\":{\"kind\":\"premise\",\"name\":\"MCP Integration Premise\",\"detail\":\"Duplicate\"}}}";
    len = sting_mcp_dispatch_message(&st, req2, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "\"isError\":false") != NULL);
    ASSERT_TRUE(strstr(resp, "node #1 recorded") != NULL);
    ASSERT_EQ_INT(st.node_count, 1);

    /* Missing name error */
    const char *req_err = "{\"jsonrpc\":\"2.0\",\"id\":13,\"method\":\"tools/call\",\"params\":{\"name\":\"stingtrail_node\",\"arguments\":{\"kind\":\"premise\"}}}";
    len = sting_mcp_dispatch_message(&st, req_err, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "\"isError\":true") != NULL);
    ASSERT_TRUE(strstr(resp, "error: node requires 'name'") != NULL);
}

TEST(mcp_tool_log_and_plan) {
    sting_state_t st;
    sting_state_init(&st, NULL);

    uint32_t nid = sting_state_add_node(&st, "premise", "Security Audit", "Target scope", false);
    ASSERT_EQ_INT(nid, 1);

    char resp[4096];
    /* Log entry under node 1 */
    const char *req_log = "{\"jsonrpc\":\"2.0\",\"id\":20,\"method\":\"tools/call\",\"params\":{\"name\":\"stingtrail_log\",\"arguments\":{\"kind\":\"observation\",\"severity\":\"high\",\"summary\":\"Open port 22 and 80\",\"detail\":\"Nmap scan results\",\"parent_id\":1}}}";
    int len = sting_mcp_dispatch_message(&st, req_log, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "\"isError\":false") != NULL);
    ASSERT_TRUE(strstr(resp, "entry #2 logged") != NULL);
    ASSERT_EQ_INT(st.entry_count, 1);
    ASSERT_STR_EQ(st.entries[0].summary, "Open port 22 and 80");

    /* Plan step with rationale */
    const char *req_plan = "{\"jsonrpc\":\"2.0\",\"id\":21,\"method\":\"tools/call\",\"params\":{\"name\":\"stingtrail_plan\",\"arguments\":{\"action\":\"Enumerate HTTP paths\",\"why\":\"Identify admin endpoints\",\"priority\":1,\"parent_id\":2,\"target\":\"192.168.1.10\"}}}";
    len = sting_mcp_dispatch_message(&st, req_plan, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "\"isError\":false") != NULL);
    ASSERT_TRUE(strstr(resp, "step #3 planned") != NULL);
    ASSERT_EQ_INT(st.step_count, 1);
    ASSERT_STR_EQ(st.steps[0].action, "Enumerate HTTP paths");
    ASSERT_STR_EQ(st.steps[0].rationale, "Identify admin endpoints");
    ASSERT_EQ_INT(st.steps[0].priority, 1);

    /* Plan step without rationale (must fail) */
    const char *req_no_why = "{\"jsonrpc\":\"2.0\",\"id\":22,\"method\":\"tools/call\",\"params\":{\"name\":\"stingtrail_plan\",\"arguments\":{\"action\":\"Reboot server\",\"parent_id\":2}}}";
    len = sting_mcp_dispatch_message(&st, req_no_why, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "\"isError\":true") != NULL);
    ASSERT_TRUE(strstr(resp, "rationale is required") != NULL);

    /* Plan step with destructive pattern (guard fail-closed) */
    const char *req_destruct = "{\"jsonrpc\":\"2.0\",\"id\":23,\"method\":\"tools/call\",\"params\":{\"name\":\"stingtrail_plan\",\"arguments\":{\"action\":\"rm -rf /var/log\",\"why\":\"Clean logs\",\"parent_id\":2}}}";
    len = sting_mcp_dispatch_message(&st, req_destruct, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "\"isError\":true") != NULL);
    ASSERT_TRUE(strstr(resp, "destructive command pattern blocked") != NULL);
}

TEST(mcp_tool_step_lifecycle) {
    sting_state_t st;
    sting_state_init(&st, NULL);

    uint32_t nid = sting_state_add_node(&st, "premise", "Test Mission", "", false);
    const char *err = NULL;
    uint32_t sid = sting_state_plan_step(&st, nid, 2, NULL, "Fuzz parameters", "Find injection bugs", false, &err);
    ASSERT_TRUE(sid > 0);

    char resp[4096];
    /* Update to in_progress */
    const char *req_step1 = "{\"jsonrpc\":\"2.0\",\"id\":30,\"method\":\"tools/call\",\"params\":{\"name\":\"stingtrail_step\",\"arguments\":{\"step_id\":2,\"status\":\"in_progress\",\"outcome\":\"Fuzzing started\"}}}";
    int len = sting_mcp_dispatch_message(&st, req_step1, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "\"isError\":false") != NULL);
    ASSERT_TRUE(strstr(resp, "step #2 updated to in_progress") != NULL);
    ASSERT_STR_EQ(st.steps[0].status, "in_progress");

    /* Update to done */
    const char *req_step2 = "{\"jsonrpc\":\"2.0\",\"id\":31,\"method\":\"tools/call\",\"params\":{\"name\":\"stingtrail_step\",\"arguments\":{\"step_id\":2,\"status\":\"done\",\"outcome\":\"SQL injection confirmed on id param\"}}}";
    len = sting_mcp_dispatch_message(&st, req_step2, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "\"isError\":false") != NULL);
    ASSERT_TRUE(strstr(resp, "step #2 updated to done") != NULL);
    ASSERT_STR_EQ(st.steps[0].status, "done");
    ASSERT_STR_EQ(st.steps[0].outcome, "SQL injection confirmed on id param");

    /* Invalid status fails */
    const char *req_step3 = "{\"jsonrpc\":\"2.0\",\"id\":32,\"method\":\"tools/call\",\"params\":{\"name\":\"stingtrail_step\",\"arguments\":{\"step_id\":2,\"status\":\"bogus\"}}}";
    len = sting_mcp_dispatch_message(&st, req_step3, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "\"isError\":true") != NULL);
}

TEST(mcp_status_and_export) {
    sting_state_t st;
    sting_state_init(&st, NULL);

    uint32_t nid = sting_state_add_node(&st, "premise", "Board Test Premise", "Testing status display", false);
    sting_state_add_entry(&st, nid, "observation", "info", "Obs 1", "Detail 1", false);
    sting_state_plan_step(&st, nid, 1, "test.host", "Execute plan", "Because testing", false, NULL);

    char resp[65536];
    /* Text board status */
    const char *req_status = "{\"jsonrpc\":\"2.0\",\"id\":40,\"method\":\"tools/call\",\"params\":{\"name\":\"stingtrail_status\",\"arguments\":{\"json\":false}}}";
    int len = sting_mcp_dispatch_message(&st, req_status, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "CRITICAL THINKING BOARD") != NULL);
    ASSERT_TRUE(strstr(resp, "Board Test Premise") != NULL);
    ASSERT_TRUE(strstr(resp, "Execute plan") != NULL);

    /* JSON status */
    const char *req_json = "{\"jsonrpc\":\"2.0\",\"id\":41,\"method\":\"tools/call\",\"params\":{\"name\":\"stingtrail_status\",\"arguments\":{\"json\":true}}}";
    len = sting_mcp_dispatch_message(&st, req_json, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "\\\"version\\\":\\\"" STINGTRAIL_VERSION "\\\"") != NULL);
    ASSERT_TRUE(strstr(resp, "Board Test Premise") != NULL);

    /* Export tool */
    const char *req_exp = "{\"jsonrpc\":\"2.0\",\"id\":42,\"method\":\"tools/call\",\"params\":{\"name\":\"stingtrail_export\",\"arguments\":{}}}";
    len = sting_mcp_dispatch_message(&st, req_exp, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "\\\"version\\\":\\\"" STINGTRAIL_VERSION "\\\"") != NULL);
}

TEST(mcp_guard_tool_and_scope) {
    sting_state_t st;
    sting_state_init(&st, NULL);
    sting_scope_init(&st.scope, true);
    sting_scope_add_host(&st.scope, "10.0.0.1");

    char resp[4096];
    /* Safe action on allowed host */
    const char *req_allow = "{\"jsonrpc\":\"2.0\",\"id\":50,\"method\":\"tools/call\",\"params\":{\"name\":\"stingtrail_guard\",\"arguments\":{\"action\":\"nmap -sV 10.0.0.1\",\"target_host\":\"10.0.0.1\"}}}";
    int len = sting_mcp_dispatch_message(&st, req_allow, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "allow") != NULL);

    /* Destructive action blocked */
    const char *req_deny1 = "{\"jsonrpc\":\"2.0\",\"id\":51,\"method\":\"tools/call\",\"params\":{\"name\":\"stingtrail_guard\",\"arguments\":{\"action\":\"rm -rf /\",\"target_host\":\"10.0.0.1\"}}}";
    len = sting_mcp_dispatch_message(&st, req_deny1, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "deny") != NULL);
    ASSERT_TRUE(strstr(resp, "destructive command pattern blocked") != NULL);

    /* Target host outside scope blocked */
    const char *req_deny2 = "{\"jsonrpc\":\"2.0\",\"id\":52,\"method\":\"tools/call\",\"params\":{\"name\":\"stingtrail_guard\",\"arguments\":{\"action\":\"curl http://evil.com\",\"target_host\":\"evil.com\"}}}";
    len = sting_mcp_dispatch_message(&st, req_deny2, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "deny") != NULL);
    ASSERT_TRUE(strstr(resp, "outside authorized scope") != NULL);
}

TEST(mcp_resources) {
    sting_state_t st;
    sting_state_init(&st, NULL);
    sting_state_add_node(&st, "premise", "Resource Root", "", false);

    char resp[65536];
    /* Resources list */
    const char *req_list = "{\"jsonrpc\":\"2.0\",\"id\":60,\"method\":\"resources/list\"}";
    int len = sting_mcp_dispatch_message(&st, req_list, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "stingtrail://board") != NULL);
    ASSERT_TRUE(strstr(resp, "stingtrail://journal") != NULL);

    /* Read board */
    const char *req_read_board = "{\"jsonrpc\":\"2.0\",\"id\":61,\"method\":\"resources/read\",\"params\":{\"uri\":\"stingtrail://board\"}}";
    len = sting_mcp_dispatch_message(&st, req_read_board, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "CRITICAL THINKING BOARD") != NULL);
    ASSERT_TRUE(strstr(resp, "Resource Root") != NULL);

    /* Read journal */
    const char *req_read_journal = "{\"jsonrpc\":\"2.0\",\"id\":62,\"method\":\"resources/read\",\"params\":{\"uri\":\"stingtrail://journal\"}}";
    len = sting_mcp_dispatch_message(&st, req_read_journal, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "\\\"version\\\":\\\"" STINGTRAIL_VERSION "\\\"") != NULL);
}

TEST(mcp_error_handling) {
    sting_state_t st;
    sting_state_init(&st, NULL);

    char resp[4096];
    /* Unknown method */
    const char *req_bad_method = "{\"jsonrpc\":\"2.0\",\"id\":70,\"method\":\"unknown_method\"}";
    int len = sting_mcp_dispatch_message(&st, req_bad_method, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "\"error\":{\"code\":-32601") != NULL);

    /* Unknown tool name */
    const char *req_bad_tool = "{\"jsonrpc\":\"2.0\",\"id\":71,\"method\":\"tools/call\",\"params\":{\"name\":\"nonexistent_tool\"}}";
    len = sting_mcp_dispatch_message(&st, req_bad_tool, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "\"isError\":true") != NULL);
    ASSERT_TRUE(strstr(resp, "Unknown tool: nonexistent_tool") != NULL);
}

TEST(mcp_persistence) {
    const char *test_db = "test_tmp_mcp.jsonl";
    unlink(test_db);

    sting_state_t s1;
    sting_state_init(&s1, test_db);

    char resp[4096];
    const char *req_node = "{\"jsonrpc\":\"2.0\",\"id\":80,\"method\":\"tools/call\",\"params\":{\"name\":\"stingtrail_node\",\"arguments\":{\"kind\":\"premise\",\"name\":\"Persistent Node\",\"detail\":\"MCP Persist\"}}}";
    int len = sting_mcp_dispatch_message(&s1, req_node, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "\"isError\":false") != NULL);

    const char *req_plan = "{\"jsonrpc\":\"2.0\",\"id\":81,\"method\":\"tools/call\",\"params\":{\"name\":\"stingtrail_plan\",\"arguments\":{\"action\":\"Persist step\",\"why\":\"Test file sync\",\"priority\":2,\"parent_id\":1}}}";
    len = sting_mcp_dispatch_message(&s1, req_plan, resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "\"isError\":false") != NULL);

    /* Verify file was created */
    ASSERT_TRUE(access(test_db, F_OK) == 0);

    /* Reload into new state */
    sting_state_t s2;
    sting_state_init(&s2, test_db);
    ASSERT_EQ_INT(s2.node_count, 1);
    ASSERT_STR_EQ(s2.nodes[0].name, "Persistent Node");
    ASSERT_EQ_INT(s2.step_count, 1);
    ASSERT_STR_EQ(s2.steps[0].action, "Persist step");

    unlink(test_db);
}

int main(void) {
    printf("=== Stingtrail MCP Test Suite ===\n");
    RUN_TEST(mcp_initialize_and_notifications);
    RUN_TEST(mcp_tools_list);
    RUN_TEST(mcp_tool_node_mutation);
    RUN_TEST(mcp_tool_log_and_plan);
    RUN_TEST(mcp_tool_step_lifecycle);
    RUN_TEST(mcp_status_and_export);
    RUN_TEST(mcp_guard_tool_and_scope);
    RUN_TEST(mcp_resources);
    RUN_TEST(mcp_error_handling);
    RUN_TEST(mcp_persistence);

    printf("=== Result: %d/%d MCP tests passed ===\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
