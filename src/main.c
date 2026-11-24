#define _POSIX_C_SOURCE 200809L
#include "stingtrail.h"
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

    fprintf(stderr, "error: unrecognized command '%s' (see --help)\n", cmd);
    return 1;
}
