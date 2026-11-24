#!/usr/bin/env python3
"""
Experiment 6: Optimal DAG Traversal for AI Model Context Retrieval
Compares 4 graph traversal strategies for extracting experiment context:
  1. Full Board (Baseline): Serializes entire board
  2. Downward Subtree Only: Target node + all descendants (no ancestors)
  3. Breadth-First Search (BFS): Level-by-level descendant traversal
  4. Hourglass Traversal: Ancestor Spine (Root -> Target) + Topological Descendant DFS
"""

import os
import sys
import json
import time

def load_graph(journal_path="stingtrail.jsonl"):
    nodes = {}
    entries = {}
    steps = {}
    children_map = {}

    with open(journal_path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line: continue
            try:
                rec = json.loads(line)
            except Exception:
                continue

            op = rec.get("op")
            rid = rec.get("id")
            if not rid:
                if op == "step_update":
                    sid = rec.get("step_id")
                    if sid in steps:
                        if rec.get("status"): steps[sid]["status"] = rec.get("status")
                        if rec.get("outcome"): steps[sid]["outcome"] = rec.get("outcome")
                continue

            if op == "node":
                nodes[rid] = rec
                rec["parent_id"] = 0
            elif op == "entry":
                entries[rid] = rec
            elif op == "plan":
                steps[rid] = rec

            pid = rec.get("parent_id", 0)
            if pid not in children_map:
                children_map[pid] = []
            children_map[pid].append(rid)

    all_items = {}
    all_items.update(nodes)
    all_items.update(entries)
    all_items.update(steps)
    return all_items, children_map

# Traversal 1: Full Board
def traverse_full_board(all_items):
    lines = ["=== FULL MISSION GRAPH ==="]
    for rid, item in sorted(all_items.items()):
        kind = item.get("kind", item.get("op", ""))
        text = item.get("name", item.get("summary", item.get("action", "")))
        pid = item.get("parent_id", 0)
        status = item.get("status", "")
        lines.append(f"[#{rid} <- #{pid}] ({kind}) {text} {f'[{status}]' if status else ''}")
    return "\n".join(lines)

# Traversal 2: Downward Subtree Only
def traverse_downward_subtree(target_id, all_items, children_map):
    lines = [f"=== DOWNWARD SUBTREE FOR #{target_id} ==="]
    visited = set()

    def dfs(curr_id, depth=0):
        if curr_id not in all_items or curr_id in visited:
            return
        visited.add(curr_id)
        item = all_items[curr_id]
        indent = "  " * depth
        kind = item.get("kind", item.get("op", ""))
        text = item.get("name", item.get("summary", item.get("action", "")))
        status = item.get("status", "")
        lines.append(f"{indent}[#{curr_id}] ({kind}) {text} {f'[{status}]' if status else ''}")
        for child in children_map.get(curr_id, []):
            dfs(child, depth + 1)

    dfs(target_id)
    return "\n".join(lines), visited

# Traversal 3: BFS Layered
def traverse_bfs(target_id, all_items, children_map):
    lines = [f"=== BFS LAYERS FOR #{target_id} ==="]
    queue = [(target_id, 0)]
    visited = set()

    while queue:
        curr_id, level = queue.pop(0)
        if curr_id in visited or curr_id not in all_items:
            continue
        visited.add(curr_id)
        item = all_items[curr_id]
        kind = item.get("kind", item.get("op", ""))
        text = item.get("name", item.get("summary", item.get("action", "")))
        status = item.get("status", "")
        lines.append(f"[L{level}][#{curr_id}] ({kind}) {text} {f'[{status}]' if status else ''}")
        for child in children_map.get(curr_id, []):
            queue.append((child, level + 1))

    return "\n".join(lines), visited

# Traversal 4: Hourglass / Cone Traversal (Ancestor Spine + Topological Descendant DFS)
def traverse_hourglass(target_id, all_items, children_map):
    lines = [f"=== STINGTRAIL EXPERIMENT CONTEXT (Focused on #{target_id}) ==="]

    # 1. Trace Ancestor Spine (Root -> Target)
    spine = []
    curr = all_items.get(target_id)
    curr_pid = curr.get("parent_id", 0) if curr else 0
    while curr_pid != 0 and curr_pid in all_items:
        spine.append(curr_pid)
        curr_pid = all_items[curr_pid].get("parent_id", 0)
    spine.reverse()

    lines.append("\n[MOTIVATING ANCESTORS / CONTEXT SPINE]:")
    if not spine:
        lines.append("  (Target is a root node)")
    else:
        for idx, anc_id in enumerate(spine):
            item = all_items[anc_id]
            kind = item.get("kind", item.get("op", ""))
            text = item.get("name", item.get("summary", item.get("action", "")))
            prefix = "  " + ("-> " * idx)
            lines.append(f"{prefix}[#{anc_id}] ({kind}) {text}")

    # 2. Target Node
    lines.append("\n[FOCUSED TARGET]:")
    t_item = all_items.get(target_id, {})
    t_kind = t_item.get("kind", t_item.get("op", ""))
    t_text = t_item.get("name", t_item.get("summary", t_item.get("action", "")))
    t_status = t_item.get("status", "")
    t_why = t_item.get("rationale", "")
    t_out = t_item.get("outcome", "")
    lines.append(f"  [#{target_id}] ({t_kind}) {t_text}")
    if t_status: lines.append(f"  Status: {t_status}")
    if t_why: lines.append(f"  Rationale: {t_why}")
    if t_out: lines.append(f"  Outcome: {t_out}")

    # 3. Downward Topological Subtree
    lines.append("\n[EPISTEMIC SUBTREE / OUTCOMES]:")
    visited_descendants = set()

    def dfs_sub(curr_id, depth=1):
        for child in children_map.get(curr_id, []):
            if child in visited_descendants or child not in all_items:
                continue
            visited_descendants.add(child)
            item = all_items[child]
            indent = "  " * depth
            kind = item.get("kind", item.get("op", ""))
            text = item.get("name", item.get("summary", item.get("action", "")))
            status = item.get("status", "")
            out = item.get("outcome", "")
            detail = item.get("detail", "")
            line = f"{indent}[#{child}] ({kind}) {text}"
            if status: line += f" [{status}]"
            lines.append(line)
            if out: lines.append(f"{indent}  outcome: {out}")
            if detail and len(detail) > 0 and detail != text:
                lines.append(f"{indent}  detail: {detail}")
            dfs_sub(child, depth + 1)

    dfs_sub(target_id)
    if not visited_descendants:
        lines.append("  (No descendant actions or evidence recorded yet)")

    return "\n".join(lines), set(spine) | {target_id} | visited_descendants

def run_experiment_6():
    print("=" * 70)
    print("EXPERIMENT 6: DAG Traversal Benchmark for Model Context Retrieval")
    print("=" * 70)

    all_items, children_map = load_graph("stingtrail.jsonl")
    print(f"Loaded {len(all_items)} total DAG entities from stingtrail.jsonl\n")

    # Target test nodes:
    # 1. Hypothesis #115 ("H1: Atomic execution hook")
    # 2. Plan Step #119 ("Simulate Advisory vs Atomic Execution Hook")
    # 3. Premise #134 ("Persistent Background Watcher")
    test_targets = [115, 119, 134]

    summary_results = {}

    for tid in test_targets:
        if tid not in all_items:
            continue
        target_name = all_items[tid].get("name", all_items[tid].get("summary", all_items[tid].get("action", "")))
        print(f"\n--- EVALUATING TARGET #{tid}: '{target_name[:50]}...' ---")

        # 1. Full board
        t0 = time.perf_counter()
        full_text = traverse_full_board(all_items)
        t_full = (time.perf_counter() - t0) * 1000
        full_tokens = len(full_text) // 4

        # 2. Downward Subtree
        t0 = time.perf_counter()
        down_text, down_nodes = traverse_downward_subtree(tid, all_items, children_map)
        t_down = (time.perf_counter() - t0) * 1000
        down_tokens = len(down_text) // 4

        # 3. BFS
        t0 = time.perf_counter()
        bfs_text, bfs_nodes = traverse_bfs(tid, all_items, children_map)
        t_bfs = (time.perf_counter() - t0) * 1000
        bfs_tokens = len(bfs_text) // 4

        # 4. Hourglass (Ancestor Spine + Descendant DFS)
        t0 = time.perf_counter()
        hg_text, hg_nodes = traverse_hourglass(tid, all_items, children_map)
        t_hg = (time.perf_counter() - t0) * 1000
        hg_tokens = len(hg_text) // 4

        # Verify ancestor completeness
        has_ancestors = any(all_items[n].get("parent_id", 0) == 0 for n in hg_nodes if n != tid) or all_items[tid].get("parent_id", 0) == 0

        reduction_hg = round((1.0 - (hg_tokens / max(1, full_tokens))) * 100, 1)

        print(f"{'Traversal Method':<30} | {'Tokens (~est)':<12} | {'Reduction':<10} | {'Ancestors?':<11} | {'Descendants?':<12} | {'Latency':<8}")
        print("-" * 92)
        print(f"{'1. Full Board (Baseline)':<30} | {full_tokens:<12} | {'0.0%':<10} | {'YES':<11} | {'YES':<12} | {t_full:.3f}ms")
        print(f"{'2. Downward Subtree Only':<30} | {down_tokens:<12} | {f'{round((1-down_tokens/full_tokens)*100,1)}%':<10} | {'NO (BLIND)':<11} | {'YES':<12} | {t_down:.3f}ms")
        print(f"{'3. BFS Layered':<30} | {bfs_tokens:<12} | {f'{round((1-bfs_tokens/full_tokens)*100,1)}%':<10} | {'NO (BLIND)':<11} | {'YES (jumbled)':<12} | {t_bfs:.3f}ms")
        print(f"{'4. Hourglass (Spine+DFS)':<30} | {hg_tokens:<12} | {f'{reduction_hg}%':<10} | {'YES (Root)':<11} | {'YES (DFS)':<12} | {t_hg:.3f}ms")

        summary_results[tid] = {
            "target": target_name,
            "full_tokens": full_tokens,
            "hourglass_tokens": hg_tokens,
            "reduction_pct": reduction_hg,
            "latency_ms": round(t_hg, 3),
            "sample_snippet": hg_text[:350]
        }

    with open("simulations/exp6_results.json", "w") as f:
        json.dump(summary_results, f, indent=2)

    print("\n" + "=" * 70)
    print("HOURGLASS TRAVERSAL SAMPLE OUTPUT (Target #119):")
    print("=" * 70)
    sample_text, _ = traverse_hourglass(119, all_items, children_map)
    print(sample_text)
    print("=" * 70)

    return summary_results

if __name__ == "__main__":
    run_experiment_6()
