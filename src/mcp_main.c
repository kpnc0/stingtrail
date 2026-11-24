#define _POSIX_C_SOURCE 200809L
#include "stingtrail.h"
#include "stingtrail_mcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(FILE *out, const char *prog) {
    if (!out) out = stderr;
    sting_color_t c = sting_color_get_for_stream(out);

    fprintf(out,
        "%sStingtrail MCP Server v%s%s — %sModel Context Protocol stdio server%s\n\n"
        "%sUsage:%s\n"
        "  %s%s%s %s[--db <file.jsonl>] [--scope <hosts>]%s\n\n"
        "%sOptions:%s\n"
        "  %s--db <file>%s       Path to the JSONL journal database (default: stingtrail.jsonl)\n"
        "  %s--scope <hosts>%s   Comma-separated list of allowed hostnames/IPs for fail-closed guards\n"
        "  %s--help, -h%s        Show this help\n"
        "  %s--version, -v%s     Print version\n\n"
        "%sThis executable communicates over stdin/stdout using standard MCP JSON-RPC 2.0 messages.%s\n",
        c.bold, STINGTRAIL_VERSION, c.reset, c.dim, c.reset,
        c.bold, c.reset,
        c.anchor, prog, c.reset, c.dim, c.reset,
        c.bold, c.reset,
        c.anchor, c.reset,
        c.anchor, c.reset,
        c.anchor, c.reset,
        c.anchor, c.reset,
        c.dim, c.reset);
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
            print_usage(stdout, argv[0]);
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
