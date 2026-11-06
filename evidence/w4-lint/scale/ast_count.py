#!/usr/bin/env python3
"""Ground truth for the A-12 elaboration numbers: how many Instance nodes the
slang AST of the whole project actually contains (all roots), versus what the
shipped hierarchy service returns.

    python3 evidence/w4-lint/scale/ast_count.py <ast.json>
"""
import json
import sys

path = sys.argv[1] if len(sys.argv) > 1 else ""
try:
    with open(path, encoding="utf-8") as handle:
        ast = json.load(handle)
except Exception as error:  # noqa: BLE001 - the evidence line must never crash a run
    print("AST-GROUND-TRUTH unavailable: %s" % error)
    raise SystemExit(0)

design = ast.get("design", {})
roots = [m for m in design.get("members", []) if m.get("kind") == "Instance"]
total = 0
max_depth = 0


def walk(node, depth):
    """`depth` is the number of Instance ancestors (the root is depth 1)."""
    global total, max_depth
    if isinstance(node, dict):
        if node.get("kind") == "Instance":
            total += 1
            depth += 1
            max_depth = max(max_depth, depth)
        for value in node.values():
            walk(value, depth)
    elif isinstance(node, list):
        for value in node:
            walk(value, depth)


walk(design, 0)
print(
    "AST-GROUND-TRUTH roots=%d instance_nodes=%d max_depth=%d"
    % (len(roots), total, max_depth)
)
