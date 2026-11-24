#define _POSIX_C_SOURCE 200809L
#include "stingtrail.h"
#include <string.h>
#include <ctype.h>

void sting_scope_init(sting_scope_t *s, bool enforce_scope) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->enforce_scope = enforce_scope;
}

bool sting_scope_add_host(sting_scope_t *s, const char *host) {
    if (!s || !host || host[0] == '\0') return false;
    if (s->allowed_host_count >= STINGTRAIL_MAX_SCOPE_HOSTS) return false;

    /* Trim whitespace */
    while (*host == ' ' || *host == '\t') host++;
    size_t len = strlen(host);
    while (len > 0 && (host[len - 1] == ' ' || host[len - 1] == '\t')) len--;
    if (len == 0 || len >= 128) return false;

    /* Check duplicate */
    for (size_t i = 0; i < s->allowed_host_count; i++) {
        if (strncmp(s->allowed_hosts[i], host, len) == 0 && s->allowed_hosts[i][len] == '\0') {
            return true;
        }
    }

    memcpy(s->allowed_hosts[s->allowed_host_count], host, len);
    s->allowed_hosts[s->allowed_host_count][len] = '\0';
    s->allowed_host_count++;
    return true;
}

bool sting_scope_parse_csv(sting_scope_t *s, const char *csv) {
    if (!s || !csv) return false;
    const char *p = csv;
    while (*p) {
        const char *comma = strchr(p, ',');
        char item[128];
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (len > 0 && len < sizeof(item)) {
            memcpy(item, p, len);
            item[len] = '\0';
            sting_scope_add_host(s, item);
        }
        if (!comma) break;
        p = comma + 1;
    }
    return true;
}

static bool has_unsafe_controls(const char *str) {
    if (!str) return false;
    for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
        if (*p < 0x20 && *p != '\t' && *p != '\n' && *p != '\r') {
            return true;
        }
    }
    return false;
}

bool sting_is_destructive(const char *action) {
    if (!action) return false;
    static const char *const BLOCKED[] = {
        "rm -rf",
        "rm -r -f",
        "rm -f -r",
        "rm --recursive",
        "shred ",
        "wipefs ",
        "blkdiscard ",
        "mkfs",
        "dd if=",
        ":(){",
        ":() {",
        "> /dev/sd",
        ">  /dev/sd",
        "chmod -R 777",
        "chmod 777 -R",
        NULL
    };
    for (size_t i = 0; BLOCKED[i]; i++) {
        if (strstr(action, BLOCKED[i])) {
            return true;
        }
    }
    return false;
}

static bool matches_destructive_pattern(const char *action) {
    return sting_is_destructive(action);
}

bool sting_is_benign_navigation(const char *cmd) {
    if (!cmd) return false;
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    if (*cmd == '\0') return true;

    /* Skip leading sudo or env if present */
    if (strncmp(cmd, "sudo ", 5) == 0) cmd += 5;
    else if (strncmp(cmd, "env ", 4) == 0) cmd += 4;
    while (*cmd == ' ' || *cmd == '\t') cmd++;

    /* Extract leading command binary name */
    char token[64];
    size_t len = 0;
    while (cmd[len] && cmd[len] != ' ' && cmd[len] != '\t' && cmd[len] != '\n' && len < sizeof(token) - 1) {
        token[len] = cmd[len];
        len++;
    }
    token[len] = '\0';

    const char *base = strrchr(token, '/');
    const char *bin = base ? base + 1 : token;

    /* Benign navigation and inspection commands */
    static const char *const BENIGN[] = {
        "cd", "ls", "pwd", "cat", "head", "tail", "less", "more",
        "file", "stat", "whoami", "id", "uname", "date", "uptime",
        "echo", "printf", "which", "whereis", "type", "env", "printenv",
        "clear", "true", "false", "test", "[", "history", "wc",
        "stingtrail", "stingtrail-mcp", "stingtrail-gui",
        NULL
    };

    for (size_t i = 0; BENIGN[i]; i++) {
        if (strcmp(bin, BENIGN[i]) == 0) {
            return true;
        }
    }

    if (strcmp(bin, "git") == 0) {
        const char *sub = cmd + len;
        while (*sub == ' ' || *sub == '\t') sub++;
        if (strncmp(sub, "status", 6) == 0 ||
            strncmp(sub, "diff", 4) == 0 ||
            strncmp(sub, "log", 3) == 0 ||
            strncmp(sub, "branch", 6) == 0 ||
            strncmp(sub, "show", 4) == 0) {
            return true;
        }
    }

    return false;
}

sting_hook_verdict_t sting_hook_classify(const char *cmd, uint32_t active_context_id, const char **reason_out) {
    if (!cmd || cmd[0] == '\0') {
        return STING_HOOK_ALLOW_BENIGN;
    }
    if (sting_is_destructive(cmd)) {
        if (reason_out) *reason_out = "destructive command pattern blocked";
        return STING_HOOK_BLOCK_DESTRUCTIVE;
    }
    if (sting_is_benign_navigation(cmd)) {
        return STING_HOOK_ALLOW_BENIGN;
    }
    if (active_context_id == 0) {
        if (reason_out) *reason_out = "command requires an active hypothesis context";
        return STING_HOOK_BLOCK_NO_CONTEXT;
    }
    return STING_HOOK_ALLOW_ACTIVE;
}

sting_guard_verdict_t sting_guard_check(const sting_scope_t *s,
                                        const char *target_host,
                                        const char *action,
                                        const char **reason_out) {
    if (action && matches_destructive_pattern(action)) {
        if (reason_out) *reason_out = "destructive command pattern blocked";
        return STING_GUARD_DENY;
    }

    if ((action && has_unsafe_controls(action)) ||
        (target_host && has_unsafe_controls(target_host))) {
        if (reason_out) *reason_out = "unsafe control characters in input";
        return STING_GUARD_DENY;
    }

    if (s && s->enforce_scope) {
        if (!target_host || target_host[0] == '\0') {
            if (reason_out) *reason_out = "target required when scope enforcement is enabled";
            return STING_GUARD_DENY;
        }
        bool allowed = false;
        for (size_t i = 0; i < s->allowed_host_count; i++) {
            if (strcmp(s->allowed_hosts[i], target_host) == 0) {
                allowed = true;
                break;
            }
        }
        if (!allowed) {
            if (reason_out) *reason_out = "target host is outside authorized scope";
            return STING_GUARD_DENY;
        }
    }

    return STING_GUARD_ALLOW;
}
