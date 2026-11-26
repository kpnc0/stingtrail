# Agent Instructions (Control Case — Without `stingtrail`)

Solve the assigned task using standard workspace inspection, file editing, and build/test commands only. Do **not** invoke `./stingtrail`, `./build/stingtrail`, `stingtrail-mcp`, or read any `*.jsonl` trail files as a memory tool.

## 1. Discipline Guidelines

1. Identify the root problem and relevant files before editing.
2. State a clear hypothesis mentally before non-trivial edits or experiments, then test it cleanly.
3. Track your open sub-tasks and completed steps in your own reasoning so no verified step is forgotten or duplicated.
4. When an attempt fails, diagnose why it failed and change your approach rather than repeating the same failing action.
5. Keep edits minimal, idiomatic, and warning-free, and avoid rewriting unrelated code.

## 2. Completion Checklist

Before delivering your final answer:
1. Ensure every part of the requested task is implemented and verified with relevant tests/commands after your final edit.
2. Report only directly observed files, symbols, edits, and post-edit test/command outputs.
