#ifndef STINGTRAIL_H
#define STINGTRAIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STINGTRAIL_VERSION "0.1.0"

#define STINGTRAIL_MAX_SCOPE_HOSTS 64
#define STINGTRAIL_MAX_NODES       256
#define STINGTRAIL_MAX_ENTRIES     512
#define STINGTRAIL_MAX_STEPS       256

/* --- Scope & Hygiene Guard --- */
typedef enum {
    STING_GUARD_ALLOW = 0,
    STING_GUARD_DENY  = 1
} sting_guard_verdict_t;

typedef struct {
    char allowed_hosts[STINGTRAIL_MAX_SCOPE_HOSTS][128];
    size_t allowed_host_count;
    bool enforce_scope; /* If true, target_host must match allowlist (fail-closed) */
} sting_scope_t;

void sting_scope_init(sting_scope_t *s, bool enforce_scope);
bool sting_scope_add_host(sting_scope_t *s, const char *host);
bool sting_scope_parse_csv(sting_scope_t *s, const char *csv);
sting_guard_verdict_t sting_guard_check(const sting_scope_t *s,
                                        const char *target_host,
                                        const char *action,
                                        const char **reason_out);

/* --- Synchronous Shell Hook & Semantic Classifier --- */
typedef enum {
    STING_HOOK_ALLOW_BENIGN = 0,     /* Benign navigation, run without logging */
    STING_HOOK_ALLOW_ACTIVE = 1,     /* Exploratory with active hypothesis, auto-logged */
    STING_HOOK_BLOCK_NO_CONTEXT = 2, /* Exploratory without active hypothesis, blocked */
    STING_HOOK_BLOCK_DESTRUCTIVE = 3 /* Destructive pattern, blocked fail-closed */
} sting_hook_verdict_t;

bool sting_is_destructive(const char *cmd);
bool sting_is_benign_navigation(const char *cmd);
sting_hook_verdict_t sting_hook_classify(const char *cmd, uint32_t active_context_id, const char **reason_out);

/* --- Session Context Management --- */
uint32_t sting_context_get(const char *journal_path);
bool sting_context_set(const char *journal_path, uint32_t context_id);
bool sting_context_clear(const char *journal_path);

/* --- Critical-Thinking DAG: Nodes, Entries, Steps --- */
typedef struct {
    uint32_t id;
    char kind[32];      /* premise | asset | context */
    char name[128];
    char detail[256];
    int64_t created_at;
    char created[16];   /* yyyymmddhhmmss */
    char modified[16];  /* yyyymmddhhmmss */
} sting_node_t;

typedef struct {
    uint32_t id;
    uint32_t parent_id; /* 0 = root; otherwise links to node or prior entry */
    char kind[32];      /* observation | hypothesis | evidence | refutation | finding */
    char severity[16];  /* info | low | medium | high | critical */
    char summary[160];
    char detail[512];
    int64_t created_at;
    char created[16];   /* yyyymmddhhmmss */
    char modified[16];  /* yyyymmddhhmmss */
} sting_entry_t;

typedef struct {
    uint32_t id;
    uint32_t parent_id; /* 0 = root; otherwise links to motivating entry/step */
    int priority;       /* 1 (highest) .. 5 (lowest) */
    char status[24];    /* pending | in_progress | done | blocked */
    char target[128];   /* optional host/subject */
    char action[256];
    char rationale[384];
    char outcome[384];
    int64_t created_at;
    int64_t updated_at;
    char created[16];   /* yyyymmddhhmmss */
    char modified[16];  /* yyyymmddhhmmss */
} sting_step_t;

typedef struct {
    sting_scope_t scope;
    sting_node_t nodes[STINGTRAIL_MAX_NODES];
    size_t node_count;
    sting_entry_t entries[STINGTRAIL_MAX_ENTRIES];
    size_t entry_count;
    sting_step_t steps[STINGTRAIL_MAX_STEPS];
    size_t step_count;
    uint32_t next_id;
    char journal_path[512];
} sting_state_t;

void sting_state_init(sting_state_t *st, const char *journal_path);
void sting_state_reload(sting_state_t *st);

/* Core mutations (return assigned ID > 0 on success, 0 on error) */
uint32_t sting_state_add_node(sting_state_t *st, const char *kind,
                              const char *name, const char *detail,
                              bool persist);
uint32_t sting_state_add_entry(sting_state_t *st, uint32_t parent_id,
                               const char *kind, const char *severity,
                               const char *summary, const char *detail,
                               bool persist);
uint32_t sting_state_plan_step(sting_state_t *st, uint32_t parent_id,
                               int priority, const char *target,
                               const char *action, const char *rationale,
                               bool persist, const char **err_out);
bool sting_state_update_step(sting_state_t *st, uint32_t step_id,
                             const char *status, const char *outcome,
                             bool persist);

/* Strategic Terminal Color Ergonomics */
typedef struct {
    bool enabled;
    const char *reset;
    const char *bold;
    const char *dim;
    const char *anchor;      /* Bold Cyan for node IDs [#123] and links [#123 <- #45] */
    const char *p1;          /* Bold Red for [P1] */
    const char *p2;          /* Yellow/Amber for [P2] */
    const char *p3;          /* Blue for [P3] */
    const char *plow;        /* Dim for [P4], [P5] */
    const char *done;        /* Bold Green for [done] */
    const char *in_progress; /* Bold Amber for [in_progress] */
    const char *blocked;     /* Bold Red for [blocked] */
    const char *pending;     /* Cyan for [pending] */
    const char *hyp;         /* Bold Magenta for [hypothesis] */
    const char *evi;         /* Bold Green for [evidence] */
    const char *ref;         /* Bold Red for [refutation] */
    const char *obs;         /* Cyan for [observation] */
    const char *find;        /* Yellow for [finding] */
} sting_color_t;

sting_color_t sting_color_get(void);
sting_color_t sting_color_get_for_stream(FILE *fp);
void sting_color_set_mode(int mode); /* 0: auto (TTY & NO_COLOR), 1: always, 2: never */
int sting_color_get_mode(void);
const char *sting_color_status(const sting_color_t *c, const char *status);
const char *sting_color_priority(const sting_color_t *c, int priority);
const char *sting_color_kind(const sting_color_t *c, const char *kind);
const char *sting_color_severity(const sting_color_t *c, const char *sev);

/* Output & Serialization */
void sting_state_plan_counts(const sting_state_t *st, uint32_t root_id,
                             size_t *total_out, size_t *incomplete_out);
bool sting_state_to_json(const sting_state_t *st, char *out_buf, size_t out_cap);
void sting_state_print_board(const sting_state_t *st);
void sting_state_print_focused(const sting_state_t *st, uint32_t target_id);
bool sting_state_focused_to_json(const sting_state_t *st, uint32_t target_id, char *out_buf, size_t out_cap);

/* Rebase & Chain Super-Summary */
typedef struct {
    uint32_t target_id;   /* Root/subtree ID to rebase (ignored when all_completed is true) */
    bool all_completed;   /* Rebase all ready chains whose steps are all done or blocked */
    bool dry_run;         /* Validate and compute counts without writing files or mutating state */
    const char *title;    /* Optional override title for the new super-premise node */
} sting_rebase_opts_t;

typedef struct {
    size_t chains_rebased;
    size_t nodes_removed;
    size_t entries_removed;
    size_t steps_removed;
    size_t wins;
    size_t fails;
    uint32_t last_super_id;
    char summary_path[512];
} sting_rebase_result_t;

bool sting_state_rebase(sting_state_t *st,
                        const sting_rebase_opts_t *opts,
                        sting_rebase_result_t *res_out,
                        const char **err_out);

/* Journal persistence */
bool sting_store_load(sting_state_t *st);
bool sting_store_append_line(const char *path, const char *json_line);

/* Timestamp utilities */
void sting_timestamp_now(char *out, size_t cap);
void sting_timestamp_from_time(int64_t t, char *out, size_t cap);

/* Minimal string buffer & JSON helper */
typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    bool overflow;
} sting_buf_t;

void sting_buf_init(sting_buf_t *b, char *buf, size_t cap);
bool sting_buf_append(sting_buf_t *b, const char *s);
bool sting_buf_append_ch(sting_buf_t *b, char c);
bool sting_buf_append_int(sting_buf_t *b, long long v);
bool sting_buf_append_json_str(sting_buf_t *b, const char *s);

bool sting_json_parse_string(const char **src, char *out, size_t out_cap);
bool sting_json_parse_int(const char **src, long long *out);

#ifdef __cplusplus
}
#endif

#endif /* STINGTRAIL_H */
