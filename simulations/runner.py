#!/usr/bin/env python3
"""
Main Experiment Runner: Runs all 4 experiments and produces consolidated conclusions.
"""

import json
import time
from experiment_1_execution_hook import run_experiment_1
from experiment_2_refutation_cascades import run_experiment_2
from experiment_3_guard_fuzzing import run_experiment_3
from experiment_4_concurrency_buffers import run_experiment_4

def main():
    print("#" * 70)
    print("STINGTRAIL COMPREHENSIVE AGENT BEHAVIOR & ARCHITECTURE SIMULATION")
    print("#" * 70)

    start = time.time()
    res1 = run_experiment_1()
    print("\n")
    res2 = run_experiment_2()
    print("\n")
    res3 = run_experiment_3()
    print("\n")
    res4 = run_experiment_4()
    total_time = time.time() - start

    summary = {
        "timestamp": time.time(),
        "total_runtime_seconds": round(total_time, 2),
        "experiment_1_execution_hook": res1,
        "experiment_2_refutation_cascades": res2,
        "experiment_3_guard_fuzzing_count": len(res3),
        "experiment_3_evasions": sum(1 for r in res3 if r["is_evasion"]),
        "experiment_3_false_blocks": sum(1 for r in res3 if r["is_false_block"]),
        "experiment_4_concurrency_buffers": res4
    }

    with open("simulations/results.json", "w") as f:
        json.dump(summary, f, indent=2)

    print("\n" + "#" * 70)
    print(f"ALL EXPERIMENTS COMPLETED IN {total_time:.2f}s! Results saved to simulations/results.json")
    print("#" * 70)

if __name__ == "__main__":
    main()
