#!/usr/bin/env python3
"""
Experiment 4: Buffer Saturation & Multi-Agent Concurrency Limits
Tests:
  Part A: Buffer limit saturation (STINGTRAIL_MAX_NODES 256, STINGTRAIL_MAX_ENTRIES 512, STINGTRAIL_MAX_STEPS 256).
          Determines whether the CLI safely reports errors or silently drops items.
  Part B: Multi-agent swarm concurrency:
          Spawns 5 concurrent Python processes simultaneously executing nodes, entries, and steps
          against the same journal. Measures race conditions, ID collisions, and journal line integrity.
"""

import os
import sys
import json
import time
import subprocess
from concurrent.futures import ProcessPoolExecutor

def run_cmd(cmd):
    res = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    return res.returncode, res.stdout.strip(), res.stderr.strip()

def run_part_a():
    print("--- PART A: Buffer Saturation Stress Test ---")
    db_buf = "simulations/exp4_buffer.jsonl"
    if os.path.exists(db_buf): os.remove(db_buf)

    run_cmd(f'./build/stingtrail --db {db_buf} node premise "Root Premise Buffer Test"')

    # Test nodes (limit is 256)
    nodes_created = 1
    node_err_count = 0
    for i in range(2, 270):
        ret, out, err = run_cmd(f'./build/stingtrail --db {db_buf} node asset "Asset_{i}"')
        if ret == 0:
            nodes_created += 1
        else:
            node_err_count += 1

    print(f"Nodes: Created = {nodes_created}, Errors = {node_err_count} (Limit = 256)")

    # Test steps (limit is 256)
    steps_created = 0
    step_err_count = 0
    for i in range(1, 270):
        ret, out, err = run_cmd(f'./build/stingtrail --db {db_buf} plan "Action {i}" --why "Rationale {i}" --parent 1')
        if ret == 0:
            steps_created += 1
        else:
            step_err_count += 1

    print(f"Steps: Created = {steps_created}, Errors = {step_err_count} (Limit = 256)")

    # Verify journal file
    with open(db_buf, 'r') as f:
        lines = [l.strip() for l in f if l.strip()]

    print(f"Total journal lines written: {len(lines)}")
    # Reload and test export
    ret, exp, _ = run_cmd(f'./build/stingtrail --db {db_buf} export')
    dag = json.loads(exp) if exp.startswith('{') else {}
    print(f"Exported DAG: {len(dag.get('nodes', []))} nodes, {len(dag.get('steps', []))} steps")

    return {
        "nodes_created": nodes_created,
        "node_err_count": node_err_count,
        "steps_created": steps_created,
        "step_err_count": step_err_count,
        "journal_lines": len(lines)
    }

def worker_agent(agent_id, db_path, ops_count=20):
    ops_done = 0
    errors = 0
    for i in range(ops_count):
        # Perform mixed sequence: log, plan, step update
        ret, out, err = run_cmd(f'./build/stingtrail --db {db_path} log observation "Agent {agent_id} obs {i}" --parent 1')
        if ret != 0: errors += 1
        else: ops_done += 1

        ret, out, err = run_cmd(f'./build/stingtrail --db {db_path} plan "Agent {agent_id} step {i}" --why "Testing concurrency" --parent 1')
        if ret != 0: errors += 1
        else: ops_done += 1

    return agent_id, ops_done, errors

def run_part_b():
    print("\n--- PART B: Multi-Agent Concurrency & Race Condition Test ---")
    db_conc = "simulations/exp4_concurrency.jsonl"
    if os.path.exists(db_conc): os.remove(db_conc)

    # Root premise
    run_cmd(f'./build/stingtrail --db {db_conc} node premise "Multi-Agent Concurrency Root"')

    num_agents = 5
    ops_per_agent = 15 # Each does 15 logs + 15 plans = 30 ops total per agent -> 150 ops total
    print(f"Spawning {num_agents} concurrent agents performing {ops_per_agent*2} operations each...")

    start_time = time.time()
    with ProcessPoolExecutor(max_workers=num_agents) as executor:
        futures = [executor.submit(worker_agent, a, db_conc, ops_per_agent) for a in range(1, num_agents + 1)]
        results = [f.result() for f in futures]
    elapsed = time.time() - start_time

    print(f"Completed in {elapsed:.2f}s")

    # Analyze Journal
    corrupt_lines = 0
    parsed_records = []
    ids_seen = {}

    with open(db_conc, 'r') as f:
        for idx, line in enumerate(f):
            line = line.strip()
            if not line: continue
            try:
                rec = json.loads(line)
                parsed_records.append(rec)
                rec_id = rec.get("id")
                if rec_id:
                    ids_seen[rec_id] = ids_seen.get(rec_id, 0) + 1
            except Exception as e:
                corrupt_lines += 1

    # Check ID collisions
    collisions = {k: v for k, v in ids_seen.items() if v > 1}

    print("\n--- CONCURRENCY ANALYSIS ---")
    print(f"Total records in journal: {len(parsed_records)}")
    print(f"Corrupted / Torn JSON lines: {corrupt_lines} (POSIX atomic append verification)")
    print(f"Unique IDs generated: {len(ids_seen)}")
    print(f"Duplicate ID Collisions: {len(collisions)} collided IDs across {sum(collisions.values())} records!")

    if collisions:
        print("SAMPLE COLLIDED IDs:")
        for k in sorted(list(collisions.keys()))[:10]:
            print(f"  ID #{k} generated {collisions[k]} times by different processes!")

    return {
        "corrupt_lines": corrupt_lines,
        "total_records": len(parsed_records),
        "collisions_count": len(collisions),
        "collisions_sample": {k: collisions[k] for k in sorted(list(collisions.keys()))[:5]}
    }

def run_experiment_4():
    print("=" * 60)
    print("EXPERIMENT 4: Buffer Saturation & Swarm Concurrency Analysis")
    print("=" * 60)
    res_a = run_part_a()
    res_b = run_part_b()
    return {"buffer": res_a, "concurrency": res_b}

if __name__ == "__main__":
    run_experiment_4()
