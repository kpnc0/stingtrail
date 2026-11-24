#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <math.h>
#include "stingtrail.h"
#include "stingtrail_gui.h"

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

static void init_headless_raven(void) {
    RInitDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.headless = R_TRUE;
    desc.backend = R_BACKEND_NULL;
    ASSERT_TRUE(Rinit_ex(&desc));
}

TEST(headless_dag_build_and_semantic_glow) {
    init_headless_raven();

    sting_state_t st;
    sting_state_init(&st, NULL);

    uint32_t root = sting_state_add_node(&st, "premise", "Target Infrastructure", "Scope Alpha", false);
    uint32_t s1 = sting_state_plan_step(&st, root, 1, "10.0.0.1", "Port scan", "Find services", false, NULL);
    sting_state_update_step(&st, s1, "done", "Ports open", false);

    uint32_t s2 = sting_state_plan_step(&st, root, 2, "10.0.0.2", "Service banner grab", "Identify version", false, NULL);
    sting_state_update_step(&st, s2, "pending", "", false);

    uint32_t s3 = sting_state_plan_step(&st, root, 3, "10.0.0.3", "Exploit attempt", "Fire payload", false, NULL);
    sting_state_update_step(&st, s3, "blocked", "Firewall dropped connection", false);

    Rid graph = Rnode_graph_create();
    int links = sting_gui_build_dag(graph, &st);
    ASSERT_TRUE(links > 0);

    sting_gui_context_t *ctx = sting_gui_get_context();
    ASSERT_TRUE(ctx != NULL);
    ASSERT_EQ_INT(ctx->total_items, 4);

    /* Verify semantic glows */
    for (size_t i = 0; i < ctx->total_items; i++) {
        if (ctx->items[i].id == s1) {
            ASSERT_EQ_INT(ctx->items[i].glow_type, STING_GLOW_GREEN);
            ASSERT_TRUE(fabsf(ctx->items[i].glow_r - STING_GLOW_GREEN_R) < 0.001f);
        } else if (ctx->items[i].id == s2) {
            ASSERT_EQ_INT(ctx->items[i].glow_type, STING_GLOW_ORANGE);
            ASSERT_TRUE(fabsf(ctx->items[i].glow_r - STING_GLOW_ORANGE_R) < 0.001f);
        } else if (ctx->items[i].id == s3) {
            ASSERT_EQ_INT(ctx->items[i].glow_type, STING_GLOW_RED);
            ASSERT_TRUE(fabsf(ctx->items[i].glow_r - STING_GLOW_RED_R) < 0.001f);
        }
    }

    sting_gui_cleanup_context();
    Rshutdown();
}

TEST(read_only_context_menu) {
    init_headless_raven();

    sting_state_t st;
    sting_state_init(&st, NULL);
    sting_state_add_node(&st, "premise", "Read-Only Test", "View check", false);

    Rid graph = Rnode_graph_create();
    sting_gui_build_dag(graph, &st);

    Rid menu = sting_gui_create_context_menu(R_INVALID_ID, graph, &st);
    ASSERT_TRUE(menu != R_INVALID_ID);
    ASSERT_TRUE(sting_gui_get_parent_menu_item() != R_INVALID_ID);
    ASSERT_TRUE(sting_gui_get_random_child_menu_item() != R_INVALID_ID);
    ASSERT_TRUE(sting_gui_get_vertical_menu_item() != R_INVALID_ID);

    /* In read-only GUI, there is NO status modification submenu */

    sting_gui_cleanup_context();
    Rshutdown();
}

TEST(vim_hjkl_keybindings) {
    init_headless_raven();

    sting_state_t st;
    sting_state_init(&st, NULL);

    /* Graph:
     *   Root (#1)
     *     ├── Child1 (#2)
     *     ├── Child2 (#3)
     *     └── Child3 (#4)
     */
    uint32_t root = sting_state_add_node(&st, "premise", "Engagement Root", "Mission", false);
    uint32_t c1 = sting_state_plan_step(&st, root, 1, "10.0.0.1", "Recon Service 1", "Why 1", false, NULL);
    uint32_t c2 = sting_state_plan_step(&st, root, 2, "10.0.0.2", "Recon Service 2", "Why 2", false, NULL);
    uint32_t c3 = sting_state_plan_step(&st, root, 3, "10.0.0.3", "Recon Service 3", "Why 3", false, NULL);

    Rid graph = Rnode_graph_create();
    sting_gui_build_dag(graph, &st);

    sting_gui_context_t *ctx = sting_gui_get_context();
    ASSERT_TRUE(ctx != NULL);

    int r_root = 0, r_c1 = 0, r_c2 = 0, r_c3 = 0;
    for (size_t i = 0; i < ctx->total_items; i++) {
        if (ctx->items[i].id == root) r_root = ctx->items[i].rnode;
        else if (ctx->items[i].id == c1) r_c1 = ctx->items[i].rnode;
        else if (ctx->items[i].id == c2) r_c2 = ctx->items[i].rnode;
        else if (ctx->items[i].id == c3) r_c3 = ctx->items[i].rnode;
    }

    ASSERT_TRUE(r_root != 0 && r_c1 != 0 && r_c2 != 0 && r_c3 != 0);

    /* 1. Test 'l' (first child): Select root -> press 'l' -> jumps to child1 */
    sting_gui_select_node(graph, r_root);
    ASSERT_EQ_INT(ctx->selected_rnode, r_root);

    bool handled = sting_gui_handle_key(graph, (RKey)'l', 0);
    ASSERT_TRUE(handled);
    ASSERT_EQ_INT(ctx->selected_rnode, r_c1);

    /* 2. Test 'h' (parent): Select child1 -> press 'h' -> jumps back to root */
    handled = sting_gui_handle_key(graph, (RKey)'h', 0);
    ASSERT_TRUE(handled);
    ASSERT_EQ_INT(ctx->selected_rnode, r_root);

    /* 3. Test 'j' (forward sibling): Select c1 -> press 'j' -> jumps to c2 */
    sting_gui_select_node(graph, r_c1);
    ASSERT_EQ_INT(ctx->selected_rnode, r_c1);

    handled = sting_gui_handle_key(graph, (RKey)'j', 0);
    ASSERT_TRUE(handled);
    ASSERT_EQ_INT(ctx->selected_rnode, r_c2);

    /* Press 'j' again -> jumps to c3 */
    handled = sting_gui_handle_key(graph, (RKey)'j', 0);
    ASSERT_TRUE(handled);
    ASSERT_EQ_INT(ctx->selected_rnode, r_c3);

    /* 4. Test 'k' (reverse sibling): Select c3 -> press 'k' -> jumps back to c2 */
    handled = sting_gui_handle_key(graph, (RKey)'k', 0);
    ASSERT_TRUE(handled);
    ASSERT_EQ_INT(ctx->selected_rnode, r_c2);

    /* Press 'k' again -> jumps to c1 */
    handled = sting_gui_handle_key(graph, (RKey)'k', 0);
    ASSERT_TRUE(handled);
    ASSERT_EQ_INT(ctx->selected_rnode, r_c1);

    /* Press 'k' at c1 -> wraps around to c3 */
    handled = sting_gui_handle_key(graph, (RKey)'k', 0);
    ASSERT_TRUE(handled);
    ASSERT_EQ_INT(ctx->selected_rnode, r_c3);

    /* 5. Modifier handling: Ctrl/Alt key combos should be ignored */
    handled = sting_gui_handle_key(graph, (RKey)'h', R_MOD_CTRL);
    ASSERT_FALSE(handled);

    sting_gui_cleanup_context();
    Rshutdown();
}

TEST(camera_transition_and_layout_toggles) {
    init_headless_raven();

    sting_state_t st;
    sting_state_init(&st, NULL);
    sting_state_add_node(&st, "premise", "Camera Test", "Details", false);

    Rid graph = Rnode_graph_create();
    sting_gui_build_dag(graph, &st);

    /* Layout toggles */
    ASSERT_FALSE(sting_gui_is_vertical_view());
    sting_gui_set_vertical_view(true);
    ASSERT_TRUE(sting_gui_is_vertical_view());
    sting_gui_toggle_vertical_view(graph, &st);
    ASSERT_FALSE(sting_gui_is_vertical_view());

    /* Camera animation */
    sting_gui_start_camera_transition(graph, 500.0f, 300.0f, 1.5f, 0.2f);
    ASSERT_TRUE(sting_gui_is_animating());

    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
    sting_gui_get_camera_target(&tx, &ty, &tz);
    ASSERT_TRUE(fabsf(tx - 500.0f) < 0.001f);
    ASSERT_TRUE(fabsf(ty - 300.0f) < 0.001f);
    ASSERT_TRUE(fabsf(tz - 1.5f) < 0.001f);

    /* Advance animation to finish */
    sting_gui_update_animation(0.25f);
    ASSERT_FALSE(sting_gui_is_animating());

    sting_gui_cleanup_context();
    Rshutdown();
}

int main(void) {
    printf("=== Stingtrail-GUI Unit & Integration Test Suite ===\n");
    RUN_TEST(headless_dag_build_and_semantic_glow);
    RUN_TEST(read_only_context_menu);
    RUN_TEST(vim_hjkl_keybindings);
    RUN_TEST(camera_transition_and_layout_toggles);

    printf("=== Result: %d/%d tests passed ===\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
