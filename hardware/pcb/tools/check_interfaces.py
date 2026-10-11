#!/usr/bin/env python3
"""Interface-freeze checker for the TwinOak standard repeater v2 boards.

The inter-board interfaces (D-subs, A<->B ribbon, platform J1/J2-J5, adapter interface rows) were
FROZEN when the first batch was ordered from JLCPCB on 2026-10-10 - see INTERFACE-FREEZE.md.
This script compares the five KiCad PCBs against tools/interface-freeze.json:

  * every interface connector's pad -> net assignment (net names normalised, NC pads as "NC";
    the platform's auto-named nets are mapped to the J1 conductor they belong to)
  * every interface connector's footprint name and pad geometry (pad positions relative to pad 1,
    local footprint frame, 0.01 mm) - catches a swapped or rotated footprint

Usage (from hardware/pcb/):
    python3 tools/check_interfaces.py          # verify -> PASS / FAIL with a diff, exit code 0/1
    python3 tools/check_interfaces.py --write  # regenerate the freeze file (ONLY after a recorded
                                               # decision to change an interface - see the .md)
Stdlib only. Works on the KiCad 10 s-expression files directly; no KiCad install needed.
"""
import re, json, sys, os, math

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
FREEZE = os.path.join(HERE, "interface-freeze.json")

BOARDS = {
    "platform-board":    ("platform-board/platform-board.kicad_pcb",            ["J1", "J2", "J3", "J4", "J5"]),
    "connector-board-a": ("connector-board-a/connector-board-a.kicad_pcb",      ["J1", "J2"]),
    "connector-board-b": ("connector-board-b/connector-board-b.kicad_pcb",      ["J1", "J5"]),
    "adapter-rak4630":   ("adapter-rak4630/rak4630-adapter.kicad_pcb",          ["U2"]),
    "adapter-walter":    ("adapter-walter/adapter-walter.kicad_pcb",            ["U2"]),
}
NET = re.compile(r'\(net (?:\d+ )?"?([^")]*)"?\)')

def block(s, i):
    d = 0; j = i
    while True:
        c = s[j]
        if c == '(': d += 1
        elif c == ')':
            d -= 1
            if d == 0: return s[i:j + 1]
        j += 1

def norm_net(name, board):
    if not name: return "NC"
    if name.startswith("unconnected-("): return "NC"
    if board == "platform-board":
        m = re.match(r"Net-\(J1-Pin_(\d+)", name)
        if m: return "J1.%s" % m.group(1)
    return name.rstrip(")")

def read_board(board):
    path, refs = BOARDS[board]
    s = open(os.path.join(ROOT, path), encoding="utf-8").read()
    out = {}
    for m in re.finditer(r'\n\t\(footprint ', s):
        fb = block(s, m.start() + 2)
        r = re.search(r'\(property "Reference" "([^"]+)"', fb)
        if not r or r.group(1) not in refs: continue
        fpname = re.search(r'\(footprint "([^"]+)"', fb).group(1)
        nets, pos = {}, {}
        for pm in re.finditer(r'\(pad "([^"]*)"', fb):
            pb = block(fb, pm.start()); name = pm.group(1)
            if name == "": continue
            n = NET.search(pb); at = re.search(r'\(at ([-\d.]+) ([-\d.]+)', pb)
            nets[name] = norm_net(n.group(1) if n else "", board)
            pos[name] = (float(at.group(1)), float(at.group(2)))
        if "1" in pos:
            ox, oy = pos["1"]
            geom = {k: [round(v[0] - ox, 2), round(v[1] - oy, 2)] for k, v in pos.items()}
        else:
            geom = {k: [round(v[0], 2), round(v[1], 2)] for k, v in pos.items()}
        out[r.group(1)] = {"footprint": fpname, "nets": nets, "geometry": geom}
    missing = [r for r in refs if r not in out]
    if missing: raise SystemExit("%s: interface connector(s) %s not found" % (board, missing))
    return out

def snapshot():
    return {b: read_board(b) for b in BOARDS}

def compare(frozen, current):
    problems = []
    for b in BOARDS:
        for ref, fz in frozen[b].items():
            cu = current[b].get(ref)
            if cu is None:
                problems.append("%s %s: connector missing" % (b, ref)); continue
            if cu["footprint"] != fz["footprint"]:
                problems.append("%s %s: footprint %s -> %s" % (b, ref, fz["footprint"], cu["footprint"]))
            for pad in sorted(set(fz["nets"]) | set(cu["nets"]), key=lambda x: (len(x), x)):
                a, c = fz["nets"].get(pad, "<absent>"), cu["nets"].get(pad, "<absent>")
                if a != c: problems.append("%s %s pad %s: net %s -> %s" % (b, ref, pad, a, c))
            for pad in sorted(set(fz["geometry"]) | set(cu["geometry"]), key=lambda x: (len(x), x)):
                a, c = fz["geometry"].get(pad), cu["geometry"].get(pad)
                if a is None or c is None or math.hypot(a[0] - c[0], a[1] - c[1]) > 0.011:
                    problems.append("%s %s pad %s: geometry %s -> %s" % (b, ref, pad, a, c))
        for ref in current[b]:
            if ref not in frozen[b]: problems.append("%s %s: connector not in freeze file" % (b, ref))
    return problems

if __name__ == "__main__":
    cur = snapshot()
    if "--write" in sys.argv:
        json.dump({"_comment": "FROZEN 2026-10-10 (first JLCPCB batch). Do not regenerate without a recorded decision - see INTERFACE-FREEZE.md",
                   "boards": cur}, open(FREEZE, "w"), indent=1, sort_keys=True)
        print("wrote", FREEZE); sys.exit(0)
    frozen = json.load(open(FREEZE))["boards"]
    probs = compare(frozen, cur)
    if probs:
        print("INTERFACE FREEZE: FAIL - %d difference(s) against the as-ordered interfaces:" % len(probs))
        for p in probs: print("  " + p)
        sys.exit(1)
    n = sum(len(c) for c in frozen.values())
    print("INTERFACE FREEZE: PASS - %d interface connectors on 5 boards match the as-ordered state (2026-10-10)." % n)
