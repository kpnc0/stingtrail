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

static bool is_hypothesis_id(const sting_state_t *st, uint32_t id) {
    if (!st || id == 0) return false;
    for (size_t i = 0; i < st->entry_count; i++) {
        if (st->entries[i].id == id && strcmp(st->entries[i].kind, "hypothesis") == 0) {
            return true;
        }
    }
    return false;
}

static size_t collect_descendants(const sting_state_t *st, uint32_t seed_id,
                                  uint32_t *out_ids, size_t cap) {
    if (!st || !out_ids || cap == 0) return 0;
    size_t cnt = 0;
    out_ids[cnt++] = seed_id;

    for (int pass = 0; pass < 32; pass++) {
        bool added = false;
        for (size_t i = 0; i < st->entry_count; i++) {
            uint32_t id = st->entries[i].id;
            uint32_t pid = st->entries[i].parent_id;
            bool has = false;
            for (size_t k = 0; k < cnt; k++) { if (out_ids[k] == id) { has = true; break; } }
            if (has) continue;
            for (size_t k = 0; k < cnt; k++) {
                if (out_ids[k] == pid) {
                    if (cnt < cap) { out_ids[cnt++] = id; added = true; }
                    break;
                }
            }
        }
        for (size_t i = 0; i < st->step_count; i++) {
            uint32_t id = st->steps[i].id;
            uint32_t pid = st->steps[i].parent_id;
            bool has = false;
            for (size_t k = 0; k < cnt; k++) { if (out_ids[k] == id) { has = true; break; } }
            if (has) continue;
            for (size_t k = 0; k < cnt; k++) {
                if (out_ids[k] == pid) {
                    if (cnt < cap) { out_ids[cnt++] = id; added = true; }
                    break;
                }
            }
        }
        if (!added) break;
    }
    return cnt;
}

void sting_state_plan_counts(const sting_state_t *st, uint32_t root_id,
                             size_t *total_out, size_t *incomplete_out) {
    size_t total = 0;
    size_t incomplete = 0;
    if (!st) {
        if (total_out) *total_out = 0;
        if (incomplete_out) *incomplete_out = 0;
        return;
    }

    if (root_id == 0) {
        total = st->step_count;
        for (size_t i = 0; i < st->step_count; i++) {
            if (strcmp(st->steps[i].status, "done") != 0) incomplete++;
        }
    } else {
        uint32_t reach[1024];
        size_t cnt = collect_descendants(st, root_id, reach, 1024);
        for (size_t i = 0; i < st->step_count; i++) {
            bool in_sub = false;
            for (size_t k = 0; k < cnt; k++) {
                if (reach[k] == st->steps[i].id) {
                    in_sub = true;
                    break;
                }
            }
            if (in_sub) {
                total++;
                if (strcmp(st->steps[i].status, "done") != 0) incomplete++;
            }
        }
    }

    if (total_out) *total_out = total;
    if (incomplete_out) *incomplete_out = incomplete;
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

static void persist_line_or_flag(sting_state_t *st, const sting_buf_t *b, const char *line) {
    if (!st || !b) return;
    if (b->overflow || !sting_store_append_line(st->journal_path, line)) {
        st->persist_overflow = true;
    }
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
        char line[4000];
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
        persist_line_or_flag(st, &b, line);
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
        char line[4000];
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
        persist_line_or_flag(st, &b, line);
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
    if (st->step_count >= STINGTRAIL_MAX_STEPS) {
        if (err_out) *err_out = "step capacity reached";
        return 0;
    }
    if (!is_hypothesis_id(st, parent_id)) {
        static char g_plan_err[256];
        if (parent_id == 0) {
            snprintf(g_plan_err, sizeof(g_plan_err),
                     "every plan must belong to an existing hypothesis (--parent <hypothesis_id>)");
        } else {
            const char *found_kind = NULL;
            for (size_t i = 0; i < st->node_count; i++) {
                if (st->nodes[i].id == parent_id) { found_kind = st->nodes[i].kind; break; }
            }
            if (!found_kind) {
                for (size_t i = 0; i < st->entry_count; i++) {
                    if (st->entries[i].id == parent_id) { found_kind = st->entries[i].kind; break; }
                }
            }
            if (!found_kind) {
                for (size_t i = 0; i < st->step_count; i++) {
                    if (st->steps[i].id == parent_id) { found_kind = "step"; break; }
                }
            }
            if (found_kind) {
                snprintf(g_plan_err, sizeof(g_plan_err),
                         "cannot plan under #%u (%s): parent must be a hypothesis", parent_id, found_kind);
            } else {
                snprintf(g_plan_err, sizeof(g_plan_err),
                         "cannot plan: parent #%u does not exist in graph (must be a hypothesis)", parent_id);
            }
        }
        if (err_out) *err_out = g_plan_err;
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
    snprintf(s->rationale, sizeof(s->rationale), "%s", rationale ? rationale : "");
    s->outcome[0] = '\0';
    s->created_at = (int64_t)time(NULL);
    s->updated_at = s->created_at;
    sting_timestamp_now(s->created, sizeof(s->created));
    memcpy(s->modified, s->created, sizeof(s->modified));

    if (persist && st->journal_path[0] != '\0') {
        char line[4000];
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
        persist_line_or_flag(st, &b, line);
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
    if ((strcmp(status, "done") == 0 || strcmp(status, "blocked") == 0) &&
        (!outcome || outcome[0] == '\0')) {
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
        char line[4000];
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
        persist_line_or_flag(st, &b, line);
    }

    return true;
}

static int g_color_mode = 0; /* 0: auto, 1: always, 2: never */

void sting_color_set_mode(int mode) {
    g_color_mode = mode;
}

int sting_color_get_mode(void) {
    return g_color_mode;
}

static bool should_use_color_fd(int fd) {
    if (g_color_mode == 1) return true;
    if (g_color_mode == 2) return false;

    /* Respect standard NO_COLOR (https://no-color.org) */
    const char *no_color = getenv("NO_COLOR");
    if (no_color && no_color[0] != '\0') return false;

    const char *term = getenv("TERM");
    if (!term || strcmp(term, "dumb") == 0) return false;

    return isatty(fd) != 0;
}

static sting_color_t sting_color_init_with_bool(bool enabled) {
    sting_color_t c;
    memset(&c, 0, sizeof(c));
    c.enabled = enabled;

    if (c.enabled) {
        c.reset       = "\033[0m";
        c.bold        = "\033[1m";
        c.dim         = "\033[2m";
        c.anchor      = "\033[1;36m";  /* Bold Cyan */
        c.p1          = "\033[1;31m";  /* Bold Red */
        c.p2          = "\033[33m";    /* Yellow */
        c.p3          = "\033[34m";    /* Blue */
        c.plow        = "\033[2m";     /* Dim */
        c.done        = "\033[1;32m";  /* Bold Green */
        c.in_progress = "\033[1;33m";  /* Bold Yellow */
        c.blocked     = "\033[1;31m";  /* Bold Red */
        c.pending     = "\033[36m";    /* Cyan */
        c.hyp         = "\033[1;35m";  /* Bold Magenta */
        c.evi         = "\033[1;32m";  /* Bold Green */
        c.ref         = "\033[1;31m";  /* Bold Red */
        c.obs         = "\033[36m";    /* Cyan */
        c.find        = "\033[1;33m";  /* Bold Yellow */
    } else {
        c.reset       = "";
        c.bold        = "";
        c.dim         = "";
        c.anchor      = "";
        c.p1          = "";
        c.p2          = "";
        c.p3          = "";
        c.plow        = "";
        c.done        = "";
        c.in_progress = "";
        c.blocked     = "";
        c.pending     = "";
        c.hyp         = "";
        c.evi         = "";
        c.ref         = "";
        c.obs         = "";
        c.find        = "";
    }
    return c;
}

sting_color_t sting_color_get(void) {
    return sting_color_init_with_bool(should_use_color_fd(1));
}

sting_color_t sting_color_get_for_stream(FILE *fp) {
    int fd = fp ? fileno(fp) : 1;
    return sting_color_init_with_bool(should_use_color_fd(fd));
}

const char *sting_color_status(const sting_color_t *c, const char *status) {
    if (!c || !c->enabled || !status) return "";
    if (strcmp(status, "done") == 0) return c->done;
    if (strcmp(status, "blocked") == 0) return c->blocked;
    if (strcmp(status, "in_progress") == 0) return c->in_progress;
    if (strcmp(status, "pending") == 0) return c->pending;
    return "";
}

const char *sting_color_priority(const sting_color_t *c, int priority) {
    if (!c || !c->enabled) return "";
    if (priority == 1) return c->p1;
    if (priority == 2) return c->p2;
    if (priority == 3) return c->p3;
    return c->plow;
}

const char *sting_color_kind(const sting_color_t *c, const char *kind) {
    if (!c || !c->enabled || !kind) return "";
    if (strcmp(kind, "hypothesis") == 0) return c->hyp;
    if (strcmp(kind, "evidence") == 0) return c->evi;
    if (strcmp(kind, "refutation") == 0) return c->ref;
    if (strcmp(kind, "finding") == 0) return c->find;
    if (strcmp(kind, "observation") == 0) return c->obs;
    return "";
}

const char *sting_color_severity(const sting_color_t *c, const char *sev) {
    if (!c || !c->enabled || !sev) return "";
    if (strcmp(sev, "critical") == 0) return c->blocked;
    if (strcmp(sev, "high") == 0) return c->p1;
    if (strcmp(sev, "medium") == 0) return c->p2;
    if (strcmp(sev, "low") == 0) return c->p3;
    if (strcmp(sev, "info") == 0) return c->dim;
    return "";
}

static void print_styled_diag(const char *label, const char *label_col, const char *msg) {
    sting_color_t c = sting_color_get_for_stream(stderr);
    if (!c.enabled) {
        fprintf(stderr, "%s: %s\n", label, msg);
        return;
    }

    const char *byel = "\033[1;93m";
    fprintf(stderr, "%s%s: ", label_col, label);
    for (const char *p = msg; *p != '\0';) {
        if (*p == '#' && p[1] >= '0' && p[1] <= '9') {
            fputs(byel, stderr);
            fputc(*p++, stderr);
            while (*p >= '0' && *p <= '9') {
                fputc(*p++, stderr);
            }
            fputs(label_col, stderr);
        } else {
            fputc(*p++, stderr);
        }
    }
    fprintf(stderr, "%s\n", c.reset);
}

void sting_print_error(const char *msg) {
    print_styled_diag("error", "\033[1;31m", msg ? msg : "unknown error");
}

void sting_print_warning(const char *msg) {
    print_styled_diag("warning", "\033[1;33m", msg ? msg : "warning");
}

void sting_print_hint(const char *msg) {
    print_styled_diag("hint", "\033[36m", msg ? msg : "");
}

static size_t count_ready_rebase_chains(const sting_state_t *st, bool *target_ready_out, uint32_t single_root_id) {
    size_t ready = 0;
    if (target_ready_out) *target_ready_out = false;
    for (size_t i = 0; i < st->node_count; i++) {
        uint32_t rid = st->nodes[i].id;
        if (single_root_id > 0 && rid != single_root_id) continue;
        uint32_t reach[1024];
        size_t cnt = collect_descendants(st, rid, reach, 1024);
        if (cnt <= 1) continue; /* No descendant entries/steps to compact */
        size_t unfinished = 0;
        for (size_t s = 0; s < st->step_count; s++) {
            bool in_sub = false;
            for (size_t k = 1; k < cnt; k++) {
                if (reach[k] == st->steps[s].id) { in_sub = true; break; }
            }
            if (in_sub &&
                strcmp(st->steps[s].status, "done") != 0 &&
                strcmp(st->steps[s].status, "blocked") != 0) {
                unfinished++;
            }
        }
        if (unfinished == 0) {
            ready++;
            if (target_ready_out && rid == single_root_id) *target_ready_out = true;
        }
    }
    return ready;
}

void sting_state_print_board(const sting_state_t *st) {
    if (!st) return;
    sting_color_t c = sting_color_get();

    printf("%s=== STINGTRAIL // CRITICAL THINKING BOARD ===%s\n", c.bold, c.reset);

    printf("%sNodes (%zu):%s\n", c.bold, st->node_count, c.reset);
    if (st->node_count == 0) {
        printf("  (none)\n");
    } else {
        for (size_t i = 0; i < st->node_count; i++) {
            size_t n_total = 0, n_inc = 0;
            sting_state_plan_counts(st, st->nodes[i].id, &n_total, &n_inc);
            printf("  %s[#%u]%s (%s) %s%s%s [incomplete plans: %zu/%zu] %s(created: %s)%s\n",
                   c.anchor, st->nodes[i].id, c.reset,
                   st->nodes[i].kind,
                   st->nodes[i].name,
                   st->nodes[i].detail[0] ? " — " : "",
                   st->nodes[i].detail[0] ? st->nodes[i].detail : "",
                   n_inc, n_total,
                   c.dim, st->nodes[i].created[0] ? st->nodes[i].created : "-", c.reset);
        }
    }

    printf("\n%sReasoning Log (%zu):%s\n", c.bold, st->entry_count, c.reset);
    if (st->entry_count == 0) {
        printf("  (none)\n");
    } else {
        for (size_t i = 0; i < st->entry_count; i++) {
            const char *k_col = sting_color_kind(&c, st->entries[i].kind);
            if (strcmp(st->entries[i].kind, "hypothesis") == 0) {
                size_t h_total = 0, h_inc = 0;
                sting_state_plan_counts(st, st->entries[i].id, &h_total, &h_inc);
                printf("  %s[#%u <- #%u]%s [%s%s%s/%s] %s%s%s [incomplete plans: %zu/%zu] %s(created: %s)%s\n",
                       c.anchor, st->entries[i].id, st->entries[i].parent_id, c.reset,
                       k_col, st->entries[i].kind, c.reset,
                       st->entries[i].severity,
                       st->entries[i].summary,
                       st->entries[i].detail[0] ? " | " : "",
                       st->entries[i].detail[0] ? st->entries[i].detail : "",
                       h_inc, h_total,
                       c.dim, st->entries[i].created[0] ? st->entries[i].created : "-", c.reset);
            } else {
                printf("  %s[#%u <- #%u]%s [%s%s%s/%s] %s%s%s %s(created: %s)%s\n",
                       c.anchor, st->entries[i].id, st->entries[i].parent_id, c.reset,
                       k_col, st->entries[i].kind, c.reset,
                       st->entries[i].severity,
                       st->entries[i].summary,
                       st->entries[i].detail[0] ? " | " : "",
                       st->entries[i].detail[0] ? st->entries[i].detail : "",
                       c.dim, st->entries[i].created[0] ? st->entries[i].created : "-", c.reset);
            }
        }
    }

    size_t all_total = 0, all_inc = 0;
    sting_state_plan_counts(st, 0, &all_total, &all_inc);
    printf("\n%sPlanned Next Steps (%zu, %zu incomplete):%s\n", c.bold, all_total, all_inc, c.reset);
    if (st->step_count == 0) {
        printf("  (none)\n");
    } else {
        /* Group and sort by priority P1 .. P5 */
        for (int p = 1; p <= 5; p++) {
            const char *p_col = sting_color_priority(&c, p);
            for (size_t i = 0; i < st->step_count; i++) {
                if (st->steps[i].priority != p) continue;
                const char *s_col = sting_color_status(&c, st->steps[i].status);
                printf("  %s[#%u <- #%u]%s [%sP%d%s][%s%s%s] %s%s%s%s%s %s(created: %s, modified: %s)%s\n",
                       c.anchor, st->steps[i].id, st->steps[i].parent_id, c.reset,
                       p_col, st->steps[i].priority, c.reset,
                       s_col, st->steps[i].status, c.reset,
                       st->steps[i].action,
                       st->steps[i].target[0] ? " (target: " : "",
                       st->steps[i].target[0] ? st->steps[i].target : "",
                       st->steps[i].target[0] ? ")" : "",
                       "",
                       c.dim,
                       st->steps[i].created[0] ? st->steps[i].created : "-",
                       st->steps[i].modified[0] ? st->steps[i].modified : "-",
                       c.reset);
                if (st->steps[i].rationale[0]) {
                    printf("      %swhy:%s %s\n", c.dim, c.reset, st->steps[i].rationale);
                }
                if (st->steps[i].outcome[0]) {
                    printf("      %soutcome:%s %s\n", c.dim, c.reset, st->steps[i].outcome);
                }
            }
        }
    }

    printf("\n%sContext & Hints:%s\n", c.bold, c.reset);
    if (st->journal_path[0] != '\0') {
        uint32_t ctx_id = sting_context_get(st->journal_path);
        if (ctx_id == 0) {
            printf("  Active context: %snone%s (hint: set with 'stingtrail context set <hyp_id>')\n", c.dim, c.reset);
        } else {
            const char *ctx_kind = NULL;
            const char *ctx_title = NULL;
            for (size_t i = 0; i < st->node_count && !ctx_kind; i++) {
                if (st->nodes[i].id == ctx_id) { ctx_kind = st->nodes[i].kind; ctx_title = st->nodes[i].name; }
            }
            for (size_t i = 0; i < st->entry_count && !ctx_kind; i++) {
                if (st->entries[i].id == ctx_id) { ctx_kind = st->entries[i].kind; ctx_title = st->entries[i].summary; }
            }
            for (size_t i = 0; i < st->step_count && !ctx_kind; i++) {
                if (st->steps[i].id == ctx_id) { ctx_kind = "step"; ctx_title = st->steps[i].action; }
            }
            if (!ctx_kind) {
                printf("  Active context: %s#%u%s %s(stale: id not found in graph — run 'stingtrail context clear')%s\n",
                       c.anchor, ctx_id, c.reset, c.in_progress, c.reset);
            } else if (strcmp(ctx_kind, "hypothesis") != 0) {
                printf("  Active context: %s#%u%s (%s: %s) %s[note: '%s' is not a hypothesis; 'plan'/'hook-eval' need a hypothesis id]%s\n",
                       c.anchor, ctx_id, c.reset, ctx_kind, ctx_title, c.in_progress, ctx_kind, c.reset);
            } else {
                printf("  Active context: %s#%u%s (%s%s%s: %s)\n",
                       c.anchor, ctx_id, c.reset, c.hyp, ctx_kind, c.reset, ctx_title);
            }
        }
    }

    bool high_cap = (st->node_count * 10 >= STINGTRAIL_MAX_NODES * 8) ||
                    (st->entry_count * 10 >= STINGTRAIL_MAX_ENTRIES * 8) ||
                    (st->step_count * 10 >= STINGTRAIL_MAX_STEPS * 8);
    printf("  Capacity: nodes %zu/%d, entries %zu/%d, steps %zu/%d%s\n",
           st->node_count, STINGTRAIL_MAX_NODES,
           st->entry_count, STINGTRAIL_MAX_ENTRIES,
           st->step_count, STINGTRAIL_MAX_STEPS,
           high_cap ? " [APPROACHING LIMIT]" : "");

    size_t ready_chains = count_ready_rebase_chains(st, NULL, 0);
    if (ready_chains > 0) {
        printf("  %sHint:%s %zu completed chain(s) ready to rebase — run 'stingtrail rebase --completed'\n",
               c.pending, c.reset, ready_chains);
    } else if (high_cap) {
        printf("  %sHint:%s journal nearing capacity — finish open steps and run 'stingtrail rebase --completed'\n",
               c.in_progress, c.reset);
    }
    if (st->dropped_nodes > 0 || st->dropped_entries > 0 || st->dropped_steps > 0 || st->dropped_long_lines > 0) {
        printf("  %sOverflow:%s dropped %zu node(s), %zu entry(s), %zu step(s), %zu oversized line(s) on load\n",
               c.blocked, c.reset, st->dropped_nodes, st->dropped_entries, st->dropped_steps, st->dropped_long_lines);
    }
}

bool sting_state_to_json(const sting_state_t *st, char *out_buf, size_t out_cap) {
    if (!st || !out_buf || out_cap == 0) return false;
    sting_buf_t b;
    sting_buf_init(&b, out_buf, out_cap);

    size_t total_plans = 0, incomplete_plans = 0;
    sting_state_plan_counts(st, 0, &total_plans, &incomplete_plans);

    sting_buf_append(&b, "{\"version\":\"" STINGTRAIL_VERSION "\",\"total_plans\":");
    sting_buf_append_int(&b, (long long)total_plans);
    sting_buf_append(&b, ",\"incomplete_plans\":");
    sting_buf_append_int(&b, (long long)incomplete_plans);
    sting_buf_append(&b, ",\"nodes\":[");
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

bool sting_state_print_focused(const sting_state_t *st, uint32_t target_id) {
    if (!st || target_id == 0) {
        sting_print_error("focus requires a valid node/entry/step ID (> 0)");
        return false;
    }

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
        char err_buf[128];
        snprintf(err_buf, sizeof(err_buf), "id #%u not found in graph", target_id);
        sting_print_error(err_buf);
        sting_print_hint("run 'stingtrail status' to list valid node, entry, and step IDs");
        return false;
    }

    sting_color_t c = sting_color_get();

    printf("%s=== STINGTRAIL // FOCUSED EXPERIMENT CONTEXT (%s#%u%s) ===%s\n\n",
           c.bold, c.anchor, target_id, c.bold, c.reset);

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

    printf("%sMotivating Lineage Spine:%s\n", c.bold, c.reset);
    if (spine_count == 0) {
        printf("  %s(Target is a root premise node)%s\n", c.dim, c.reset);
    } else {
        for (size_t k = spine_count; k > 0; k--) {
            uint32_t anc = spine[k - 1];
            size_t depth = spine_count - k;
            size_t shown = (depth > 16) ? 16 : depth;
            char indent[64];
            size_t pos = 0;
            for (size_t d = 0; d < shown; d++) {
                indent[pos++] = ' ';
                indent[pos++] = ' ';
            }
            if (depth > 16) {
                memcpy(indent + pos, "... ", 4);
                pos += 4;
            }
            if (depth > 0) {
                memcpy(indent + pos, "-> ", 3);
                pos += 3;
            } else {
                memcpy(indent + pos, "  ", 2);
                pos += 2;
            }
            indent[pos] = '\0';

            bool printed = false;
            for (size_t i = 0; i < st->node_count; i++) {
                if (st->nodes[i].id == anc) {
                    printf("%s%s[#%u]%s (%s) %s\n", indent, c.anchor, anc, c.reset, st->nodes[i].kind, st->nodes[i].name);
                    printed = true;
                    break;
                }
            }
            if (!printed) {
                for (size_t i = 0; i < st->entry_count; i++) {
                    if (st->entries[i].id == anc) {
                        const char *k_col = sting_color_kind(&c, st->entries[i].kind);
                        printf("%s%s[#%u]%s (%s%s%s) %s\n", indent, c.anchor, anc, c.reset, k_col, st->entries[i].kind, c.reset, st->entries[i].summary);
                        printed = true;
                        break;
                    }
                }
            }
            if (!printed) {
                for (size_t i = 0; i < st->step_count; i++) {
                    if (st->steps[i].id == anc) {
                        const char *p_col = sting_color_priority(&c, st->steps[i].priority);
                        const char *s_col = sting_color_status(&c, st->steps[i].status);
                        printf("%s%s[#%u]%s [%sP%d%s][%s%s%s] %s\n",
                               indent, c.anchor, anc, c.reset,
                               p_col, st->steps[i].priority, c.reset,
                               s_col, st->steps[i].status, c.reset,
                               st->steps[i].action);
                        printed = true;
                        break;
                    }
                }
            }
        }
    }

    /* 3. Target Node Details */
    size_t f_total = 0, f_inc = 0;
    sting_state_plan_counts(st, target_id, &f_total, &f_inc);
    printf("\n%sTarget Node (%s#%u%s):%s\n", c.bold, c.anchor, target_id, c.bold, c.reset);
    if (target_node) {
        printf("  %sKind:%s %s\n", c.dim, c.reset, target_node->kind);
        printf("  %sName:%s %s\n", c.dim, c.reset, target_node->name);
        if (target_node->detail[0]) printf("  %sDetail:%s %s\n", c.dim, c.reset, target_node->detail);
        printf("  %sIncomplete Plans:%s %zu / %zu\n", c.dim, c.reset, f_inc, f_total);
        printf("  %sCreated:%s %s\n", c.dim, c.reset, target_node->created);
    } else if (target_entry) {
        const char *k_col = sting_color_kind(&c, target_entry->kind);
        printf("  %sKind:%s %s%s%s (sev: %s)\n", c.dim, c.reset, k_col, target_entry->kind, c.reset, target_entry->severity);
        printf("  %sSummary:%s %s\n", c.dim, c.reset, target_entry->summary);
        if (target_entry->detail[0]) printf("  %sDetail:%s %s\n", c.dim, c.reset, target_entry->detail);
        printf("  %sIncomplete Plans:%s %zu / %zu\n", c.dim, c.reset, f_inc, f_total);
        printf("  %sCreated:%s %s\n", c.dim, c.reset, target_entry->created);
    } else if (target_step) {
        const char *p_col = sting_color_priority(&c, target_step->priority);
        const char *s_col = sting_color_status(&c, target_step->status);
        printf("  %sPriority:%s %sP%d%s | %sStatus:%s %s%s%s\n",
               c.dim, c.reset, p_col, target_step->priority, c.reset,
               c.dim, c.reset, s_col, target_step->status, c.reset);
        printf("  %sAction:%s %s\n", c.dim, c.reset, target_step->action);
        if (target_step->rationale[0]) printf("  %sRationale:%s %s\n", c.dim, c.reset, target_step->rationale);
        if (target_step->outcome[0]) printf("  %sOutcome:%s %s\n", c.dim, c.reset, target_step->outcome);
        printf("  %sIncomplete Plans:%s %zu / %zu\n", c.dim, c.reset, f_inc, f_total);
        printf("  %sCreated:%s %s | %sModified:%s %s\n",
               c.dim, c.reset, target_step->created,
               c.dim, c.reset, target_step->modified);
    }

    /* 4. Descendants Subtree (Topological DFS) */
    printf("\n%sEpistemic Subtree / Outcomes:%s\n", c.bold, c.reset);
    uint32_t reach[1024];
    size_t reach_cnt = collect_descendants(st, target_id, reach, 1024);

    if (reach_cnt <= 1) {
        printf("  %s(No descendant actions or evidence recorded yet)%s\n", c.dim, c.reset);
    } else {
        for (size_t r = 1; r < reach_cnt; r++) {
            uint32_t id = reach[r];
            for (size_t i = 0; i < st->entry_count; i++) {
                if (st->entries[i].id == id) {
                    const char *k_col = sting_color_kind(&c, st->entries[i].kind);
                    printf("  %s[#%u <- #%u]%s [%s%s%s/%s] %s\n",
                           c.anchor, id, st->entries[i].parent_id, c.reset,
                           k_col, st->entries[i].kind, c.reset,
                           st->entries[i].severity, st->entries[i].summary);
                    if (st->entries[i].detail[0]) {
                        printf("      %sdetail:%s %s\n", c.dim, c.reset, st->entries[i].detail);
                    }
                    break;
                }
            }
            for (size_t i = 0; i < st->step_count; i++) {
                if (st->steps[i].id == id) {
                    const char *p_col = sting_color_priority(&c, st->steps[i].priority);
                    const char *s_col = sting_color_status(&c, st->steps[i].status);
                    printf("  %s[#%u <- #%u]%s [%sP%d%s][%s%s%s] %s\n",
                           c.anchor, id, st->steps[i].parent_id, c.reset,
                           p_col, st->steps[i].priority, c.reset,
                           s_col, st->steps[i].status, c.reset,
                           st->steps[i].action);
                    if (st->steps[i].rationale[0]) {
                        printf("      %swhy:%s %s\n", c.dim, c.reset, st->steps[i].rationale);
                    }
                    if (st->steps[i].outcome[0]) {
                        printf("      %soutcome:%s %s\n", c.dim, c.reset, st->steps[i].outcome);
                    }
                    break;
                }
            }
        }
    }

    if (spine_count >= 64 || reach_cnt >= 1024) {
        printf("\n%sWarning:%s focused display hit traversal cap (spine: %zu/64, subtree: %zu/1024)\n",
               c.in_progress, c.reset, spine_count, reach_cnt);
    }
    if (target_entry && strcmp(target_entry->kind, "hypothesis") == 0 && f_total == 0) {
        printf("\n%sHint:%s propose a plan under this hypothesis: stingtrail plan \"<action>\" --parent %u\n",
               c.pending, c.reset, target_id);
    } else if (target_node) {
        bool ready = false;
        count_ready_rebase_chains(st, &ready, target_id);
        if (ready) {
            printf("\n%sHint:%s all steps under #%u are done/blocked — run 'stingtrail rebase %u' to compact\n",
                   c.pending, c.reset, target_id, target_id);
        }
    }
    return true;
}

bool sting_state_focused_to_json(const sting_state_t *st, uint32_t target_id, char *out_buf, size_t out_cap) {
    if (!st || target_id == 0 || !out_buf || out_cap == 0) return false;

    /* Locate target first; fail explicitly when target_id does not exist */
    bool target_found = false;
    uint32_t target_pid = 0;
    for (size_t i = 0; i < st->node_count; i++) {
        if (st->nodes[i].id == target_id) { target_found = true; target_pid = 0; break; }
    }
    if (!target_found) {
        for (size_t i = 0; i < st->entry_count; i++) {
            if (st->entries[i].id == target_id) { target_found = true; target_pid = st->entries[i].parent_id; break; }
        }
    }
    if (!target_found) {
        for (size_t i = 0; i < st->step_count; i++) {
            if (st->steps[i].id == target_id) { target_found = true; target_pid = st->steps[i].parent_id; break; }
        }
    }
    if (!target_found) return false;

    sting_buf_t b;
    sting_buf_init(&b, out_buf, out_cap);

    size_t f_total = 0, f_inc = 0;
    sting_state_plan_counts(st, target_id, &f_total, &f_inc);

    sting_buf_append(&b, "{\"version\":\"" STINGTRAIL_VERSION "\",\"traversal\":\"hourglass_v1\",\"target_id\":");
    sting_buf_append_int(&b, (long long)target_id);
    sting_buf_append(&b, ",\"total_plans\":");
    sting_buf_append_int(&b, (long long)f_total);
    sting_buf_append(&b, ",\"incomplete_plans\":");
    sting_buf_append_int(&b, (long long)f_inc);

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
    uint32_t reach[1024];
    size_t reach_cnt = 0;
    for (size_t k = 0; k < spine_count && reach_cnt < 1024; k++) {
        reach[reach_cnt++] = spine[k];
    }
    if (reach_cnt < 1024) {
        reach[reach_cnt++] = target_id;
    }

    for (int pass = 0; pass < 1024; pass++) {
        bool added = false;
        for (size_t i = 0; i < st->entry_count; i++) {
            uint32_t id = st->entries[i].id;
            uint32_t pid = st->entries[i].parent_id;
            bool has = false;
            for (size_t k = 0; k < reach_cnt; k++) { if (reach[k] == id) { has = true; break; } }
            if (has) continue;
            for (size_t k = 0; k < reach_cnt; k++) {
                if (reach[k] == pid) {
                    if (reach_cnt < 1024) { reach[reach_cnt++] = id; added = true; }
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
                    if (reach_cnt < 1024) { reach[reach_cnt++] = id; added = true; }
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
