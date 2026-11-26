# Agent Instructions (With `stingtrail` CLI)

You have the `stingtrail` binary right alongside this instruction file (invoke `./stingtrail --db mission.jsonl ...` or `./build/stingtrail --db mission.jsonl ...` from your workspace). You MUST use it as your external critical-thinking memory throughout the task.

## 1. Core Epistemic Loop (Mandatory)

Follow this strict loop whenever you investigate, debug, build, or refactor:

1. **Establish a Root Context Helper** (`premise`, `asset`, or `context`):
   ```bash
   ./stingtrail --db mission.jsonl node premise "<Task title>" "<Initial scope or constraints>"
   ```
2. **Record Passive Context Helpers** (`observation`, `evidence`, `refutation`, `finding`):
   ```bash
   ./stingtrail --db mission.jsonl log observation "<Concise fact observed>" --parent 1 --detail "<Exact file, symbol, output>"
   ```
3. **Formulate a Hypothesis (Thought)** before attempting non-trivial edits or tests:
   ```bash
   ./stingtrail --db mission.jsonl log hypothesis "<Specific falsifiable claim>" --parent <premise_or_obs_id>
   ```
4. **Propose Planned Steps** strictly under an existing `hypothesis` (`--parent <hyp_id>` is mandatory; `--why` is optional):
   ```bash
   ./stingtrail --db mission.jsonl plan "<Concrete action>" --parent <hyp_id> --prio 1
   ```
5. **Execute & Close Each Step** (`in_progress` -> `done` or `blocked` with a concrete outcome):
   ```bash
   ./stingtrail --db mission.jsonl step <step_id> in_progress
   # On win:
   ./stingtrail --db mission.jsonl step <step_id> done "<Observed pass output / change result>"
   # On fail / dead end:
   ./stingtrail --db mission.jsonl step <step_id> blocked "<Exact failure reason>"
   ./stingtrail --db mission.jsonl log refutation "<Why hypothesis failed>" --parent <step_id>
   ```
6. **Iterate or Compact (`rebase`)**:
   - After a failure (`blocked` + `refutation`), form a **new** `hypothesis` instead of repeating the same broken approach.
   - Check board state or slice a single branch before making assumptions:
     ```bash
     ./stingtrail --db mission.jsonl status
     ./stingtrail --db mission.jsonl focus <id>
     ```
   - When a root chain is completely finished (every step is `done` or `blocked`), compact it to keep working memory small:
     ```bash
     ./stingtrail --db mission.jsonl rebase --completed
     ```

## 2. Exact Field Limits & CLI Diagnostics

`stingtrail` enforces bounded buffers and prints colorized `error:`, `warning:`, and `hint:` messages to `stderr`:

- **Field caps** (keep entries crisp; CLI warns if truncated):
  - `node`: `name` <= 127 chars, `detail` <= 255 chars; kinds: `premise`, `asset`, `context` (deduplicated by `kind` + `name`).
  - `log`: `summary` <= 159 chars, `detail` <= 511 chars; kinds: `observation`, `hypothesis`, `evidence`, `refutation`, `finding`; severities: `info`, `low`, `medium`, `high`, `critical`.
  - `plan`: `action` <= 255 chars, `rationale` <= 383 chars, `target` <= 127 chars, `--prio` `1..5`.
  - `step`: `outcome` <= 383 chars (mandatory for `done` and `blocked`).
- **Act on CLI Warnings & Hints Immediately**:
  - `warning: ... capacity at >=80%` or `Hint: N completed chain(s) ready to rebase`: run `./stingtrail --db mission.jsonl rebase --completed` (archives finished chains to `.stingtrail/yyyymmddhhmmss-stingtrail_<root_id>_<slug>.md` and replaces each with a concise `[Rebased]` super-premise).
  - `warning: JSON export is ... bytes`: prefer `focus <id> [--json]` over full-board `status --json`, or rebase completed chains.
  - `context get` / `context set <hyp_id>`: if you use `context` or shell hooks (`hook-eval`), bind context to a **hypothesis** ID, not a `premise`.

## 3. Completion Checklist

Before delivering your final answer:
1. Verify all active planned steps in your chain are closed (`done` or `blocked`) with accurate outcomes.
2. Run `./stingtrail --db mission.jsonl status` (confirm `0` unintended incomplete plans remain and check `Context & Hints:`).
3. Report only directly observed files, symbols, edits, and post-edit test/command outputs.
