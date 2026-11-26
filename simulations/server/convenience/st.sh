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

sthelp() {
	if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
		_st_bold='\033[1m'
		_st_cyan='\033[36m'
		_st_reset='\033[0m'
	else
		_st_bold=''
		_st_cyan=''
		_st_reset=''
	fi

	printf '%b\n' "${_st_bold}stingtrail convenience commands${_st_reset}"
	printf '%b\n' "${_st_cyan}Setup${_st_reset}"
	printf '  source ./st.sh [journal.jsonl] [scope_hosts]  Set ST_DB and optional ST_SCOPE\n'
	printf '  stdb [journal.jsonl]                          Show or switch the active journal\n'
	printf '  st [args...]                                  Run stingtrail with --db and optional --scope\n'

	printf '%b\n' "${_st_cyan}Nodes and context${_st_reset}"
	printf '  stprem "text" [detail]                        Create a premise; set it as context\n'
	printf '  stasset "text" [detail]                       Create an asset\n'
	printf '  stctx [get|set|clear] [args...]               Get, set, or clear context; other input creates a context node\n'

	printf '%b\n' "${_st_cyan}Logging${_st_reset}"
	printf '  stobs "summary" [parent_id] [sev] [detail]     Log an observation\n'
	printf '  sthyp "summary" [parent_id] [sev] [detail]     Log a hypothesis; set it as context\n'
	printf '  stev "summary" [parent_id] [sev] [detail]      Log evidence\n'
	printf '  stref "summary" [parent_id] [sev] [detail]     Log a refutation\n'
	printf '  stfind "summary" [parent_id] [sev] [detail]    Log a finding\n'

	printf '%b\n' "${_st_cyan}Planning and steps${_st_reset}"
	printf '  stplan "action" "rationale" [prio] [parent] [target]\n'
	printf '                                                Create a plan item\n'
	printf '  ststart STEP_ID [note]                        Mark a step in progress\n'
	printf '  stdone STEP_ID [note]                         Mark a step done\n'
	printf '  stblock STEP_ID [note]                        Mark a step blocked\n'
	printf '  stpend STEP_ID [note]                         Mark a step pending\n'

	printf '%b\n' "${_st_cyan}Inspection${_st_reset}"
	printf '  sts [args...]                                 Show status\n'
	printf '  stf [args...]                                 Show or manage focus\n'
	printf '  stx [args...]                                 Export data\n'

	unset _st_bold _st_cyan _st_reset
}
