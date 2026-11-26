#define _POSIX_C_SOURCE 200809L
#include "stingtrail.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define STING_MAX_TOTAL_ITEMS (STINGTRAIL_MAX_NODES + STINGTRAIL_MAX_ENTRIES + STINGTRAIL_MAX_STEPS)

static bool id_in_list(uint32_t id, const uint32_t *ids, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (ids[i] == id) return true;
    }
    return false;
}

static size_t collect_chain_ids(const sting_state_t *st, uint32_t root_id,
                                uint32_t *out_ids, size_t cap) {
    if (!st || root_id == 0 || !out_ids || cap == 0) return 0;
    size_t cnt = 0;
    out_ids[cnt++] = root_id;

    for (int pass = 0; pass < 32; pass++) {
        bool added = false;
        for (size_t i = 0; i < st->entry_count; i++) {
            uint32_t id = st->entries[i].id;
            uint32_t pid = st->entries[i].parent_id;
            if (!id_in_list(id, out_ids, cnt) && id_in_list(pid, out_ids, cnt)) {
                if (cnt < cap) {
                    out_ids[cnt++] = id;
                    added = true;
                }
            }
        }
        for (size_t i = 0; i < st->step_count; i++) {
            uint32_t id = st->steps[i].id;
            uint32_t pid = st->steps[i].parent_id;
            if (!id_in_list(id, out_ids, cnt) && id_in_list(pid, out_ids, cnt)) {
                if (cnt < cap) {
                    out_ids[cnt++] = id;
                    added = true;
                }
            }
        }
        if (!added) break;
    }
    return cnt;
}

static char g_rebase_err[512];

static bool chain_is_ready(const sting_state_t *st, const uint32_t *chain_ids, size_t chain_cnt,
                           size_t *entries_out, size_t *steps_out,
                           size_t *wins_out, size_t *fails_out,
                           uint32_t *bad_ids_out, size_t bad_cap, size_t *bad_total_out) {
    size_t e_cnt = 0, s_cnt = 0, wins = 0, fails = 0, bad_total = 0;

    for (size_t i = 0; i < st->entry_count; i++) {
        if (id_in_list(st->entries[i].id, chain_ids, chain_cnt)) {
            e_cnt++;
        }
    }
    for (size_t i = 0; i < st->step_count; i++) {
        if (id_in_list(st->steps[i].id, chain_ids, chain_cnt)) {
            s_cnt++;
            if (strcmp(st->steps[i].status, "done") == 0) {
                wins++;
            } else if (strcmp(st->steps[i].status, "blocked") == 0) {
                fails++;
            } else {
                if (bad_ids_out && bad_total < bad_cap) {
                    bad_ids_out[bad_total] = st->steps[i].id;
                }
                bad_total++;
            }
        }
    }

    if (entries_out) *entries_out = e_cnt;
    if (steps_out) *steps_out = s_cnt;
    if (wins_out) *wins_out = wins;
    if (fails_out) *fails_out = fails;
    if (bad_total_out) *bad_total_out = bad_total;
    return bad_total == 0;
}

static void slugify_snake_20(const char *title, char *out, size_t out_cap) {
    if (!out || out_cap == 0) return;
    const char *src = title ? title : "";
    if (strncmp(src, "[Rebased] ", 10) == 0) {
        src += 10;
    }

    size_t w = 0;
    bool prev_us = true;
    for (size_t i = 0; src[i] != '\0' && w < 20 && w + 1 < out_cap; i++) {
        unsigned char ch = (unsigned char)src[i];
        if (ch >= 'A' && ch <= 'Z') {
            out[w++] = (char)(ch + ('a' - 'A'));
            prev_us = false;
        } else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            out[w++] = (char)ch;
            prev_us = false;
        } else if (!prev_us) {
            out[w++] = '_';
            prev_us = true;
        }
    }
    while (w > 0 && out[w - 1] == '_') {
        w--;
    }
    out[w] = '\0';
    if (w == 0 && out_cap > 8) {
        snprintf(out, out_cap, "summary");
    }
}

static void build_summary_path(const char *journal_path, uint32_t root_id, const char *title,
                               const char *ts, bool create_dir,
                               char *fs_path_out, size_t fs_cap,
                               char *rel_label_out, size_t rel_cap) {
    const char *jp = (journal_path && journal_path[0]) ? journal_path : "stingtrail.jsonl";
    const char *slash = strrchr(jp, '/');
    char dir[256];

    if (slash) {
        size_t dlen = (size_t)(slash - jp);
        if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1;
        memcpy(dir, jp, dlen);
        dir[dlen] = '\0';
    } else {
        snprintf(dir, sizeof(dir), ".");
    }

    char slug[24];
    slugify_snake_20(title, slug, sizeof(slug));

    char dir_path[300];
    if (strcmp(dir, ".") == 0) {
        snprintf(dir_path, sizeof(dir_path), ".stingtrail");
    } else {
        snprintf(dir_path, sizeof(dir_path), "%.255s/.stingtrail", dir);
    }
    if (create_dir) {
        mkdir(dir_path, 0755);
    }

    snprintf(rel_label_out, rel_cap, ".stingtrail/%.14s-stingtrail_%u_%.20s.md", ts, root_id, slug);
    snprintf(fs_path_out, fs_cap, "%.300s/%.14s-stingtrail_%u_%.20s.md", dir_path, ts, root_id, slug);
}

static bool write_chain_markdown(const sting_state_t *st, uint32_t root_id,
                                 const char *root_title, const char *root_kind,
                                 const uint32_t *chain_ids, size_t chain_cnt,
                                 size_t wins, size_t fails, size_t entries_cnt,
                                 const char *fs_path, const char *ts) {
    FILE *f = fopen(fs_path, "w");
    if (!f) return false;

    fprintf(f, "# Super Summary: %s (#%u)\n\n", root_title, root_id);
    fprintf(f, "- **Source**: `%s`\n", st->journal_path[0] ? st->journal_path : "(in-memory)");
    fprintf(f, "- **Root Kind**: `%s` | **Rebased**: `%s`\n", root_kind, ts);
    fprintf(f, "- **Stats**: %zu wins (`done`), %zu fails (`blocked`), %zu entries\n\n",
            wins, fails, entries_cnt);

    fprintf(f, "## Facts & Outcomes (Wins)\n");
    bool any_win = false;
    for (size_t i = 0; i < st->entry_count; i++) {
        const sting_entry_t *e = &st->entries[i];
        if (!id_in_list(e->id, chain_ids, chain_cnt)) continue;
        if (strcmp(e->kind, "evidence") == 0 || strcmp(e->kind, "finding") == 0) {
            fprintf(f, "- **[%s]** (#%u) %s", e->kind, e->id, e->summary);
            if (e->detail[0]) fprintf(f, " — %s", e->detail);
            fprintf(f, "\n");
            any_win = true;
        }
    }
    for (size_t i = 0; i < st->step_count; i++) {
        const sting_step_t *s = &st->steps[i];
        if (!id_in_list(s->id, chain_ids, chain_cnt)) continue;
        if (strcmp(s->status, "done") == 0) {
            fprintf(f, "- **[done]** (#%u) %s", s->id, s->action);
            if (s->outcome[0]) fprintf(f, " -> %s", s->outcome);
            fprintf(f, "\n");
            any_win = true;
        }
    }
    if (!any_win) fprintf(f, "- (none)\n");

    fprintf(f, "\n## Dead-Ends & Refutations (Fails)\n");
    bool any_fail = false;
    for (size_t i = 0; i < st->entry_count; i++) {
        const sting_entry_t *e = &st->entries[i];
        if (!id_in_list(e->id, chain_ids, chain_cnt)) continue;
        if (strcmp(e->kind, "refutation") == 0) {
            fprintf(f, "- **[refutation]** (#%u) %s", e->id, e->summary);
            if (e->detail[0]) fprintf(f, " — %s", e->detail);
            fprintf(f, "\n");
            any_fail = true;
        }
    }
    for (size_t i = 0; i < st->step_count; i++) {
        const sting_step_t *s = &st->steps[i];
        if (!id_in_list(s->id, chain_ids, chain_cnt)) continue;
        if (strcmp(s->status, "blocked") == 0) {
            fprintf(f, "- **[blocked]** (#%u) %s", s->id, s->action);
            if (s->outcome[0]) fprintf(f, " -> %s", s->outcome);
            fprintf(f, "\n");
            any_fail = true;
        }
    }
    if (!any_fail) fprintf(f, "- (none)\n");

    fprintf(f, "\n## Hypotheses & Context Helpers\n");
    bool any_ctx = false;
    for (size_t i = 0; i < st->entry_count; i++) {
        const sting_entry_t *e = &st->entries[i];
        if (!id_in_list(e->id, chain_ids, chain_cnt)) continue;
        if (strcmp(e->kind, "hypothesis") == 0 || strcmp(e->kind, "observation") == 0) {
            fprintf(f, "- **[%s/%s]** (#%u <- #%u) %s",
                    e->kind, e->severity, e->id, e->parent_id, e->summary);
            if (e->detail[0]) fprintf(f, " | %s", e->detail);
            fprintf(f, "\n");
            any_ctx = true;
        }
    }
    if (!any_ctx) fprintf(f, "- (none)\n");

    int rc = fflush(f);
    int fd = fileno(f);
    if (fd >= 0) fsync(fd);
    fclose(f);
    return rc == 0;
}

static bool write_line_fd(int fd, const char *line) {
    size_t len = strlen(line);
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, line + off, len - off);
        if (w <= 0) return false;
        off += (size_t)w;
    }
    return write(fd, "\n", 1) == 1;
}

static bool persist_compacted_journal(const sting_state_t *st) {
    if (!st || st->journal_path[0] == '\0') return true;

    char tmp_path[528];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", st->journal_path);
    int fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;

    char line[2048];
    sting_buf_t b;
    bool ok = true;

    for (size_t i = 0; i < st->node_count && ok; i++) {
        const sting_node_t *n = &st->nodes[i];
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
        if (b.overflow || !write_line_fd(fd, line)) ok = false;
    }

    for (size_t i = 0; i < st->entry_count && ok; i++) {
        const sting_entry_t *e = &st->entries[i];
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
        if (b.overflow || !write_line_fd(fd, line)) ok = false;
    }

    for (size_t i = 0; i < st->step_count && ok; i++) {
        const sting_step_t *s = &st->steps[i];
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
        if (b.overflow || !write_line_fd(fd, line)) {
            ok = false;
            break;
        }
        if (strcmp(s->status, "pending") != 0 || s->outcome[0] != '\0') {
            sting_buf_init(&b, line, sizeof(line));
            sting_buf_append(&b, "{\"op\":\"step_update\",\"step_id\":");
            sting_buf_append_int(&b, (long long)s->id);
            sting_buf_append(&b, ",\"status\":");
            sting_buf_append_json_str(&b, s->status);
            sting_buf_append(&b, ",\"outcome\":");
            sting_buf_append_json_str(&b, s->outcome);
            sting_buf_append(&b, ",\"created\":");
            sting_buf_append_json_str(&b, s->created);
            sting_buf_append(&b, ",\"modified\":");
            sting_buf_append_json_str(&b, s->modified);
            sting_buf_append_ch(&b, '}');
            if (b.overflow || !write_line_fd(fd, line)) ok = false;
        }
    }

    if (ok) fsync(fd);
    close(fd);

    if (!ok || rename(tmp_path, st->journal_path) != 0) {
        unlink(tmp_path);
        return false;
    }
    return true;
}

static const char *find_top_fact(const sting_state_t *st, const uint32_t *chain_ids, size_t chain_cnt) {
    for (size_t i = st->entry_count; i > 0; i--) {
        const sting_entry_t *e = &st->entries[i - 1];
        if (!id_in_list(e->id, chain_ids, chain_cnt)) continue;
        if (strcmp(e->kind, "evidence") == 0 || strcmp(e->kind, "finding") == 0) {
            return e->summary;
        }
    }
    for (size_t i = st->step_count; i > 0; i--) {
        const sting_step_t *s = &st->steps[i - 1];
        if (!id_in_list(s->id, chain_ids, chain_cnt)) continue;
        if (strcmp(s->status, "done") == 0 && s->outcome[0]) {
            return s->outcome;
        }
    }
    return "";
}

static void compact_chain_from_state(sting_state_t *st, const uint32_t *chain_ids, size_t chain_cnt,
                                     const sting_node_t *super_node) {
    size_t w_n = 0;
    for (size_t i = 0; i < st->node_count; i++) {
        if (!id_in_list(st->nodes[i].id, chain_ids, chain_cnt)) {
            st->nodes[w_n++] = st->nodes[i];
        }
    }
    st->node_count = w_n;

    size_t w_e = 0;
    for (size_t i = 0; i < st->entry_count; i++) {
        if (!id_in_list(st->entries[i].id, chain_ids, chain_cnt)) {
            st->entries[w_e++] = st->entries[i];
        }
    }
    st->entry_count = w_e;

    size_t w_s = 0;
    for (size_t i = 0; i < st->step_count; i++) {
        if (!id_in_list(st->steps[i].id, chain_ids, chain_cnt)) {
            st->steps[w_s++] = st->steps[i];
        }
    }
    st->step_count = w_s;

    if (st->node_count < STINGTRAIL_MAX_NODES) {
        st->nodes[st->node_count++] = *super_node;
    }
}

static void format_unfinished_err(uint32_t root_id, const uint32_t *bad_ids, size_t bad_total) {
    sting_buf_t b;
    sting_buf_init(&b, g_rebase_err, sizeof(g_rebase_err));
    sting_buf_append(&b, "cannot rebase #");
    sting_buf_append_int(&b, (long long)root_id);
    sting_buf_append(&b, ": unfinished plan(s) at ");
    size_t shown = (bad_total < 5) ? bad_total : 5;
    for (size_t i = 0; i < shown; i++) {
        if (i > 0) sting_buf_append(&b, ", ");
        sting_buf_append_ch(&b, '#');
        sting_buf_append_int(&b, (long long)bad_ids[i]);
    }
    if (bad_total > shown) {
        sting_buf_append(&b, " (+");
        sting_buf_append_int(&b, (long long)(bad_total - shown));
        sting_buf_append(&b, " more)");
    }
    sting_buf_append(&b, " (all plans must be done or blocked)");
}

static bool rebase_single_chain(sting_state_t *st, uint32_t root_id, const char *custom_title,
                                bool dry_run, uint32_t *sim_next_id, bool *unfinished_out,
                                sting_rebase_result_t *res, const char **err_out) {
    if (unfinished_out) *unfinished_out = false;
    const char *orig_title = NULL;
    const char *orig_kind = "premise";
    bool is_root_node = false;

    for (size_t i = 0; i < st->node_count; i++) {
        if (st->nodes[i].id == root_id) {
            orig_title = st->nodes[i].name;
            orig_kind = st->nodes[i].kind;
            is_root_node = true;
            break;
        }
    }
    if (!orig_title) {
        for (size_t i = 0; i < st->entry_count; i++) {
            if (st->entries[i].id == root_id) {
                orig_title = st->entries[i].summary;
                orig_kind = st->entries[i].kind;
                break;
            }
        }
    }
    if (!orig_title) {
        snprintf(g_rebase_err, sizeof(g_rebase_err), "node #%u not found among nodes or entries", root_id);
        if (err_out) *err_out = g_rebase_err;
        return false;
    }

    uint32_t chain_ids[STING_MAX_TOTAL_ITEMS];
    size_t chain_cnt = collect_chain_ids(st, root_id, chain_ids, STING_MAX_TOTAL_ITEMS);
    size_t e_cnt = 0, s_cnt = 0, wins = 0, fails = 0, bad_total = 0;
    uint32_t bad_ids[5];

    if (!chain_is_ready(st, chain_ids, chain_cnt, &e_cnt, &s_cnt, &wins, &fails, bad_ids, 5, &bad_total)) {
        if (unfinished_out) *unfinished_out = true;
        format_unfinished_err(root_id, bad_ids, bad_total);
        if (err_out) *err_out = g_rebase_err;
        return false;
    }
    if (e_cnt == 0 && s_cnt == 0) {
        snprintf(g_rebase_err, sizeof(g_rebase_err), "cannot rebase #%u: chain has no entries or steps to summarize", root_id);
        if (err_out) *err_out = g_rebase_err;
        return false;
    }
    if (!is_root_node && st->node_count >= STINGTRAIL_MAX_NODES) {
        snprintf(g_rebase_err, sizeof(g_rebase_err), "cannot rebase #%u: node capacity (%d) reached; rebase a root node first",
                 root_id, STINGTRAIL_MAX_NODES);
        if (err_out) *err_out = g_rebase_err;
        return false;
    }

    char ts[16];
    sting_timestamp_now(ts, sizeof(ts));

    const char *base_name = (custom_title && custom_title[0]) ? custom_title : orig_title;

    char fs_path[512];
    char rel_label[128];
    build_summary_path(st->journal_path, root_id, base_name, ts, !dry_run,
                       fs_path, sizeof(fs_path), rel_label, sizeof(rel_label));
    if (!dry_run) {
        if (!write_chain_markdown(st, root_id, orig_title, orig_kind,
                                  chain_ids, chain_cnt, wins, fails, e_cnt, fs_path, ts)) {
            snprintf(g_rebase_err, sizeof(g_rebase_err), "failed to write summary file for #%u", root_id);
            if (err_out) *err_out = g_rebase_err;
            return false;
        }
    }

    sting_node_t super_node;
    memset(&super_node, 0, sizeof(super_node));
    super_node.id = dry_run ? (*sim_next_id)++ : st->next_id++;
    snprintf(super_node.kind, sizeof(super_node.kind), "premise");

    if (strncmp(base_name, "[Rebased] ", 10) == 0) {
        snprintf(super_node.name, sizeof(super_node.name), "%.127s", base_name);
    } else {
        snprintf(super_node.name, sizeof(super_node.name), "[Rebased] %.117s", base_name);
    }

    const char *top_fact = find_top_fact(st, chain_ids, chain_cnt);
    if (top_fact[0]) {
        snprintf(super_node.detail, sizeof(super_node.detail),
                 "Rebased #%u (%zu done, %zu blocked) -> %.96s | %.80s",
                 root_id, wins, fails, rel_label, top_fact);
    } else {
        snprintf(super_node.detail, sizeof(super_node.detail),
                 "Rebased #%u (%zu done, %zu blocked) -> %.127s",
                 root_id, wins, fails, rel_label);
    }
    super_node.created_at = (int64_t)time(NULL);
    snprintf(super_node.created, sizeof(super_node.created), "%s", ts);
    snprintf(super_node.modified, sizeof(super_node.modified), "%s", ts);

    if (!dry_run) {
        uint32_t active_ctx = sting_context_get(st->journal_path);
        if (active_ctx != 0 && id_in_list(active_ctx, chain_ids, chain_cnt)) {
            sting_context_clear(st->journal_path);
        }
        compact_chain_from_state(st, chain_ids, chain_cnt, &super_node);
    }

    res->chains_rebased++;
    res->nodes_removed += is_root_node ? 1 : 0;
    res->entries_removed += e_cnt;
    res->steps_removed += s_cnt;
    res->wins += wins;
    res->fails += fails;
    res->last_super_id = super_node.id;
    snprintf(res->summary_path, sizeof(res->summary_path), "%s", rel_label);
    return true;
}

bool sting_state_rebase(sting_state_t *st,
                        const sting_rebase_opts_t *opts,
                        sting_rebase_result_t *res_out,
                        const char **err_out) {
    if (!st || !opts) {
        if (err_out) *err_out = "invalid rebase arguments";
        return false;
    }

    sting_rebase_result_t res;
    memset(&res, 0, sizeof(res));
    uint32_t sim_next_id = st->next_id;

    if (opts->all_completed) {
        uint32_t candidate_ids[STINGTRAIL_MAX_NODES];
        size_t cand_cnt = 0;
        for (size_t i = 0; i < st->node_count; i++) {
            candidate_ids[cand_cnt++] = st->nodes[i].id;
        }
        uint32_t blocked_roots[8];
        size_t blocked_cnt = 0;
        for (size_t i = 0; i < cand_cnt; i++) {
            const char *ignore_err = NULL;
            bool was_unfinished = false;
            if (!rebase_single_chain(st, candidate_ids[i], NULL, opts->dry_run, &sim_next_id, &was_unfinished, &res, &ignore_err)) {
                if (was_unfinished && blocked_cnt < 8) {
                    blocked_roots[blocked_cnt++] = candidate_ids[i];
                }
            }
        }
        if (res.chains_rebased == 0) {
            if (blocked_cnt > 0) {
                sting_buf_t b;
                sting_buf_init(&b, g_rebase_err, sizeof(g_rebase_err));
                sting_buf_append(&b, "no completed chains ready to rebase (unfinished plans under ");
                for (size_t i = 0; i < blocked_cnt; i++) {
                    if (i > 0) sting_buf_append(&b, ", ");
                    sting_buf_append_ch(&b, '#');
                    sting_buf_append_int(&b, (long long)blocked_roots[i]);
                }
                sting_buf_append_ch(&b, ')');
            } else {
                snprintf(g_rebase_err, sizeof(g_rebase_err),
                         "no completed chains ready to rebase (all chains already compact)");
            }
            if (err_out) *err_out = g_rebase_err;
            return false;
        }
    } else {
        if (opts->target_id == 0) {
            if (err_out) *err_out = "rebase requires <id> or --completed";
            return false;
        }
        bool was_unfinished = false;
        if (!rebase_single_chain(st, opts->target_id, opts->title, opts->dry_run, &sim_next_id, &was_unfinished, &res, err_out)) {
            return false;
        }
    }

    if (!opts->dry_run && st->journal_path[0] != '\0') {
        if (!persist_compacted_journal(st)) {
            if (err_out) *err_out = "failed to atomically rewrite journal";
            return false;
        }
    }

    if (res_out) *res_out = res;
    return true;
}
