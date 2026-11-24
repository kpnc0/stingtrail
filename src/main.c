#define _POSIX_C_SOURCE 200809L
#include "stingtrail.h"
#include "stingtrail_mcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Stingtrail v%s — Minimalist Critical-Thinking CLI for Agents\n\n"
        "Usage:\n"
        "  %s [--db <file.jsonl>] [--scope <hosts>] <command> [args...]\n\n"
        "Commands:\n"
        "  node <kind> <name> [detail]                                 Record a root node\n"
        "       kinds: premise, asset, context\n"
        "  log <kind> <summary> [--parent N] [--sev S] [--detail D]    Append reasoning log\n"
        "       kinds: observation, hypothesis, evidence, refutation, finding\n"
        "       severities: info, low, medium, high, critical\n"
        "  plan <action> --why <rationale> [--prio 1-5] [--parent N] [--target T]\n"
        "       Propose a prioritized action step with mandatory rationale\n"
        "  step <id> <status> [outcome]                                Update step state\n"
        "       statuses: pending, in_progress, done, blocked\n"
        "  status [--json]                                             Display board\n"
        "  export                                                      Export full state JSON\n"
        "  hook                                                        Print synchronous shell hook integration script\n"
        "  hook-eval <command>                                         Evaluate command under guard and active context\n"
        "  context [get | set <id> | clear]                            Manage active hypothesis context\n"
        "  mcp                                                         Run MCP JSON-RPC stdio server\n"
        "  version                                                     Print version\n"
        "  help                                                        Show this help\n",
        STINGTRAIL_VERSION, prog);
}

int main(int argc, char **argv) {
    const char *db_path = "stingtrail.jsonl";
    const char *scope_csv = NULL;
    int arg_idx = 1;

    while (arg_idx < argc && argv[arg_idx][0] == '-') {
        if (strcmp(argv[arg_idx], "--db") == 0) {
            if (arg_idx + 1 >= argc) {
                fprintf(stderr, "error: --db requires a file path\n");
                return 1;
            }
            db_path = argv[++arg_idx];
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "--scope") == 0) {
            if (arg_idx + 1 >= argc) {
                fprintf(stderr, "error: --scope requires a host list\n");
                return 1;
            }
            scope_csv = argv[++arg_idx];
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "--help") == 0 || strcmp(argv[arg_idx], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[arg_idx], "--version") == 0 || strcmp(argv[arg_idx], "-v") == 0) {
            printf("stingtrail %s\n", STINGTRAIL_VERSION);
            return 0;
        } else {
            fprintf(stderr, "error: unrecognized option '%s'\n", argv[arg_idx]);
            return 1;
        }
    }

    if (arg_idx >= argc) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[arg_idx++];
    if (strcmp(cmd, "help") == 0) {
        print_usage(argv[0]);
        return 0;
    }
    if (strcmp(cmd, "version") == 0) {
        printf("stingtrail %s\n", STINGTRAIL_VERSION);
        return 0;
    }

    sting_state_t st;
    sting_state_init(&st, db_path);

    if (scope_csv) {
        sting_scope_init(&st.scope, true);
        sting_scope_parse_csv(&st.scope, scope_csv);
    }

    if (strcmp(cmd, "node") == 0) {
        if (arg_idx >= argc) {
            fprintf(stderr, "error: node requires <kind> and <name>\n");
            return 1;
        }
        const char *kind = argv[arg_idx++];
        if (arg_idx >= argc) {
            fprintf(stderr, "error: node requires <name>\n");
            return 1;
        }
        const char *name = argv[arg_idx++];
        const char *detail = (arg_idx < argc) ? argv[arg_idx++] : "";

        uint32_t id = sting_state_add_node(&st, kind, name, detail, true);
        if (id == 0) {
            fprintf(stderr, "error: failed to add node\n");
            return 1;
        }
        printf("node #%u recorded\n", id);
        return 0;
    }

    if (strcmp(cmd, "log") == 0) {
        if (arg_idx >= argc) {
            fprintf(stderr, "error: log requires <kind> and <summary>\n");
            return 1;
        }
        const char *kind = argv[arg_idx++];
        if (arg_idx >= argc) {
            fprintf(stderr, "error: log requires <summary>\n");
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
                arg_idx++;
            }
        }

        uint32_t id = sting_state_add_entry(&st, parent_id, kind, severity, summary, detail, true);
        if (id == 0) {
            fprintf(stderr, "error: failed to log entry (verify parent_id exists)\n");
            return 1;
        }
        printf("entry #%u logged\n", id);
        return 0;
    }

    if (strcmp(cmd, "plan") == 0) {
        if (arg_idx >= argc) {
            fprintf(stderr, "error: plan requires <action>\n");
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
                arg_idx++;
            }
        }

        if (!why || why[0] == '\0') {
            fprintf(stderr, "error: rationale is required for disciplined planning (--why \"...\")\n");
            return 1;
        }

        const char *err = NULL;
        uint32_t id = sting_state_plan_step(&st, parent_id, prio, target, action, why, true, &err);
        if (id == 0) {
            fprintf(stderr, "error: %s\n", err ? err : "failed to plan step");
            return 1;
        }
        printf("step #%u planned\n", id);
        return 0;
    }

    if (strcmp(cmd, "step") == 0) {
        if (arg_idx >= argc) {
            fprintf(stderr, "error: step requires <id> and <status>\n");
            return 1;
        }
        uint32_t step_id = (uint32_t)strtoul(argv[arg_idx++], NULL, 10);
        if (arg_idx >= argc) {
            fprintf(stderr, "error: step requires <status>\n");
            return 1;
        }
        const char *status = argv[arg_idx++];
        const char *outcome = (arg_idx < argc) ? argv[arg_idx++] : "";

        if (!sting_state_update_step(&st, step_id, status, outcome, true)) {
            fprintf(stderr, "error: invalid step id or status (valid: pending, in_progress, done, blocked)\n");
            return 1;
        }
        printf("step #%u updated to %s\n", step_id, status);
        return 0;
    }

    if (strcmp(cmd, "status") == 0) {
        bool json_mode = false;
        while (arg_idx < argc) {
            if (strcmp(argv[arg_idx], "--json") == 0) json_mode = true;
            arg_idx++;
        }
        if (json_mode) {
            char buf[65536];
            if (sting_state_to_json(&st, buf, sizeof(buf))) {
                printf("%s\n", buf);
            }
        } else {
            sting_state_print_board(&st);
        }
        return 0;
    }

    if (strcmp(cmd, "export") == 0) {
        char buf[65536];
        if (sting_state_to_json(&st, buf, sizeof(buf))) {
            printf("%s\n", buf);
        }
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
            if (sid > 0) {
                printf("%u\n", sid);
            }
            return 0;
        }

        return 0;
    }

    if (strcmp(cmd, "context") == 0) {
        if (arg_idx >= argc || strcmp(argv[arg_idx], "get") == 0) {
            uint32_t active_id = sting_context_get(db_path);
            if (active_id == 0) {
                printf("Active context: none (no hypothesis/premise active)\n");
            } else {
                bool found = false;
                for (size_t i = 0; i < st.node_count; i++) {
                    if (st.nodes[i].id == active_id) {
                        printf("Active context: #%u (%s: %s)\n", active_id, st.nodes[i].kind, st.nodes[i].name);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    for (size_t i = 0; i < st.entry_count; i++) {
                        if (st.entries[i].id == active_id) {
                            printf("Active context: #%u (%s: %s)\n", active_id, st.entries[i].kind, st.entries[i].summary);
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    printf("Active context: #%u\n", active_id);
                }
            }
            return 0;
        }

        if (strcmp(argv[arg_idx], "set") == 0) {
            arg_idx++;
            if (arg_idx >= argc) {
                fprintf(stderr, "error: context set requires <id>\n");
                return 1;
            }
            uint32_t id = (uint32_t)strtoul(argv[arg_idx++], NULL, 10);
            if (id == 0) {
                fprintf(stderr, "error: context id must be > 0\n");
                return 1;
            }

            bool exists = false;
            const char *kind_str = "";
            const char *name_str = "";
            for (size_t i = 0; i < st.node_count; i++) {
                if (st.nodes[i].id == id) {
                    exists = true;
                    kind_str = st.nodes[i].kind;
                    name_str = st.nodes[i].name;
                    break;
                }
            }
            if (!exists) {
                for (size_t i = 0; i < st.entry_count; i++) {
                    if (st.entries[i].id == id) {
                        exists = true;
                        kind_str = st.entries[i].kind;
                        name_str = st.entries[i].summary;
                        break;
                    }
                }
            }
            if (!exists) {
                fprintf(stderr, "error: id #%u does not exist in graph\n", id);
                return 1;
            }

            if (!sting_context_set(db_path, id)) {
                fprintf(stderr, "error: failed to write context\n");
                return 1;
            }
            printf("Active context set to #%u (%s: %s)\n", id, kind_str, name_str);
            return 0;
        }

        if (strcmp(argv[arg_idx], "clear") == 0) {
            sting_context_clear(db_path);
            printf("Active context cleared\n");
            return 0;
        }

        fprintf(stderr, "error: unrecognized context subcommand '%s' (use get, set <id>, clear)\n", argv[arg_idx]);
        return 1;
    }

    fprintf(stderr, "error: unrecognized command '%s' (see --help)\n", cmd);
    return 1;
}
