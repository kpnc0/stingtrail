#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stingtrail.h"
#include "stingtrail_gui.h"

static void print_usage(FILE *out, const char *prog) {
    if (!out) out = stderr;
    sting_color_t c = sting_color_get_for_stream(out);

    fprintf(out,
        "%sStingtrail-GUI v%s%s — %sRead-Only 2D Flow DAG Viewer for Autonomous Agents%s\n\n"
        "%sUsage:%s\n"
        "  %s%s%s %s[options] [journal.jsonl]%s\n\n"
        "%sOptions:%s\n"
        "  %s--db <file.jsonl>%s   Path to JSONL journal database (default: stingtrail.jsonl)\n"
        "  %s-v, --version%s       Display version information\n"
        "  %s-h, --help%s          Show this help message\n\n"
        "%sVim Keybindings (Read-Only):%s\n"
        "  %sh%s                   Go to immediate parent node\n"
        "  %sj%s                   Go to next sibling node (forward)\n"
        "  %sk%s                   Go to previous sibling node (reverse)\n"
        "  %sl%s                   Go to first child node\n",
        c.bold, STINGTRAIL_VERSION, c.reset, c.dim, c.reset,
        c.bold, c.reset,
        c.anchor, prog, c.reset, c.dim, c.reset,
        c.bold, c.reset,
        c.anchor, c.reset,
        c.anchor, c.reset,
        c.anchor, c.reset,
        c.bold, c.reset,
        c.anchor, c.reset,
        c.anchor, c.reset,
        c.anchor, c.reset,
        c.anchor, c.reset);
}

int main(int argc, char **argv) {
    const char *db_path = "stingtrail.jsonl";
    int arg_idx = 1;

    while (arg_idx < argc && argv[arg_idx][0] == '-') {
        if (strcmp(argv[arg_idx], "--db") == 0) {
            if (arg_idx + 1 >= argc) {
                fprintf(stderr, "error: --db requires a file path\n");
                return 1;
            }
            db_path = argv[++arg_idx];
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "-h") == 0 || strcmp(argv[arg_idx], "--help") == 0) {
            print_usage(stdout, argv[0]);
            return 0;
        } else if (strcmp(argv[arg_idx], "-v") == 0 || strcmp(argv[arg_idx], "--version") == 0) {
            printf("stingtrail-gui %s\n", STINGTRAIL_VERSION);
            return 0;
        } else {
            fprintf(stderr, "error: unrecognized option '%s'\n", argv[arg_idx]);
            return 1;
        }
    }

    if (arg_idx < argc) {
        db_path = argv[arg_idx++];
    }

    sting_state_t st;
    sting_state_init(&st, db_path);

    return sting_gui_run(&st);
}
