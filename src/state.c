#define _POSIX_C_SOURCE 200809L
#include "stingtrail.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void sting_state_init(sting_state_t *st, const char *journal_path) {
    if (!st) return;
    memset(st, 0, sizeof(*st));
    st->next_id = 1;
    if (journal_path && journal_path[0] != '\0') {
        snprintf(st->journal_path, sizeof(st->journal_path), "%s", journal_path);
        sting_store_load(st);
    }
}

void sting_state_reload(sting_state_t *st) {
    if (!st || st->journal_path[0] == '\0') return;
    char path[512];
    snprintf(path, sizeof(path), "%s", st->journal_path);
    sting_scope_t sc = st->scope;
    memset(st, 0, sizeof(*st));
    st->scope = sc;
    st->next_id = 1;
    snprintf(st->journal_path, sizeof(st->journal_path), "%s", path);
    sting_store_load(st);
}

static bool id_exists(const sting_state_t *st, uint32_t id) {
    if (id == 0) return true;
    for (size_t i = 0; i < st->node_count; i++) {
        if (st->nodes[i].id == id) return true;
    }
    for (size_t i = 0; i < st->entry_count; i++) {
        if (st->entries[i].id == id) return true;
    }
    for (size_t i = 0; i < st->step_count; i++) {
        if (st->steps[i].id == id) return true;
    }
    return false;
}

uint32_t sting_state_add_node(sting_state_t *st, const char *kind,
                              const char *name, const char *detail,
                              bool persist) {
    if (!st || !name || name[0] == '\0') return 0;
    if (st->node_count >= STINGTRAIL_MAX_NODES) return 0;

    const char *k = (kind && (strcmp(kind, "premise") == 0 ||
                              strcmp(kind, "asset") == 0 ||
                              strcmp(kind, "context") == 0)) ? kind : "asset";

    /* Deduplication: return existing node ID if identical (kind, name) */
    for (size_t i = 0; i < st->node_count; i++) {
        if (strcmp(st->nodes[i].kind, k) == 0 && strcmp(st->nodes[i].name, name) == 0) {
            return st->nodes[i].id;
        }
    }

    sting_node_t *n = &st->nodes[st->node_count++];
    n->id = st->next_id++;
    snprintf(n->kind, sizeof(n->kind), "%s", k);
    snprintf(n->name, sizeof(n->name), "%s", name);
    snprintf(n->detail, sizeof(n->detail), "%s", detail ? detail : "");
    n->created_at = (int64_t)time(NULL);
    sting_timestamp_now(n->created, sizeof(n->created));
    memcpy(n->modified, n->created, sizeof(n->modified));

    if (persist && st->journal_path[0] != '\0') {
        char line[2048];
        sting_buf_t b;
        sting_buf_init(&b, line, sizeof(line));
        sting_buf_append(&b, "{\"op\":\"node\",\"id\":");
        sting_buf_append_int(&b, (long long)n->id);
        sting_buf_append(&b, ",\"kind\":");
        sting_buf_append_json_str(&b, n->kind);
        sting_buf_append(&b, ",\"name\":");
        sting_buf_append_json_str(&b, n->name);
        sting_buf_append(&b, ",\"detail\":");
        sting_buf_append_json_str(&b, n->detail);
        sting_buf_append(&b, ",\"created\":");
        sting_buf_append_json_str(&b, n->created);
        sting_buf_append(&b, ",\"modified\":");
        sting_buf_append_json_str(&b, n->modified);
        sting_buf_append_ch(&b, '}');
        if (!b.overflow) {
            sting_store_append_line(st->journal_path, line);
        }
    }

    return n->id;
}

uint32_t sting_state_add_entry(sting_state_t *st, uint32_t parent_id,
                               const char *kind, const char *severity,
                               const char *summary, const char *detail,
                               bool persist) {
    if (!st || !summary || summary[0] == '\0') return 0;
    if (st->entry_count >= STINGTRAIL_MAX_ENTRIES) return 0;
    if (!id_exists(st, parent_id)) return 0;

    const char *k = (kind && (strcmp(kind, "observation") == 0 ||
                              strcmp(kind, "hypothesis") == 0 ||
                              strcmp(kind, "evidence") == 0 ||
                              strcmp(kind, "refutation") == 0 ||
                              strcmp(kind, "finding") == 0)) ? kind : "observation";

    const char *sev = (severity && (strcmp(severity, "info") == 0 ||
                                    strcmp(severity, "low") == 0 ||
                                    strcmp(severity, "medium") == 0 ||
                                    strcmp(severity, "high") == 0 ||
                                    strcmp(severity, "critical") == 0)) ? severity : "info";

    sting_entry_t *e = &st->entries[st->entry_count++];
    e->id = st->next_id++;
    e->parent_id = parent_id;
    snprintf(e->kind, sizeof(e->kind), "%s", k);
    snprintf(e->severity, sizeof(e->severity), "%s", sev);
    snprintf(e->summary, sizeof(e->summary), "%s", summary);
    snprintf(e->detail, sizeof(e->detail), "%s", detail ? detail : "");
    e->created_at = (int64_t)time(NULL);
    sting_timestamp_now(e->created, sizeof(e->created));
    memcpy(e->modified, e->created, sizeof(e->modified));

    if (persist && st->journal_path[0] != '\0') {
        char line[2048];
        sting_buf_t b;
        sting_buf_init(&b, line, sizeof(line));
        sting_buf_append(&b, "{\"op\":\"entry\",\"id\":");
        sting_buf_append_int(&b, (long long)e->id);
        sting_buf_append(&b, ",\"parent_id\":");
        sting_buf_append_int(&b, (long long)e->parent_id);
        sting_buf_append(&b, ",\"kind\":");
        sting_buf_append_json_str(&b, e->kind);
        sting_buf_append(&b, ",\"severity\":");
        sting_buf_append_json_str(&b, e->severity);
        sting_buf_append(&b, ",\"summary\":");
        sting_buf_append_json_str(&b, e->summary);
        sting_buf_append(&b, ",\"detail\":");
        sting_buf_append_json_str(&b, e->detail);
        sting_buf_append(&b, ",\"created\":");
        sting_buf_append_json_str(&b, e->created);
        sting_buf_append(&b, ",\"modified\":");
        sting_buf_append_json_str(&b, e->modified);
        sting_buf_append_ch(&b, '}');
        if (!b.overflow) {
            sting_store_append_line(st->journal_path, line);
        }
    }

    return e->id;
}

uint32_t sting_state_plan_step(sting_state_t *st, uint32_t parent_id,
                               int priority, const char *target,
                               const char *action, const char *rationale,
                               bool persist, const char **err_out) {
    if (!st || !action || action[0] == '\0') {
        if (err_out) *err_out = "action is required";
        return 0;
    }
    if (!rationale || rationale[0] == '\0') {
        if (err_out) *err_out = "rationale is required for disciplined planning";
        return 0;
    }
    if (st->step_count >= STINGTRAIL_MAX_STEPS) {
        if (err_out) *err_out = "step capacity reached";
        return 0;
    }
    if (!id_exists(st, parent_id)) {
        if (err_out) *err_out = "parent_id does not exist in graph";
        return 0;
    }

    const char *guard_reason = NULL;
    if (sting_guard_check(&st->scope, target, action, &guard_reason) != STING_GUARD_ALLOW) {
        if (err_out) *err_out = guard_reason ? guard_reason : "blocked by guard";
        return 0;
    }

    if (priority < 1) priority = 1;
    if (priority > 5) priority = 5;

    sting_step_t *s = &st->steps[st->step_count++];
    s->id = st->next_id++;
    s->parent_id = parent_id;
    s->priority = priority;
    snprintf(s->status, sizeof(s->status), "%s", "pending");
    snprintf(s->target, sizeof(s->target), "%s", target ? target : "");
    snprintf(s->action, sizeof(s->action), "%s", action);
    snprintf(s->rationale, sizeof(s->rationale), "%s", rationale);
    s->outcome[0] = '\0';
    s->created_at = (int64_t)time(NULL);
    s->updated_at = s->created_at;
    sting_timestamp_now(s->created, sizeof(s->created));
    memcpy(s->modified, s->created, sizeof(s->modified));

    if (persist && st->journal_path[0] != '\0') {
        char line[2048];
        sting_buf_t b;
        sting_buf_init(&b, line, sizeof(line));
        sting_buf_append(&b, "{\"op\":\"plan\",\"id\":");
        sting_buf_append_int(&b, (long long)s->id);
        sting_buf_append(&b, ",\"parent_id\":");
        sting_buf_append_int(&b, (long long)s->parent_id);
        sting_buf_append(&b, ",\"priority\":");
        sting_buf_append_int(&b, s->priority);
        sting_buf_append(&b, ",\"target\":");
        sting_buf_append_json_str(&b, s->target);
        sting_buf_append(&b, ",\"action\":");
        sting_buf_append_json_str(&b, s->action);
        sting_buf_append(&b, ",\"rationale\":");
        sting_buf_append_json_str(&b, s->rationale);
        sting_buf_append(&b, ",\"created\":");
        sting_buf_append_json_str(&b, s->created);
        sting_buf_append(&b, ",\"modified\":");
        sting_buf_append_json_str(&b, s->modified);
        sting_buf_append_ch(&b, '}');
        if (!b.overflow) {
            sting_store_append_line(st->journal_path, line);
        }
    }

    return s->id;
}

bool sting_state_update_step(sting_state_t *st, uint32_t step_id,
                             const char *status, const char *outcome,
                             bool persist) {
    if (!st || !status) return false;
    if (strcmp(status, "pending") != 0 &&
        strcmp(status, "in_progress") != 0 &&
        strcmp(status, "done") != 0 &&
        strcmp(status, "blocked") != 0) {
        return false;
    }

    sting_step_t *target_step = NULL;
    for (size_t i = 0; i < st->step_count; i++) {
        if (st->steps[i].id == step_id) {
            target_step = &st->steps[i];
            break;
        }
    }
    if (!target_step) return false;

    snprintf(target_step->status, sizeof(target_step->status), "%s", status);
    if (outcome) {
        snprintf(target_step->outcome, sizeof(target_step->outcome), "%s", outcome);
    }
    target_step->updated_at = (int64_t)time(NULL);
    sting_timestamp_now(target_step->modified, sizeof(target_step->modified));

    if (persist && st->journal_path[0] != '\0') {
        char line[2048];
        sting_buf_t b;
        sting_buf_init(&b, line, sizeof(line));
        sting_buf_append(&b, "{\"op\":\"step_update\",\"step_id\":");
        sting_buf_append_int(&b, (long long)step_id);
        sting_buf_append(&b, ",\"status\":");
        sting_buf_append_json_str(&b, target_step->status);
        sting_buf_append(&b, ",\"outcome\":");
        sting_buf_append_json_str(&b, target_step->outcome);
        sting_buf_append(&b, ",\"created\":");
        sting_buf_append_json_str(&b, target_step->created);
        sting_buf_append(&b, ",\"modified\":");
        sting_buf_append_json_str(&b, target_step->modified);
        sting_buf_append_ch(&b, '}');
        if (!b.overflow) {
            sting_store_append_line(st->journal_path, line);
        }
    }

    return true;
}

void sting_state_print_board(const sting_state_t *st) {
    if (!st) return;
    printf("=== STINGTRAIL // CRITICAL THINKING BOARD ===\n");

    printf("Nodes (%zu):\n", st->node_count);
    if (st->node_count == 0) {
        printf("  (none)\n");
    } else {
        for (size_t i = 0; i < st->node_count; i++) {
            printf("  [#%u] (%s) %s%s%s (created: %s)\n",
                   st->nodes[i].id,
                   st->nodes[i].kind,
                   st->nodes[i].name,
                   st->nodes[i].detail[0] ? " — " : "",
                   st->nodes[i].detail[0] ? st->nodes[i].detail : "",
                   st->nodes[i].created[0] ? st->nodes[i].created : "-");
        }
    }

    printf("\nReasoning Log (%zu):\n", st->entry_count);
    if (st->entry_count == 0) {
        printf("  (none)\n");
    } else {
        for (size_t i = 0; i < st->entry_count; i++) {
            printf("  [#%u <- #%u] [%s/%s] %s%s%s (created: %s)\n",
                   st->entries[i].id,
                   st->entries[i].parent_id,
                   st->entries[i].kind,
                   st->entries[i].severity,
                   st->entries[i].summary,
                   st->entries[i].detail[0] ? " | " : "",
                   st->entries[i].detail[0] ? st->entries[i].detail : "",
                   st->entries[i].created[0] ? st->entries[i].created : "-");
        }
    }

    printf("\nPlanned Next Steps (%zu):\n", st->step_count);
    if (st->step_count == 0) {
        printf("  (none)\n");
    } else {
        /* Group and sort by priority P1 .. P5 */
        for (int p = 1; p <= 5; p++) {
            for (size_t i = 0; i < st->step_count; i++) {
                if (st->steps[i].priority != p) continue;
                printf("  [#%u <- #%u] [P%d][%s] %s%s%s (created: %s, modified: %s)\n",
                       st->steps[i].id,
                       st->steps[i].parent_id,
                       st->steps[i].priority,
                       st->steps[i].status,
                       st->steps[i].action,
                       st->steps[i].target[0] ? " (target: " : "",
                       st->steps[i].target[0] ? st->steps[i].target : "",
                       st->steps[i].created[0] ? st->steps[i].created : "-",
                       st->steps[i].modified[0] ? st->steps[i].modified : "-");
                printf("      why: %s\n", st->steps[i].rationale);
                if (st->steps[i].outcome[0]) {
                    printf("      outcome: %s\n", st->steps[i].outcome);
                }
            }
        }
    }
}

bool sting_state_to_json(const sting_state_t *st, char *out_buf, size_t out_cap) {
    if (!st || !out_buf || out_cap == 0) return false;
    sting_buf_t b;
    sting_buf_init(&b, out_buf, out_cap);

    sting_buf_append(&b, "{\"version\":\"" STINGTRAIL_VERSION "\",\"nodes\":[");
    for (size_t i = 0; i < st->node_count; i++) {
        if (i > 0) sting_buf_append_ch(&b, ',');
        sting_buf_append(&b, "{\"id\":");
        sting_buf_append_int(&b, (long long)st->nodes[i].id);
        sting_buf_append(&b, ",\"kind\":");
        sting_buf_append_json_str(&b, st->nodes[i].kind);
        sting_buf_append(&b, ",\"name\":");
        sting_buf_append_json_str(&b, st->nodes[i].name);
        sting_buf_append(&b, ",\"detail\":");
        sting_buf_append_json_str(&b, st->nodes[i].detail);
        sting_buf_append(&b, ",\"created\":");
        sting_buf_append_json_str(&b, st->nodes[i].created);
        sting_buf_append(&b, ",\"modified\":");
        sting_buf_append_json_str(&b, st->nodes[i].modified);
        sting_buf_append_ch(&b, '}');
    }

    sting_buf_append(&b, "],\"entries\":[");
    for (size_t i = 0; i < st->entry_count; i++) {
        if (i > 0) sting_buf_append_ch(&b, ',');
        sting_buf_append(&b, "{\"id\":");
        sting_buf_append_int(&b, (long long)st->entries[i].id);
        sting_buf_append(&b, ",\"parent_id\":");
        sting_buf_append_int(&b, (long long)st->entries[i].parent_id);
        sting_buf_append(&b, ",\"kind\":");
        sting_buf_append_json_str(&b, st->entries[i].kind);
        sting_buf_append(&b, ",\"severity\":");
        sting_buf_append_json_str(&b, st->entries[i].severity);
        sting_buf_append(&b, ",\"summary\":");
        sting_buf_append_json_str(&b, st->entries[i].summary);
        sting_buf_append(&b, ",\"detail\":");
        sting_buf_append_json_str(&b, st->entries[i].detail);
        sting_buf_append(&b, ",\"created\":");
        sting_buf_append_json_str(&b, st->entries[i].created);
        sting_buf_append(&b, ",\"modified\":");
        sting_buf_append_json_str(&b, st->entries[i].modified);
        sting_buf_append_ch(&b, '}');
    }

    sting_buf_append(&b, "],\"steps\":[");
    for (size_t i = 0; i < st->step_count; i++) {
        if (i > 0) sting_buf_append_ch(&b, ',');
        sting_buf_append(&b, "{\"id\":");
        sting_buf_append_int(&b, (long long)st->steps[i].id);
        sting_buf_append(&b, ",\"parent_id\":");
        sting_buf_append_int(&b, (long long)st->steps[i].parent_id);
        sting_buf_append(&b, ",\"priority\":");
        sting_buf_append_int(&b, st->steps[i].priority);
        sting_buf_append(&b, ",\"status\":");
        sting_buf_append_json_str(&b, st->steps[i].status);
        sting_buf_append(&b, ",\"target\":");
        sting_buf_append_json_str(&b, st->steps[i].target);
        sting_buf_append(&b, ",\"action\":");
        sting_buf_append_json_str(&b, st->steps[i].action);
        sting_buf_append(&b, ",\"rationale\":");
        sting_buf_append_json_str(&b, st->steps[i].rationale);
        sting_buf_append(&b, ",\"outcome\":");
        sting_buf_append_json_str(&b, st->steps[i].outcome);
        sting_buf_append(&b, ",\"created\":");
        sting_buf_append_json_str(&b, st->steps[i].created);
        sting_buf_append(&b, ",\"modified\":");
        sting_buf_append_json_str(&b, st->steps[i].modified);
        sting_buf_append_ch(&b, '}');
    }

    sting_buf_append(&b, "]}");
    return !b.overflow;
}

static void get_context_path(const char *journal_path, char *out, size_t cap) {
    const char *jp = (journal_path && journal_path[0]) ? journal_path : "stingtrail.jsonl";
    snprintf(out, cap, "%s.context", jp);
}

uint32_t sting_context_get(const char *journal_path) {
    const char *env = getenv("STINGTRAIL_CONTEXT");
    if (env && env[0]) {
        unsigned long v = strtoul(env, NULL, 10);
        if (v > 0) return (uint32_t)v;
    }
    char path[512];
    get_context_path(journal_path, path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[64];
    uint32_t id = 0;
    if (fgets(line, sizeof(line), f)) {
        id = (uint32_t)strtoul(line, NULL, 10);
    }
    fclose(f);
    return id;
}

bool sting_context_set(const char *journal_path, uint32_t context_id) {
    char path[512];
    get_context_path(journal_path, path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "%u\n", context_id);
    fclose(f);
    return true;
}

bool sting_context_clear(const char *journal_path) {
    char path[512];
    get_context_path(journal_path, path, sizeof(path));
    unlink(path);
    return true;
}

void sting_state_print_focused(const sting_state_t *st, uint32_t target_id) {
    if (!st || target_id == 0) return;

    /* 1. Locate target */
    const sting_node_t *target_node = NULL;
    const sting_entry_t *target_entry = NULL;
    const sting_step_t *target_step = NULL;
    uint32_t target_pid = 0;

    for (size_t i = 0; i < st->node_count; i++) {
        if (st->nodes[i].id == target_id) {
            target_node = &st->nodes[i];
            target_pid = 0;
            break;
        }
    }
    if (!target_node) {
        for (size_t i = 0; i < st->entry_count; i++) {
            if (st->entries[i].id == target_id) {
                target_entry = &st->entries[i];
                target_pid = st->entries[i].parent_id;
                break;
            }
        }
    }
    if (!target_node && !target_entry) {
        for (size_t i = 0; i < st->step_count; i++) {
            if (st->steps[i].id == target_id) {
                target_step = &st->steps[i];
                target_pid = st->steps[i].parent_id;
                break;
            }
        }
    }

    if (!target_node && !target_entry && !target_step) {
        fprintf(stderr, "error: id #%u not found in graph\n", target_id);
        return;
    }

    printf("=== STINGTRAIL // FOCUSED EXPERIMENT CONTEXT (#%u) ===\n\n", target_id);

    /* 2. Trace Ancestor Spine (Root -> Target) */
    uint32_t spine[64];
    size_t spine_count = 0;
    uint32_t cur_p = target_pid;
    while (cur_p != 0 && spine_count < 64) {
        spine[spine_count++] = cur_p;
        uint32_t next_p = 0;
        for (size_t i = 0; i < st->node_count; i++) {
            if (st->nodes[i].id == cur_p) { next_p = 0; break; }
        }
        if (next_p == 0) {
            for (size_t i = 0; i < st->entry_count; i++) {
                if (st->entries[i].id == cur_p) { next_p = st->entries[i].parent_id; break; }
            }
        }
        if (next_p == 0) {
            for (size_t i = 0; i < st->step_count; i++) {
                if (st->steps[i].id == cur_p) { next_p = st->steps[i].parent_id; break; }
            }
        }
        cur_p = next_p;
    }

    printf("Motivating Lineage Spine:\n");
    if (spine_count == 0) {
        printf("  (Target is a root premise node)\n");
    } else {
        for (size_t k = spine_count; k > 0; k--) {
            uint32_t anc = spine[k - 1];
            size_t depth = spine_count - k;
            char indent[128];
            indent[0] = '\0';
            for (size_t d = 0; d < depth; d++) strcat(indent, "  ");
            if (depth > 0) strcat(indent, "-> ");
            else strcat(indent, "  ");

            bool printed = false;
            for (size_t i = 0; i < st->node_count; i++) {
                if (st->nodes[i].id == anc) {
                    printf("%s[#%u] (%s) %s\n", indent, anc, st->nodes[i].kind, st->nodes[i].name);
                    printed = true;
                    break;
                }
            }
            if (!printed) {
                for (size_t i = 0; i < st->entry_count; i++) {
                    if (st->entries[i].id == anc) {
                        printf("%s[#%u] (%s) %s\n", indent, anc, st->entries[i].kind, st->entries[i].summary);
                        printed = true;
                        break;
                    }
                }
            }
            if (!printed) {
                for (size_t i = 0; i < st->step_count; i++) {
                    if (st->steps[i].id == anc) {
                        printf("%s[#%u] [P%d][%s] %s\n", indent, anc, st->steps[i].priority, st->steps[i].status, st->steps[i].action);
                        printed = true;
                        break;
                    }
                }
            }
        }
    }

    /* 3. Target Node Details */
    printf("\nTarget Node (#%u):\n", target_id);
    if (target_node) {
        printf("  Kind: %s\n", target_node->kind);
        printf("  Name: %s\n", target_node->name);
        if (target_node->detail[0]) printf("  Detail: %s\n", target_node->detail);
        printf("  Created: %s\n", target_node->created);
    } else if (target_entry) {
        printf("  Kind: %s (sev: %s)\n", target_entry->kind, target_entry->severity);
        printf("  Summary: %s\n", target_entry->summary);
        if (target_entry->detail[0]) printf("  Detail: %s\n", target_entry->detail);
        printf("  Created: %s\n", target_entry->created);
    } else if (target_step) {
        printf("  Priority: P%d | Status: %s\n", target_step->priority, target_step->status);
        printf("  Action: %s\n", target_step->action);
        printf("  Rationale: %s\n", target_step->rationale);
        if (target_step->outcome[0]) printf("  Outcome: %s\n", target_step->outcome);
        printf("  Created: %s | Modified: %s\n", target_step->created, target_step->modified);
    }

    /* 4. Descendants Subtree (Topological DFS) */
    printf("\nEpistemic Subtree / Outcomes:\n");
    uint32_t reach[512];
    size_t reach_cnt = 0;
    reach[reach_cnt++] = target_id;
    for (int pass = 0; pass < 32; pass++) {
        bool added = false;
        for (size_t i = 0; i < st->entry_count; i++) {
            uint32_t id = st->entries[i].id;
            uint32_t pid = st->entries[i].parent_id;
            bool has = false;
            for (size_t k = 0; k < reach_cnt; k++) { if (reach[k] == id) { has = true; break; } }
            if (has) continue;
            for (size_t k = 0; k < reach_cnt; k++) {
                if (reach[k] == pid) {
                    if (reach_cnt < 512) { reach[reach_cnt++] = id; added = true; }
                    break;
                }
            }
        }
        for (size_t i = 0; i < st->step_count; i++) {
            uint32_t id = st->steps[i].id;
            uint32_t pid = st->steps[i].parent_id;
            bool has = false;
            for (size_t k = 0; k < reach_cnt; k++) { if (reach[k] == id) { has = true; break; } }
            if (has) continue;
            for (size_t k = 0; k < reach_cnt; k++) {
                if (reach[k] == pid) {
                    if (reach_cnt < 512) { reach[reach_cnt++] = id; added = true; }
                    break;
                }
            }
        }
        if (!added) break;
    }

    if (reach_cnt <= 1) {
        printf("  (No descendant actions or evidence recorded yet)\n");
    } else {
        for (size_t r = 1; r < reach_cnt; r++) {
            uint32_t id = reach[r];
            for (size_t i = 0; i < st->entry_count; i++) {
                if (st->entries[i].id == id) {
                    printf("  [#%u <- #%u] [%s/%s] %s\n",
                           id, st->entries[i].parent_id, st->entries[i].kind, st->entries[i].severity, st->entries[i].summary);
                    if (st->entries[i].detail[0]) {
                        printf("      detail: %s\n", st->entries[i].detail);
                    }
                    break;
                }
            }
            for (size_t i = 0; i < st->step_count; i++) {
                if (st->steps[i].id == id) {
                    printf("  [#%u <- #%u] [P%d][%s] %s\n",
                           id, st->steps[i].parent_id, st->steps[i].priority, st->steps[i].status, st->steps[i].action);
                    printf("      why: %s\n", st->steps[i].rationale);
                    if (st->steps[i].outcome[0]) {
                        printf("      outcome: %s\n", st->steps[i].outcome);
                    }
                    break;
                }
            }
        }
    }
}

bool sting_state_focused_to_json(const sting_state_t *st, uint32_t target_id, char *out_buf, size_t out_cap) {
    if (!st || target_id == 0 || !out_buf || out_cap == 0) return false;

    sting_buf_t b;
    sting_buf_init(&b, out_buf, out_cap);

    sting_buf_append(&b, "{\"version\":\"" STINGTRAIL_VERSION "\",\"traversal\":\"hourglass_v1\",\"target_id\":");
    sting_buf_append_int(&b, (long long)target_id);

    /* Ancestors */
    uint32_t target_pid = 0;
    for (size_t i = 0; i < st->node_count; i++) {
        if (st->nodes[i].id == target_id) { target_pid = 0; break; }
    }
    if (target_pid == 0) {
        for (size_t i = 0; i < st->entry_count; i++) {
            if (st->entries[i].id == target_id) { target_pid = st->entries[i].parent_id; break; }
        }
    }
    if (target_pid == 0) {
        for (size_t i = 0; i < st->step_count; i++) {
            if (st->steps[i].id == target_id) { target_pid = st->steps[i].parent_id; break; }
        }
    }

    uint32_t spine[64];
    size_t spine_count = 0;
    uint32_t cur_p = target_pid;
    while (cur_p != 0 && spine_count < 64) {
        spine[spine_count++] = cur_p;
        uint32_t next_p = 0;
        for (size_t i = 0; i < st->entry_count; i++) {
            if (st->entries[i].id == cur_p) { next_p = st->entries[i].parent_id; break; }
        }
        if (next_p == 0) {
            for (size_t i = 0; i < st->step_count; i++) {
                if (st->steps[i].id == cur_p) { next_p = st->steps[i].parent_id; break; }
            }
        }
        cur_p = next_p;
    }

    sting_buf_append(&b, ",\"ancestor_ids\":[");
    for (size_t k = spine_count; k > 0; k--) {
        if (k < spine_count) sting_buf_append_ch(&b, ',');
        uint32_t anc = spine[k - 1];
        sting_buf_append_int(&b, (long long)anc);
    }
    sting_buf_append(&b, "]");

    /* Combined Hourglass set: spine (ancestors) + target + descendants */
    uint32_t reach[512];
    size_t reach_cnt = 0;
    for (size_t k = 0; k < spine_count && reach_cnt < 512; k++) {
        reach[reach_cnt++] = spine[k];
    }
    if (reach_cnt < 512) {
        reach[reach_cnt++] = target_id;
    }

    for (int pass = 0; pass < 32; pass++) {
        bool added = false;
        for (size_t i = 0; i < st->entry_count; i++) {
            uint32_t id = st->entries[i].id;
            uint32_t pid = st->entries[i].parent_id;
            bool has = false;
            for (size_t k = 0; k < reach_cnt; k++) { if (reach[k] == id) { has = true; break; } }
            if (has) continue;
            for (size_t k = 0; k < reach_cnt; k++) {
                if (reach[k] == pid) {
                    if (reach_cnt < 512) { reach[reach_cnt++] = id; added = true; }
                    break;
                }
            }
        }
        for (size_t i = 0; i < st->step_count; i++) {
            uint32_t id = st->steps[i].id;
            uint32_t pid = st->steps[i].parent_id;
            bool has = false;
            for (size_t k = 0; k < reach_cnt; k++) { if (reach[k] == id) { has = true; break; } }
            if (has) continue;
            for (size_t k = 0; k < reach_cnt; k++) {
                if (reach[k] == pid) {
                    if (reach_cnt < 512) { reach[reach_cnt++] = id; added = true; }
                    break;
                }
            }
        }
        if (!added) break;
    }

    sting_buf_append(&b, ",\"nodes\":[");
    bool first_node = true;
    for (size_t i = 0; i < st->node_count; i++) {
        bool in_r = false;
        for (size_t k = 0; k < reach_cnt; k++) { if (reach[k] == st->nodes[i].id) { in_r = true; break; } }
        if (!in_r) continue;
        if (!first_node) sting_buf_append_ch(&b, ',');
        first_node = false;
        sting_buf_append(&b, "{\"id\":");
        sting_buf_append_int(&b, (long long)st->nodes[i].id);
        sting_buf_append(&b, ",\"kind\":");
        sting_buf_append_json_str(&b, st->nodes[i].kind);
        sting_buf_append(&b, ",\"name\":");
        sting_buf_append_json_str(&b, st->nodes[i].name);
        sting_buf_append(&b, ",\"detail\":");
        sting_buf_append_json_str(&b, st->nodes[i].detail);
        sting_buf_append_ch(&b, '}');
    }

    sting_buf_append(&b, "],\"entries\":[");
    bool first_entry = true;
    for (size_t i = 0; i < st->entry_count; i++) {
        bool in_r = false;
        for (size_t k = 0; k < reach_cnt; k++) { if (reach[k] == st->entries[i].id) { in_r = true; break; } }
        if (!in_r) continue;
        if (!first_entry) sting_buf_append_ch(&b, ',');
        first_entry = false;
        sting_buf_append(&b, "{\"id\":");
        sting_buf_append_int(&b, (long long)st->entries[i].id);
        sting_buf_append(&b, ",\"parent_id\":");
        sting_buf_append_int(&b, (long long)st->entries[i].parent_id);
        sting_buf_append(&b, ",\"kind\":");
        sting_buf_append_json_str(&b, st->entries[i].kind);
        sting_buf_append(&b, ",\"severity\":");
        sting_buf_append_json_str(&b, st->entries[i].severity);
        sting_buf_append(&b, ",\"summary\":");
        sting_buf_append_json_str(&b, st->entries[i].summary);
        sting_buf_append(&b, ",\"detail\":");
        sting_buf_append_json_str(&b, st->entries[i].detail);
        sting_buf_append_ch(&b, '}');
    }

    sting_buf_append(&b, "],\"steps\":[");
    bool first_step = true;
    for (size_t i = 0; i < st->step_count; i++) {
        bool in_r = false;
        for (size_t k = 0; k < reach_cnt; k++) { if (reach[k] == st->steps[i].id) { in_r = true; break; } }
        if (!in_r) continue;
        if (!first_step) sting_buf_append_ch(&b, ',');
        first_step = false;
        sting_buf_append(&b, "{\"id\":");
        sting_buf_append_int(&b, (long long)st->steps[i].id);
        sting_buf_append(&b, ",\"parent_id\":");
        sting_buf_append_int(&b, (long long)st->steps[i].parent_id);
        sting_buf_append(&b, ",\"priority\":");
        sting_buf_append_int(&b, st->steps[i].priority);
        sting_buf_append(&b, ",\"status\":");
        sting_buf_append_json_str(&b, st->steps[i].status);
        sting_buf_append(&b, ",\"action\":");
        sting_buf_append_json_str(&b, st->steps[i].action);
        sting_buf_append(&b, ",\"rationale\":");
        sting_buf_append_json_str(&b, st->steps[i].rationale);
        sting_buf_append(&b, ",\"outcome\":");
        sting_buf_append_json_str(&b, st->steps[i].outcome);
        sting_buf_append_ch(&b, '}');
    }

    sting_buf_append(&b, "]}");
    return !b.overflow;
}
