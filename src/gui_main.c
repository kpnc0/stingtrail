#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stingtrail.h"
#include "stingtrail_gui.h"

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Stingtrail-GUI v%s — Read-Only 2D Flow DAG Viewer for Autonomous Agents\n\n"
        "Usage:\n"
        "  %s [options] [journal.jsonl]\n\n"
        "Options:\n"
        "  --db <file.jsonl>   Path to JSONL journal database (default: stingtrail.jsonl)\n"
        "  -v, --version       Display version information\n"
        "  -h, --help          Show this help message\n\n"
        "Vim Keybindings (Read-Only):\n"
        "  h                   Go to immediate parent node\n"
        "  j                   Go to next sibling node (forward)\n"
        "  k                   Go to previous sibling node (reverse)\n"
        "  l                   Go to first child node\n",
        STINGTRAIL_VERSION, prog);
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
            print_usage(argv[0]);
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
