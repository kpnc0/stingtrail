#define _POSIX_C_SOURCE 200809L
#include "stingtrail_gui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <time.h>

static sting_gui_context_t *g_gui_ctx = NULL;
static int g_hovered_rnode = 0;
static bool g_vertical_view = false;
static Rid g_context_menu = R_INVALID_ID;
static Rid g_menu_item_vertical = R_INVALID_ID;
static Rid g_menu_item_parent = R_INVALID_ID;
static Rid g_menu_item_random_child = R_INVALID_ID;
static Rid g_menu_item_first_child = R_INVALID_ID;
static Rid g_current_window = R_INVALID_ID;
static Rid g_current_graph = R_INVALID_ID;
static sting_state_t *g_current_state = NULL;
static int g_context_rnode = 0;
static uint32_t g_focus_id = 0;
static Rid g_menu_item_see_only_this = R_INVALID_ID;
static Rid g_menu_item_see_all = R_INVALID_ID;

/* Camera smooth animation state */
typedef struct {
    bool active;
    float start_cx;
    float start_cy;
    float start_zoom;
    float target_cx;
    float target_cy;
    float target_zoom;
    float elapsed;
    float duration;
} sting_camera_anim_t;

static sting_camera_anim_t g_camera_anim = {0};

/* Forward declarations */
static void wrap_text(const char *in, char *out, size_t out_sz, int max_line);
static void format_card_title(char *out, size_t out_sz, const char *prefix, const char *text, size_t max_text_len);
static void compute_semantic_glows(sting_gui_item_t *items, size_t total_items, const sting_state_t *st);
static const char *get_glow_border_color(sting_glow_type_t t);

void sting_gui_start_camera_transition(Rid graph, float target_cx, float target_cy, float target_zoom, float duration) {
    if (graph == R_INVALID_ID) graph = g_current_graph;
    if (graph == R_INVALID_ID) return;

    float cur_cx = 0.0f, cur_cy = 0.0f, cur_zoom = 1.0f;
    Rnode_graph_get_view(graph, &cur_cx, &cur_cy, &cur_zoom);

    g_camera_anim.start_cx = cur_cx;
    g_camera_anim.start_cy = cur_cy;
    g_camera_anim.start_zoom = cur_zoom;
    g_camera_anim.target_cx = target_cx;
    g_camera_anim.target_cy = target_cy;
    g_camera_anim.target_zoom = target_zoom;
    g_camera_anim.elapsed = 0.0f;
    g_camera_anim.duration = (duration > 0.01f) ? duration : 0.25f;
    g_camera_anim.active = true;
}

bool sting_gui_is_animating(void) {
    return g_camera_anim.active;
}

void sting_gui_update_animation(float dt) {
    if (!g_camera_anim.active) return;
    g_camera_anim.elapsed += dt;
    float t = g_camera_anim.elapsed / g_camera_anim.duration;
    if (t >= 1.0f) {
        t = 1.0f;
        g_camera_anim.active = false;
    }

    /* Smoothstep interpolation: 3t^2 - 2t^3 */
    float smooth_t = t * t * (3.0f - 2.0f * t);

    float cx = g_camera_anim.start_cx + (g_camera_anim.target_cx - g_camera_anim.start_cx) * smooth_t;
    float cy = g_camera_anim.start_cy + (g_camera_anim.target_cy - g_camera_anim.start_cy) * smooth_t;
    float zoom = g_camera_anim.start_zoom + (g_camera_anim.target_zoom - g_camera_anim.start_zoom) * smooth_t;

    if (g_current_graph != R_INVALID_ID) {
        Rnode_graph_set_view(g_current_graph, cx, cy, zoom);
    }
}

void sting_gui_finish_animation(Rid graph) {
    if (!g_camera_anim.active) return;
    if (graph == R_INVALID_ID) graph = g_current_graph;
    if (graph != R_INVALID_ID) {
        Rnode_graph_set_view(graph, g_camera_anim.target_cx, g_camera_anim.target_cy, g_camera_anim.target_zoom);
    }
    g_camera_anim.active = false;
}

void sting_gui_get_camera_target(float *target_x, float *target_y, float *target_zoom) {
    if (target_x) *target_x = g_camera_anim.target_cx;
    if (target_y) *target_y = g_camera_anim.target_cy;
    if (target_zoom) *target_zoom = g_camera_anim.target_zoom;
}

static sting_gui_item_t *find_item_by_rnode(int rnode) {
    if (!g_gui_ctx || rnode == 0) return NULL;
    for (size_t i = 0; i < g_gui_ctx->total_items; i++) {
        if (g_gui_ctx->items[i].rnode == rnode) {
            return &g_gui_ctx->items[i];
        }
    }
    return NULL;
}

static sting_gui_item_t *find_item_by_id(uint32_t id) {
    if (!g_gui_ctx || id == 0) return NULL;
    for (size_t i = 0; i < g_gui_ctx->total_items; i++) {
        if (g_gui_ctx->items[i].id == id) {
            return &g_gui_ctx->items[i];
        }
    }
    return NULL;
}

int sting_gui_get_parent_rnode(int rnode) {
    sting_gui_item_t *cur = find_item_by_rnode(rnode);
    if (!cur || cur->parent_id == 0) return 0;
    sting_gui_item_t *parent = find_item_by_id(cur->parent_id);
    return parent ? parent->rnode : 0;
}

size_t sting_gui_get_children_count(int rnode) {
    sting_gui_item_t *cur = find_item_by_rnode(rnode);
    if (!cur || !g_gui_ctx) return 0;
    size_t count = 0;
    for (size_t i = 0; i < g_gui_ctx->total_items; i++) {
        if (g_gui_ctx->items[i].parent_id == cur->id) {
            count++;
        }
    }
    return count;
}

int sting_gui_get_random_child_rnode(int rnode) {
    sting_gui_item_t *cur = find_item_by_rnode(rnode);
    if (!cur || !g_gui_ctx) return 0;

    int children[64];
    size_t count = 0;
    for (size_t i = 0; i < g_gui_ctx->total_items && count < 64; i++) {
        if (g_gui_ctx->items[i].parent_id == cur->id) {
            children[count++] = g_gui_ctx->items[i].rnode;
        }
    }
    if (count == 0) return 0;
    return children[rand() % count];
}

int sting_gui_get_first_child_rnode(int rnode) {
    sting_gui_item_t *cur = find_item_by_rnode(rnode);
    if (!cur || !g_gui_ctx) return 0;

    for (size_t i = 0; i < g_gui_ctx->total_items; i++) {
        if (g_gui_ctx->items[i].parent_id == cur->id) {
            return g_gui_ctx->items[i].rnode;
        }
    }
    return 0;
}

int sting_gui_get_next_sibling_rnode(int rnode, bool reverse) {
    sting_gui_item_t *cur = find_item_by_rnode(rnode);
    if (!cur || !g_gui_ctx) return 0;

    int siblings[128];
    size_t count = 0;
    int cur_idx = -1;

    for (size_t i = 0; i < g_gui_ctx->total_items && count < 128; i++) {
        if (g_gui_ctx->items[i].parent_id == cur->parent_id) {
            if (g_gui_ctx->items[i].rnode == rnode) {
                cur_idx = (int)count;
            }
            siblings[count++] = g_gui_ctx->items[i].rnode;
        }
    }

    if (count <= 1 || cur_idx < 0) return 0;

    if (reverse) {
        int next_idx = (cur_idx - 1 + (int)count) % (int)count;
        return siblings[next_idx];
    } else {
        int next_idx = (cur_idx + 1) % (int)count;
        return siblings[next_idx];
    }
}

void sting_gui_go_to_node(Rid graph, int rnode) {
    if (graph == R_INVALID_ID) graph = g_current_graph;
    if (graph == R_INVALID_ID || rnode == 0) return;

    sting_gui_select_node(graph, rnode);

    float nx = 0.0f, ny = 0.0f, nw = 260.0f, nh = 120.0f;
    Rnode_graph_get_node_position(graph, rnode, &nx, &ny);
    Rnode_graph_get_node_size(graph, rnode, &nw, &nh);

    float cur_cx = 0.0f, cur_cy = 0.0f, cur_zoom = 1.0f;
    Rnode_graph_get_view(graph, &cur_cx, &cur_cy, &cur_zoom);
    if (cur_zoom <= 0.0f) cur_zoom = 1.0f;

    float target_cx = nx + nw * 0.5f;
    float target_cy = ny + nh * 0.5f;

    sting_gui_start_camera_transition(graph, target_cx, target_cy, cur_zoom, 0.25f);
}

void sting_gui_go_to_parent(Rid graph, int rnode) {
    int p_rnode = sting_gui_get_parent_rnode(rnode);
    if (p_rnode != 0) {
        sting_gui_go_to_node(graph, p_rnode);
    }
}

void sting_gui_go_to_random_child(Rid graph, int rnode) {
    int c_rnode = sting_gui_get_random_child_rnode(rnode);
    if (c_rnode != 0) {
        sting_gui_go_to_node(graph, c_rnode);
    }
}

void sting_gui_go_to_first_child(Rid graph, int rnode) {
    int c_rnode = sting_gui_get_first_child_rnode(rnode);
    if (c_rnode != 0) {
        sting_gui_go_to_node(graph, c_rnode);
    }
}

void sting_gui_go_to_sibling(Rid graph, int rnode, bool reverse) {
    int s_rnode = sting_gui_get_next_sibling_rnode(rnode, reverse);
    if (s_rnode != 0) {
        sting_gui_go_to_node(graph, s_rnode);
    }
}

/* Vim navigation keybindings:
 *   h -> immediate parent (was p)
 *   j -> next sibling (was n)
 *   k -> previous sibling (was N / reverse)
 *   l -> first child (was c)
 */
bool sting_gui_handle_key(Rid graph, RKey key, Ruint modifiers) {
    if (graph == R_INVALID_ID) graph = g_current_graph;
    int target = (g_gui_ctx && g_gui_ctx->selected_rnode != 0) ? g_gui_ctx->selected_rnode : g_context_rnode;
    if (target == 0) return false;

    /* Normalize lowercase ASCII keycodes if passed */
    if (key == (RKey)'h') key = R_KEY_H;
    else if (key == (RKey)'j') key = R_KEY_J;
    else if (key == (RKey)'k') key = R_KEY_K;
    else if (key == (RKey)'l') key = R_KEY_L;

    /* Ignore when Ctrl, Alt, or Super modifiers are held */
    if (modifiers & (R_MOD_CTRL | R_MOD_ALT | R_MOD_SUPER)) {
        return false;
    }

    if (key == R_KEY_H) {
        /* 'h' -> Parent */
        int parent = sting_gui_get_parent_rnode(target);
        if (parent != 0) {
            sting_gui_go_to_node(graph, parent);
            return true;
        }
    } else if (key == R_KEY_J) {
        /* 'j' -> Next sibling (forward) */
        int sibling = sting_gui_get_next_sibling_rnode(target, false);
        if (sibling != 0) {
            sting_gui_go_to_node(graph, sibling);
            return true;
        }
    } else if (key == R_KEY_K) {
        /* 'k' -> Previous sibling (reverse) */
        int sibling = sting_gui_get_next_sibling_rnode(target, true);
        if (sibling != 0) {
            sting_gui_go_to_node(graph, sibling);
            return true;
        }
    } else if (key == R_KEY_L) {
        /* 'l' -> First child */
        int first_child = sting_gui_get_first_child_rnode(target);
        if (first_child != 0) {
            sting_gui_go_to_node(graph, first_child);
            return true;
        }
    }

    return false;
}

static void on_go_to_parent_clicked(Rid item, void *user_data) {
    (void)item; (void)user_data;
    int target = (g_context_rnode != 0) ? g_context_rnode : (g_gui_ctx ? g_gui_ctx->selected_rnode : 0);
    if (target != 0) {
        sting_gui_go_to_parent(g_current_graph, target);
    }
}

static void on_go_to_random_child_clicked(Rid item, void *user_data) {
    (void)item; (void)user_data;
    int target = (g_context_rnode != 0) ? g_context_rnode : (g_gui_ctx ? g_gui_ctx->selected_rnode : 0);
    if (target != 0) {
        sting_gui_go_to_random_child(g_current_graph, target);
    }
}

static void on_go_to_first_child_clicked(Rid item, void *user_data) {
    (void)item; (void)user_data;
    int target = (g_context_rnode != 0) ? g_context_rnode : (g_gui_ctx ? g_gui_ctx->selected_rnode : 0);
    if (target != 0) {
        sting_gui_go_to_first_child(g_current_graph, target);
    }
}

void sting_gui_focus_node(Rid graph, sting_state_t *st, uint32_t focus_id) {
    if (focus_id == 0) return;
    g_focus_id = focus_id;
    if (graph == R_INVALID_ID) graph = g_current_graph;
    if (!st) st = g_current_state;
    if (graph != R_INVALID_ID && st) {
        Rnode_graph_clear(graph);
        sting_gui_build_dag(graph, st);
        Rnode_graph_frame_all(graph);
    }
}

void sting_gui_reset_focus(Rid graph, sting_state_t *st) {
    g_focus_id = 0;
    if (graph == R_INVALID_ID) graph = g_current_graph;
    if (!st) st = g_current_state;
    if (graph != R_INVALID_ID && st) {
        Rnode_graph_clear(graph);
        sting_gui_build_dag(graph, st);
        Rnode_graph_frame_all(graph);
    }
}

uint32_t sting_gui_get_focus_id(void) {
    return g_focus_id;
}

Rid sting_gui_get_see_only_this_menu_item(void) { return g_menu_item_see_only_this; }
Rid sting_gui_get_see_all_menu_item(void) { return g_menu_item_see_all; }

static void on_see_only_this_clicked(Rid item, void *user_data) {
    (void)item; (void)user_data;
    int target = (g_context_rnode != 0) ? g_context_rnode : (g_gui_ctx ? g_gui_ctx->selected_rnode : 0);
    sting_gui_item_t *it = find_item_by_rnode(target);
    if (it && it->id != 0) {
        sting_gui_focus_node(g_current_graph, g_current_state, it->id);
    }
}

static void on_see_all_clicked(Rid item, void *user_data) {
    (void)item; (void)user_data;
    sting_gui_reset_focus(g_current_graph, g_current_state);
}

void sting_gui_set_vertical_view(bool enabled) {
    g_vertical_view = enabled;
    if (g_menu_item_vertical != R_INVALID_ID) {
        Rmenu_item_set_checked(g_menu_item_vertical, g_vertical_view ? 1 : 0);
    }
}

bool sting_gui_is_vertical_view(void) {
    return g_vertical_view;
}

void sting_gui_toggle_vertical_view(Rid graph, sting_state_t *st) {
    g_vertical_view = !g_vertical_view;
    if (g_menu_item_vertical != R_INVALID_ID) {
        Rmenu_item_set_checked(g_menu_item_vertical, g_vertical_view ? 1 : 0);
    }
    if (graph != R_INVALID_ID && st) {
        float cur_cx = 0.0f, cur_cy = 0.0f, cur_zoom = 1.0f;
        Rnode_graph_get_view(graph, &cur_cx, &cur_cy, &cur_zoom);
        Rnode_graph_clear(graph);
        sting_gui_build_dag(graph, st);
        if (cur_zoom > 0.0f) {
            Rnode_graph_set_view(graph, cur_cx, cur_cy, cur_zoom);
        } else {
            Rnode_graph_frame_all(graph);
        }
    }
}

void sting_gui_reload_preserving_zoom(Rid graph, sting_state_t *st) {
    if (graph == R_INVALID_ID || !st) return;

    float cur_cx = 0.0f, cur_cy = 0.0f, cur_zoom = 1.0f;
    Rnode_graph_get_view(graph, &cur_cx, &cur_cy, &cur_zoom);

    uint32_t sel_id = 0;
    if (g_gui_ctx && g_gui_ctx->selected_rnode != 0) {
        for (size_t i = 0; i < g_gui_ctx->total_items; i++) {
            if (g_gui_ctx->items[i].rnode == g_gui_ctx->selected_rnode) {
                sel_id = g_gui_ctx->items[i].id;
                break;
            }
        }
    }

    sting_state_reload(st);
    Rnode_graph_clear(graph);
    sting_gui_build_dag(graph, st);

    if (cur_zoom > 0.0f) {
        Rnode_graph_set_view(graph, cur_cx, cur_cy, cur_zoom);
    } else {
        Rnode_graph_frame_all(graph);
    }

    if (sel_id != 0 && g_gui_ctx) {
        for (size_t i = 0; i < g_gui_ctx->total_items; i++) {
            if (g_gui_ctx->items[i].id == sel_id) {
                g_gui_ctx->selected_rnode = g_gui_ctx->items[i].rnode;
                sting_gui_highlight_chain(graph, g_gui_ctx, g_gui_ctx->items[i].rnode);
                Rnode_graph_select_node(graph, g_gui_ctx->items[i].rnode, 0);
                break;
            }
        }
    }
}

static void on_toggle_vertical_view_clicked(Rid item, void *user_data) {
    (void)item; (void)user_data;
    sting_gui_toggle_vertical_view(g_current_graph, g_current_state);
}

static void on_frame_all_clicked(Rid item, void *user_data) {
    (void)item; (void)user_data;
    if (g_current_graph != R_INVALID_ID) {
        Rnode_graph_frame_all(g_current_graph);
    }
}

static void on_graph_context_menu(Rid graph, int node, float x, float y, void *user_data) {
    (void)graph;
    Rid win = (Rid)(uintptr_t)user_data;
    if (win == R_INVALID_ID) win = g_current_window;

    g_context_rnode = node;

    if (node != 0) {
        sting_gui_select_node(graph, node);
    }

    if (g_menu_item_vertical != R_INVALID_ID) {
        Rmenu_item_set_checked(g_menu_item_vertical, g_vertical_view ? 1 : 0);
    }

    int parent_rnode = (node != 0) ? sting_gui_get_parent_rnode(node) : 0;
    size_t child_count = (node != 0) ? sting_gui_get_children_count(node) : 0;

    if (g_menu_item_parent != R_INVALID_ID) {
        Rwidget_set_enabled(g_menu_item_parent, parent_rnode != 0 ? 1 : 0);
    }
    if (g_menu_item_first_child != R_INVALID_ID) {
        Rwidget_set_enabled(g_menu_item_first_child, child_count > 0 ? 1 : 0);
    }
    if (g_menu_item_random_child != R_INVALID_ID) {
        Rwidget_set_enabled(g_menu_item_random_child, child_count > 0 ? 1 : 0);
    }
    if (g_menu_item_see_only_this != R_INVALID_ID) {
        Rwidget_set_enabled(g_menu_item_see_only_this, (node != 0) ? 1 : 0);
    }
    if (g_menu_item_see_all != R_INVALID_ID) {
        Rwidget_set_enabled(g_menu_item_see_all, (g_focus_id != 0) ? 1 : 0);
    }

    if (g_context_menu != R_INVALID_ID && win != R_INVALID_ID) {
        Rmenu_open(g_context_menu, win, x, y);
    }
}

void sting_gui_open_context_menu_for_node(Rid graph, int node, float x, float y) {
    on_graph_context_menu(graph, node, x, y, (void *)(uintptr_t)g_current_window);
}

/* Build Read-Only context menu: navigation and view controls only (NO status mutations) */
Rid sting_gui_create_context_menu(Rid window, Rid graph, sting_state_t *st) {
    g_current_window = window;
    g_current_graph = graph;
    g_current_state = st;

    g_context_menu = Rwidget_create(R_WIDGET_MENU);

    g_menu_item_parent = Rmenu_add_item(g_context_menu, "Go to Parent (h)");
    Rmenu_item_set_clicked_callback(g_menu_item_parent, on_go_to_parent_clicked, NULL);

    g_menu_item_first_child = Rmenu_add_item(g_context_menu, "Go to First Child (l)");
    Rmenu_item_set_clicked_callback(g_menu_item_first_child, on_go_to_first_child_clicked, NULL);

    g_menu_item_random_child = Rmenu_add_item(g_context_menu, "Go to Random Child");
    Rmenu_item_set_clicked_callback(g_menu_item_random_child, on_go_to_random_child_clicked, NULL);

    Rmenu_add_separator(g_context_menu);

    g_menu_item_vertical = Rmenu_add_item(g_context_menu, "Vertical View");
    Rmenu_item_set_checkable(g_menu_item_vertical, 1);
    Rmenu_item_set_checked(g_menu_item_vertical, g_vertical_view ? 1 : 0);
    Rmenu_item_set_clicked_callback(g_menu_item_vertical, on_toggle_vertical_view_clicked, NULL);

    Rmenu_add_separator(g_context_menu);
    Rid item_frame = Rmenu_add_item(g_context_menu, "Center / Frame All");
    Rmenu_item_set_clicked_callback(item_frame, on_frame_all_clicked, NULL);

    Rmenu_add_separator(g_context_menu);
    g_menu_item_see_only_this = Rmenu_add_item(g_context_menu, "See only this");
    Rmenu_item_set_clicked_callback(g_menu_item_see_only_this, on_see_only_this_clicked, NULL);

    g_menu_item_see_all = Rmenu_add_item(g_context_menu, "See all");
    Rmenu_item_set_clicked_callback(g_menu_item_see_all, on_see_all_clicked, NULL);

    if (graph != R_INVALID_ID) {
        Rwidget_set_context_menu(graph, g_context_menu);
        Rnode_graph_set_context_callback(graph, on_graph_context_menu, (void *)(uintptr_t)window);
    }

    return g_context_menu;
}

Rid sting_gui_get_context_menu(void) { return g_context_menu; }
Rid sting_gui_get_vertical_menu_item(void) { return g_menu_item_vertical; }
Rid sting_gui_get_parent_menu_item(void) { return g_menu_item_parent; }
Rid sting_gui_get_random_child_menu_item(void) { return g_menu_item_random_child; }

static const char *get_glow_border_color(sting_glow_type_t t) {
    switch (t) {
        case STING_GLOW_GREEN:  return "#29AB87";
        case STING_GLOW_ORANGE: return "#FF9500";
        case STING_GLOW_RED:    return "#FF3B30";
				case STING_GLOW_BLUE:		return "#016064";
				case STING_GLOW_PINK:		return "#FC46AA";	
        case STING_GLOW_NONE:
        default:                return "#333333";
    }
}

static void wrap_text(const char *in, char *out, size_t out_sz, int max_line) {
    if (!in || !out || out_sz == 0) return;
    size_t in_len = strlen(in);
    size_t out_idx = 0;
    int line_len = 0;
    for (size_t i = 0; i < in_len && out_idx + 2 < out_sz; i++) {
        char c = in[i];
        if (c == '\n') {
            out[out_idx++] = '\n';
            line_len = 0;
        } else if (line_len >= max_line && (c == ' ' || c == '\t')) {
            out[out_idx++] = '\n';
            line_len = 0;
        } else {
            out[out_idx++] = c;
            line_len++;
        }
    }
    out[out_idx] = '\0';
}

static void format_card_title(char *out, size_t out_sz, const char *prefix, const char *text, size_t max_text_len) {
    if (!text) text = "";
    size_t tlen = strlen(text);
    if (tlen > max_text_len) {
        snprintf(out, out_sz, "%s %.*s...", prefix ? prefix : "", (int)max_text_len, text);
    } else {
        snprintf(out, out_sz, "%s %s", prefix ? prefix : "", text);
    }
}

static void compute_semantic_glows(sting_gui_item_t *items, size_t total_items, const sting_state_t *st) {
    if (!items || !st) return;

    for (size_t i = 0; i < total_items; i++) {
        items[i].glow_type = STING_GLOW_NONE;
        items[i].glow_r = 0.5f;
        items[i].glow_g = 0.5f;
        items[i].glow_b = 0.5f;

        uint32_t id = items[i].id;
        for (size_t s = 0; s < st->step_count; s++) {
            if (st->steps[s].id == id) {
                if (strcmp(st->steps[s].status, "done") == 0) {
                    items[i].glow_type = STING_GLOW_GREEN;
                    items[i].glow_r = STING_GLOW_GREEN_R;
                    items[i].glow_g = STING_GLOW_GREEN_G;
                    items[i].glow_b = STING_GLOW_GREEN_B;
                } else if (strcmp(st->steps[s].status, "blocked") == 0) {
                    items[i].glow_type = STING_GLOW_RED;
                    items[i].glow_r = STING_GLOW_RED_R;
                    items[i].glow_g = STING_GLOW_RED_G;
                    items[i].glow_b = STING_GLOW_RED_B;
                } else {
                    items[i].glow_type = STING_GLOW_ORANGE;
                    items[i].glow_r = STING_GLOW_ORANGE_R;
                    items[i].glow_g = STING_GLOW_ORANGE_G;
                    items[i].glow_b = STING_GLOW_ORANGE_B;
                }
                break;
            }
        }

        if (items[i].glow_type == STING_GLOW_NONE) {
            for (size_t e = 0; e < st->entry_count; e++) {
                if (st->entries[e].id == id) {
                    if (strcmp(st->entries[e].kind, "refutation") == 0) {
                        items[i].glow_type = STING_GLOW_RED;
                        items[i].glow_r = STING_GLOW_RED_R;
                        items[i].glow_g = STING_GLOW_RED_G;
                        items[i].glow_b = STING_GLOW_RED_B;
                    } else {
												items[i].glow_type = STING_GLOW_PINK;
												items[i].glow_r = STING_GLOW_PINK_R;
												items[i].glow_g = STING_GLOW_PINK_G;
												items[i].glow_b = STING_GLOW_PINK_B;
										}
                    break;
                }
            }
        }

        if (items[i].glow_type == STING_GLOW_NONE) {
            bool has_children = false;
            for (size_t c = 0; c < total_items; c++) {
                if (items[c].parent_id == id) {
                    has_children = true;
                    break;
                }
            }
            if (!has_children) {
                items[i].glow_type = STING_GLOW_RED;
                items[i].glow_r = STING_GLOW_RED_R;
                items[i].glow_g = STING_GLOW_RED_G;
                items[i].glow_b = STING_GLOW_RED_B;
            }
        }
    }
}

static void apply_chain_highlight(Rid graph, sting_gui_context_t *ctx, int active_rnode, bool immediate_only) {
    if (!ctx || ctx->total_items == 0) return;
    sting_gui_item_t *items = ctx->items;
    size_t total = ctx->total_items;

    if (active_rnode == 0) {
        if (!immediate_only) {
            ctx->selected_rnode = 0;
            g_context_rnode = 0;
        }
        ctx->hovered_rnode = 0;
        for (size_t i = 0; i < total; i++) {
            items[i].highlighted = 1;
            Rnode_graph_set_node_active(graph, items[i].rnode, 0);
            Rnode_graph_set_node_color(graph, items[i].rnode, items[i].orig_r, items[i].orig_g, items[i].orig_b);
            Rnode_graph_set_node_badge_color(graph, items[i].rnode, items[i].orig_badge_r, items[i].orig_badge_g, items[i].orig_badge_b);
            Rnode_graph_set_node_style(graph, items[i].rnode, "BorderColor", get_glow_border_color(items[i].glow_type));
            Rnode_graph_set_node_style(graph, items[i].rnode, "Glow", (items[i].glow_type != STING_GLOW_NONE) ? "1" : "0");
            Rnode_graph_set_node_active_color(graph, items[i].rnode, items[i].glow_r, items[i].glow_g, items[i].glow_b);

            if (items[i].out_pin > 0) {
                Rnode_graph_set_pin_emphasis(graph, items[i].out_pin, 0);
                Rnode_graph_set_pin_style(graph, items[i].out_pin, "Color", "#ffffff");
                Rnode_graph_set_pin_style(graph, items[i].out_pin, "Glow", "0");
            }
            if (items[i].in_pin > 0) {
                Rnode_graph_set_pin_emphasis(graph, items[i].in_pin, 0);
                Rnode_graph_set_pin_style(graph, items[i].in_pin, "Color", "#ffffff");
                Rnode_graph_set_pin_style(graph, items[i].in_pin, "Glow", "0");
            }
        }
        return;
    }

    uint8_t *in_chain = (uint8_t *)calloc(total, sizeof(uint8_t));
    if (!in_chain) return;

    int active_idx = -1;
    for (size_t i = 0; i < total; i++) {
        if (items[i].rnode == active_rnode) {
            active_idx = (int)i;
            in_chain[i] = 1;
            break;
        }
    }
    if (active_idx == -1) {
        free(in_chain);
        return;
    }

    if (immediate_only) {
        uint32_t active_pid = items[active_idx].parent_id;
        uint32_t active_id = items[active_idx].id;
        for (size_t i = 0; i < total; i++) {
            if ((active_pid != 0 && items[i].id == active_pid) || (items[i].parent_id == active_id)) {
                in_chain[i] = 1;
            }
        }
    } else {
        /* Full bidirectional lineage */
        uint32_t cur_parent = items[active_idx].parent_id;
        while (cur_parent != 0) {
            bool found_parent = false;
            for (size_t i = 0; i < total; i++) {
                if (items[i].id == cur_parent) {
                    in_chain[i] = 1;
                    cur_parent = items[i].parent_id;
                    found_parent = true;
                    break;
                }
            }
            if (!found_parent) break;
        }

        bool changed = true;
        while (changed) {
            changed = false;
            for (size_t i = 0; i < total; i++) {
                if (in_chain[i]) continue;
                if (items[i].parent_id != 0) {
                    for (size_t j = 0; j < total; j++) {
                        if (in_chain[j] && items[j].id == items[i].parent_id) {
                            in_chain[i] = 1;
                            changed = true;
                            break;
                        }
                    }
                }
            }
        }
    }

    if (immediate_only) {
        ctx->hovered_rnode = active_rnode;
    } else {
        ctx->selected_rnode = active_rnode;
    }

    for (size_t i = 0; i < total; i++) {
        items[i].highlighted = in_chain[i];
        if (in_chain[i]) {
            Rnode_graph_set_node_active(graph, items[i].rnode, 1);
            if (items[i].rnode == active_rnode) {
                Rnode_graph_set_node_active_color(graph, items[i].rnode, 1.0f, 1.0f, 1.0f);
                Rnode_graph_set_node_style(graph, items[i].rnode, "BorderColor", "#ffffff");
            } else {
                Rnode_graph_set_node_active_color(graph, items[i].rnode, items[i].glow_r, items[i].glow_g, items[i].glow_b);
                Rnode_graph_set_node_style(graph, items[i].rnode, "BorderColor", get_glow_border_color(items[i].glow_type));
            }
            Rnode_graph_set_node_style(graph, items[i].rnode, "Glow", "1");
            Rnode_graph_set_node_color(graph, items[i].rnode, 0.22f, 0.22f, 0.22f);
            Rnode_graph_set_node_badge_color(graph, items[i].rnode, 0.32f, 0.32f, 0.32f);
        } else {
            Rnode_graph_set_node_active(graph, items[i].rnode, 0);
            Rnode_graph_set_node_style(graph, items[i].rnode, "BorderColor", "#1a1a1a");
            Rnode_graph_set_node_style(graph, items[i].rnode, "Glow", "0");
            Rnode_graph_set_node_color(graph, items[i].rnode, 0.01f, 0.01f, 0.01f);
            Rnode_graph_set_node_badge_color(graph, items[i].rnode, 0.02f, 0.02f, 0.02f);
        }
    }

    for (size_t i = 0; i < total; i++) {
        if (items[i].out_pin > 0) {
            bool out_in_chain = false;
            if (in_chain[i]) {
                for (size_t j = 0; j < total; j++) {
                    if (in_chain[j] && items[j].parent_id == items[i].id) {
                        out_in_chain = true;
                        break;
                    }
                }
            }
            if (out_in_chain) {
                Rnode_graph_set_pin_emphasis(graph, items[i].out_pin, 1);
                Rnode_graph_set_pin_style(graph, items[i].out_pin, "Color", "#ffffff");
                Rnode_graph_set_pin_style(graph, items[i].out_pin, "Glow", "1");
            } else {
                Rnode_graph_set_pin_emphasis(graph, items[i].out_pin, -1);
                Rnode_graph_set_pin_style(graph, items[i].out_pin, "Color", "#222222");
                Rnode_graph_set_pin_style(graph, items[i].out_pin, "Glow", "0");
            }
        }

        if (items[i].in_pin > 0) {
            bool in_in_chain = false;
            if (in_chain[i] && items[i].parent_id != 0) {
                for (size_t j = 0; j < total; j++) {
                    if (in_chain[j] && items[j].id == items[i].parent_id) {
                        in_in_chain = true;
                        break;
                    }
                }
            }
            if (in_in_chain) {
                Rnode_graph_set_pin_emphasis(graph, items[i].in_pin, 1);
                Rnode_graph_set_pin_style(graph, items[i].in_pin, "Color", "#ffffff");
                Rnode_graph_set_pin_style(graph, items[i].in_pin, "Glow", "1");
            } else {
                Rnode_graph_set_pin_emphasis(graph, items[i].in_pin, -1);
                Rnode_graph_set_pin_style(graph, items[i].in_pin, "Color", "#222222");
                Rnode_graph_set_pin_style(graph, items[i].in_pin, "Glow", "0");
            }
        }
    }

    free(in_chain);
}

void sting_gui_highlight_chain(Rid graph, sting_gui_context_t *ctx, int selected_rnode) {
    if (ctx) ctx->selected_rnode = selected_rnode;
    apply_chain_highlight(graph, ctx, selected_rnode, false);
}

void sting_gui_highlight_immediate(Rid graph, sting_gui_context_t *ctx, int hovered_rnode) {
    g_hovered_rnode = hovered_rnode;
    apply_chain_highlight(graph, ctx, hovered_rnode, true);
}

static void sting_gui_on_selection_callback(Rid graph, int item, void *user_data) {
    sting_gui_context_t *ctx = (sting_gui_context_t *)user_data;
    if (!ctx) return;

    if (item == 0 || item == ctx->selected_rnode) {
        ctx->selected_rnode = 0;
        g_context_rnode = 0;
        sting_gui_highlight_chain(graph, ctx, 0);
    } else {
        ctx->selected_rnode = item;
        g_context_rnode = item;
        sting_gui_highlight_chain(graph, ctx, item);
    }
}

int sting_gui_handle_event(Rid widget, const REvent *event, void *user_data) {
    (void)user_data;
    if (!event || !g_gui_ctx) return R_FALSE;
    if (event->type == R_EVENT_KEY_DOWN) {
        if (sting_gui_handle_key(widget, event->key, event->modifiers)) {
            return R_TRUE;
        }
    }
    if (event->type == R_EVENT_MOUSE_LEAVE) {
        if (g_gui_ctx->selected_rnode == 0 && (g_hovered_rnode != 0 || g_gui_ctx->hovered_rnode != 0)) {
            g_hovered_rnode = 0;
            g_gui_ctx->hovered_rnode = 0;
            sting_gui_highlight_immediate(widget, g_gui_ctx, 0);
        }
        return R_FALSE;
    }
    if (event->type == R_EVENT_MOUSE_MOVE) {
        if (g_gui_ctx->selected_rnode != 0) return R_FALSE;

        int node = Rnode_graph_get_node_at(widget, event->local_x, event->local_y);
        if (node != g_hovered_rnode) {
            g_hovered_rnode = node;
            g_gui_ctx->hovered_rnode = node;
            sting_gui_highlight_immediate(widget, g_gui_ctx, node);
        }
        return R_FALSE;
    }
    return R_FALSE;
}

void sting_gui_select_node(Rid graph, int rnode) {
    if (!g_gui_ctx || rnode == 0) return;
    g_gui_ctx->selected_rnode = rnode;
    g_context_rnode = rnode;
    Rnode_graph_select_node(graph, rnode, 0);
    sting_gui_highlight_chain(graph, g_gui_ctx, rnode);
}

sting_gui_context_t *sting_gui_get_context(void) {
    return g_gui_ctx;
}

void sting_gui_cleanup_context(void) {
    if (g_gui_ctx) {
        if (g_gui_ctx->items) free(g_gui_ctx->items);
        free(g_gui_ctx);
        g_gui_ctx = NULL;
    }
}

static bool is_in_reach(uint32_t id, const uint32_t *arr, size_t count, uint32_t focus_id) {
    if (focus_id == 0) return true;
    for (size_t i = 0; i < count; i++) {
        if (arr[i] == id) return true;
    }
    return false;
}

int sting_gui_build_dag(Rid graph, sting_state_t *st) {
    if (!st || graph == R_INVALID_ID) return 0;

    size_t total_items = st->node_count + st->entry_count + st->step_count;
    if (total_items == 0) return 0;

    /* Compute reachability if focused subtree filter is active */
    uint32_t reachable[1024];
    size_t reach_count = 0;
    if (g_focus_id != 0) {
        reachable[reach_count++] = g_focus_id;
        for (int pass = 0; pass < 32; pass++) {
            bool added = false;
            for (size_t i = 0; i < st->entry_count; i++) {
                uint32_t id = st->entries[i].id;
                uint32_t pid = st->entries[i].parent_id;
                bool has = false;
                for (size_t k = 0; k < reach_count; k++) { if (reachable[k] == id) { has = true; break; } }
                if (has) continue;
                for (size_t k = 0; k < reach_count; k++) {
                    if (reachable[k] == pid) {
                        if (reach_count < 1024) { reachable[reach_count++] = id; added = true; }
                        break;
                    }
                }
            }
            for (size_t i = 0; i < st->step_count; i++) {
                uint32_t id = st->steps[i].id;
                uint32_t pid = st->steps[i].parent_id;
                bool has = false;
                for (size_t k = 0; k < reach_count; k++) { if (reachable[k] == id) { has = true; break; } }
                if (has) continue;
                for (size_t k = 0; k < reach_count; k++) {
                    if (reachable[k] == pid) {
                        if (reach_count < 1024) { reachable[reach_count++] = id; added = true; }
                        break;
                    }
                }
            }
            if (!added) break;
        }
    }

    size_t matching_items = 0;
    for (size_t i = 0; i < st->node_count; i++) {
        if (is_in_reach(st->nodes[i].id, reachable, reach_count, g_focus_id)) matching_items++;
    }
    for (size_t i = 0; i < st->entry_count; i++) {
        if (is_in_reach(st->entries[i].id, reachable, reach_count, g_focus_id)) matching_items++;
    }
    for (size_t i = 0; i < st->step_count; i++) {
        if (is_in_reach(st->steps[i].id, reachable, reach_count, g_focus_id)) matching_items++;
    }

    if (matching_items == 0) {
        g_focus_id = 0;
        matching_items = total_items;
    }

    total_items = matching_items;

    if (g_gui_ctx) sting_gui_cleanup_context();

    g_current_graph = graph;
    g_current_state = st;

    g_gui_ctx = (sting_gui_context_t *)calloc(1, sizeof(sting_gui_context_t));
    if (!g_gui_ctx) return 0;

    g_gui_ctx->graph = graph;
    g_gui_ctx->total_items = total_items;
    g_gui_ctx->items = (sting_gui_item_t *)calloc(total_items, sizeof(sting_gui_item_t));
    if (!g_gui_ctx->items) {
        free(g_gui_ctx);
        g_gui_ctx = NULL;
        return 0;
    }

    sting_gui_item_t *items = g_gui_ctx->items;
    size_t idx = 0;

    for (size_t i = 0; i < st->node_count; i++) {
        if (!is_in_reach(st->nodes[i].id, reachable, reach_count, g_focus_id)) continue;
        items[idx].id = st->nodes[i].id;
        items[idx].parent_id = 0;
        items[idx].depth = 0;
        items[idx].highlighted = 1;
        items[idx].orig_r = 0.0f;
        items[idx].orig_g = 0.0f;
        items[idx].orig_b = 0.0f;
        items[idx].orig_badge_r = 0.0f;
        items[idx].orig_badge_g = 0.0f;
        items[idx].orig_badge_b = 0.0f;
        idx++;
    }
    for (size_t i = 0; i < st->entry_count; i++) {
        if (!is_in_reach(st->entries[i].id, reachable, reach_count, g_focus_id)) continue;
        items[idx].id = st->entries[i].id;
        items[idx].parent_id = (st->entries[i].id == g_focus_id) ? 0 : st->entries[i].parent_id;
        items[idx].depth = (items[idx].parent_id == 0) ? 0 : 1;
        items[idx].highlighted = 1;
        items[idx].orig_r = 0.08f;
        items[idx].orig_g = 0.08f;
        items[idx].orig_b = 0.08f;
        items[idx].orig_badge_r = 0.12f;
        items[idx].orig_badge_g = 0.12f;
        items[idx].orig_badge_b = 0.12f;
        idx++;
    }
    for (size_t i = 0; i < st->step_count; i++) {
        if (!is_in_reach(st->steps[i].id, reachable, reach_count, g_focus_id)) continue;
        items[idx].id = st->steps[i].id;
        items[idx].parent_id = (st->steps[i].id == g_focus_id) ? 0 : st->steps[i].parent_id;
        items[idx].depth = (items[idx].parent_id == 0) ? 0 : 2;
        items[idx].highlighted = 1;
        items[idx].orig_r = 0.05f;
        items[idx].orig_g = 0.05f;
        items[idx].orig_b = 0.05f;
        items[idx].orig_badge_r = 0.10f;
        items[idx].orig_badge_g = 0.10f;
        items[idx].orig_badge_b = 0.10f;
        idx++;
    }

    /* Iterative topological depth calculation */
    for (int iter = 0; iter < 30; iter++) {
        bool changed = false;
        for (size_t i = 0; i < total_items; i++) {
            uint32_t pid = items[i].parent_id;
            if (pid == 0) continue;
            for (size_t j = 0; j < total_items; j++) {
                if (items[j].id == pid && items[i].depth <= items[j].depth) {
                    items[i].depth = items[j].depth + 1;
                    changed = true;
                    break;
                }
            }
        }
        if (!changed) break;
    }

    compute_semantic_glows(items, total_items, st);

    Rnode_graph_set_type_color(graph, "flow", 1.0f, 1.0f, 1.0f);
    Rnode_graph_set_type_style(graph, "flow", "LinkShape", "Step");

    /* Add node cards into Raven graph */
    for (size_t i = 0; i < total_items; i++) {
        uint32_t cid = items[i].id;

        /* 1. Node check */
        const sting_node_t *node_ptr = NULL;
        for (size_t n = 0; n < st->node_count; n++) {
            if (st->nodes[n].id == cid) { node_ptr = &st->nodes[n]; break; }
        }
        if (node_ptr) {
            char prefix[64];
            snprintf(prefix, sizeof(prefix), "[%s]", node_ptr->kind);
            char title[180];
            format_card_title(title, sizeof(title), prefix, node_ptr->name, 45);

            int rn = Rnode_graph_add_node(graph, title, 0, 0);
            Rnode_graph_set_node_color(graph, rn, items[i].orig_r, items[i].orig_g, items[i].orig_b);

            char badge[32];
            snprintf(badge, sizeof(badge), "#%u", node_ptr->id);
            Rnode_graph_set_node_badge(graph, rn, badge);
            Rnode_graph_set_node_badge_color(graph, rn, items[i].orig_badge_r, items[i].orig_badge_g, items[i].orig_badge_b);

            items[i].rnode = rn;
            items[i].out_pin = Rnode_graph_add_output(graph, rn, "out", "flow");

            char raw_detail[1024];
            snprintf(raw_detail, sizeof(raw_detail), "%s%s%s%sCreated: %s\nModified: %s",
                     node_ptr->name,
                     node_ptr->detail[0] ? "\n" : "",
                     node_ptr->detail[0] ? node_ptr->detail : "",
                     node_ptr->detail[0] ? "\n" : "\n",
                     node_ptr->created[0] ? node_ptr->created : "-",
                     node_ptr->modified[0] ? node_ptr->modified : "-");
            char wrapped[1200];
            wrap_text(raw_detail, wrapped, sizeof(wrapped), 48);

            Rid lbl = Rlabel_create(wrapped);
            Rnode_graph_set_node_content(graph, rn, lbl);
            Rnode_graph_get_node_size(graph, rn, &items[i].w, &items[i].h);

            Rnode_graph_set_node_active(graph, rn, 1);
            Rnode_graph_set_node_active_color(graph, rn, items[i].glow_r, items[i].glow_g, items[i].glow_b);
            Rnode_graph_set_node_style(graph, rn, "BorderColor", get_glow_border_color(items[i].glow_type));
            Rnode_graph_set_node_style(graph, rn, "Glow", "1");
            continue;
        }

        /* 2. Entry check */
        const sting_entry_t *entry_ptr = NULL;
        for (size_t e = 0; e < st->entry_count; e++) {
            if (st->entries[e].id == cid) { entry_ptr = &st->entries[e]; break; }
        }
        if (entry_ptr) {
            char prefix[64];
            snprintf(prefix, sizeof(prefix), "[%s]", entry_ptr->kind);
            char title[180];
            format_card_title(title, sizeof(title), prefix, entry_ptr->summary, 45);

            int rn = Rnode_graph_add_node(graph, title, 0, 0);
            Rnode_graph_set_node_color(graph, rn, items[i].orig_r, items[i].orig_g, items[i].orig_b);

            char badge[32];
            snprintf(badge, sizeof(badge), "#%u [%s]", entry_ptr->id, entry_ptr->severity);
            Rnode_graph_set_node_badge(graph, rn, badge);
            Rnode_graph_set_node_badge_color(graph, rn, items[i].orig_badge_r, items[i].orig_badge_g, items[i].orig_badge_b);

            items[i].rnode = rn;
            if (items[i].parent_id != 0) {
                items[i].in_pin = Rnode_graph_add_input(graph, rn, "in", "flow");
            }
            items[i].out_pin = Rnode_graph_add_output(graph, rn, "out", "flow");

            char raw_detail[1500];
            snprintf(raw_detail, sizeof(raw_detail), "%s%s%s%sCreated: %s\nModified: %s",
                     entry_ptr->summary,
                     entry_ptr->detail[0] ? "\n" : "",
                     entry_ptr->detail[0] ? entry_ptr->detail : "",
                     entry_ptr->detail[0] ? "\n" : "\n",
                     entry_ptr->created[0] ? entry_ptr->created : "-",
                     entry_ptr->modified[0] ? entry_ptr->modified : "-");
            char wrapped[1700];
            wrap_text(raw_detail, wrapped, sizeof(wrapped), 48);

            Rid lbl = Rlabel_create(wrapped);
            Rnode_graph_set_node_content(graph, rn, lbl);
            Rnode_graph_get_node_size(graph, rn, &items[i].w, &items[i].h);

            Rnode_graph_set_node_active(graph, rn, 1);
            Rnode_graph_set_node_active_color(graph, rn, items[i].glow_r, items[i].glow_g, items[i].glow_b);
            Rnode_graph_set_node_style(graph, rn, "BorderColor", get_glow_border_color(items[i].glow_type));
            Rnode_graph_set_node_style(graph, rn, "Glow", "1");
            continue;
        }

        /* 3. Step check */
        const sting_step_t *step_ptr = NULL;
        for (size_t s = 0; s < st->step_count; s++) {
            if (st->steps[s].id == cid) { step_ptr = &st->steps[s]; break; }
        }
        if (step_ptr) {
            char prefix[64];
            snprintf(prefix, sizeof(prefix), "[P%d]", step_ptr->priority);
            char title[180];
            format_card_title(title, sizeof(title), prefix, step_ptr->action, 45);

            int rn = Rnode_graph_add_node(graph, title, 0, 0);
            Rnode_graph_set_node_color(graph, rn, items[i].orig_r, items[i].orig_g, items[i].orig_b);

            char badge[32];
            snprintf(badge, sizeof(badge), "#%u [%s]", step_ptr->id, step_ptr->status);
            Rnode_graph_set_node_badge(graph, rn, badge);
            Rnode_graph_set_node_badge_color(graph, rn, items[i].orig_badge_r, items[i].orig_badge_g, items[i].orig_badge_b);

            items[i].rnode = rn;
            if (items[i].parent_id != 0) {
                items[i].in_pin = Rnode_graph_add_input(graph, rn, "in", "flow");
            }
            items[i].out_pin = Rnode_graph_add_output(graph, rn, "out", "flow");

            char raw_detail[2048];
            snprintf(raw_detail, sizeof(raw_detail), "Action: %s\nWhy: %s%s%s%s%s\nCreated: %s\nModified: %s",
                     step_ptr->action,
                     step_ptr->rationale,
                     step_ptr->target[0] ? "\nTarget: " : "",
                     step_ptr->target[0] ? step_ptr->target : "",
                     step_ptr->outcome[0] ? "\nOutcome: " : "",
                     step_ptr->outcome[0] ? step_ptr->outcome : "",
                     step_ptr->created[0] ? step_ptr->created : "-",
                     step_ptr->modified[0] ? step_ptr->modified : "-");
            char wrapped[2200];
            wrap_text(raw_detail, wrapped, sizeof(wrapped), 48);

            Rid lbl = Rlabel_create(wrapped);
            Rnode_graph_set_node_content(graph, rn, lbl);
            Rnode_graph_get_node_size(graph, rn, &items[i].w, &items[i].h);

            Rnode_graph_set_node_active(graph, rn, 1);
            Rnode_graph_set_node_active_color(graph, rn, items[i].glow_r, items[i].glow_g, items[i].glow_b);
            Rnode_graph_set_node_style(graph, rn, "BorderColor", get_glow_border_color(items[i].glow_type));
            Rnode_graph_set_node_style(graph, rn, "Glow", "1");
        }
    }

    /* 4. Measure geometry */
    int max_depth = 0;
    for (size_t i = 0; i < total_items; i++) {
        float nw = 260.0f, nh = 120.0f;
        Rnode_graph_get_node_size(graph, items[i].rnode, &nw, &nh);
        if (nw < 240.0f) nw = 240.0f;
        if (nh < 80.0f) nh = 80.0f;
        items[i].w = nw;
        items[i].h = nh;
        if (items[i].depth > max_depth) max_depth = items[i].depth;
    }

    const float SIBLING_MIN_GAP_Y = 28.0f;
    const float SIBLING_MIN_GAP_X = 36.0f;

    /* 5. Layout positioning (Vertical vs Horizontal) */
    if (g_vertical_view) {
        float row_max_h[64];
        for (int d = 0; d < 64; d++) row_max_h[d] = 100.0f;

        for (size_t i = 0; i < total_items; i++) {
            int d = items[i].depth;
            if (d >= 64) d = 63;
            if (items[i].h > row_max_h[d]) row_max_h[d] = items[i].h;
        }

        float row_y[64];
        float cur_y = 50.0f;
        const float GAP_Y = 70.0f;
        const float GAP_X = 30.0f;

        for (int d = 0; d <= max_depth; d++) {
            row_y[d] = cur_y;
            cur_y += row_max_h[d] + GAP_Y;
        }

        for (int d = 0; d <= max_depth; d++) {
            float cur_x = 40.0f;
            int last_placed_idx = -1;
            for (size_t i = 0; i < total_items; i++) {
                if (items[i].depth != d) continue;
                float target_x = cur_x;

                if (items[i].parent_id != 0) {
                    for (size_t j = 0; j < total_items; j++) {
                        if (items[j].id == items[i].parent_id) {
                            float p_mid_x = items[j].x + items[j].w * 0.5f;
                            float pref_x = p_mid_x - items[i].w * 0.5f;
                            if (pref_x > target_x) target_x = pref_x;
                            break;
                        }
                    }
                }

                if (last_placed_idx != -1 && items[i].parent_id != 0 && items[i].parent_id == items[last_placed_idx].parent_id) {
                    float sibling_min_x = items[last_placed_idx].x + items[last_placed_idx].w + SIBLING_MIN_GAP_X;
                    if (target_x < sibling_min_x) {
                        target_x = sibling_min_x;
                    }
                }

                items[i].x = target_x;
                items[i].y = row_y[d];
                Rnode_graph_set_node_position(graph, items[i].rnode, items[i].x, items[i].y);
                cur_x = items[i].x + items[i].w + GAP_X;
                last_placed_idx = (int)i;
            }
        }
    } else {
        /* Horizontal multi-column layout */
        float col_max_w[64];
        for (int d = 0; d < 64; d++) col_max_w[d] = 240.0f;

        for (size_t i = 0; i < total_items; i++) {
            int d = items[i].depth;
            if (d >= 64) d = 63;
            if (items[i].w > col_max_w[d]) col_max_w[d] = items[i].w;
        }

        float col_x[64];
        float cur_x = 40.0f;
        const float GAP_X = 60.0f;
        const float GAP_Y = 24.0f;

        for (int d = 0; d <= max_depth; d++) {
            col_x[d] = cur_x;
            cur_x += col_max_w[d] + GAP_X;
        }

        for (int d = 0; d <= max_depth; d++) {
            float cur_y = 50.0f;
            int last_placed_idx = -1;
            for (size_t i = 0; i < total_items; i++) {
                if (items[i].depth != d) continue;
                float target_y = cur_y;

                if (items[i].parent_id != 0) {
                    for (size_t j = 0; j < total_items; j++) {
                        if (items[j].id == items[i].parent_id) {
                            float p_mid_y = items[j].y + items[j].h * 0.5f;
                            float pref_y = p_mid_y - items[i].h * 0.5f;
                            if (pref_y > target_y) target_y = pref_y;
                            break;
                        }
                    }
                }

                if (last_placed_idx != -1 && items[i].parent_id != 0 && items[i].parent_id == items[last_placed_idx].parent_id) {
                    float sibling_min_y = items[last_placed_idx].y + items[last_placed_idx].h + SIBLING_MIN_GAP_Y;
                    if (target_y < sibling_min_y) {
                        target_y = sibling_min_y;
                    }
                }

                items[i].x = col_x[d];
                items[i].y = target_y;
                Rnode_graph_set_node_position(graph, items[i].rnode, items[i].x, items[i].y);
                cur_y = items[i].y + items[i].h + GAP_Y;
                last_placed_idx = (int)i;
            }
        }
    }

    /* 6. Connect pins */
    int total_links = 0;
    for (size_t i = 0; i < total_items; i++) {
        if (items[i].parent_id == 0) continue;
        for (size_t j = 0; j < total_items; j++) {
            if (items[j].id == items[i].parent_id) {
                if (Rnode_graph_connect(graph, items[j].out_pin, items[i].in_pin)) {
                    total_links++;
                }
                break;
            }
        }
    }

    Rnode_graph_set_selection_callback(graph, sting_gui_on_selection_callback, g_gui_ctx);
    Rwidget_set_event_callback(graph, sting_gui_handle_event, NULL);

    return total_links;
}

static int sting_gui_on_window_event(Rid window, const REvent *event, void *user_data) {
    (void)window;
    Rid graph = (Rid)(uintptr_t)user_data;
    if (!event) return R_FALSE;
    if (event->type == R_EVENT_KEY_DOWN) {
        if (sting_gui_handle_key(graph, event->key, event->modifiers)) {
            return R_TRUE;
        }
    }
    return R_FALSE;
}

int sting_gui_run(sting_state_t *st) {
    if (!st) return 1;

    bool is_headless = false;
    if (!Rinit(R_BACKEND_DEFAULT)) {
        RInitDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.headless = R_TRUE;
        desc.backend = R_BACKEND_NULL;
        if (!Rinit_ex(&desc)) {
            const char *detail = NULL;
            RError err = Rerror_get_last(&detail, NULL);
            fprintf(stderr, "[stingtrail-gui] Raven initialization failed: %s (%s)\n",
                    Rerror_to_string(err), detail ? detail : "no detail");
            return 1;
        }
        is_headless = true;
    }

    Rtheme_set_dark(R_FALSE);

    Rid graph = Rnode_graph_create();
    Rnode_graph_set_grid_visible(graph, R_TRUE);
    Rnode_graph_set_flow_visible(graph, R_TRUE);
    Rnode_graph_set_type_color(graph, "flow", 1.0f, 1.0f, 1.0f);
    Rnode_graph_set_type_style(graph, "flow", "LinkShape", "Step");

    int total_links = sting_gui_build_dag(graph, st);
    Rnode_graph_frame_all(graph);

    if (is_headless) {
        printf("[stingtrail-gui] Headless mode: %d nodes, %d links rendered.\n",
               Rnode_graph_get_node_count(graph), total_links);
        sting_gui_cleanup_context();
        Rshutdown();
        return 0;
    }

    Rid window = Rwindow_create("STINGTRAIL // 2D Flow DAG Viewer (Read-Only)", 1200, 800, R_WINDOW_DEFAULT);
    Rwindow_set_content(window, graph);
    sting_gui_create_context_menu(window, graph, st);
    Rwindow_set_event_hook(window, sting_gui_on_window_event, (void *)(uintptr_t)graph);
    printf("[stingtrail-gui] Running: %d nodes, %d links rendered.\n",
           Rnode_graph_get_node_count(graph), total_links);

    struct stat st_buf;
    time_t last_mtime = 0;
    if (st->journal_path[0] != '\0' && stat(st->journal_path, &st_buf) == 0) {
        last_mtime = st_buf.st_mtime;
    }

    struct timespec last_frame_ts;
    clock_gettime(CLOCK_MONOTONIC, &last_frame_ts);

    while (Rwait_frame(g_camera_anim.active ? 0.016 : 0.05)) {
        struct timespec now_ts;
        clock_gettime(CLOCK_MONOTONIC, &now_ts);
        double dt = (double)(now_ts.tv_sec - last_frame_ts.tv_sec) +
                    (double)(now_ts.tv_nsec - last_frame_ts.tv_nsec) / 1e9;
        last_frame_ts = now_ts;

        if (g_camera_anim.active) {
            if (dt <= 0.0 || dt > 0.1) dt = 0.016;
            sting_gui_update_animation((float)dt);
        }

        /* Live watch: reload preserving zoom when journal file changes on disk */
        if (st->journal_path[0] != '\0' && stat(st->journal_path, &st_buf) == 0) {
            if (st_buf.st_mtime != last_mtime) {
                last_mtime = st_buf.st_mtime;
                sting_gui_reload_preserving_zoom(graph, st);
            }
        }
    }

    sting_gui_cleanup_context();
    Rshutdown();
    return 0;
}
