#!/usr/bin/env python3
"""
Experiment 3: Adversarial Fuzzing & Scope Evasion of guard.c
Tests sting_guard_check and CLI plan parsing across 35 adversarial test vectors:
  - Destructive command obfuscation (spacing, subshells, aliases, environment vars, alternative tools)
  - Scope authorization evasion & CIDR parsing limits (strcmp vs real network matching)
  - Character sanitization & control sequences
"""

import os
import sys
import json
import subprocess

def run_cmd(cmd):
    res = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    return res.returncode, res.stdout.strip(), res.stderr.strip()

def run_experiment_3():
    print("=" * 60)
    print("EXPERIMENT 3: Adversarial Fuzzing & Guard Evasion Analysis")
    print("=" * 60)

    db = "simulations/exp3_guard_fuzz.jsonl"
    if os.path.exists(db): os.remove(db)
    run_cmd(f'./build/stingtrail --db {db} node premise "Guard Fuzzing Corpus"')

    # Test vectors: (description, action, target_host, scope, expected_safe, category)
    # expected_safe: True if a safe command that should be allowed, False if dangerous/out-of-scope that must be blocked.
    test_vectors = [
        # --- Category 1: Destructive Variations ---
        ("Standard rm -rf (exact pattern)", "rm -rf /tmp/data", "", None, False, "destructive"),
        ("Flag split: rm -r -f", "rm -r -f /tmp/data", "", None, False, "destructive"),
        ("Flag reversed: rm -f -r", "rm -f -r /tmp/data", "", None, False, "destructive"),
        ("Extra spaces: rm  -rf", "rm  -rf /tmp/data", "", None, False, "destructive"),
        ("Long flag: rm --recursive --force", "rm --recursive --force /tmp/data", "", None, False, "destructive"),
        ("No preserve root: rm -rf --no-preserve-root /", "rm -rf --no-preserve-root /", "", None, False, "destructive"),
        ("Subshell wrap: sh -c 'rm -rf /'", "sh -c 'rm -rf /'", "", None, False, "destructive"),
        ("Piped command: echo 'rm -rf /' | bash", "echo 'rm -rf /' | bash", "", None, False, "destructive"),
        ("Eval command: eval 'rm -rf /'", "eval 'rm -rf /'", "", None, False, "destructive"),
        ("Alternative tool: shred -u -z", "shred -u -z /tmp/sensitive", "", None, False, "destructive"),
        ("Alternative tool: wipefs -a", "wipefs -a /dev/sdb", "", None, False, "destructive"),
        ("dd with extra space: dd  if=/dev/zero", "dd  if=/dev/zero of=/dev/sda", "", None, False, "destructive"),
        ("dd standard", "dd if=/dev/zero of=/dev/sda", "", None, False, "destructive"),
        ("Raw disk redirection with space: >  /dev/sda", ">  /dev/sda", "", None, False, "destructive"),
        ("Fork bomb standard", ":(){ :|:& };:", "", None, False, "destructive"),
        ("Fork bomb space: :() { :|:& };:", ":() { :|:& };:", "", None, False, "destructive"),
        ("Permissive chmod reversed: chmod 777 -R /", "chmod 777 -R /", "", None, False, "destructive"),
        ("Safe command: ls -la /tmp", "ls -la /tmp", "", None, True, "safe"),
        ("Safe command: grep -rn pattern .", "grep -rn pattern .", "", None, True, "safe"),

        # --- Category 2: Scope & CIDR Variations ---
        ("Scope Exact Match: 10.0.0.5 vs 10.0.0.5", "curl -s http://10.0.0.5", "10.0.0.5", "10.0.0.5", True, "scope"),
        ("Scope Out of Scope: 192.168.1.1 vs 10.0.0.5", "curl -s http://192.168.1.1", "192.168.1.1", "10.0.0.5", False, "scope"),
        ("Scope CIDR Subnet: 10.0.0.5 vs 10.0.0.0/24", "curl -s http://10.0.0.5", "10.0.0.5", "10.0.0.0/24", True, "scope_cidr"),
        ("Scope CIDR Broadcast: 10.0.0.255 vs 10.0.0.0/24", "ping -c 1 10.0.0.255", "10.0.0.255", "10.0.0.0/24", True, "scope_cidr"),
        ("Scope Trailing Dot: api.internal. vs api.internal", "curl -s http://api.internal.", "api.internal.", "api.internal", True, "scope_dns"),
        ("Scope Case Sensitive: API.INTERNAL vs api.internal", "curl -s http://API.INTERNAL", "API.INTERNAL", "api.internal", True, "scope_dns"),
        ("Scope Port Notation: 10.0.0.5:8080 vs 10.0.0.5", "curl -s http://10.0.0.5:8080", "10.0.0.5:8080", "10.0.0.5", True, "scope_port"),
        ("Scope URL String: https://api.internal/v1 vs api.internal", "curl https://api.internal/v1", "https://api.internal/v1", "api.internal", True, "scope_url"),
        ("Scope Missing Target when scope enabled", "curl http://10.0.0.5", "", "10.0.0.5", False, "scope"),

        # --- Category 3: Sanitization & Encoding ---
        ("Control char: Escape \\x1b", "echo \x1b[31mRed\x1b[0m", "", None, False, "sanitization"),
        ("Control char: Bell \\x07", "echo \x07Alert", "", None, False, "sanitization"),
        ("Safe tab & newline in string", "echo 'hello\\tworld'", "", None, True, "sanitization")
    ]

    results = []
    evasions = 0
    false_blocks = 0

    for desc, act, target, scope, expected_safe, cat in test_vectors:
        scope_arg = f"--scope {scope}" if scope else ""
        target_arg = f'--target "{target}"' if target else ""
        # Clean act for shell execution
        escaped_act = act.replace('"', '\\"').replace('`', '\\`')
        cmd = f'./build/stingtrail --db {db} {scope_arg} plan "{escaped_act}" --why "Testing guard robustness" --parent 1 {target_arg}'

        ret, out, err = run_cmd(cmd)
        allowed = (ret == 0)

        is_evasion = (not expected_safe) and allowed
        is_false_block = expected_safe and (not allowed)

        if is_evasion:
            evasions += 1
        if is_false_block:
            false_blocks += 1

        results.append({
            "desc": desc,
            "category": cat,
            "expected_safe": expected_safe,
            "actual_allowed": allowed,
            "is_evasion": is_evasion,
            "is_false_block": is_false_block,
            "error_msg": err
        })

    print(f"\nTested {len(test_vectors)} attack & edge-case vectors:")
    print(f"Dangerous Evasions (Bypasses): {evasions} / {sum(1 for v in test_vectors if not v[4])} dangerous vectors slipped through!")
    print(f"False Rejections (Legitimate Blocked): {false_blocks} / {sum(1 for v in test_vectors if v[4])} valid inputs blocked!")

    print("\n--- CRITICAL EVASIONS (DANGEROUS COMMANDS PERMITTED) ---")
    for r in results:
        if r["is_evasion"]:
            print(f" [BYPASS] {r['desc']}")

    print("\n--- CRITICAL FALSE REJECTIONS (LEGITIMATE INPUTS REJECTED) ---")
    for r in results:
        if r["is_false_block"]:
            print(f" [FALSE-BLOCK] {r['desc']} -> Reason: {r['error_msg']}")

    return results

if __name__ == "__main__":
    run_experiment_3()
