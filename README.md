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

#### 2. Log Epistemic Reasoning & Context Helpers (`log`)
Records thoughts (`hypothesis`) and passive context helpers (`observation`, `evidence`, `refutation`, `finding`) under a parent node.
```bash
./build/stingtrail log <kind> "<summary>" [--parent <id>] [--sev <severity>] [--detail "<detail>"]
```
- `<kind>`: `hypothesis`, `observation`, `evidence`, `refutation`, `finding`.
- **Iterative Loop**: `thought (hypothesis) -> plan (step) -> fail/win (done/blocked + outcome) -> next thought (hypothesis)`, with `premise`/`asset`/`context` and `observation`/`evidence`/`refutation`/`finding` acting as passive context helpers.

#### 3. Plan Operational Steps (`plan`)
Enforces strict hypothesis-driven planning: every plan must be linked to an existing `hypothesis` via `--parent <hypothesis_id>`.
```bash
./build/stingtrail plan "<action>" --parent <hypothesis_id> [--why "<rationale>"] [--prio 1-5] [--target <host>]
```
- `--parent <hypothesis_id>` is **mandatory** and must point to an existing `hypothesis` entry.
- `--why <rationale>` is **optional**.
- Priority ranges from `1` (critical) to `5` (low). Default is `3`.
- Returns: `step #<id> planned`.

#### 4. Update Step Lifecycle (`step`)
Updates step execution state with concrete results (`done` = win, `blocked` = fail).
```bash
./build/stingtrail step <id> <status> "<outcome>"
```
- `<status>`: `pending`, `in_progress`, `done`, `blocked`.
- A non-empty `<outcome>` is **mandatory** when marking a step `done` or `blocked`.
- Returns: `step #<id> updated to <status>`.

#### 5. Mission Status Board (`status`)
Prints an ASCII status board showing incomplete plan counts per root node (`[incomplete plans: X/Y]`), per hypothesis, and globally (`Planned Next Steps (total, incomplete)`), grouping planned, active, completed, and blocked steps by priority (`[P1]` through `[P5]`).
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

#### 10. Rebase & Chain Super-Summary (`rebase`)
Compacts completed chains (where every plan is terminal: `done` or `blocked`) into lossless Markdown summaries under `.stingtrail/*.md` and replaces each compacted chain with a single `[Rebased]` super-premise `node`, keeping active `.jsonl` journals well within static memory limits.
```bash
# Preview compaction without modifying files
./build/stingtrail rebase <id> --dry-run

# Rebase a single completed chain (with optional custom super-premise title)
./build/stingtrail rebase <id> [--title "<name>"]

# Rebase all completed chains while leaving active/pending chains intact
./build/stingtrail rebase --completed
```
- **Readiness Gate**: Rejects rebasing any chain that still has `pending` or `in_progress` steps.
- **Markdown Archives**: Streams `Facts & Outcomes (Wins)`, `Dead-Ends & Refutations (Fails)`, and `Hypotheses & Context Helpers` into `.stingtrail/yyyymmddhhmmss-stingtrail_<root_id>_<snake_case_20>.md` and links it from the new super-premise node's detail.

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

# 2. Record an observation (passive context helper)
./build/stingtrail log observation "Target responds at 10.10.10.25; Port 80 and 443 open" --parent 1

# 3. Formulate a hypothesis (thought)
./build/stingtrail log hypothesis "Web Service exposes administrative endpoints" --parent 1

# 4. Plan step under hypothesis (--why is optional)
./build/stingtrail plan "Fuzz directories using wordlist" --parent 3 --prio 1

# 5. Begin execution
./build/stingtrail step 4 in_progress "Running ffuf against http://10.10.10.25/..."

# 6. Complete step with concrete win/fail outcome (done or blocked)
./build/stingtrail step 4 done "Discovered /admin_dev endpoint returning HTTP 200"

# 7. Check mission board (shows incomplete plan counts per node and globally)
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
3. **`stingtrail_plan`**: Propose an operational step linked to an existing `hypothesis` (`parent_id`), with optional `why` rationale and priority `1` (critical) to `5` (low). Fail-closed guards reject unplanned hypothesis-less or destructive actions.
4. **`stingtrail_step`**: Update step lifecycle state (`pending`, `in_progress`, `done`, `blocked`) and record concrete outcomes (outcome required for `done` / `blocked`).
5. **`stingtrail_status`**: Retrieve the mission board (ASCII text or JSON structure).
6. **`stingtrail_export`**: Export the complete DAG state journal as JSON.
7. **`stingtrail_guard`**: Validate arbitrary action commands and target hosts against destructive patterns and authorization scope.
8. **`stingtrail_rebase`**: Compact completed chains (`done` or `blocked` only) into `.stingtrail/*.md` summaries and super-premise nodes.

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
- **Vim Navigation & ID Jump Keybindings**:
  - `h`: Go to immediate parent node
  - `j`: Go to next sibling node (forward)
  - `k`: Go to previous sibling node (reverse)
  - `l`: Go to first child node
  - `f`: Enter node-ID jump mode (type a numeric node ID and press `Enter` to jump to it, or `Esc` to cancel; shows subtle yellow feedback at the bottom-left while active)
- **Smooth View Transitions**: Smoothstep camera interpolation when jumping between parent, child, or sibling nodes.
- **Live File Watcher**: Automatically re-renders the DAG preserving zoom level and selection when the underlying `.jsonl` journal changes.
- **Hierarchical Layouts**: Supports both multi-column horizontal and vertical layout orientations.
- **Spatial Render Distance Culling**: Computes full DAG layout once and dynamically materializes/unmaterializes only nodes and links within render distance of the camera (`1800` canvas units by default, with 15% hysteresis), keeping large 200+ node graphs smooth while preserving lineage highlighting and `hjkl` navigation. When a node inside render distance is selected/focused, its immediate parent and immediate child nodes remain rendered so incoming and outgoing paths stay visible (without pulling in grandparents or grandchildren).
- **Semantic Glow Coding**: Root conceptual nodes (`premise`, `asset`, `context`) glow teal-blue (`#016064`), completed steps glow green (`#29AB87`), active/pending steps glow orange (`#FF9500`), blocked/refuted nodes glow red (`#FF3B30`), and neutral reasoning entries glow pink (`#FC46AA`).

### Invocation
```bash
./build/stingtrail-gui [options] [path/to/journal.jsonl]
```
- `--db <file.jsonl>`: Path to the JSONL journal database (default: `stingtrail.jsonl`).
- `--render-distance <n>`: Render distance radius in canvas units from the camera center (default: `1800`; pass `0` to disable culling and render all nodes).

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
│   ├── rebase.c                # Chain super-summary (.stingtrail/*.md) & atomic compaction
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
