#!/usr/bin/env bash
# Ants-balling load-handling probe for grocery_server using stingtrail.
# Follows instruction.md: records premise/assets, writes hypothesis, info log,
# attached plan of action, executes each load probe, and logs success/failure.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ST_BIN="$SCRIPT_DIR/stingtrail"
SERVER_BIN="$SCRIPT_DIR/grocery_server"
HOST="${TARGET_HOST:-127.0.0.1}"
PORT="${TARGET_PORT:-8080}"
BASE_URL="http://${HOST}:${PORT}"
DB_FILE="${ST_DB:-$SCRIPT_DIR/grocery_load_probe.jsonl}"
SERVER_LOG="${SERVER_LOG:-/tmp/grocery_server_probe_$$.log}"

STARTED_SERVER=0
SERVER_PID=""
TMP_DIR="$(mktemp -d /tmp/grocery_probe_XXXXXX)"

cleanup() {
    local rc=$?
    "$ST_BIN" --db "$DB_FILE" context clear >/dev/null 2>&1 || true
    if [ "$STARTED_SERVER" -eq 1 ] && [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill -INT "$SERVER_PID" 2>/dev/null || true
        for _ in 1 2 3 4 5; do
            kill -0 "$SERVER_PID" 2>/dev/null || break
            sleep 0.1
        done
        if kill -0 "$SERVER_PID" 2>/dev/null; then
            kill -TERM "$SERVER_PID" 2>/dev/null || true
        fi
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$TMP_DIR" "$SERVER_LOG"
    exit "$rc"
}
trap cleanup EXIT INT TERM

for bin in "$ST_BIN" "$SERVER_BIN"; do
    if [ ! -x "$bin" ]; then
        printf 'error: required executable not found: %s\n' "$bin" >&2
        exit 1
    fi
done

for cmd in curl ab python3; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        printf 'error: required tool not found in PATH: %s\n' "$cmd" >&2
        exit 1
    fi
done

# Reset journal for a clean, deterministic experiment run unless --append is passed
if [ "${1:-}" != "--append" ]; then
    rm -f "$DB_FILE" "${DB_FILE}.context"
fi

# --- Stingtrail CLI wrappers ---
st() {
    "$ST_BIN" --db "$DB_FILE" --scope "${HOST},localhost" --color=never "$@"
}

extract_id() {
    sed -n 's/.*#\([0-9][0-9]*\).*/\1/p' | head -n 1
}

st_node() {
    local out
    out="$(st node "$@")" || return $?
    printf '%s\n' "$out" >&2
    printf '%s' "$out" | extract_id
}

st_log() {
    local out
    out="$(st log "$@")" || return $?
    printf '%s\n' "$out" >&2
    printf '%s' "$out" | extract_id
}

st_plan() {
    local out
    out="$(st plan "$@")" || return $?
    printf '%s\n' "$out" >&2
    printf '%s' "$out" | extract_id
}

st_step() {
    st step "$@" >&2
}

check_server_alive() {
    curl -sf --max-time 3 "${BASE_URL}/items" >/dev/null 2>&1
}

# Start grocery_server if not already listening on target port
if check_server_alive; then
    printf '[probe] Reusing existing server at %s\n' "$BASE_URL"
else
    if [ "$PORT" -ne 8080 ]; then
        printf 'error: grocery_server hardcodes port 8080; TARGET_PORT=%s is unreachable\n' "$PORT" >&2
        exit 1
    fi
    "$SERVER_BIN" >"$SERVER_LOG" 2>&1 &
    SERVER_PID=$!
    STARTED_SERVER=1
    ready=0
    for _ in $(seq 1 30); do
        if check_server_alive; then
            ready=1
            break
        fi
        sleep 0.1
    done
    if [ "$ready" -ne 1 ]; then
        printf 'error: grocery_server failed to become ready on %s\n' "$BASE_URL" >&2
        cat "$SERVER_LOG" >&2 || true
        exit 1
    fi
    printf '[probe] Started grocery_server (PID %s) on %s\n' "$SERVER_PID" "$BASE_URL"
fi

# --- 1. Root DAG Nodes & Baseline Info ---
PREMISE_ID="$(st_node premise "Probe grocery_server load capability" "Test throughput, concurrency, backlog behavior, HOL blocking, and write saturation on ${BASE_URL}")"
ASSET_ID="$(st_node asset "grocery_server" "C99 single-threaded blocking TCP server (${BASE_URL}, listen backlog=10, MAX_ITEMS=100, BUFFER_SIZE=8192)")"
CTX_ID="$(st_node context "probe-env" "host=${HOST}:${PORT} db=$(basename "$DB_FILE") tools=ab,curl,python3")"

# Sanity check HTTP endpoints before load testing
HOME_CODE="$(curl -s -o /dev/null -w '%{http_code}' --max-time 3 "${BASE_URL}/")"
ITEMS_JSON="$(curl -s --max-time 3 "${BASE_URL}/items")"
ITEMS_CODE="$(curl -s -o /dev/null -w '%{http_code}' --max-time 3 "${BASE_URL}/items")"
INIT_COUNT="$(printf '%s' "$ITEMS_JSON" | python3 -c 'import json,sys; print(len(json.load(sys.stdin)))' 2>/dev/null || echo 0)"

INFO_ID="$(st_log observation \
    "Baseline endpoints healthy (GET /=${HOME_CODE}, GET /items=${ITEMS_CODE}, items=${INIT_COUNT})" \
    --parent "$PREMISE_ID" --sev info \
    --detail "asset=#${ASSET_ID} ctx=#${CTX_ID}; single-threaded accept loop ready for load probes")"

# Helper to parse ApacheBench output into key=value metrics
parse_ab_summary() {
    python3 - "$1" <<'PY'
import re, sys
txt = open(sys.argv[1], encoding="utf-8", errors="replace").read()
def find(pat, default="na"):
    m = re.search(pat, txt)
    return m.group(1) if m else default
complete = find(r"Complete requests:\s+(\d+)", "0")
failed = find(r"Failed requests:\s+(\d+)", "0")
non2xx = find(r"Non-2xx responses:\s+(\d+)", "0")
rps = find(r"Requests per second:\s+([0-9.]+)")
mean_ms = find(r"Time per request:\s+([0-9.]+)\s+\[ms\]\s+\(mean\)")
p99_ms = find(r"99%\s+(\d+)")
max_ms = find(r"100%\s+(\d+)")
print(f"complete={complete} failed={failed} non2xx={non2xx} rps={rps} mean_ms={mean_ms} p99_ms={p99_ms} max_ms={max_ms}")
PY
}

# --- 2. Probe 1: Sequential Baseline Load ---
H1_ID="$(st_log hypothesis \
    "H1: Server sustains >= 500 req/s with 0 failures on sequential GET /items (n=500, c=1)" \
    --parent "$PREMISE_ID" --sev info \
    --detail "Sequential load avoids listen(10) queueing and blocking contention")"
st context set "$H1_ID" >/dev/null
st_log observation "Info: Launching sequential ab benchmark (500 requests, c=1) on /items" \
    --parent "$H1_ID" --sev info --detail "Target=${BASE_URL}/items" >/dev/null
S1_ID="$(st_plan "ab -n 500 -c 1 ${BASE_URL}/items" \
    --why "Measure sequential baseline throughput and latency with zero concurrency contention" \
    --prio 1 --parent "$H1_ID" --target "$HOST")"

st_step "$S1_ID" in_progress "Running 500 sequential GET /items requests"
AB1_OUT="$TMP_DIR/ab1.txt"
if ab -n 500 -c 1 -s 10 "${BASE_URL}/items" >"$AB1_OUT" 2>&1; then
    AB1_RC=0
else
    AB1_RC=$?
fi
AB1_METRICS="$(parse_ab_summary "$AB1_OUT")"
AB1_COMPLETE="$(printf '%s' "$AB1_METRICS" | sed -n 's/.*complete=\([0-9]*\).*/\1/p')"
AB1_FAILED="$(printf '%s' "$AB1_METRICS" | sed -n 's/.*failed=\([0-9]*\).*/\1/p')"
AB1_NON2XX="$(printf '%s' "$AB1_METRICS" | sed -n 's/.*non2xx=\([0-9]*\).*/\1/p')"
AB1_RPS="$(printf '%s' "$AB1_METRICS" | sed -n 's/.*rps=\([^ ]*\).*/\1/p')"

RPS_OK="$(python3 -c "print(1 if float('${AB1_RPS:-0}'.replace('na','0')) >= 500.0 else 0)")"
if [ "$AB1_RC" -eq 0 ] && [ "${AB1_COMPLETE:-0}" -eq 500 ] && [ "${AB1_FAILED:-1}" -eq 0 ] && [ "${AB1_NON2XX:-1}" -eq 0 ] && [ "$RPS_OK" -eq 1 ]; then
    st_step "$S1_ID" done "Succeeded: ${AB1_METRICS}"
    st_log evidence "H1 confirmed: sequential GET /items achieved ${AB1_RPS} req/s with 0 errors" \
        --parent "$S1_ID" --sev info --detail "$AB1_METRICS" >/dev/null
elif [ "$AB1_RC" -eq 0 ] && [ "${AB1_FAILED:-1}" -eq 0 ]; then
    st_step "$S1_ID" done "Completed below RPS threshold: ${AB1_METRICS}"
    st_log refutation "H1 partial refutation: 0 errors but RPS ${AB1_RPS} < 500 req/s" \
        --parent "$S1_ID" --sev low --detail "$AB1_METRICS" >/dev/null
else
    st_step "$S1_ID" blocked "Failed (rc=${AB1_RC}): ${AB1_METRICS}"
    st_log refutation "H1 failed: sequential benchmark errored (${AB1_METRICS})" \
        --parent "$S1_ID" --sev high --detail "$(tail -n 5 "$AB1_OUT" | tr '\n' ' ')" >/dev/null
fi

# --- 3. Probe 2: Concurrency Above listen(10) Backlog ---
H2_ID="$(st_log hypothesis \
    "H2: Concurrent load (n=1000, c=32 > listen backlog 10) drains cleanly with 0 dropped requests" \
    --parent "$PREMISE_ID" --sev info \
    --detail "Loopback SYN backlog may buffer bursts, but single-threaded accept serializes latency")"
st context set "$H2_ID" >/dev/null
st_log observation "Info: Launching concurrent ab benchmark (1000 requests, c=32) against listen(10)" \
    --parent "$H2_ID" --sev info --detail "Expect elevated mean/p99 latency from serialized accept()" >/dev/null
S2_ID="$(st_plan "ab -n 1000 -c 32 ${BASE_URL}/items" \
    --why "Stress single-threaded accept loop and listen(10) backlog with 32 concurrent workers" \
    --prio 1 --parent "$H2_ID" --target "$HOST")"

st_step "$S2_ID" in_progress "Running 1000 concurrent GET /items requests (c=32)"
AB2_OUT="$TMP_DIR/ab2.txt"
if ab -n 1000 -c 32 -s 10 "${BASE_URL}/items" >"$AB2_OUT" 2>&1; then
    AB2_RC=0
else
    AB2_RC=$?
fi
AB2_METRICS="$(parse_ab_summary "$AB2_OUT")"
AB2_COMPLETE="$(printf '%s' "$AB2_METRICS" | sed -n 's/.*complete=\([0-9]*\).*/\1/p')"
AB2_FAILED="$(printf '%s' "$AB2_METRICS" | sed -n 's/.*failed=\([0-9]*\).*/\1/p')"
AB2_NON2XX="$(printf '%s' "$AB2_METRICS" | sed -n 's/.*non2xx=\([0-9]*\).*/\1/p')"

if [ "$AB2_RC" -eq 0 ] && [ "${AB2_COMPLETE:-0}" -eq 1000 ] && [ "${AB2_FAILED:-1}" -eq 0 ] && [ "${AB2_NON2XX:-1}" -eq 0 ]; then
    st_step "$S2_ID" done "Succeeded: ${AB2_METRICS}"
    st_log evidence "H2 confirmed: 1000 requests at c=32 drained cleanly (${AB2_METRICS})" \
        --parent "$S2_ID" --sev info --detail "$AB2_METRICS" >/dev/null
else
    st_step "$S2_ID" blocked "Degraded/failed (rc=${AB2_RC}): ${AB2_METRICS}"
    st_log refutation "H2 refuted: c=32 burst experienced failures/drops (${AB2_METRICS})" \
        --parent "$S2_ID" --sev medium --detail "$(tail -n 5 "$AB2_OUT" | tr '\n' ' ')" >/dev/null
fi

# --- 4. Probe 3: Heavy Concurrency Burst (c=256) ---
H3_ID="$(st_log hypothesis \
    "H3: Heavy concurrency wave (n=2000, c=256 >> listen backlog 10) triggers connection failures or >= 25ms tail latency" \
    --parent "$PREMISE_ID" --sev low \
    --detail "256 simultaneous clients exceed listen(10) by >25x")"
st context set "$H3_ID" >/dev/null
st_log observation "Info: Launching heavy concurrent burst (2000 requests, c=256) on /items" \
    --parent "$H3_ID" --sev info --detail "Testing kernel backlog overflow and serialized accept() tail latency" >/dev/null
S3_ID="$(st_plan "ab -n 2000 -c 256 ${BASE_URL}/items" \
    --why "Determine saturation point, drops, and tail latency under 256 concurrent connections" \
    --prio 2 --parent "$H3_ID" --target "$HOST")"

st_step "$S3_ID" in_progress "Running 2000 GET /items requests at c=256"
AB3_OUT="$TMP_DIR/ab3.txt"
if ab -n 2000 -c 256 -s 15 "${BASE_URL}/items" >"$AB3_OUT" 2>&1; then
    AB3_RC=0
else
    AB3_RC=$?
fi
AB3_METRICS="$(parse_ab_summary "$AB3_OUT")"
AB3_COMPLETE="$(printf '%s' "$AB3_METRICS" | sed -n 's/.*complete=\([0-9]*\).*/\1/p')"
AB3_FAILED="$(printf '%s' "$AB3_METRICS" | sed -n 's/.*failed=\([0-9]*\).*/\1/p')"
AB3_NON2XX="$(printf '%s' "$AB3_METRICS" | sed -n 's/.*non2xx=\([0-9]*\).*/\1/p')"
AB3_MAX_MS="$(printf '%s' "$AB3_METRICS" | sed -n 's/.*max_ms=\([^ ]*\).*/\1/p')"
TAIL_SPIKE="$(python3 -c "print(1 if int('${AB3_MAX_MS:-0}'.replace('na','0')) >= 25 else 0)")"

if [ "$AB3_RC" -ne 0 ] || [ "${AB3_FAILED:-0}" -gt 0 ] || [ "${AB3_NON2XX:-0}" -gt 0 ]; then
    st_step "$S3_ID" blocked "Errors at c=256 (rc=${AB3_RC}): ${AB3_METRICS}"
    st_log finding "H3 confirmed: c=256 burst hit connection errors/drops (${AB3_METRICS})" \
        --parent "$S3_ID" --sev medium --detail "$(tail -n 5 "$AB3_OUT" | tr '\n' ' ')" >/dev/null
elif [ "$TAIL_SPIKE" -eq 1 ]; then
    st_step "$S3_ID" done "Succeeded with tail spike: ${AB3_METRICS}"
    st_log evidence "H3 confirmed: 0 drops at c=256 but max latency reached ${AB3_MAX_MS}ms (${AB3_METRICS})" \
        --parent "$S3_ID" --sev low --detail "$AB3_METRICS" >/dev/null
else
    st_step "$S3_ID" done "Succeeded cleanly: ${AB3_METRICS}"
    st_log refutation "H3 refuted: 2000 req at c=256 drained with 0 drops and max_ms=${AB3_MAX_MS}ms" \
        --parent "$S3_ID" --sev info --detail "$AB3_METRICS" >/dev/null
fi

# --- 5. Probe 4: Slow-Client Head-of-Line (HOL) Blocking ---
H4_ID="$(st_log hypothesis \
    "H4: A single idle connected client blocks all other HTTP clients until it sends or closes (HOL blocking)" \
    --parent "$PREMISE_ID" --sev medium \
    --detail "server.c calls blocking recv() synchronously inside accept() loop")"
st context set "$H4_ID" >/dev/null
st_log observation "Info: Holding 1 idle TCP connection for 1.2s while probing GET /items concurrently" \
    --parent "$H4_ID" --sev info --detail "Expect fast GET /items to stall >= 0.9s behind idle socket" >/dev/null
S4_ID="$(st_plan "python3 idle-socket + concurrent curl GET /items" \
    --why "Test whether one slow client stalls the entire grocery_server event loop" \
    --prio 2 --parent "$H4_ID" --target "$HOST")"

st_step "$S4_ID" in_progress "Running slow-client HOL blocking probe (1.2s hold)"
HOL_RESULT="$(python3 - "$HOST" "$PORT" <<'PY'
import socket, threading, time, urllib.request

host, port = __import__("sys").argv[1], int(__import__("sys").argv[2])
accepted = threading.Event()
slow_res = {}

def slow_client():
    s = socket.create_connection((host, port), timeout=5)
    accepted.set()
    time.sleep(1.2)
    s.sendall(b"GET /items HTTP/1.1\r\nHost: localhost\r\n\r\n")
    data = s.recv(4096)
    s.close()
    slow_res["bytes"] = len(data)

t = threading.Thread(target=slow_client)
t.start()
accepted.wait()
time.sleep(0.1)

t0 = time.monotonic()
try:
    with urllib.request.urlopen(f"http://{host}:{port}/items", timeout=5) as r:
        code = r.status
        body_len = len(r.read())
except Exception as e:
    code = f"ERR:{e}"
    body_len = 0
dt = time.monotonic() - t0
t.join()
print(f"fast_client_code={code} elapsed_s={dt:.3f} fast_bytes={body_len} slow_bytes={slow_res.get('bytes', 0)}")
PY
)"
HOL_RC=$?
HOL_ELAPSED="$(printf '%s' "$HOL_RESULT" | sed -n 's/.*elapsed_s=\([0-9.]*\).*/\1/p')"
HOL_CODE="$(printf '%s' "$HOL_RESULT" | sed -n 's/.*fast_client_code=\([^ ]*\).*/\1/p')"
HOL_BLOCKED="$(python3 -c "print(1 if float('${HOL_ELAPSED:-0}') >= 0.9 else 0)")"

if [ "$HOL_RC" -eq 0 ] && [ "$HOL_CODE" = "200" ] && [ "$HOL_BLOCKED" -eq 1 ]; then
    st_step "$S4_ID" done "Confirmed HOL blocking: ${HOL_RESULT}"
    st_log finding "HOL blocking confirmed: single 1.2s idle socket stalled concurrent GET /items for ${HOL_ELAPSED}s" \
        --parent "$S4_ID" --sev high --detail "${HOL_RESULT}; single-threaded recv() stalls entire server" >/dev/null
elif [ "$HOL_RC" -eq 0 ] && [ "$HOL_CODE" = "200" ]; then
    st_step "$S4_ID" done "No HOL stall observed: ${HOL_RESULT}"
    st_log refutation "H4 refuted: concurrent GET /items completed in ${HOL_ELAPSED}s despite idle socket" \
        --parent "$S4_ID" --sev info --detail "$HOL_RESULT" >/dev/null
else
    st_step "$S4_ID" blocked "Probe failed: ${HOL_RESULT}"
    st_log refutation "HOL probe errored: ${HOL_RESULT}" \
        --parent "$S4_ID" --sev medium --detail "$HOL_RESULT" >/dev/null
fi

# --- 6. Probe 5: Parallel Write Contention & Inventory Capacity Saturation ---
H5_ID="$(st_log hypothesis \
    "H5: Parallel POST /buy enforces exact stock limit (50x 200, 10x 409) and POST /items caps at MAX_ITEMS=100 (507)" \
    --parent "$PREMISE_ID" --sev info \
    --detail "Serial accept() prevents data races on stock, while MAX_ITEMS=100 caps inventory")"
st context set "$H5_ID" >/dev/null
st_log observation "Info: Launching 60 parallel POST /buy (id=1, qty=1) and 100 POST /items (short names)" \
    --parent "$H5_ID" --sev info --detail "Initial item #1 stock=50, initial item_count=${INIT_COUNT}, MAX_ITEMS=100" >/dev/null
S5_ID="$(st_plan "60x parallel POST /buy (id=1,qty=1) + 100x POST /items saturation" \
    --why "Verify state consistency under parallel mutations and capacity enforcement at MAX_ITEMS=100" \
    --prio 2 --parent "$H5_ID" --target "$HOST")"

st_step "$S5_ID" in_progress "Running parallel POST /buy and POST /items saturation"
BUY_CODES_FILE="$TMP_DIR/buy_codes.txt"
: >"$BUY_CODES_FILE"
BUY_PIDS=()
for i in $(seq 1 60); do
    curl -s -o /dev/null -w '%{http_code}\n' --max-time 10 \
        -X POST -d 'id=1&qty=1' "${BASE_URL}/buy" >>"$BUY_CODES_FILE" &
    BUY_PIDS+=($!)
done
wait "${BUY_PIDS[@]}"

BUY_200="$(grep -c '^200$' "$BUY_CODES_FILE" || true)"
BUY_409="$(grep -c '^409$' "$BUY_CODES_FILE" || true)"
BUY_OTHER="$(grep -cvE '^(200|409)$' "$BUY_CODES_FILE" || true)"

REM_STOCK="$(curl -s --max-time 3 "${BASE_URL}/items" | python3 -c 'import json,sys; d=json.load(sys.stdin); print([x["stock"] for x in d if x["id"]==1][0])' 2>/dev/null || echo err)"

ADD_CODES_FILE="$TMP_DIR/add_codes.txt"
: >"$ADD_CODES_FILE"
ADD_PIDS=()
for i in $(seq 1 100); do
    curl -s -o /dev/null -w '%{http_code}\n' --max-time 5 \
        -X POST -d "name=Ant-${i}&price=1.25&stock=10" "${BASE_URL}/items" >>"$ADD_CODES_FILE" &
    ADD_PIDS+=($!)
    # Batch in waves of 20 so we don't overwhelm client sockets
    if [ $((i % 20)) -eq 0 ]; then
        wait "${ADD_PIDS[@]}"
        ADD_PIDS=()
    fi
done
if [ ${#ADD_PIDS[@]} -gt 0 ]; then
    wait "${ADD_PIDS[@]}"
fi

ADD_201="$(grep -c '^201$' "$ADD_CODES_FILE" || true)"
ADD_507="$(grep -c '^507$' "$ADD_CODES_FILE" || true)"
ADD_OTHER="$(grep -cvE '^(201|507)$' "$ADD_CODES_FILE" || true)"
FINAL_COUNT="$(curl -s --max-time 3 "${BASE_URL}/items" | python3 -c 'import json,sys; print(len(json.load(sys.stdin)))' 2>/dev/null || echo err)"

WRITE_SUMMARY="buy(200=${BUY_200},409=${BUY_409},other=${BUY_OTHER},rem_stock=${REM_STOCK}) add(201=${ADD_201},507=${ADD_507},other=${ADD_OTHER},final_items=${FINAL_COUNT})"

if [ "$BUY_200" -eq 50 ] && [ "$BUY_409" -eq 10 ] && [ "$BUY_OTHER" -eq 0 ] && [ "$REM_STOCK" = "0" ] && \
   [ "$ADD_201" -eq 95 ] && [ "$ADD_507" -eq 5 ] && [ "$ADD_OTHER" -eq 0 ] && [ "$FINAL_COUNT" = "100" ]; then
    st_step "$S5_ID" done "Succeeded: ${WRITE_SUMMARY}"
    st_log evidence "H5 confirmed: exact stock exhaustion and MAX_ITEMS=100 cap enforced cleanly" \
        --parent "$S5_ID" --sev info --detail "$WRITE_SUMMARY" >/dev/null
else
    st_step "$S5_ID" blocked "Unexpected counts: ${WRITE_SUMMARY}"
    st_log refutation "H5 discrepancy under parallel writes: ${WRITE_SUMMARY}" \
        --parent "$S5_ID" --sev medium --detail "$WRITE_SUMMARY" >/dev/null
fi

# Verify server survived all probes intact
if check_server_alive; then
    st_log observation "Post-probe health check: grocery_server still alive and serving GET /items (200 OK)" \
        --parent "$PREMISE_ID" --sev info --detail "Journal saved to ${DB_FILE}" >/dev/null
else
    st_log finding "Post-probe health check failed: grocery_server stopped responding!" \
        --parent "$PREMISE_ID" --sev critical --detail "Server died during load probe" >/dev/null
fi

"$ST_BIN" --db "$DB_FILE" context clear >/dev/null 2>&1 || true

printf '\n=== STINGTRAIL BOARD (%s) ===\n' "$DB_FILE"
"$ST_BIN" --db "$DB_FILE" status
printf '\n=== FOCUSED LINEAGE FOR HOL BLOCKING STEP (#%s) ===\n' "$S4_ID"
"$ST_BIN" --db "$DB_FILE" focus "$S4_ID"
