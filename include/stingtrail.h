#ifndef STINGTRAIL_H
#define STINGTRAIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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

/* Output & Serialization */
bool sting_state_to_json(const sting_state_t *st, char *out_buf, size_t out_cap);
void sting_state_print_board(const sting_state_t *st);

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
