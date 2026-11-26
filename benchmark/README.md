# Stingtrail Agent Benchmark Kit (With Tool vs Without Tool)

This directory contains the paired instruction set and standalone `stingtrail` CLI binary for A/B benchmarking autonomous coding/debugging agents **with Stingtrail** vs **without Stingtrail**.

## Contents

- `stingtrail`: Standalone compiled C11 CLI binary (libc-only, zero external dependencies).
- [WITH_TOOL_INSTRUCTIONS.md](WITH_TOOL_INSTRUCTIONS.md): System/task instructions for the treatment arm using `./stingtrail`.
- [WITHOUT_TOOL_INSTRUCTIONS.md](WITHOUT_TOOL_INSTRUCTIONS.md): Matched control instructions for the baseline arm without `stingtrail`.

## Quick Setup for Running Both Cases

Copy or mount the workspace twice (one clean copy per arm) and keep `stingtrail` + the corresponding instruction file available to the agent:

```bash
# Rebuild fresh CLI binary and sync benchmark/stingtrail
make all && make benchmark

# Verify standalone binary alongside instructions
./benchmark/stingtrail --version
./benchmark/stingtrail --help
```

### Arm A — With Tool (Treatment)
1. Provide [WITH_TOOL_INSTRUCTIONS.md](WITH_TOOL_INSTRUCTIONS.md) as the agent's instruction file (e.g., append to system prompt or copy to the task workspace).
2. Ensure the `stingtrail` binary is present at `./stingtrail` (or `./build/stingtrail`) inside that workspace.
3. Give the agent the benchmark task prompt and use a dedicated journal (e.g. `--db mission.jsonl`).

### Arm B — Without Tool (Control)
1. Provide [WITHOUT_TOOL_INSTRUCTIONS.md](WITHOUT_TOOL_INSTRUCTIONS.md) as the agent's instruction file.
2. Remove or withhold the `stingtrail` binary and any `.jsonl` trail files from the workspace.
3. Give the agent the **exact same** benchmark task prompt.

## Recommended Benchmark Tasks & Metrics

Good tasks for differentiating structured epistemic tracking from ad-hoc context include:
- **Multi-root-cause bug hunts** with misleading symptoms (tests `refutation` logging and avoids repeat loops).
- **Long-horizon refactors / multi-session handoffs** (tests `focus <id>`, incomplete-plan tracking, and `rebase --completed` across context limits).
- **Tight capacity / noisy output stress** (tests reaction to `warning:` truncation, `>=80%` capacity, and JSON size hints).

Record per run:
1. **Task pass rate** (final tests/assertions passing without manual fixes).
2. **Turns & token usage** (total prompt + completion tokens and tool calls).
3. **Wasted / repeated failed steps** (same hypothesis or failing edit retried without diagnosis).
4. **Traceability** (in Arm A, inspect `mission.jsonl` and `.stingtrail/*.md` archives).
