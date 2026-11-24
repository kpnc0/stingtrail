#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "stingtrail.h"

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

TEST(json_buffer_and_escaping) {
    char mem[512];
    sting_buf_t buf;
    sting_buf_init(&buf, mem, sizeof(mem));

    const char *raw = "Test \"quotes\"\nand\ttabs\\and\\slashes";
    bool ok = sting_buf_append_json_str(&buf, raw);
    ASSERT_TRUE(ok);
    ASSERT_FALSE(buf.overflow);

    /* Verify escaped characters in JSON representation */
    ASSERT_TRUE(strstr(buf.buf, "\\\"quotes\\\"") != NULL);
    ASSERT_TRUE(strstr(buf.buf, "\\n") != NULL);
    ASSERT_TRUE(strstr(buf.buf, "\\t") != NULL);
    ASSERT_TRUE(strstr(buf.buf, "\\\\") != NULL);

    /* Test parsing string back */
    const char *src = buf.buf;
    char parsed[256];
    ok = sting_json_parse_string(&src, parsed, sizeof(parsed));
    ASSERT_TRUE(ok);
    ASSERT_STR_EQ(parsed, raw);

    /* Test parsing integer */
    const char *num_src = "   1337   ";
    long long val = 0;
    ok = sting_json_parse_int(&num_src, &val);
    ASSERT_TRUE(ok);
    ASSERT_EQ_INT(val, 1337);
}

TEST(guard_destructive_patterns) {
    const char *reason = NULL;

    /* Destructive command patterns blocked */
    ASSERT_EQ_INT(sting_guard_check(NULL, NULL, "rm -rf /", &reason), STING_GUARD_DENY);
    ASSERT_TRUE(reason != NULL);
    ASSERT_TRUE(strstr(reason, "destructive") != NULL);

    ASSERT_EQ_INT(sting_guard_check(NULL, NULL, "mkfs.ext4 /dev/sda1", &reason), STING_GUARD_DENY);
    ASSERT_EQ_INT(sting_guard_check(NULL, NULL, "dd if=/dev/zero of=/dev/sda", &reason), STING_GUARD_DENY);
    ASSERT_EQ_INT(sting_guard_check(NULL, NULL, ":(){ :|:& };:", &reason), STING_GUARD_DENY);
    ASSERT_EQ_INT(sting_guard_check(NULL, NULL, "chmod -R 777 /", &reason), STING_GUARD_DENY);

    /* Safe benign command */
    reason = NULL;
    ASSERT_EQ_INT(sting_guard_check(NULL, NULL, "nmap -sV -p 80,443 10.0.0.1", &reason), STING_GUARD_ALLOW);
    ASSERT_TRUE(reason == NULL);
}

TEST(guard_scope_validation) {
    sting_scope_t scope;
    sting_scope_init(&scope, true);

    bool ok = sting_scope_parse_csv(&scope, "10.0.0.15,api.target.internal,192.168.1.100");
    ASSERT_TRUE(ok);
    ASSERT_EQ_INT(scope.allowed_host_count, 3);

    const char *reason = NULL;

    /* In-scope targets allowed */
    ASSERT_EQ_INT(sting_guard_check(&scope, "api.target.internal", "curl https://api.target.internal/v1", &reason), STING_GUARD_ALLOW);
    ASSERT_EQ_INT(sting_guard_check(&scope, "10.0.0.15", "ping 10.0.0.15", &reason), STING_GUARD_ALLOW);

    /* Out-of-scope targets denied */
    ASSERT_EQ_INT(sting_guard_check(&scope, "evil-external.com", "curl http://evil-external.com", &reason), STING_GUARD_DENY);
    ASSERT_TRUE(reason != NULL);
    ASSERT_TRUE(strstr(reason, "outside authorized scope") != NULL);

    /* Missing target when scope is enforced denied */
    ASSERT_EQ_INT(sting_guard_check(&scope, NULL, "curl something", &reason), STING_GUARD_DENY);
}

TEST(guard_unsafe_controls) {
    const char *reason = NULL;
    char dangerous_input[] = "ls -la\x1b[31m; reboot";

    ASSERT_EQ_INT(sting_guard_check(NULL, NULL, dangerous_input, &reason), STING_GUARD_DENY);
    ASSERT_TRUE(reason != NULL);
    ASSERT_TRUE(strstr(reason, "unsafe control") != NULL);
}

TEST(node_deduplication_and_lookup) {
    sting_state_t st;
    sting_state_init(&st, NULL);

    uint32_t id1 = sting_state_add_node(&st, "premise", "Engagement Scope", "Primary Target Network", false);
    ASSERT_TRUE(id1 > 0);
    ASSERT_EQ_INT(st.node_count, 1);

    /* Deduplication: same kind and name must return existing ID */
    uint32_t id1_dup = sting_state_add_node(&st, "premise", "Engagement Scope", "Different Detail", false);
    ASSERT_EQ_INT(id1, id1_dup);
    ASSERT_EQ_INT(st.node_count, 1);

    /* Different kind or name creates a new node */
    uint32_t id2 = sting_state_add_node(&st, "asset", "Web Application", "Target HTTP API", false);
    ASSERT_TRUE(id2 > id1);
    ASSERT_EQ_INT(st.node_count, 2);

    ASSERT_STR_EQ(st.nodes[0].name, "Engagement Scope");
    ASSERT_STR_EQ(st.nodes[0].kind, "premise");
    ASSERT_STR_EQ(st.nodes[1].name, "Web Application");
    ASSERT_STR_EQ(st.nodes[1].kind, "asset");
}

TEST(reasoning_entries_and_steps) {
    sting_state_t st;
    sting_state_init(&st, NULL);

    uint32_t root_id = sting_state_add_node(&st, "premise", "Mission Alpha", "Root Context", false);
    ASSERT_TRUE(root_id > 0);

    /* Add reasoning entry linked to root */
    uint32_t entry_id = sting_state_add_entry(&st, root_id, "observation", "high",
                                              "Target open port 8080", "Tomcat manager detected", false);
    ASSERT_TRUE(entry_id > 0);
    ASSERT_EQ_INT(st.entry_count, 1);
    ASSERT_EQ_INT(st.entries[0].parent_id, root_id);
    ASSERT_STR_EQ(st.entries[0].summary, "Target open port 8080");

    /* Attempt to add step without rationale - must fail */
    const char *err = NULL;
    uint32_t fail_step = sting_state_plan_step(&st, root_id, 1, "10.0.0.1", "Run gobuster", "", false, &err);
    ASSERT_EQ_INT(fail_step, 0);
    ASSERT_TRUE(err != NULL);
    ASSERT_TRUE(strstr(err, "rationale is required") != NULL);

    /* Plan step with mandatory rationale */
    err = NULL;
    uint32_t step_id = sting_state_plan_step(&st, root_id, 1, "10.0.0.1",
                                            "Scan Tomcat directory", "Find admin interface endpoint",
                                            false, &err);
    ASSERT_TRUE(step_id > 0);
    ASSERT_EQ_INT(st.step_count, 1);
    ASSERT_STR_EQ(st.steps[0].action, "Scan Tomcat directory");
    ASSERT_STR_EQ(st.steps[0].rationale, "Find admin interface endpoint");
    ASSERT_EQ_INT(st.steps[0].priority, 1);
    ASSERT_STR_EQ(st.steps[0].status, "pending");

    /* Priority clamping test */
    uint32_t prio_high = sting_state_plan_step(&st, root_id, 99, NULL, "Action A", "Why A", false, NULL);
    ASSERT_TRUE(prio_high > 0);
    ASSERT_EQ_INT(st.steps[1].priority, 5);

    uint32_t prio_low = sting_state_plan_step(&st, root_id, -5, NULL, "Action B", "Why B", false, NULL);
    ASSERT_TRUE(prio_low > 0);
    ASSERT_EQ_INT(st.steps[2].priority, 1);

    /* Update step lifecycle: pending -> in_progress -> done */
    bool ok = sting_state_update_step(&st, step_id, "in_progress", "Wordlist scanning started", false);
    ASSERT_TRUE(ok);
    ASSERT_STR_EQ(st.steps[0].status, "in_progress");
    ASSERT_STR_EQ(st.steps[0].outcome, "Wordlist scanning started");

    ok = sting_state_update_step(&st, step_id, "done", "Discovered /manager/html endpoint", false);
    ASSERT_TRUE(ok);
    ASSERT_STR_EQ(st.steps[0].status, "done");
    ASSERT_STR_EQ(st.steps[0].outcome, "Discovered /manager/html endpoint");

    /* Invalid status rejected */
    ok = sting_state_update_step(&st, step_id, "invalid_status", "outcome", false);
    ASSERT_FALSE(ok);
}

TEST(journal_persistence_and_reload) {
    const char *test_db = "test_tmp_persist.jsonl";
    unlink(test_db);

    sting_state_t s1;
    sting_state_init(&s1, test_db);

    uint32_t n1 = sting_state_add_node(&s1, "premise", "Penetration Test Root", "Customer Network", true);
    ASSERT_TRUE(n1 > 0);

    uint32_t e1 = sting_state_add_entry(&s1, n1, "observation", "medium", "Port 443 active", "TLS certificate expired", true);
    ASSERT_TRUE(e1 > 0);

    const char *err = NULL;
    uint32_t st1 = sting_state_plan_step(&s1, n1, 2, "target.org", "Analyze TLS cipher suites", "Check weak ciphers", true, &err);
    ASSERT_TRUE(st1 > 0);

    bool ok = sting_state_update_step(&s1, st1, "done", "Vulnerable to RC4 deprecation", true);
    ASSERT_TRUE(ok);

    /* Verify file exists */
    ASSERT_TRUE(access(test_db, F_OK) == 0);

    /* Reload into a new state instance */
    sting_state_t s2;
    sting_state_init(&s2, test_db);

    ASSERT_EQ_INT(s2.node_count, 1);
    ASSERT_EQ_INT(s2.entry_count, 1);
    ASSERT_EQ_INT(s2.step_count, 1);

    ASSERT_STR_EQ(s2.nodes[0].name, "Penetration Test Root");
    ASSERT_STR_EQ(s2.entries[0].summary, "Port 443 active");
    ASSERT_STR_EQ(s2.steps[0].action, "Analyze TLS cipher suites");
    ASSERT_STR_EQ(s2.steps[0].status, "done");
    ASSERT_STR_EQ(s2.steps[0].outcome, "Vulnerable to RC4 deprecation");

    /* Append second round of operations */
    uint32_t e2 = sting_state_add_entry(&s2, n1, "hypothesis", "high", "SSL Renegotiation DoS", "Testing server renegotiation", true);
    ASSERT_TRUE(e2 > 0);

    sting_state_t s3;
    sting_state_init(&s3, test_db);
    ASSERT_EQ_INT(s3.entry_count, 2);
    ASSERT_STR_EQ(s3.entries[1].summary, "SSL Renegotiation DoS");

    unlink(test_db);
}

TEST(state_export_and_board) {
    sting_state_t st;
    sting_state_init(&st, NULL);

    uint32_t n = sting_state_add_node(&st, "premise", "Export Test Premise", "Testing JSON dump", false);
    sting_state_add_entry(&st, n, "observation", "info", "Export observation summary", "Detail notes", false);
    sting_state_plan_step(&st, n, 1, "test.local", "Execute step 1", "Rationale 1", false, NULL);

    /* JSON serialization */
    char json_buf[4096];
    bool ok = sting_state_to_json(&st, json_buf, sizeof(json_buf));
    ASSERT_TRUE(ok);

    ASSERT_TRUE(strstr(json_buf, "\"version\":\"" STINGTRAIL_VERSION "\"") != NULL);
    ASSERT_TRUE(strstr(json_buf, "\"Export Test Premise\"") != NULL);
    ASSERT_TRUE(strstr(json_buf, "\"Export observation summary\"") != NULL);
    ASSERT_TRUE(strstr(json_buf, "\"Execute step 1\"") != NULL);
    ASSERT_TRUE(strstr(json_buf, "\"Rationale 1\"") != NULL);

    /* Board print smoke test */
    printf("\n--- Test Board Rendering ---\n");
    sting_state_print_board(&st);
    printf("--- End Test Board Rendering ---\n");
}

TEST(hook_semantic_classification_and_destructives) {
    /* Destructive patterns */
    ASSERT_TRUE(sting_is_destructive("rm -rf /tmp"));
    ASSERT_TRUE(sting_is_destructive("rm -r -f /var"));
    ASSERT_TRUE(sting_is_destructive("rm -f -r /var"));
    ASSERT_TRUE(sting_is_destructive("shred /tmp/file"));
    ASSERT_TRUE(sting_is_destructive("dd if=/dev/zero of=/dev/sda"));
    ASSERT_TRUE(sting_is_destructive(":(){ :|:& };:"));
    ASSERT_TRUE(sting_is_destructive("chmod -R 777 /"));
    ASSERT_FALSE(sting_is_destructive("ls -la /tmp"));
    ASSERT_FALSE(sting_is_destructive("curl http://example.com"));

    /* Benign navigation commands */
    ASSERT_TRUE(sting_is_benign_navigation("pwd"));
    ASSERT_TRUE(sting_is_benign_navigation("ls -la /tmp"));
    ASSERT_TRUE(sting_is_benign_navigation("cd /tmp/Experiments"));
    ASSERT_TRUE(sting_is_benign_navigation("cat /etc/hosts"));
    ASSERT_TRUE(sting_is_benign_navigation("git status"));
    ASSERT_TRUE(sting_is_benign_navigation("git diff HEAD~1"));
    ASSERT_TRUE(sting_is_benign_navigation("git log -n 5"));
    ASSERT_TRUE(sting_is_benign_navigation("echo 'hello'"));
    ASSERT_TRUE(sting_is_benign_navigation("which python3"));
    ASSERT_FALSE(sting_is_benign_navigation("curl http://10.0.0.1"));
    ASSERT_FALSE(sting_is_benign_navigation("nmap -sT 10.0.0.1"));
    ASSERT_FALSE(sting_is_benign_navigation("make clean"));
    ASSERT_FALSE(sting_is_benign_navigation("gcc -o test test.c"));

    /* Hook classification with and without active context */
    const char *reason = NULL;
    /* Benign allows without context */
    ASSERT_EQ_INT(sting_hook_classify("ls -la", 0, &reason), STING_HOOK_ALLOW_BENIGN);
    ASSERT_EQ_INT(sting_hook_classify("pwd", 0, &reason), STING_HOOK_ALLOW_BENIGN);

    /* Destructive blocks even with active context */
    ASSERT_EQ_INT(sting_hook_classify("rm -rf /tmp/data", 10, &reason), STING_HOOK_BLOCK_DESTRUCTIVE);

    /* Exploratory blocks when no context (context = 0) */
    reason = NULL;
    ASSERT_EQ_INT(sting_hook_classify("curl http://10.0.0.1", 0, &reason), STING_HOOK_BLOCK_NO_CONTEXT);
    ASSERT_TRUE(reason != NULL);

    /* Exploratory allows when context is set (context = 10) */
    reason = NULL;
    ASSERT_EQ_INT(sting_hook_classify("curl http://10.0.0.1", 10, &reason), STING_HOOK_ALLOW_ACTIVE);
}

TEST(context_management_and_persistence) {
    const char *test_db = "test_tmp_context.jsonl";
    unlink(test_db);
    unlink("test_tmp_context.jsonl.context");

    /* Initially no context */
    uint32_t ctx = sting_context_get(test_db);
    ASSERT_EQ_INT(ctx, 0);

    /* Set context to node #42 */
    bool ok = sting_context_set(test_db, 42);
    ASSERT_TRUE(ok);

    ctx = sting_context_get(test_db);
    ASSERT_EQ_INT(ctx, 42);

    /* Clear context */
    ok = sting_context_clear(test_db);
    ASSERT_TRUE(ok);

    ctx = sting_context_get(test_db);
    ASSERT_EQ_INT(ctx, 0);

    unlink(test_db);
    unlink("test_tmp_context.jsonl.context");
}

TEST(state_focus_traversal_and_json) {
    sting_state_t st;
    sting_state_init(&st, NULL);

    uint32_t p1 = sting_state_add_node(&st, "premise", "Root Premise", "Initial premise", false);
    uint32_t h1 = sting_state_add_entry(&st, p1, "hypothesis", "medium", "Hypothesis 1", "Detailed hyp", false);
    uint32_t s1 = sting_state_plan_step(&st, h1, 1, "target.local", "Execute Scan", "Check vulnerability", false, NULL);
    uint32_t o1 = sting_state_add_entry(&st, s1, "observation", "info", "Scan Output", "Port 80 open", false);
    uint32_t r1 = sting_state_add_entry(&st, s1, "refutation", "high", "Scan Refutation", "Firewall blocked", false);
    uint32_t other = sting_state_add_node(&st, "premise", "Unrelated Premise", "Other scope", false);
    (void)o1; (void)r1; (void)other;

    char json_buf[16384];
    bool ok = sting_state_focused_to_json(&st, s1, json_buf, sizeof(json_buf));
    ASSERT_TRUE(ok);

    /* Target ID and focused mode present */
    ASSERT_TRUE(strstr(json_buf, "\"target_id\": 3") != NULL || strstr(json_buf, "\"target_id\":3") != NULL);
    ASSERT_TRUE(strstr(json_buf, "\"traversal\": \"hourglass_v1\"") != NULL || strstr(json_buf, "\"traversal\":\"hourglass_v1\"") != NULL);

    /* Ancestors present */
    ASSERT_TRUE(strstr(json_buf, "Root Premise") != NULL);
    ASSERT_TRUE(strstr(json_buf, "Hypothesis 1") != NULL);

    /* Target present */
    ASSERT_TRUE(strstr(json_buf, "Execute Scan") != NULL);

    /* Descendants present */
    ASSERT_TRUE(strstr(json_buf, "Scan Output") != NULL);
    ASSERT_TRUE(strstr(json_buf, "Scan Refutation") != NULL);

    /* Unrelated branch omitted */
    ASSERT_TRUE(strstr(json_buf, "Unrelated Premise") == NULL);

    /* Exercise text printing without crashing */
    sting_state_print_focused(&st, s1);
}

TEST(terminal_color_subsystem_and_no_color) {
    /* 1. Never mode: All color strings are empty strings */
    sting_color_set_mode(2);
    ASSERT_EQ_INT(sting_color_get_mode(), 2);
    sting_color_t c = sting_color_get();
    ASSERT_FALSE(c.enabled);
    ASSERT_STR_EQ(c.reset, "");
    ASSERT_STR_EQ(c.bold, "");
    ASSERT_STR_EQ(c.dim, "");
    ASSERT_STR_EQ(c.anchor, "");
    ASSERT_STR_EQ(c.p1, "");
    ASSERT_STR_EQ(c.done, "");
    ASSERT_STR_EQ(c.blocked, "");
    ASSERT_STR_EQ(c.hyp, "");
    ASSERT_STR_EQ(sting_color_status(&c, "done"), "");
    ASSERT_STR_EQ(sting_color_status(&c, "blocked"), "");
    ASSERT_STR_EQ(sting_color_priority(&c, 1), "");
    ASSERT_STR_EQ(sting_color_kind(&c, "hypothesis"), "");
    ASSERT_STR_EQ(sting_color_kind(&c, "evidence"), "");

    /* 2. Always mode: ANSI escape codes present */
    sting_color_set_mode(1);
    ASSERT_EQ_INT(sting_color_get_mode(), 1);
    c = sting_color_get();
    ASSERT_TRUE(c.enabled);
    ASSERT_STR_EQ(c.reset, "\033[0m");
    ASSERT_STR_EQ(c.bold, "\033[1m");
    ASSERT_STR_EQ(c.dim, "\033[2m");
    ASSERT_STR_EQ(c.anchor, "\033[1;36m");
    ASSERT_STR_EQ(c.p1, "\033[1;31m");
    ASSERT_STR_EQ(c.p2, "\033[33m");
    ASSERT_STR_EQ(c.p3, "\033[34m");
    ASSERT_STR_EQ(c.done, "\033[1;32m");
    ASSERT_STR_EQ(c.blocked, "\033[1;31m");
    ASSERT_STR_EQ(c.in_progress, "\033[1;33m");
    ASSERT_STR_EQ(c.pending, "\033[36m");
    ASSERT_STR_EQ(c.hyp, "\033[1;35m");
    ASSERT_STR_EQ(c.evi, "\033[1;32m");
    ASSERT_STR_EQ(c.ref, "\033[1;31m");
    ASSERT_STR_EQ(c.find, "\033[1;33m");
    ASSERT_STR_EQ(c.obs, "\033[36m");

    /* Helper functions return corresponding colors when enabled */
    ASSERT_STR_EQ(sting_color_status(&c, "done"), "\033[1;32m");
    ASSERT_STR_EQ(sting_color_status(&c, "blocked"), "\033[1;31m");
    ASSERT_STR_EQ(sting_color_status(&c, "in_progress"), "\033[1;33m");
    ASSERT_STR_EQ(sting_color_status(&c, "pending"), "\033[36m");

    ASSERT_STR_EQ(sting_color_priority(&c, 1), "\033[1;31m");
    ASSERT_STR_EQ(sting_color_priority(&c, 2), "\033[33m");
    ASSERT_STR_EQ(sting_color_priority(&c, 3), "\033[34m");
    ASSERT_STR_EQ(sting_color_priority(&c, 4), "\033[2m");

    ASSERT_STR_EQ(sting_color_kind(&c, "hypothesis"), "\033[1;35m");
    ASSERT_STR_EQ(sting_color_kind(&c, "evidence"), "\033[1;32m");
    ASSERT_STR_EQ(sting_color_kind(&c, "refutation"), "\033[1;31m");
    ASSERT_STR_EQ(sting_color_kind(&c, "observation"), "\033[36m");
    ASSERT_STR_EQ(sting_color_kind(&c, "finding"), "\033[1;33m");

    ASSERT_STR_EQ(sting_color_severity(&c, "critical"), "\033[1;31m");
    ASSERT_STR_EQ(sting_color_severity(&c, "high"), "\033[1;31m");
    ASSERT_STR_EQ(sting_color_severity(&c, "medium"), "\033[33m");
    ASSERT_STR_EQ(sting_color_severity(&c, "low"), "\033[34m");
    ASSERT_STR_EQ(sting_color_severity(&c, "info"), "\033[2m");

    /* Stream auto-detection check */
    sting_color_t c_stdout = sting_color_get_for_stream(stdout);
    ASSERT_TRUE(c_stdout.enabled); /* mode 1 always */

    /* 3. Auto mode reset */
    sting_color_set_mode(0);
    ASSERT_EQ_INT(sting_color_get_mode(), 0);
}

int main(void) {
    printf("=== Stingtrail Test Suite ===\n");
    RUN_TEST(json_buffer_and_escaping);
    RUN_TEST(guard_destructive_patterns);
    RUN_TEST(guard_scope_validation);
    RUN_TEST(guard_unsafe_controls);
    RUN_TEST(node_deduplication_and_lookup);
    RUN_TEST(reasoning_entries_and_steps);
    RUN_TEST(journal_persistence_and_reload);
    RUN_TEST(state_export_and_board);
    RUN_TEST(hook_semantic_classification_and_destructives);
    RUN_TEST(context_management_and_persistence);
    RUN_TEST(state_focus_traversal_and_json);
    RUN_TEST(terminal_color_subsystem_and_no_color);

    printf("=== Result: %d/%d tests passed ===\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
