#!/usr/bin/env python3
"""
Experiment 7: CLI Terminal Color Scannability & Visual Ergonomics Benchmark
Evaluates 3 terminal styling models for Stingtrail CLI across realistic dense mission boards:
  Model A: Pure Monochrome (Current CLI)
  Model B: Full-Line Rainbow (Aggressive Over-Coloring)
  Model C: Strategic Sparse Anchors (Proposed 3-Tier Hierarchy: Bold Cyan IDs, Semantic Accents, Dim Metadata)
"""

import re
import sys
import json
import time

ANSI_ESCAPE_RE = re.compile(r'\x1b\[[0-9;]*m')

def strip_ansi(text: str) -> str:
    return ANSI_ESCAPE_RE.sub('', text)

# Sample dense mission board entries (15 representative entries from stingtrail.jsonl)
SAMPLE_BOARD = {
    "nodes": [
        {"id": 1, "kind": "premise", "name": "Target Infrastructure Penetration Test", "detail": "Scope: 10.0.0.0/24, api.corp.internal", "created": "20261124200000"},
        {"id": 100, "kind": "premise", "name": "Authentication Subsystem Zero-Trust Review", "detail": "Evaluating OAuth2 and JWT implementations", "created": "20261124210000"}
    ],
    "entries": [
        {"id": 2, "parent_id": 1, "kind": "observation", "severity": "info", "summary": "Port 443 open with TLS 1.3 only; port 80 redirects", "detail": "Nmap scan output confirms modern ciphers", "created": "20261124200500"},
        {"id": 3, "parent_id": 1, "kind": "hypothesis", "severity": "high", "summary": "API endpoint /v1/auth/exchange vulnerable to token replay", "detail": "Nonce verification appears stateless", "created": "20261124201000"},
        {"id": 4, "parent_id": 3, "kind": "evidence", "severity": "critical", "summary": "Replay token successfully accepted with HTTP 200 and valid session cookie", "detail": "Captured via Burp proxy; replay window > 15 minutes", "created": "20261124201500"},
        {"id": 5, "parent_id": 3, "kind": "refutation", "severity": "medium", "summary": "Sub-hypothesis H3b (JWT key confusion) refuted: HS256 rejected when RS256 specified", "detail": "Algorithm field strictly verified in Jose library", "created": "20261124202000"},
        {"id": 6, "parent_id": 100, "kind": "observation", "severity": "low", "summary": "Legacy session endpoint /auth/legacy returns 404", "detail": "Decommissioned in release 4.2", "created": "20261124210500"}
    ],
    "steps": [
        {"id": 10, "parent_id": 3, "priority": 1, "status": "done", "action": "Fuzz token expiration timestamps with expired claims", "target": "api.corp.internal", "rationale": "Verify clock skew window in backend validator", "outcome": "Tokens older than 60s rejected with 401 Unauthorized", "created": "20261124201200", "modified": "20261124201400"},
        {"id": 11, "parent_id": 3, "priority": 1, "status": "blocked", "action": "Inject arbitrary claims via forged secret key", "target": "api.corp.internal", "rationale": "Test weak HMAC secret entropy", "outcome": "WAF blocked request containing high-entropy base64 string", "created": "20261124201600", "modified": "20261124201800"},
        {"id": 12, "parent_id": 100, "priority": 2, "status": "in_progress", "action": "Audit refresh token rotation policy", "target": "api.corp.internal", "rationale": "Determine if refresh token can be reused concurrently", "outcome": "", "created": "20261124211000", "modified": "20261124211200"},
        {"id": 13, "parent_id": 100, "priority": 3, "status": "pending", "action": "Enumerate CORS misconfigurations across API subdomains", "target": "subdomains.corp.internal", "rationale": "Check for wildcard Access-Control-Allow-Origin with credentials", "outcome": "", "created": "20261124211500", "modified": "20261124211500"},
        {"id": 14, "parent_id": 1, "priority": 4, "status": "pending", "action": "Passive OSINT on IP ranges", "target": "10.0.0.0/24", "rationale": "Map ASNs and external peering partners", "outcome": "", "created": "20261124200200", "modified": "20261124200200"},
        {"id": 15, "parent_id": 1, "priority": 5, "status": "pending", "action": "Archive network pcap captures", "target": "local", "rationale": "Compliance record retention", "outcome": "", "created": "20261124200300", "modified": "20261124200300"}
    ]
}

def render_monochrome(board):
    out = []
    out.append("=== STINGTRAIL // CRITICAL THINKING BOARD ===")
    out.append(f"Nodes ({len(board['nodes'])}):")
    for n in board["nodes"]:
        det = f" — {n['detail']}" if n['detail'] else ""
        out.append(f"  [#{n['id']}] ({n['kind']}) {n['name']}{det} (created: {n['created']})")

    out.append(f"\nReasoning Log ({len(board['entries'])}):")
    for e in board["entries"]:
        det = f" | {e['detail']}" if e['detail'] else ""
        out.append(f"  [#{e['id']} <- #{e['parent_id']}] [{e['kind']}/{e['severity']}] {e['summary']}{det} (created: {e['created']})")

    out.append(f"\nPlanned Next Steps ({len(board['steps'])}):")
    for p in range(1, 6):
        for s in board["steps"]:
            if s["priority"] != p:
                continue
            tgt = f" (target: {s['target']})" if s['target'] else ""
            out.append(f"  [#{s['id']} <- #{s['parent_id']}] [P{s['priority']}][{s['status']}] {s['action']}{tgt} (created: {s['created']}, modified: {s['modified']})")
            out.append(f"      why: {s['rationale']}")
            if s['outcome']:
                out.append(f"      outcome: {s['outcome']}")
    return "\n".join(out)

def render_rainbow_aggressive(board):
    out = []
    out.append("\033[1;37;44m=== STINGTRAIL // CRITICAL THINKING BOARD ===\033[0m")
    out.append(f"\033[1;35mNodes ({len(board['nodes'])}):\033[0m")
    for n in board["nodes"]:
        out.append(f"  \033[1;36m[#{n['id']}]\033[0m \033[1;33m({n['kind']})\033[0m \033[1;32m{n['name']}\033[0m \033[34m— {n['detail']}\033[0m \033[35m(created: {n['created']})\033[0m")

    out.append(f"\n\033[1;35mReasoning Log ({len(board['entries'])}):\033[0m")
    for e in board["entries"]:
        out.append(f"  \033[1;36m[#{e['id']} <- #{e['parent_id']}]\033[0m \033[1;33m[{e['kind']}/{e['severity']}]\033[0m \033[1;32m{e['summary']}\033[0m \033[34m| {e['detail']}\033[0m \033[35m(created: {e['created']})\033[0m")

    out.append(f"\n\033[1;35mPlanned Next Steps ({len(board['steps'])}):\033[0m")
    for p in range(1, 6):
        for s in board["steps"]:
            if s["priority"] != p:
                continue
            out.append(f"  \033[1;36m[#{s['id']} <- #{s['parent_id']}]\033[0m \033[1;31m[P{s['priority']}]\033[0m\033[1;33m[{s['status']}]\033[0m \033[1;32m{s['action']}\033[0m \033[34m(target: {s['target']})\033[0m \033[35m(created: {s['created']}, modified: {s['modified']})\033[0m")
            out.append(f"      \033[1;33mwhy:\033[0m \033[36m{s['rationale']}\033[0m")
            if s['outcome']:
                out.append(f"      \033[1;32moutcome:\033[0m \033[37m{s['outcome']}\033[0m")
    return "\n".join(out)

def render_strategic_sparse(board):
    # Tier 1: Bold Cyan Spatial Anchors for Node IDs
    # Tier 2: Semantic Accents for status/priority/severity
    # Tier 3: Muted Dim Gray for metadata and timestamps
    # Body text: Clean default terminal foreground (white)
    
    C_RESET = "\033[0m"
    C_BOLD = "\033[1m"
    C_DIM = "\033[2m"
    C_ANCHOR = "\033[1;36m"   # Bold Cyan for Node IDs
    C_P1 = "\033[1;31m"       # Bold Red
    C_P2 = "\033[33m"         # Amber / Yellow
    C_P3 = "\033[34m"         # Blue
    C_PLOW = "\033[2m"        # Dim for P4, P5
    
    C_DONE = "\033[1;32m"     # Bold Green
    C_IN_PROG = "\033[1;33m"  # Bold Amber
    C_BLOCKED = "\033[1;31m"  # Bold Red
    C_PENDING = "\033[36m"    # Cyan
    
    C_HYP = "\033[1;35m"      # Bold Magenta for hypothesis
    C_EVI = "\033[1;32m"      # Bold Green for evidence
    C_REF = "\033[1;31m"      # Bold Red for refutation
    C_OBS = "\033[36m"        # Cyan for observation
    C_FIND = "\033[1;33m"     # Yellow for finding

    out = []
    out.append(f"{C_BOLD}=== STINGTRAIL // CRITICAL THINKING BOARD ==={C_RESET}")
    out.append(f"{C_BOLD}Nodes ({len(board['nodes'])}):{C_RESET}")
    for n in board["nodes"]:
        det = f" — {n['detail']}" if n['detail'] else ""
        out.append(f"  {C_ANCHOR}[#{n['id']}]{C_RESET} ({n['kind']}) {n['name']}{det} {C_DIM}(created: {n['created']}){C_RESET}")

    out.append(f"\n{C_BOLD}Reasoning Log ({len(board['entries'])}):{C_RESET}")
    for e in board["entries"]:
        det = f" | {e['detail']}" if e['detail'] else ""
        k_color = C_HYP if e['kind'] == 'hypothesis' else (C_EVI if e['kind'] == 'evidence' else (C_REF if e['kind'] == 'refutation' else C_OBS))
        out.append(f"  {C_ANCHOR}[#{e['id']} <- #{e['parent_id']}]{C_RESET} [{k_color}{e['kind']}{C_RESET}/{e['severity']}] {e['summary']}{det} {C_DIM}(created: {e['created']}){C_RESET}")

    out.append(f"\n{C_BOLD}Planned Next Steps ({len(board['steps'])}):{C_RESET}")
    for p in range(1, 6):
        p_col = C_P1 if p == 1 else (C_P2 if p == 2 else (C_P3 if p == 3 else C_PLOW))
        for s in board["steps"]:
            if s["priority"] != p:
                continue
            st = s["status"]
            s_col = C_DONE if st == "done" else (C_BLOCKED if st == "blocked" else (C_IN_PROG if st == "in_progress" else C_PENDING))
            tgt = f" {C_DIM}(target: {s['target']}){C_RESET}" if s['target'] else ""
            out.append(f"  {C_ANCHOR}[#{s['id']} <- #{s['parent_id']}]{C_RESET} [{p_col}P{s['priority']}{C_RESET}][{s_col}{s['status']}{C_RESET}] {s['action']}{tgt} {C_DIM}(created: {s['created']}, modified: {s['modified']}){C_RESET}")
            out.append(f"      {C_DIM}why:{C_RESET} {s['rationale']}")
            if s['outcome']:
                out.append(f"      {C_DIM}outcome:{C_RESET} {s['outcome']}")
    return "\n".join(out)

def analyze_model(name: str, rendered: str):
    raw_chars = len(rendered)
    plain = strip_ansi(rendered)
    plain_chars = len(plain)
    ansi_overhead = raw_chars - plain_chars
    overhead_pct = (ansi_overhead / plain_chars) * 100.0 if plain_chars > 0 else 0.0

    # Color tokens count (occurrences of ANSI sequences)
    color_sequences = ANSI_ESCAPE_RE.findall(rendered)
    resets = [c for c in color_sequences if c == '\x1b[0m']
    active_colors = len(color_sequences) - len(resets)

    # Scannability / Visual Anchor Density
    # In strategic sparse: anchors are concentrated on ID coordinates & statuses.
    # We estimate visual noise ratio: colored characters / plain characters
    # Count characters that are printed in non-default color:
    colored_char_count = 0
    in_color = False
    for token in re.split(r'(\x1b\[[0-9;]*m)', rendered):
        if token.startswith('\x1b['):
            in_color = (token != '\x1b[0m')
        else:
            if in_color:
                colored_char_count += len(token)

    color_density = (colored_char_count / plain_chars) * 100.0 if plain_chars > 0 else 0.0

    contrast = "None (Monochrome flat)" if "Monochrome" in name else ("Chaotic / Over-saturated" if "Rainbow" in name else "3-Tier (Anchor/Status/Muted)")
    fatigue = "High (Linear text search required)" if "Monochrome" in name else ("High (Visual clutter / sensory overload)" if "Rainbow" in name else "Low (Eyes instantly jump to IDs & Blocked/P1)")

    return {
        "model": name,
        "raw_bytes": raw_chars,
        "plain_bytes": plain_chars,
        "ansi_overhead_bytes": ansi_overhead,
        "overhead_percent": round(overhead_pct, 2),
        "ansi_sequence_count": len(color_sequences),
        "colored_text_ratio_pct": round(color_density, 2),
        "contrast_hierarchy": contrast,
        "human_scan_fatigue": fatigue,
    }

def main():
    mono = render_monochrome(SAMPLE_BOARD)
    rainbow = render_rainbow_aggressive(SAMPLE_BOARD)
    sparse = render_strategic_sparse(SAMPLE_BOARD)

    res_mono = analyze_model("Monochrome (Current)", mono)
    res_rainbow = analyze_model("Rainbow (Aggressive)", rainbow)
    res_sparse = analyze_model("Strategic Sparse (Proposed)", sparse)

    results = [res_mono, res_rainbow, res_sparse]
    with open("simulations/exp7_results.json", "w") as f:
        json.dump(results, f, indent=2)

    print("=== TERMINAL STYLING ERGONOMIC BENCHMARK ===")
    for r in results:
        print(f"\n[{r['model']}]:")
        print(f"  Plain text: {r['plain_bytes']} bytes")
        print(f"  ANSI Overhead: {r['ansi_overhead_bytes']} bytes (+{r['overhead_percent']}%)")
        print(f"  Color sequences: {r['ansi_sequence_count']}")
        print(f"  Colored character ratio: {r['colored_text_ratio_pct']}%")
        print(f"  Contrast hierarchy: {r['contrast_hierarchy']}")
        print(f"  Human scan fatigue: {r['human_scan_fatigue']}")

    print("\n[Preview of Strategic Sparse Output]:")
    print("-" * 60)
    print(sparse)
    print("-" * 60)

if __name__ == "__main__":
    main()
