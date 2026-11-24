#define _POSIX_C_SOURCE 200809L
#include "stingtrail_mcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Stingtrail MCP Server v%s — Model Context Protocol stdio server\n\n"
        "Usage:\n"
        "  %s [--db <file.jsonl>] [--scope <hosts>]\n\n"
        "Options:\n"
        "  --db <file>       Path to the JSONL journal database (default: stingtrail.jsonl)\n"
        "  --scope <hosts>   Comma-separated list of allowed hostnames/IPs for fail-closed guards\n"
        "  --help, -h        Show this help\n"
        "  --version, -v     Print version\n\n"
        "This executable communicates over stdin/stdout using standard MCP JSON-RPC 2.0 messages.\n",
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
            printf("stingtrail-mcp %s (MCP %s)\n", STINGTRAIL_VERSION, STINGTRAIL_MCP_PROTOCOL_VERSION);
            return 0;
        } else {
            fprintf(stderr, "error: unrecognized option '%s'\n", argv[arg_idx]);
            return 1;
        }
    }

    sting_state_t st;
    sting_state_init(&st, db_path);

    if (scope_csv) {
        sting_scope_init(&st.scope, true);
        sting_scope_parse_csv(&st.scope, scope_csv);
    }

    return sting_mcp_run_stdio(&st);
}
