#define _POSIX_C_SOURCE 200809L
#include "stingtrail.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

void sting_timestamp_from_time(int64_t t, char *out, size_t cap) {
    if (!out || cap < 15) return;
    time_t sec = (time_t)t;
    struct tm tm_info;
#if defined(_WIN32)
    localtime_s(&tm_info, &sec);
#else
    localtime_r(&sec, &tm_info);
#endif
    strftime(out, cap, "%Y%m%d%H%M%S", &tm_info);
}

void sting_timestamp_now(char *out, size_t cap) {
    sting_timestamp_from_time((int64_t)time(NULL), out, cap);
}

bool sting_store_append_line(const char *path, const char *json_line) {
    if (!path || path[0] == '\0' || !json_line) return false;
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return false;

    size_t len = strlen(json_line);
    ssize_t w = write(fd, json_line, len);
    if (w < 0 || (size_t)w != len) {
        close(fd);
        return false;
    }
    if (len == 0 || json_line[len - 1] != '\n') {
        if (write(fd, "\n", 1) != 1) {
            close(fd);
            return false;
        }
    }
#if defined(_POSIX_SYNCHRONIZED_IO) && _POSIX_SYNCHRONIZED_IO > 0
    fdatasync(fd);
#else
    fsync(fd);
#endif
    close(fd);
    return true;
}

static const char *find_json_field(const char *json, const char *key) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t' || *p == ':') p++;
    return p;
}

static void parse_json_str_field(const char *line, const char *key, char *out, size_t out_cap) {
    out[0] = '\0';
    const char *p = find_json_field(line, key);
    if (p && *p == '"') {
        sting_json_parse_string(&p, out, out_cap);
    }
}

static long long parse_json_int_field(const char *line, const char *key, long long def_val) {
    const char *p = find_json_field(line, key);
    if (p) {
        long long v = 0;
        if (sting_json_parse_int(&p, &v)) {
            return v;
        }
    }
    return def_val;
}

bool sting_store_load(sting_state_t *st) {
    if (!st || st->journal_path[0] == '\0') return false;
    FILE *f = fopen(st->journal_path, "r");
    if (!f) return false;

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len == sizeof(line) - 1 && line[len - 1] != '\n' && !feof(f)) {
            int ch;
            while ((ch = fgetc(f)) != EOF && ch != '\n') {}
            st->dropped_long_lines++;
            continue;
        }
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;

        char op[32];
        parse_json_str_field(line, "op", op, sizeof(op));
        long long id = parse_json_int_field(line, "id", 0);
        char created[16], modified[16];
        parse_json_str_field(line, "created", created, sizeof(created));
        parse_json_str_field(line, "modified", modified, sizeof(modified));

        if (strcmp(op, "node") == 0) {
            char kind[32], name[128], detail[256];
            parse_json_str_field(line, "kind", kind, sizeof(kind));
            parse_json_str_field(line, "name", name, sizeof(name));
            parse_json_str_field(line, "detail", detail, sizeof(detail));

            if (st->node_count < STINGTRAIL_MAX_NODES) {
                sting_node_t *n = &st->nodes[st->node_count++];
                n->id = (id > 0) ? (uint32_t)id : st->next_id++;
                snprintf(n->kind, sizeof(n->kind), "%s", kind[0] ? kind : "asset");
                snprintf(n->name, sizeof(n->name), "%s", name);
                snprintf(n->detail, sizeof(n->detail), "%s", detail);
                snprintf(n->created, sizeof(n->created), "%s", created);
                snprintf(n->modified, sizeof(n->modified), "%s", modified[0] ? modified : created);
                if (n->id >= st->next_id) st->next_id = n->id + 1;
            } else {
                st->dropped_nodes++;
            }
        } else if (strcmp(op, "entry") == 0) {
            long long pid = parse_json_int_field(line, "parent_id", 0);
            char kind[32], severity[16], summary[160], detail[512];
            parse_json_str_field(line, "kind", kind, sizeof(kind));
            parse_json_str_field(line, "severity", severity, sizeof(severity));
            parse_json_str_field(line, "summary", summary, sizeof(summary));
            parse_json_str_field(line, "detail", detail, sizeof(detail));

            if (st->entry_count < STINGTRAIL_MAX_ENTRIES) {
                sting_entry_t *e = &st->entries[st->entry_count++];
                e->id = (id > 0) ? (uint32_t)id : st->next_id++;
                e->parent_id = (uint32_t)pid;
                snprintf(e->kind, sizeof(e->kind), "%s", kind[0] ? kind : "observation");
                snprintf(e->severity, sizeof(e->severity), "%s", severity[0] ? severity : "info");
                snprintf(e->summary, sizeof(e->summary), "%s", summary);
                snprintf(e->detail, sizeof(e->detail), "%s", detail);
                snprintf(e->created, sizeof(e->created), "%s", created);
                snprintf(e->modified, sizeof(e->modified), "%s", modified[0] ? modified : created);
                if (e->id >= st->next_id) st->next_id = e->id + 1;
            } else {
                st->dropped_entries++;
            }
        } else if (strcmp(op, "plan") == 0) {
            long long pid = parse_json_int_field(line, "parent_id", 0);
            long long prio = parse_json_int_field(line, "priority", 3);
            char target[128], action[256], rationale[384], status[24];
            parse_json_str_field(line, "target", target, sizeof(target));
            parse_json_str_field(line, "action", action, sizeof(action));
            parse_json_str_field(line, "rationale", rationale, sizeof(rationale));
            parse_json_str_field(line, "status", status, sizeof(status));

            if (st->step_count < STINGTRAIL_MAX_STEPS) {
                sting_step_t *s = &st->steps[st->step_count++];
                s->id = (id > 0) ? (uint32_t)id : st->next_id++;
                s->parent_id = (uint32_t)pid;
                s->priority = (int)prio;
                snprintf(s->status, sizeof(s->status), "%s", status[0] ? status : "pending");
                snprintf(s->target, sizeof(s->target), "%s", target);
                snprintf(s->action, sizeof(s->action), "%s", action);
                snprintf(s->rationale, sizeof(s->rationale), "%s", rationale);
                s->outcome[0] = '\0';
                snprintf(s->created, sizeof(s->created), "%s", created);
                snprintf(s->modified, sizeof(s->modified), "%s", modified[0] ? modified : created);
                if (s->id >= st->next_id) st->next_id = s->id + 1;
            } else {
                st->dropped_steps++;
            }
        } else if (strcmp(op, "step_update") == 0) {
            long long step_id = parse_json_int_field(line, "step_id", 0);
            char status[24], outcome[384];
            parse_json_str_field(line, "status", status, sizeof(status));
            parse_json_str_field(line, "outcome", outcome, sizeof(outcome));

            for (size_t s = 0; s < st->step_count; s++) {
                if (st->steps[s].id == (uint32_t)step_id) {
                    if (status[0]) {
                        snprintf(st->steps[s].status, sizeof(st->steps[s].status), "%s", status);
                    }
                    if (outcome[0]) {
                        snprintf(st->steps[s].outcome, sizeof(st->steps[s].outcome), "%s", outcome);
                    }
                    if (modified[0]) {
                        snprintf(st->steps[s].modified, sizeof(st->steps[s].modified), "%s", modified);
                    } else if (created[0]) {
                        snprintf(st->steps[s].modified, sizeof(st->steps[s].modified), "%s", created);
                    }
                    break;
                }
            }
        }
    }

    fclose(f);
    return true;
}
