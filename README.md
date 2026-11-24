# Stingtrail: Ultra-Lightweight Critical Thinking CLI

Stingtrail is a zero-dependency, ultra-lightweight C CLI designed for autonomous AI agents and security researchers. It provides structured reasoning, dynamic hypothesis testing, fail-closed operational safety, and append-only DAG journal persistence.

Inspired by the "red ant" philosophy:
- **Persistent & Resilient**: State survives process crashes, context resets, and subshell forks.
- **Zero-Dependency**: Standard ISO C11 + POSIX libc only. No external libraries, no Raven, no X11, no libcurl, no cJSON.
- **Microsecond Execution**: Builds in < 0.1s, executes in sub-milliseconds.
- **Fail-Closed Operational Safety**: Prevents destructive shell commands and enforces strict network/host engagement boundaries.

---

## 1. Quick Start

### Build & Test
```bash
cd stingtrail
make clean
make all
make test
```

The compiled binary will be located at `build/stingtrail`.

---

## 2. Command Line Interface

Stingtrail stores state in an append-only JSONL journal (`stingtrail.jsonl` by default, or specified via `--db <path>`).

### Global Flags
- `--db <file>`: Path to the JSONL journal database (default: `stingtrail.jsonl`).
- `--scope <cidr_or_hosts>`: Comma-separated list of allowed IPs, CIDR blocks, or hostnames (e.g. `10.0.0.0/24,api.internal`). Enforces fail-closed scope validation on targets and commands.
- `--color[=auto|always|never]`: Control strategic ANSI color output (default: `auto`, enabling colors when stdout is a TTY and `NO_COLOR` is unset).
- `--no-color`: Disable all ANSI color escape sequences (respects [NO_COLOR](https://no-color.org)).

### Commands

#### 1. Define Conceptual Nodes (`node`)
Registers a high-level concept or milestone in the mission DAG.
```bash
./build/stingtrail node <kind> "<name>" [--parent <id>]
```
- `<kind>`: `premise`, `hypothesis`, `evidence`, `refutation`, `milestone`.
- Returns: `{"status":"ok","id":<node_id>,"name":"..."}`.
- *Deduplication*: Calling `node` with the same `kind` and `name` is idempotent and returns the existing node ID.

#### 2. Log Epistemic Reasoning (`log`)
Records granular observations, hypotheses, tests, evidence, or refutations under a parent node.
```bash
./build/stingtrail log <kind> "<content>" [--parent <id>]
```
- `<kind>`: `observation`, `hypothesis`, `test`, `evidence`, `refutation`.
- Returns: `{"status":"ok","entry_id":<id>,"parent":<id>}`.

#### 3. Plan Operational Steps (`plan`)
Enforces proactive planning with mandatory `--why` rationale before execution.
```bash
./build/stingtrail plan "<action>" --why "<rationale>" [--priority 1-5] [--parent <id>]
```
- `--why <rationale>` is **mandatory**. Unjustified actions will be rejected with an error.
- Priority ranges from `1` (critical) to `5` (low). Default is `2`.
- Returns: `{"status":"ok","step_id":<id>,"priority":<p>,"status":"pending"}`.

#### 4. Update Step Lifecycle (`step`)
Updates step execution state with concrete results.
```bash
./build/stingtrail step <id> <status> "<outcome>"
```
- `<status>`: `in_progress`, `done`, `blocked`, `abandoned`.
- Returns: `{"status":"ok","step_id":<id>,"status":"<status>"}`.

#### 5. Mission Status Board (`status`)
Prints an ASCII status board grouping all planned, active, completed, and blocked steps by priority (`[P1]` through `[P5]`).
```bash
./build/stingtrail status
```

#### 6. Export Full or Focused Graph (`export`)
Exports the complete DAG or a focused subtree (all nodes, reasoning entries, steps, and transitions) as compact, valid JSON to stdout.
```bash
# Export full graph
./build/stingtrail export

# Export focused experiment context for LLM ingestion
./build/stingtrail export --focus <id>
```

#### 7. Focused Experiment Context (`focus`)
Extracts a compact, self-contained epistemic slice for an experiment or node using the **Hourglass Traversal** (motivating root-to-target ancestor spine + topological descendant outcomes).
```bash
# ASCII formatted context (motivating spine, target details, epistemic outcomes)
./build/stingtrail focus <id>

# Structured JSON for model prompting and analysis
./build/stingtrail focus <id> --json

# Also supported via status
./build/stingtrail status --focus <id>
```

#### 8. Active Context Management (`context`)
Manages the active hypothesis or premise pointer for the shell session, enabling automatic DAG parent attribution.
```bash
# Check current active context
./build/stingtrail context get

# Bind session to an active hypothesis or premise node
./build/stingtrail context set <node_or_entry_id>

# Clear active context
./build/stingtrail context clear
```

#### 9. Synchronous Shell Interceptor Hook (`hook`)
Installs a zero-dependency, microsecond (`~1.18ms`) synchronous shell trap into an agent's `bash` or `zsh` session.
```bash
# Evaluate hook in current shell
eval "$(./build/stingtrail hook)"
```
- **Benign Navigation Bypass**: Commands like `cd`, `ls`, `pwd`, `cat`, `git status`, and `which` execute silently without friction.
- **Fail-Closed Destructive Guard**: Dangerous commands (`rm -rf`, `shred`, `dd if=`, fork bombs) are blocked synchronously before execution (exit code 126).
- **Epistemic Discipline Gate**: Exploratory or mutative commands (`curl`, `nmap`, `make`, `python3`) are blocked if executed without an active hypothesis, prompting the agent to bind an intent.
- **Automatic Step Attribution**: When an active context is set, exploratory commands are automatically planned and closed under the active hypothesis in `stingtrail.jsonl`.

---

## 3. Operational Safety Guards

Stingtrail includes built-in safety guards to prevent autonomous agents from running out of bounds:

1. **Destructive Command Filtering**: Blocks dangerous commands such as `rm -rf`, `mkfs`, raw block writes (`dd`), fork bombs (`:(){ :|:& };:`), mass permission resets, or pipe-to-shell.
2. **Fail-Closed Scope Enforcement**: When `--scope` is configured, target hosts and URLs outside the authorization boundary are blocked with exit code `2`.
3. **ANSI & Control Character Sanitization**: Strips escape sequences and terminal control characters from log inputs.

---

## 4. Agent Protocol Example

```bash
# 1. Establish the engagement scope and premise
PREMISE_ID=$(./build/stingtrail node premise "Authorized Internal Penetration Test" | jq -r .id)

# 2. Record an observation
./build/stingtrail log observation "Target responds at 10.10.10.25; Port 80 and 443 open" --parent $PREMISE_ID

# 3. Formulate a hypothesis
HYP_ID=$(./build/stingtrail node hypothesis "Web Service exposes administrative endpoints" --parent $PREMISE_ID | jq -r .id)

# 4. Plan step with rationale
STEP_ID=$(./build/stingtrail plan "Fuzz directories using wordlist" --why "Find hidden admin panels" --priority 1 --parent $HYP_ID | jq -r .step_id)

# 5. Begin execution
./build/stingtrail step $STEP_ID in_progress "Running ffuf against http://10.10.10.25/..."

# 6. Complete step with outcome
./build/stingtrail step $STEP_ID done "Discovered /admin_dev endpoint returning HTTP 200"

# 7. Check mission board
./build/stingtrail status
```

---

## 5. Model Context Protocol Server (`stingtrail-mcp`)

Stingtrail includes a zero-dependency Model Context Protocol (MCP) server wrapper built directly on `include/stingtrail.h` without any GUI or external library requirements. It enables AI agents to plan, log hypotheses, update steps, inspect boards, and enforce fail-closed security guards natively via standard MCP JSON-RPC 2.0 over `stdio`.

### Invocation
```bash
# Standalone binary
./build/stingtrail-mcp [--db <journal.jsonl>] [--scope <allowed_hosts>]

# Or via CLI subcommand
./build/stingtrail mcp [--db <journal.jsonl>] [--scope <allowed_hosts>]
```

### Client Configuration (e.g. Claude Desktop, Cursor, AGY)
```json
{
  "mcpServers": {
    "stingtrail": {
      "command": "/path/to/stingtrail/build/stingtrail-mcp",
      "args": ["--db", "stingtrail.jsonl", "--scope", "10.0.0.0/24,api.internal"]
    }
  }
}
```

### Exposed MCP Tools
1. **`stingtrail_node`**: Record a conceptual DAG root node (`premise`, `asset`, `context`).
2. **`stingtrail_log`**: Record epistemic reasoning (`observation`, `hypothesis`, `evidence`, `refutation`, `finding`).
3. **`stingtrail_plan`**: Propose an operational step with mandatory `why` rationale and priority `1` (critical) to `5` (low). Fail-closed guards reject unjustified or destructive actions.
4. **`stingtrail_step`**: Update step lifecycle state (`pending`, `in_progress`, `done`, `blocked`) and record concrete outcomes.
5. **`stingtrail_status`**: Retrieve the mission board (ASCII text or JSON structure).
6. **`stingtrail_export`**: Export the complete DAG state journal as JSON.
7. **`stingtrail_guard`**: Validate arbitrary action commands and target hosts against destructive patterns and authorization scope.

### Exposed MCP Resources
- `stingtrail://board`: Current formatted mission critical-thinking board (`text/plain`).
- `stingtrail://journal`: Complete DAG journal state (`application/json`).

---

## 6. Read-Only GUI Viewer (`stingtrail-gui`)

For visual inspection of mission graphs without altering state, `stingtrail-gui` provides a read-only 2D flow visualizer built on Raven.

### Key Features
- **Strictly Read-Only**: Disables all mutations, status transitions, and journal writes. Context menu and hotkeys only provide non-destructive viewing controls.
- **Focused Subtree Isolation ("See only this" & "See all")**:
  - Right-click any cell and select **"See only this"** to filter the graph strictly to that node and its descendants, re-anchoring the selected node as the root card (`depth 0, parent 0`).
  - Right-click on empty canvas space and select **"See all"** to restore the complete mission graph view.
- **Vim Navigation Keybindings**:
  - `h`: Go to immediate parent node
  - `j`: Go to next sibling node (forward)
  - `k`: Go to previous sibling node (reverse)
  - `l`: Go to first child node
- **Smooth View Transitions**: Smoothstep camera interpolation when jumping between parent, child, or sibling nodes.
- **Live File Watcher**: Automatically re-renders the DAG preserving zoom level and selection when the underlying `.jsonl` journal changes.
- **Hierarchical Layouts**: Supports both multi-column horizontal and vertical layout orientations.

### Invocation
```bash
./build/stingtrail-gui [options] [path/to/journal.jsonl]
```

---

## 7. Architecture & File Layout

```
stingtrail/
├── Makefile                     # Multi-target Makefile (builds CLI, MCP & Read-Only GUI)
├── README.md                    # User & Agent documentation
├── .gitignore                   # Ignores build/ and *.jsonl
├── include/
│   ├── stingtrail.h             # Core DAG structs, buffer & safety guard APIs
│   ├── stingtrail_mcp.h         # Zero-dependency MCP server declarations
│   └── stingtrail_gui.h         # Read-only GUI declarations & vim keybindings
├── src/
│   ├── main.c                  # Stateless CLI argument dispatcher
│   ├── mcp_main.c              # Standalone MCP server binary entrypoint
│   ├── mcp.c                   # Zero-dependency MCP JSON-RPC protocol engine
│   ├── gui_main.c              # Read-only GUI binary entrypoint
│   ├── gui.c                   # Raven DAG visualizer & vim hjkl navigation
│   ├── state.c                 # In-memory DAG state manager & deduplicator
│   ├── store.c                 # Append-only JSONL journal serialization & fsync
│   ├── guard.c                 # Safety guard (destructive filter & scope check)
│   └── json.c                  # Zero-dependency JSON string parser & serializer
├── simulations/
│   ├── runner.py                # Multi-agent behavioral simulator & stress tester
│   ├── experiment_5_persistent_watcher.py # Shell hook vs daemon simulator
│   ├── experiment_6_context_traversal.py  # Empirical DAG traversal benchmark
│   └── experiment_7_terminal_colors.py    # Terminal styling scannability benchmark
└── tests/
    ├── test_stingtrail.c        # Core CLI & DAG unit test suite
    ├── test_mcp.c               # MCP protocol & tool dispatch test suite
    └── test_gui.c               # Read-only GUI & vim navigation test suite
```
