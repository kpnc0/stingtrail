#define _POSIX_C_SOURCE 200809L
#include "stingtrail.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void sting_buf_init(sting_buf_t *b, char *buf, size_t cap) {
    if (!b) return;
    b->buf = buf;
    b->cap = cap;
    b->len = 0;
    b->overflow = false;
    if (buf && cap > 0) {
        buf[0] = '\0';
    }
}

bool sting_buf_append(sting_buf_t *b, const char *s) {
    if (!b || !b->buf || b->overflow || !s) return false;
    size_t slen = strlen(s);
    if (b->len + slen >= b->cap) {
        b->overflow = true;
        return false;
    }
    memcpy(b->buf + b->len, s, slen);
    b->len += slen;
    b->buf[b->len] = '\0';
    return true;
}

bool sting_buf_append_ch(sting_buf_t *b, char c) {
    if (!b || !b->buf || b->overflow) return false;
    if (b->len + 1 >= b->cap) {
        b->overflow = true;
        return false;
    }
    b->buf[b->len++] = c;
    b->buf[b->len] = '\0';
    return true;
}

bool sting_buf_append_int(sting_buf_t *b, long long v) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%lld", v);
    return sting_buf_append(b, tmp);
}

bool sting_buf_append_json_str(sting_buf_t *b, const char *s) {
    if (!b || !b->buf || b->overflow) return false;
    if (!sting_buf_append_ch(b, '"')) return false;
    if (!s) return sting_buf_append_ch(b, '"');

    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
            case '"':  if (!sting_buf_append(b, "\\\"")) return false; break;
            case '\\': if (!sting_buf_append(b, "\\\\")) return false; break;
            case '\b': if (!sting_buf_append(b, "\\b"))  return false; break;
            case '\f': if (!sting_buf_append(b, "\\f"))  return false; break;
            case '\n': if (!sting_buf_append(b, "\\n"))  return false; break;
            case '\r': if (!sting_buf_append(b, "\\r"))  return false; break;
            case '\t': if (!sting_buf_append(b, "\\t"))  return false; break;
            default:
                if (*p < 0x20) {
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", *p);
                    if (!sting_buf_append(b, esc)) return false;
                } else {
                    if (!sting_buf_append_ch(b, (char)*p)) return false;
                }
                break;
        }
    }
    return sting_buf_append_ch(b, '"');
}

static void skip_ws(const char **src) {
    while (**src == ' ' || **src == '\t' || **src == '\r' || **src == '\n') {
        (*src)++;
    }
}

bool sting_json_parse_string(const char **src, char *out, size_t out_cap) {
    if (!src || !*src || !out || out_cap == 0) return false;
    skip_ws(src);
    if (**src != '"') return false;
    (*src)++; /* skip opening quote */

    size_t idx = 0;
    while (**src && **src != '"') {
        if (**src == '\\') {
            (*src)++;
            if (!**src) return false;
            char c = **src;
            if (c == '"' || c == '\\' || c == '/') {
                if (idx + 1 < out_cap) out[idx++] = c;
            } else if (c == 'b') {
                if (idx + 1 < out_cap) out[idx++] = '\b';
            } else if (c == 'f') {
                if (idx + 1 < out_cap) out[idx++] = '\f';
            } else if (c == 'n') {
                if (idx + 1 < out_cap) out[idx++] = '\n';
            } else if (c == 'r') {
                if (idx + 1 < out_cap) out[idx++] = '\r';
            } else if (c == 't') {
                if (idx + 1 < out_cap) out[idx++] = '\t';
            } else if (c == 'u') {
                /* skip 4 hex digits */
                for (int h = 0; h < 4 && **src; h++) (*src)++;
                if (idx + 1 < out_cap) out[idx++] = '?';
                continue;
            } else {
                if (idx + 1 < out_cap) out[idx++] = c;
            }
            (*src)++;
        } else {
            if (idx + 1 < out_cap) {
                out[idx++] = **src;
            }
            (*src)++;
        }
    }
    if (**src != '"') return false;
    (*src)++; /* skip closing quote */
    out[idx] = '\0';
    return true;
}

bool sting_json_parse_int(const char **src, long long *out) {
    if (!src || !*src || !out) return false;
    skip_ws(src);
    char *endptr = NULL;
    long long val = strtoll(*src, &endptr, 10);
    if (endptr == *src) return false;
    *out = val;
    *src = endptr;
    return true;
}
