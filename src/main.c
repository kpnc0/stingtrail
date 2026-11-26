#define _POSIX_C_SOURCE 200809L
#include "stingtrail.h"
#include "stingtrail_mcp.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cli_vdiag(void (*fn)(const char *), const char *fmt, va_list ap) {
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    fn(buf);
}

static void cli_errf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    cli_vdiag(sting_print_error, fmt, ap);
    va_end(ap);
}

static void cli_warnf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    cli_vdiag(sting_print_warning, fmt, ap);
    va_end(ap);
}

static void cli_hintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    cli_vdiag(sting_print_hint, fmt, ap);
    va_end(ap);
}

static void warn_if_truncated(const char *field, const char *val, size_t field_cap) {
    if (!val || field_cap == 0) return;
    size_t len = strlen(val);
    if (len >= field_cap) {
        cli_warnf("field '%s' truncated from %zu to %zu chars", field, len, field_cap - 1);
    }
}

static void warn_capacity_high(const char *what, size_t count, size_t max_cap) {
    if (max_cap > 0 && count * 10 >= max_cap * 8) {
        cli_warnf("%s capacity at %zu/%zu (%zu%%)", what, count, max_cap, (count * 100) / max_cap);
        cli_hintf("run 'stingtrail rebase --completed' to compact finished chains into .stingtrail/*.md");
    }
}

static bool find_graph_item(const sting_state_t *st, uint32_t id,
                            const char **kind_out, const char **title_out) {
    if (!st || id == 0) return false;
    for (size_t i = 0; i < st->node_count; i++) {
        if (st->nodes[i].id == id) {
            if (kind_out) *kind_out = st->nodes[i].kind;
            if (title_out) *title_out = st->nodes[i].name;
            return true;
        }
    }
    for (size_t i = 0; i < st->entry_count; i++) {
        if (st->entries[i].id == id) {
            if (kind_out) *kind_out = st->entries[i].kind;
            if (title_out) *title_out = st->entries[i].summary;
            return true;
        }
    }
    for (size_t i = 0; i < st->step_count; i++) {
        if (st->steps[i].id == id) {
            if (kind_out) *kind_out = "step";
            if (title_out) *title_out = st->steps[i].action;
            return true;
        }
    }
    return false;
}

static int emit_json_export(const sting_state_t *st, uint32_t focus_id) {
    const size_t cap = 262144;
    char *buf = malloc(cap);
    if (!buf) {
        cli_errf("failed to allocate %zu-byte JSON export buffer", cap);
        return 1;
    }

    if (focus_id > 0 && !find_graph_item(st, focus_id, NULL, NULL)) {
        free(buf);
        cli_errf("id #%u not found in graph", focus_id);
        cli_hintf("run 'stingtrail status' to list valid node, entry, and step IDs");
        return 1;
    }

    bool ok = (focus_id > 0)
        ? sting_state_focused_to_json(st, focus_id, buf, cap)
        : sting_state_to_json(st, buf, cap);
    if (!ok) {
        free(buf);
        cli_errf("JSON serialization overflowed %zu-byte buffer", cap);
        cli_hintf("use 'stingtrail focus <id> --json' for a subtree or 'stingtrail rebase --completed' to compact");
        return 1;
    }

    size_t len = strlen(buf);
    if (focus_id == 0 && len >= 52428) {
        cli_warnf("JSON export is %zu bytes (nearing 65536-byte fixed tool limit)", len);
        cli_hintf("run 'stingtrail rebase --completed' or use 'stingtrail focus <id> --json'");
    }
    printf("%s\n", buf);
    free(buf);
    return 0;
}

static void print_usage(FILE *out, const char *prog) {
    if (!out) out = stderr;
    sting_color_t c = sting_color_get_for_stream(out);

    fprintf(out,
        "%sStingtrail v%s%s — %sMinimalist Critical-Thinking CLI for Agents%s\n\n"
        "%sUsage:%s\n"
        "  %s%s%s %s[--db <file.jsonl>] [--scope <hosts>] [--color[=auto|always|never]] [--no-color]%s %s<command>%s %s[args...]%s\n\n"
        "%sCommands:%s\n"
        "  %snode%s %s<kind> <name> [detail]%s                                 Record a root node\n"
        "       %skinds:%s %spremise%s, %sasset%s, %scontext%s\n"
        "  %slog%s %s<kind> <summary> [--parent N] [--sev S] [--detail D]%s    Append reasoning log\n"
        "       %skinds:%s %sobservation%s, %shypothesis%s, %sevidence%s, %srefutation%s, %sfinding%s\n"
        "       %sseverities:%s %sinfo%s, %slow%s, %smedium%s, %shigh%s, %scritical%s\n"
        "  %splan%s %s<action> --parent <hyp_id> [--why <rationale>] [--prio 1-5] [--target T]%s\n"
        "       Propose a prioritized action step under an existing hypothesis (--why optional)\n"
        "  %sstep%s %s<id> <status> [outcome]%s                                Update step state\n"
        "       %sstatuses:%s %spending%s, %sin_progress%s, %sdone%s, %sblocked%s (outcome required for done/blocked)\n"
        "  %sstatus%s %s[--json] [--focus <id>]%s                              Display board or focused subtree\n"
        "  %sfocus%s %s<id> [--json]%s                                         Display focused context (hourglass traversal)\n"
        "  %sexport%s %s[--focus <id>]%s                                       Export full or focused state JSON\n"
        "  %srebase%s %s[<id> | --completed] [--dry-run] [--title <name>]%s  Compact completed chain(s) into .stingtrail/<ts>-stingtrail_<id>_<slug>.md\n"
        "  %shook%s                                                        Print synchronous shell hook integration script\n"
        "  %shook-eval%s %s<command>%s                                         Evaluate command under guard and active context\n"
        "  %scontext%s %s[get | set <id> | clear]%s                            Manage active hypothesis context\n"
        "  %smcp%s                                                         Run MCP JSON-RPC stdio server\n"
        "  %sversion%s                                                     Print version\n"
        "  %shelp%s                                                        Show this help\n",
        c.bold, STINGTRAIL_VERSION, c.reset, c.dim, c.reset,
        c.bold, c.reset,
        c.anchor, prog, c.reset, c.dim, c.reset, c.anchor, c.reset, c.dim, c.reset,
        c.bold, c.reset,
        c.anchor, c.reset, c.dim, c.reset,
        c.dim, c.reset, c.anchor, c.reset, c.pending, c.reset, c.dim, c.reset,
        c.anchor, c.reset, c.dim, c.reset,
        c.dim, c.reset, c.obs, c.reset, c.hyp, c.reset, c.evi, c.reset, c.ref, c.reset, c.find, c.reset,
        c.dim, c.reset, c.dim, c.reset, c.p3, c.reset, c.p2, c.reset, c.p1, c.reset, c.blocked, c.reset,
        c.anchor, c.reset, c.dim, c.reset,
        c.anchor, c.reset, c.dim, c.reset,
        c.dim, c.reset, c.pending, c.reset, c.in_progress, c.reset, c.done, c.reset, c.blocked, c.reset,
        c.anchor, c.reset, c.dim, c.reset,
        c.anchor, c.reset, c.dim, c.reset,
        c.anchor, c.reset, c.dim, c.reset,
        c.anchor, c.reset, c.dim, c.reset,
        c.anchor, c.reset,
        c.anchor, c.reset, c.dim, c.reset,
        c.anchor, c.reset, c.dim, c.reset,
        c.anchor, c.reset,
        c.anchor, c.reset,
        c.anchor, c.reset);
}

int main(int argc, char **argv) {
    const char *db_path = "stingtrail.jsonl";
    const char *scope_csv = NULL;
    int arg_idx = 1;

    while (arg_idx < argc && argv[arg_idx][0] == '-') {
        if (strcmp(argv[arg_idx], "--db") == 0) {
            if (arg_idx + 1 >= argc) {
                cli_errf("--db requires a file path");
                return 1;
            }
            db_path = argv[++arg_idx];
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "--scope") == 0) {
            if (arg_idx + 1 >= argc) {
                cli_errf("--scope requires a host list");
                return 1;
            }
            scope_csv = argv[++arg_idx];
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "--color") == 0) {
            sting_color_set_mode(1);
            arg_idx++;
        } else if (strncmp(argv[arg_idx], "--color=", 8) == 0) {
            const char *val = argv[arg_idx] + 8;
            if (strcmp(val, "always") == 0) sting_color_set_mode(1);
            else if (strcmp(val, "never") == 0) sting_color_set_mode(2);
            else sting_color_set_mode(0);
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "--no-color") == 0) {
            sting_color_set_mode(2);
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "--help") == 0 || strcmp(argv[arg_idx], "-h") == 0) {
            print_usage(stdout, argv[0]);
            return 0;
        } else if (strcmp(argv[arg_idx], "--version") == 0 || strcmp(argv[arg_idx], "-v") == 0) {
            printf("stingtrail %s\n", STINGTRAIL_VERSION);
            return 0;
        } else {
            cli_errf("unrecognized option '%s'", argv[arg_idx]);
            return 1;
        }
    }

    if (arg_idx >= argc) {
        print_usage(stderr, argv[0]);
        return 1;
    }

    const char *cmd = argv[arg_idx++];
    if (strcmp(cmd, "help") == 0) {
        print_usage(stdout, argv[0]);
        return 0;
    }
    if (strcmp(cmd, "version") == 0) {
        printf("stingtrail %s\n", STINGTRAIL_VERSION);
        return 0;
    }

    sting_state_t st;
    sting_state_init(&st, db_path);

    if (strcmp(cmd, "mcp") != 0 && strcmp(cmd, "mcp-stdio") != 0 &&
        (st.dropped_nodes > 0 || st.dropped_entries > 0 ||
         st.dropped_steps > 0 || st.dropped_long_lines > 0)) {
        cli_warnf("journal load overflow: dropped %zu node(s), %zu entry(s), %zu step(s), %zu oversized line(s)",
                  st.dropped_nodes, st.dropped_entries, st.dropped_steps, st.dropped_long_lines);
        cli_hintf("run 'stingtrail rebase --completed' to compact or split into a new --db journal");
    }

    if (scope_csv) {
        sting_scope_init(&st.scope, true);
        sting_scope_parse_csv(&st.scope, scope_csv);
    }

    if (strcmp(cmd, "node") == 0) {
        if (arg_idx >= argc) {
            cli_errf("node requires <kind> and <name>");
            return 1;
        }
        const char *kind = argv[arg_idx++];
        if (arg_idx >= argc) {
            cli_errf("node requires <name>");
            return 1;
        }
        const char *name = argv[arg_idx++];
        const char *detail = (arg_idx < argc) ? argv[arg_idx++] : "";

        if (strcmp(kind, "premise") != 0 && strcmp(kind, "asset") != 0 && strcmp(kind, "context") != 0) {
            cli_warnf("unknown node kind '%s'; defaulted to 'asset' (valid: premise, asset, context)", kind);
        }

        size_t prev_nodes = st.node_count;
        uint32_t id = sting_state_add_node(&st, kind, name, detail, true);
        if (id == 0) {
            cli_errf("failed to add node '%s' (node capacity %zu/%d)", name, st.node_count, STINGTRAIL_MAX_NODES);
            cli_hintf("run 'stingtrail rebase --completed' to compact completed chains");
            return 1;
        }
        warn_if_truncated("name", name, 128);
        warn_if_truncated("detail", detail, 256);
        if (st.persist_overflow) {
            cli_warnf("node #%u exceeded persistence line buffer or failed disk write", id);
        }
        sting_color_t c = sting_color_get();
        if (st.node_count == prev_nodes) {
            printf("node %s#%u%s recorded (matched existing %s node)\n", c.anchor, id, c.reset, kind);
            if (detail[0] != '\0') {
                for (size_t i = 0; i < st.node_count; i++) {
                    if (st.nodes[i].id == id && strcmp(st.nodes[i].detail, detail) != 0) {
                        cli_warnf("existing node #%u reused; provided detail was ignored", id);
                        break;
                    }
                }
            }
        } else {
            printf("node %s#%u%s recorded\n", c.anchor, id, c.reset);
        }
        warn_capacity_high("node", st.node_count, STINGTRAIL_MAX_NODES);
        return 0;
    }

    if (strcmp(cmd, "log") == 0) {
        if (arg_idx >= argc) {
            cli_errf("log requires <kind> and <summary>");
            return 1;
        }
        const char *kind = argv[arg_idx++];
        if (arg_idx >= argc) {
            cli_errf("log requires <summary>");
            return 1;
        }
        const char *summary = argv[arg_idx++];
        uint32_t parent_id = 0;
        const char *severity = "info";
        const char *detail = "";

        while (arg_idx < argc) {
            if (strcmp(argv[arg_idx], "--parent") == 0 && arg_idx + 1 < argc) {
                parent_id = (uint32_t)strtoul(argv[++arg_idx], NULL, 10);
                arg_idx++;
            } else if (strcmp(argv[arg_idx], "--sev") == 0 && arg_idx + 1 < argc) {
                severity = argv[++arg_idx];
                arg_idx++;
            } else if (strcmp(argv[arg_idx], "--detail") == 0 && arg_idx + 1 < argc) {
                detail = argv[++arg_idx];
                arg_idx++;
            } else {
                cli_errf("unrecognized log argument '%s' (expected --parent N, --sev S, --detail D)", argv[arg_idx]);
                return 1;
            }
        }

        if (strcmp(kind, "observation") != 0 && strcmp(kind, "hypothesis") != 0 &&
            strcmp(kind, "evidence") != 0 && strcmp(kind, "refutation") != 0 &&
            strcmp(kind, "finding") != 0) {
            cli_warnf("unknown log kind '%s'; defaulted to 'observation' (valid: observation, hypothesis, evidence, refutation, finding)", kind);
        }
        if (strcmp(severity, "info") != 0 && strcmp(severity, "low") != 0 &&
            strcmp(severity, "medium") != 0 && strcmp(severity, "high") != 0 &&
            strcmp(severity, "critical") != 0) {
            cli_warnf("unknown severity '%s'; defaulted to 'info' (valid: info, low, medium, high, critical)", severity);
        }

        uint32_t id = sting_state_add_entry(&st, parent_id, kind, severity, summary, detail, true);
        if (id == 0) {
            if (st.entry_count >= STINGTRAIL_MAX_ENTRIES) {
                cli_errf("failed to log entry: entry capacity (%zu/%d) reached", st.entry_count, STINGTRAIL_MAX_ENTRIES);
                cli_hintf("run 'stingtrail rebase --completed' to compact finished chains");
            } else if (parent_id > 0) {
                cli_errf("failed to log entry: parent #%u does not exist in graph", parent_id);
                cli_hintf("run 'stingtrail status' to list valid parent IDs");
            } else {
                cli_errf("failed to log entry (non-empty summary required)");
            }
            return 1;
        }
        warn_if_truncated("summary", summary, 160);
        warn_if_truncated("detail", detail, 512);
        if (st.persist_overflow) {
            cli_warnf("entry #%u exceeded persistence line buffer or failed disk write", id);
        }
        sting_color_t c = sting_color_get();
        printf("entry %s#%u%s logged\n", c.anchor, id, c.reset);
        warn_capacity_high("entry", st.entry_count, STINGTRAIL_MAX_ENTRIES);
        return 0;
    }

    if (strcmp(cmd, "plan") == 0) {
        if (arg_idx >= argc) {
            cli_errf("plan requires <action>");
            return 1;
        }
        const char *action = argv[arg_idx++];
        const char *why = NULL;
        const char *target = "";
        int prio = 3;
        uint32_t parent_id = 0;

        while (arg_idx < argc) {
            if (strcmp(argv[arg_idx], "--why") == 0 && arg_idx + 1 < argc) {
                why = argv[++arg_idx];
                arg_idx++;
            } else if (strcmp(argv[arg_idx], "--prio") == 0 && arg_idx + 1 < argc) {
                prio = atoi(argv[++arg_idx]);
                arg_idx++;
            } else if (strcmp(argv[arg_idx], "--parent") == 0 && arg_idx + 1 < argc) {
                parent_id = (uint32_t)strtoul(argv[++arg_idx], NULL, 10);
                arg_idx++;
            } else if (strcmp(argv[arg_idx], "--target") == 0 && arg_idx + 1 < argc) {
                target = argv[++arg_idx];
                arg_idx++;
            } else {
                cli_errf("unrecognized plan argument '%s' (expected --parent H, --why R, --prio 1-5, --target T)", argv[arg_idx]);
                return 1;
            }
        }

        if (prio < 1 || prio > 5) {
            cli_warnf("priority %d clamped to 1..5", prio);
        }

        const char *err = NULL;
        uint32_t id = sting_state_plan_step(&st, parent_id, prio, target, action, why ? why : "", true, &err);
        if (id == 0) {
            sting_print_error(err ? err : "failed to plan step");
            if (st.step_count >= STINGTRAIL_MAX_STEPS) {
                cli_hintf("step capacity at %zu/%d — run 'stingtrail rebase --completed' to compact finished chains",
                          st.step_count, STINGTRAIL_MAX_STEPS);
            } else if (parent_id == 0) {
                cli_hintf("create or pick a hypothesis with 'stingtrail log hypothesis \"...\" --parent <premise_id>'");
            }
            return 1;
        }
        warn_if_truncated("action", action, 256);
        warn_if_truncated("target", target, 128);
        if (why) warn_if_truncated("rationale", why, 384);
        if (st.persist_overflow) {
            cli_warnf("step #%u exceeded persistence line buffer or failed disk write", id);
        }
        sting_color_t c = sting_color_get();
        printf("step %s#%u%s planned\n", c.anchor, id, c.reset);
        warn_capacity_high("step", st.step_count, STINGTRAIL_MAX_STEPS);
        return 0;
    }

    if (strcmp(cmd, "step") == 0) {
        if (arg_idx >= argc) {
            cli_errf("step requires <id> and <status>");
            return 1;
        }
        uint32_t step_id = (uint32_t)strtoul(argv[arg_idx++], NULL, 10);
        if (arg_idx >= argc) {
            cli_errf("step #%u requires <status> (pending, in_progress, done, blocked)", step_id);
            return 1;
        }
        const char *status = argv[arg_idx++];
        const char *outcome = (arg_idx < argc) ? argv[arg_idx++] : "";

        if (!sting_state_update_step(&st, step_id, status, outcome, true)) {
            bool step_found = false;
            for (size_t i = 0; i < st.step_count; i++) {
                if (st.steps[i].id == step_id) { step_found = true; break; }
            }
            if (!step_found) {
                cli_errf("step #%u does not exist in graph", step_id);
                cli_hintf("run 'stingtrail status' to list active steps");
            } else if (strcmp(status, "pending") != 0 && strcmp(status, "in_progress") != 0 &&
                       strcmp(status, "done") != 0 && strcmp(status, "blocked") != 0) {
                cli_errf("invalid status '%s' for step #%u (valid: pending, in_progress, done, blocked)", status, step_id);
            } else {
                cli_errf("step #%u cannot be marked '%s' without an outcome", step_id, status);
                cli_hintf("example: stingtrail step %u %s \"concise result\"", step_id, status);
            }
            return 1;
        }
        warn_if_truncated("outcome", outcome, 384);
        if (st.persist_overflow) {
            cli_warnf("step #%u update exceeded persistence line buffer or failed disk write", step_id);
        }
        sting_color_t c = sting_color_get();
        const char *s_col = sting_color_status(&c, status);
        printf("step %s#%u%s updated to %s%s%s\n", c.anchor, step_id, c.reset, s_col, status, c.reset);
        return 0;
    }

    if (strcmp(cmd, "focus") == 0) {
        if (arg_idx >= argc) {
            cli_errf("focus requires <id>");
            return 1;
        }
        uint32_t target_id = (uint32_t)strtoul(argv[arg_idx++], NULL, 10);
        bool json_mode = false;
        while (arg_idx < argc) {
            if (strcmp(argv[arg_idx], "--json") == 0) {
                json_mode = true;
            } else {
                cli_errf("unrecognized focus argument '%s'", argv[arg_idx]);
                return 1;
            }
            arg_idx++;
        }
        if (json_mode) {
            return emit_json_export(&st, target_id);
        }
        return sting_state_print_focused(&st, target_id) ? 0 : 1;
    }

    if (strcmp(cmd, "status") == 0) {
        bool json_mode = false;
        uint32_t focus_id = 0;
        while (arg_idx < argc) {
            if (strcmp(argv[arg_idx], "--json") == 0) {
                json_mode = true;
            } else if (strcmp(argv[arg_idx], "--focus") == 0 && arg_idx + 1 < argc) {
                focus_id = (uint32_t)strtoul(argv[++arg_idx], NULL, 10);
            } else {
                cli_errf("unrecognized status argument '%s'", argv[arg_idx]);
                return 1;
            }
            arg_idx++;
        }
        if (json_mode) {
            return emit_json_export(&st, focus_id);
        }
        if (focus_id > 0) {
            return sting_state_print_focused(&st, focus_id) ? 0 : 1;
        }
        sting_state_print_board(&st);
        return 0;
    }

    if (strcmp(cmd, "export") == 0) {
        uint32_t focus_id = 0;
        while (arg_idx < argc) {
            if (strcmp(argv[arg_idx], "--focus") == 0 && arg_idx + 1 < argc) {
                focus_id = (uint32_t)strtoul(argv[++arg_idx], NULL, 10);
            } else {
                cli_errf("unrecognized export argument '%s'", argv[arg_idx]);
                return 1;
            }
            arg_idx++;
        }
        return emit_json_export(&st, focus_id);
    }

    if (strcmp(cmd, "rebase") == 0) {
        sting_rebase_opts_t opts;
        memset(&opts, 0, sizeof(opts));

        while (arg_idx < argc) {
            if (strcmp(argv[arg_idx], "--completed") == 0) {
                opts.all_completed = true;
                arg_idx++;
            } else if (strcmp(argv[arg_idx], "--dry-run") == 0) {
                opts.dry_run = true;
                arg_idx++;
            } else if (strcmp(argv[arg_idx], "--title") == 0 && arg_idx + 1 < argc) {
                opts.title = argv[++arg_idx];
                arg_idx++;
            } else if (argv[arg_idx][0] != '-' && opts.target_id == 0) {
                opts.target_id = (uint32_t)strtoul(argv[arg_idx++], NULL, 10);
            } else {
                cli_errf("unrecognized rebase argument '%s'", argv[arg_idx]);
                return 1;
            }
        }

        const char *err = NULL;
        sting_rebase_result_t res;
        if (!sting_state_rebase(&st, &opts, &res, &err)) {
            sting_print_error(err ? err : "rebase failed");
            if (opts.target_id > 0) {
                cli_hintf("inspect blocked/pending steps with 'stingtrail focus %u' or update them via 'stingtrail step <id> done|blocked \"<outcome>\"'",
                          opts.target_id);
            } else {
                cli_hintf("inspect incomplete plans with 'stingtrail status' or run 'stingtrail rebase <root_id>' on a finished chain");
            }
            return 1;
        }

        sting_color_t c = sting_color_get();
        printf("%s%s%s: rebased %zu chain(s) -> super-premise %s#%u%s (%zu entries, %zu steps compacted; %zu done, %zu blocked)\n",
               c.bold, opts.dry_run ? "rebase dry-run" : "rebase complete", c.reset,
               res.chains_rebased,
               c.anchor, res.last_super_id, c.reset,
               res.entries_removed, res.steps_removed, res.wins, res.fails);
        printf("  summary: %s\n", res.summary_path);
        return 0;
    }

    if (strcmp(cmd, "mcp") == 0 || strcmp(cmd, "mcp-stdio") == 0) {
        return sting_mcp_run_stdio(&st);
    }

    if (strcmp(cmd, "hook") == 0) {
        const char *prog_bin = argv[0];
        printf(
            "# Stingtrail Synchronous Shell Hook for Bash / Zsh\n"
            "# Usage in agent shell: eval \"$(%s%s%s hook)\"\n\n"
            "_stingtrail_preexec() {\n"
            "    local cmd=\"$BASH_COMMAND\"\n"
            "    [ -z \"$cmd\" ] && return 0\n\n"
            "    # Skip recursive trapping on stingtrail itself or shell builtins\n"
            "    case \"$cmd\" in\n"
            "        *stingtrail*|_stingtrail*|eval*|source*|shopt*|trap*|export*|unset*|history*) return 0 ;;\n"
            "    esac\n\n"
            "    # Synchronous pre-execution evaluation\n"
            "    local step_id\n"
            "    step_id=$(%s%s%s hook-eval \"$cmd\")\n"
            "    local eval_ret=$?\n"
            "    if [ $eval_ret -ne 0 ]; then\n"
            "        return $eval_ret\n"
            "    fi\n\n"
            "    if [ -n \"$step_id\" ] && [ \"$step_id\" -gt 0 ] 2>/dev/null; then\n"
            "        export _STINGTRAIL_ACTIVE_STEP=\"$step_id\"\n"
            "    else\n"
            "        unset _STINGTRAIL_ACTIVE_STEP\n"
            "    fi\n"
            "    return 0\n"
            "}\n\n"
            "_stingtrail_postexec() {\n"
            "    local last_exit=$?\n"
            "    if [ -n \"$_STINGTRAIL_ACTIVE_STEP\" ]; then\n"
            "        local status=\"done\"\n"
            "        local outcome=\"Exit code $last_exit\"\n"
            "        if [ $last_exit -ne 0 ]; then\n"
            "            status=\"blocked\"\n"
            "            outcome=\"Failed with exit code $last_exit\"\n"
            "        fi\n"
            "        %s%s%s step \"$_STINGTRAIL_ACTIVE_STEP\" \"$status\" \"$outcome\" >/dev/null 2>&1\n"
            "        unset _STINGTRAIL_ACTIVE_STEP\n"
            "    fi\n"
            "}\n\n"
            "if [ -n \"$BASH_VERSION\" ]; then\n"
            "    shopt -s extdebug 2>/dev/null\n"
            "    trap '_stingtrail_preexec' DEBUG\n"
            "    if [[ \";$PROMPT_COMMAND;\" != *\";_stingtrail_postexec;\"* ]]; then\n"
            "        PROMPT_COMMAND=\"_stingtrail_postexec${PROMPT_COMMAND:+;$PROMPT_COMMAND}\"\n"
            "    fi\n"
            "    echo \"[stingtrail] Synchronous shell hook installed in bash (PID $$)\" >&2\n"
            "elif [ -n \"$ZSH_VERSION\" ]; then\n"
            "    autoload -Uz add-zsh-hook 2>/dev/null\n"
            "    _stingtrail_zsh_preexec() { _stingtrail_preexec \"$1\"; }\n"
            "    add-zsh-hook preexec _stingtrail_zsh_preexec\n"
            "    add-zsh-hook precmd _stingtrail_postexec\n"
            "    echo \"[stingtrail] Synchronous shell hook installed in zsh (PID $$)\" >&2\n"
            "fi\n",
            prog_bin,
            strcmp(db_path, "stingtrail.jsonl") != 0 ? " --db " : "",
            strcmp(db_path, "stingtrail.jsonl") != 0 ? db_path : "",
            prog_bin,
            strcmp(db_path, "stingtrail.jsonl") != 0 ? " --db " : "",
            strcmp(db_path, "stingtrail.jsonl") != 0 ? db_path : "",
            prog_bin,
            strcmp(db_path, "stingtrail.jsonl") != 0 ? " --db " : "",
            strcmp(db_path, "stingtrail.jsonl") != 0 ? db_path : "");
        return 0;
    }

    if (strcmp(cmd, "hook-eval") == 0) {
        if (arg_idx >= argc) {
            return 0;
        }
        const char *action_cmd = argv[arg_idx++];
        uint32_t active_ctx = sting_context_get(db_path);

        const char *reason = NULL;
        sting_hook_verdict_t verdict = sting_hook_classify(action_cmd, active_ctx, &reason);

        if (verdict == STING_HOOK_BLOCK_DESTRUCTIVE) {
            fprintf(stderr, "\n[STINGTRAIL BLOCKED]: Destructive command pattern blocked (%s)\n",
                    reason ? reason : "unsafe");
            return 126;
        }

        if (verdict == STING_HOOK_BLOCK_NO_CONTEXT) {
            fprintf(stderr,
                "\n[STINGTRAIL BLOCKED]: Command requires an active hypothesis context:\n"
                "  Command: %s\n\n"
                "To continue, bind this shell session to an active hypothesis:\n"
                "  %s context set <hypothesis_id>\n\n"
                "Or register a new hypothesis first:\n"
                "  %s log hypothesis \"<your intent>\" --parent <premise_id>\n",
                action_cmd, argv[0], argv[0]);
            return 126;
        }

        if (verdict == STING_HOOK_ALLOW_BENIGN) {
            return 0;
        }

        if (verdict == STING_HOOK_ALLOW_ACTIVE) {
            const char *err = NULL;
            uint32_t sid = sting_state_plan_step(&st, active_ctx, 2, "", action_cmd,
                                                 "Auto-captured via synchronous shell hook", true, &err);
            if (sid == 0) {
                sting_print_error(err ? err : "failed to auto-plan hook command");
                cli_hintf("bind shell context to an active hypothesis: %s context set <hyp_id>", argv[0]);
                return 126;
            }
            printf("%u\n", sid);
            return 0;
        }

        return 0;
    }

    if (strcmp(cmd, "context") == 0) {
        sting_color_t c = sting_color_get();
        if (arg_idx >= argc || strcmp(argv[arg_idx], "get") == 0) {
            uint32_t active_id = sting_context_get(db_path);
            if (active_id == 0) {
                printf("Active context: %snone%s (no hypothesis active)\n", c.dim, c.reset);
                cli_hintf("set an active hypothesis with 'stingtrail context set <hyp_id>'");
            } else {
                const char *kind_str = NULL;
                const char *name_str = NULL;
                if (!find_graph_item(&st, active_id, &kind_str, &name_str)) {
                    printf("Active context: %s#%u%s (not in graph)\n", c.anchor, active_id, c.reset);
                    cli_warnf("active context #%u no longer exists in graph (rebased or missing)", active_id);
                    cli_hintf("run 'stingtrail context clear' or 'stingtrail context set <hyp_id>'");
                } else {
                    const char *k_col = sting_color_kind(&c, kind_str);
                    if (strcmp(kind_str, "hypothesis") == 0) {
                        size_t tot = 0, inc = 0;
                        sting_state_plan_counts(&st, active_id, &tot, &inc);
                        printf("Active context: %s#%u%s (%s%s%s: %s) [incomplete plans: %zu/%zu]\n",
                               c.anchor, active_id, c.reset, k_col, kind_str, c.reset, name_str, inc, tot);
                    } else {
                        printf("Active context: %s#%u%s (%s%s%s: %s)\n",
                               c.anchor, active_id, c.reset, k_col, kind_str, c.reset, name_str);
                        cli_warnf("active context #%u is a '%s', not a hypothesis; 'hook-eval' auto-planning requires a hypothesis id",
                                  active_id, kind_str);
                        cli_hintf("switch context to a hypothesis: stingtrail context set <hyp_id>");
                    }
                }
            }
            return 0;
        }

        if (strcmp(argv[arg_idx], "set") == 0) {
            arg_idx++;
            if (arg_idx >= argc) {
                cli_errf("context set requires <id>");
                return 1;
            }
            uint32_t id = (uint32_t)strtoul(argv[arg_idx++], NULL, 10);
            if (id == 0) {
                cli_errf("context id must be > 0");
                return 1;
            }

            const char *kind_str = "";
            const char *name_str = "";
            if (!find_graph_item(&st, id, &kind_str, &name_str)) {
                cli_errf("id #%u does not exist in graph", id);
                cli_hintf("run 'stingtrail status' to list valid IDs");
                return 1;
            }

            if (!sting_context_set(db_path, id)) {
                cli_errf("failed to write context for #%u", id);
                return 1;
            }
            const char *k_col = sting_color_kind(&c, kind_str);
            printf("Active context set to %s#%u%s (%s%s%s: %s)\n", c.anchor, id, c.reset, k_col, kind_str, c.reset, name_str);
            if (strcmp(kind_str, "hypothesis") != 0) {
                cli_warnf("#%u is a '%s', not a hypothesis; 'hook-eval' auto-planning requires a hypothesis id", id, kind_str);
                cli_hintf("log a hypothesis under #%u and run 'stingtrail context set <hyp_id>'", id);
            }
            return 0;
        }

        if (strcmp(argv[arg_idx], "clear") == 0) {
            sting_context_clear(db_path);
            printf("Active context cleared\n");
            return 0;
        }

        cli_errf("unrecognized context subcommand '%s' (use get, set <id>, clear)", argv[arg_idx]);
        return 1;
    }

    cli_errf("unrecognized command '%s' (see --help)", cmd);
    return 1;
}
