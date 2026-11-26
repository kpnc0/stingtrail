#ifndef STINGTRAIL_GUI_H
#define STINGTRAIL_GUI_H

#include <Raven.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "stingtrail.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STING_GLOW_GREEN_R  0.1608f
#define STING_GLOW_GREEN_G  0.6706f
#define STING_GLOW_GREEN_B  0.5294f

#define STING_GLOW_ORANGE_R 1.0000f
#define STING_GLOW_ORANGE_G 0.5843f
#define STING_GLOW_ORANGE_B 0.0000f

#define STING_GLOW_RED_R    1.0000f
#define STING_GLOW_RED_G    0.2314f
#define STING_GLOW_RED_B    0.1882f

//Adding color  #016064 to the mix
#define STING_GLOW_BLUE_R		0.0039f
#define STING_GLOW_BLUE_G   0.3765f
#define STING_GLOW_BLUE_B   0.3922f


#define STING_GLOW_PINK_R   0.9882f
#define STING_GLOW_PINK_G   0.1804f
#define STING_GLOW_PINK_B   0.6667f

typedef enum {
    STING_GLOW_NONE   = 0,
    STING_GLOW_GREEN  = 1, /* Subtle green #29AB87 for successful/done nodes */
    STING_GLOW_ORANGE = 2, /* Orange #FF9500 for pending/in_progress nodes */
    STING_GLOW_RED    = 3, /* Red #FF3B30 for blocked/dead-end nodes */
    STING_GLOW_BLUE   = 4, /* Teal-blue #016064 for conceptual root nodes (premise/asset/context) */
    STING_GLOW_PINK   = 5
} sting_glow_type_t;

#define STING_GUI_DEFAULT_RENDER_DISTANCE 1800.0f

typedef enum {
    STING_ITEM_NODE  = 0,
    STING_ITEM_ENTRY = 1,
    STING_ITEM_STEP  = 2
} sting_gui_item_kind_t;

typedef struct {
    uint32_t id;
    uint32_t parent_id;
    int parent_idx;
    sting_gui_item_kind_t item_kind;
    size_t state_index;
    int rnode;
    int in_pin;
    int out_pin;
    int depth;
    float orig_r;
    float orig_g;
    float orig_b;
    float orig_badge_r;
    float orig_badge_g;
    float orig_badge_b;
    sting_glow_type_t glow_type;
    float glow_r;
    float glow_g;
    float glow_b;
    float w;
    float h;
    float x;
    float y;
    uint8_t highlighted;
} sting_gui_item_t;

typedef struct {
    Rid graph;
    sting_gui_item_t *items;
    size_t total_items;
    int selected_rnode;
    int hovered_rnode;
    uint32_t selected_id;
    uint32_t hovered_id;
    float last_cull_cx;
    float last_cull_cy;
    float last_cull_zoom;
    float last_cull_vw;
    float last_cull_vh;
    uint32_t last_cull_selected_id;
    bool has_culled;
} sting_gui_context_t;

typedef enum {
    STING_LAYOUT_HORIZONTAL = 0,
    STING_LAYOUT_VERTICAL   = 1
} sting_layout_mode_t;

/* Layout & View configuration (Read-Only) */
void sting_gui_set_vertical_view(bool enabled);
bool sting_gui_is_vertical_view(void);
void sting_gui_toggle_vertical_view(Rid graph, sting_state_t *st);

/* Subtree Focus Filtering ("See only this" / "See all") */
void sting_gui_focus_node(Rid graph, sting_state_t *st, uint32_t focus_id);
void sting_gui_reset_focus(Rid graph, sting_state_t *st);
uint32_t sting_gui_get_focus_id(void);
Rid sting_gui_get_see_only_this_menu_item(void);
Rid sting_gui_get_see_all_menu_item(void);

/* Context Menu (Read-Only: navigation and view controls only, NO mutations) */
Rid sting_gui_create_context_menu(Rid window, Rid graph, sting_state_t *st);
Rid sting_gui_get_context_menu(void);
Rid sting_gui_get_vertical_menu_item(void);
Rid sting_gui_get_parent_menu_item(void);
Rid sting_gui_get_random_child_menu_item(void);
void sting_gui_open_context_menu_for_node(Rid graph, int node, float x, float y);

/* Viewport & Camera animation */
void sting_gui_start_camera_transition(Rid graph, float target_cx, float target_cy, float target_zoom, float duration);
bool sting_gui_is_animating(void);
void sting_gui_update_animation(float dt);
void sting_gui_finish_animation(Rid graph);
void sting_gui_get_camera_target(float *target_x, float *target_y, float *target_zoom);
void sting_gui_reload_preserving_zoom(Rid graph, sting_state_t *st);

/* Node inspection and navigation */
int sting_gui_get_parent_rnode(int rnode);
size_t sting_gui_get_children_count(int rnode);
int sting_gui_get_random_child_rnode(int rnode);
int sting_gui_get_first_child_rnode(int rnode);
int sting_gui_get_next_sibling_rnode(int rnode, bool reverse);

void sting_gui_go_to_node(Rid graph, int rnode);
void sting_gui_go_to_parent(Rid graph, int rnode);
void sting_gui_go_to_random_child(Rid graph, int rnode);
void sting_gui_go_to_first_child(Rid graph, int rnode);
void sting_gui_go_to_sibling(Rid graph, int rnode, bool reverse);

/* Vim hjkl & 'f' go-to-id key handler:
 *   h -> immediate parent
 *   j -> next sibling
 *   k -> previous sibling
 *   l -> first child
 *   f -> enter node-ID jump mode (digits + Enter, Escape cancels)
 */
bool sting_gui_handle_key(Rid graph, RKey key, Ruint modifiers);
bool sting_gui_is_find_active(void);
const char *sting_gui_get_find_input(void);
Rid sting_gui_create_find_overlay(Rid graph);
Rid sting_gui_get_find_label(void);

/* Render distance culling (canvas units from camera center; <= 0 disables culling) */
void sting_gui_set_render_distance(float distance);
float sting_gui_get_render_distance(void);
size_t sting_gui_get_rendered_node_count(void);
int sting_gui_update_render_distance(Rid graph);

/* DAG construction & event handling */
int sting_gui_build_dag(Rid graph, sting_state_t *st);
sting_gui_context_t *sting_gui_get_context(void);
void sting_gui_highlight_chain(Rid graph, sting_gui_context_t *ctx, int selected_rnode);
void sting_gui_highlight_immediate(Rid graph, sting_gui_context_t *ctx, int hovered_rnode);
void sting_gui_select_node(Rid graph, int rnode);
int sting_gui_handle_event(Rid widget, const REvent *event, void *user_data);
void sting_gui_cleanup_context(void);

/* Interactive entrypoint */
int sting_gui_run(sting_state_t *st);

#ifdef __cplusplus
}
#endif

#endif /* STINGTRAIL_GUI_H */
