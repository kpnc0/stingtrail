#!/usr/bin/env sh
# Sourceable convenience wrappers for stingtrail.
# Usage: source ./st.sh [path/to/journal.jsonl] [scope_hosts]

if [ -n "${1:-}" ] && [[ "${1:-}" != -* ]]; then
  export ST_DB="$1"
fi
if [ -n "${2:-}" ]; then
  export ST_SCOPE="$2"
fi

: "${ST_DB:=stingtrail.jsonl}"

_st_bin() {
  if [ -x "./build/stingtrail" ]; then
    printf '%s\n' "./build/stingtrail"
  elif command -v stingtrail >/dev/null 2>&1; then
    command -v stingtrail
  else
    printf 'st: stingtrail binary not found (build ./build/stingtrail or add to PATH)\n' >&2
    return 127
  fi
}

# Core runner: automatically passes --db and optional --scope
st() {
  local bin
  bin="$(_st_bin)" || return $?
  if [ -n "${ST_SCOPE:-}" ]; then
    "$bin" --db "$ST_DB" --scope "$ST_SCOPE" "$@"
  else
    "$bin" --db "$ST_DB" "$@"
  fi
}

# Extract numeric "id" or "step_id" without requiring external JSON tools
_st_id() {
  sed -n 's/.*"id"[[:space:]]*:[[:space:]]*\([0-9][0-9]*\).*/\1/p' | head -n 1
}

_st_step_id() {
  sed -n 's/.*"step_id"[[:space:]]*:[[:space:]]*\([0-9][0-9]*\).*/\1/p' | head -n 1
}

# Switch active journal file on the fly
stdb() {
  if [ $# -eq 0 ]; then
    printf '%s\n' "$ST_DB"
    return 0
  fi
  export ST_DB="$1"
  printf 'ST_DB=%s\n' "$ST_DB"
}

# Root nodes: stprem / stasset / stctx ["detail"] -> prints & optionally sets context
stprem() {
  local out id
  out="$(st node premise "$1" ${2:+"$2"})" || return $?
  printf '%s\n' "$out"
  id="$(printf '%s' "$out" | _st_id)"
  [ -n "$id" ] && st context set "$id" >/dev/null 2>&1 || true
}

stasset() {
  st node asset "$1" ${2:+"$2"}
}

stctx() {
  case "${1:-get}" in
    get|set|clear) st context "$@" ;;
    *) st node context "$1" ${2:+"$2"} ;;
  esac
}

# Fast thought/reasoning loggers:
# Usage: stobs "summary" [parent_id] [sev] [detail]
_st_log() {
  local kind="$1" summary="$2" parent="${3:-}" sev="${4:-}" detail="${5:-}"
  set -- log "$kind" "$summary"
  [ -n "$parent" ] && set -- "$@" --parent "$parent"
  [ -n "$sev" ]    && set -- "$@" --sev "$sev"
  [ -n "$detail" ] && set -- "$@" --detail "$detail"
  st "$@"
}

stobs()  { _st_log observation "$1" "${2:-}" "${3:-}" "${4:-}"; }
sthyp()  {
  local out id
  out="$(_st_log hypothesis "$1" "${2:-}" "${3:-}" "${4:-}")" || return $?
  printf '%s\n' "$out"
  id="$(printf '%s' "$out" | _st_id)"
  [ -n "$id" ] && st context set "$id" >/dev/null 2>&1 || true
}
stev()   { _st_log evidence    "$1" "${2:-}" "${3:-}" "${4:-}"; }
stref()  { _st_log refutation  "$1" "${2:-}" "${3:-}" "${4:-}"; }
stfind() { _st_log finding     "$1" "${2:-}" "${3:-}" "${4:-}"; }

# Fast planning:
# Usage: stplan "action" "rationale" [prio 1-5] [parent] [target]
stplan() {
  local action="$1" why="$2" prio="${3:-}" parent="${4:-}" target="${5:-}"
  set -- plan "$action" --why "$why"
  [ -n "$prio" ]   && set -- "$@" --prio "$prio"
  [ -n "$parent" ] && set -- "$@" --parent "$parent"
  [ -n "$target" ] && set -- "$@" --target "$target"
  st "$@"
}

# Fast step lifecycle shortcuts
ststart() { st step "$1" in_progress "${2:-started}"; }
stdone()  { st step "$1" done        "${2:-completed}"; }
stblock() { st step "$1" blocked     "${2:-blocked}"; }
stpend()  { st step "$1" pending     "${2:-}"; }

# Inspection shortcuts
sts()  { st status "$@"; }
stf()  { st focus "$@"; }
stx()  { st export "$@"; }
