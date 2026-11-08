#!/usr/bin/env python3
"""Hierarchical elaboration probe for the AC-3.0 spike.

Runs `slang --top top --ast-json … --ast-json-source-info` over a fixture and
extracts, from the SAME JSON the future hierarchy service would consume:

  * the instance hierarchy with hierarchical paths (top / top.u0 / top.u1) and
    each instance's module definition plus its source file/line/column;
  * for every net of every instance: the direct drivers and loads, derived from
    instance port connections (direction In/Out) and continuous assignments,
    plus the driver chain that continues inside the child instance
    (top.link <- top.u0.q <- top.u0.state <- always_ff).

It also measures wall time and the child's peak RSS (`resource.RUSAGE_CHILDREN`)
so the >30 s / >1 GiB kill criterion can be applied from the same run.

Usage:
  elab_probe.py --slang build/slang/out/slang --fixture probes/slang-spike/fixture \\
                --out evidence/w2-spike/05-elaboration.json
"""
import argparse
import json
import os
import resource
import subprocess
import sys
import time


BODIES = {}


def index_bodies(node):
    """slang DEDUPLICATES instance bodies: the second instance of a module
    serialises `"body": "<addr> <name>"` instead of a copy. Index every real body
    by address so a reference can be resolved."""
    if isinstance(node, dict):
        if node.get("kind") == "InstanceBody" and "addr" in node:
            BODIES[node["addr"]] = node
        for value in node.values():
            index_bodies(value)
    elif isinstance(node, list):
        for item in node:
            index_bodies(item)


def body_of(node):
    body = node.get("body") if isinstance(node, dict) else None
    if isinstance(body, str):
        key = body.partition(" ")[0]
        try:
            return BODIES.get(int(key))
        except ValueError:
            return None
    return body if isinstance(body, dict) else None


def symbol_name(value):
    """slang serialises a symbol reference as '<address> <name>'."""
    if isinstance(value, dict):
        value = value.get("symbol", "")
    if not isinstance(value, str):
        return None
    _, _, name = value.partition(" ")
    return name or value


def expr_net(expr):
    """The net an output-port connection writes, or a NamedValue expression."""
    if not isinstance(expr, dict):
        return None
    if expr.get("kind") == "NamedValue":
        return symbol_name(expr)
    if expr.get("kind") == "Assignment":
        return symbol_name(expr.get("left"))
    return None


def named_values(node, out):
    """Every NamedValue symbol name found under `node`."""
    if isinstance(node, dict):
        if node.get("kind") == "NamedValue":
            name = symbol_name(node)
            if name:
                out.append(name)
        for value in node.values():
            named_values(value, out)
    elif isinstance(node, list):
        for item in node:
            named_values(item, out)
    return out


def location(node):
    if not isinstance(node, dict):
        return None
    fmt = node.get("source_file") or node.get("source_file_start")
    line = node.get("source_line") or node.get("source_line_start")
    column = node.get("source_column") or node.get("source_column_start")
    if fmt is None:
        return None
    return {"file": fmt, "line": line, "column": column}


def walk_instances(node, path, out):
    """Depth-first over instance bodies; records hierarchical paths."""
    body = body_of(node)
    if not isinstance(body, dict):
        return
    for member in body.get("members", []):
        if not isinstance(member, dict) or member.get("kind") != "Instance":
            continue
        child_path = f"{path}.{member.get('name')}"
        child_body = body_of(member)
        out.append(
            {
                "path": child_path,
                "module": child_body.get("name") if isinstance(child_body, dict) else None,
                "instantiation": location(member),
            }
        )
        walk_instances(member, child_path, out)


def assignments(node, found):
    """Collect every Assignment expression found anywhere under `node`."""
    if isinstance(node, dict):
        if node.get("kind") == "Assignment":
            found.append(node)
        for value in node.values():
            assignments(value, found)
    elif isinstance(node, list):
        for item in node:
            assignments(item, found)


def collect_scope(node, path, signals):
    """One instance body -> its local signals with drivers/loads."""
    body = body_of(node)
    if not isinstance(body, dict):
        return
    members = [m for m in body.get("members", []) if isinstance(m, dict)]
    local = {}
    for member in members:
        if not isinstance(member, dict):
            continue
        if member.get("kind") in ("Net", "Variable", "Port"):
            name = member.get("name")
            if not name:
                continue
            entry = local.setdefault(
                name,
                {
                    "path": f"{path}.{name}",
                    "kind": member.get("kind"),
                    "declaration": location(member),
                    "drivers": [],
                    "loads": [],
                },
            )
            if entry["kind"] == "Port":
                entry["kind"] = member.get("kind")

    # Instance port connections: an Out port drives the bound net, an In port
    # loads it (InOut does both).
    for member in members:
        if not isinstance(member, dict) or member.get("kind") != "Instance":
            continue
        child_path = f"{path}.{member.get('name')}"
        for connection in member.get("connections", []):
            port = connection.get("port", {})
            direction = (port.get("direction") or "").lower()
            expr = connection.get("expr") or {}
            target = f"{child_path}.{port.get('name')}"
            at = location(expr) or location(member)
            # An output port connection is an Assignment (bound_net = port); an
            # input port connection is the expression read by the port.
            if direction in ("out", "inout"):
                net = expr_net(expr)
                if net and net in local:
                    local[net]["drivers"].append(
                        {"via": target, "kind": "instance-port", "direction": "out", "at": at}
                    )
            if direction in ("in", "inout"):
                for net in named_values(expr, []):
                    if net in local:
                        local[net]["loads"].append(
                            {"via": target, "kind": "instance-port", "direction": "in", "at": at}
                        )
        collect_scope(member, child_path, signals)

    # Continuous assignments in this scope: LHS drives, RHS loads.
    for member in members:
        if not isinstance(member, dict) or member.get("kind") != "ContinuousAssign":
            continue
        assignment = member.get("assignment", {})
        left = symbol_name(assignment.get("left"))
        right = symbol_name(assignment.get("right"))
        at = location(assignment) or location(member)
        if left in local:
            local[left]["drivers"].append(
                {"via": f"{path}.{left} (continuous assign)", "kind": "assign", "at": at}
            )
        if right in local:
            local[right]["loads"].append(
                {"via": f"{path}.{right} (continuous assign source)", "kind": "assign", "at": at}
            )
        # Remember the intra-scope edge for the driver chain below.
        if left in local and right in local:
            local[left].setdefault("assign_from", []).append(f"{path}.{right}")

    # Procedural blocks (always_ff / always_comb / …): any LHS inside drives.
    for member in members:
        if not isinstance(member, dict) or member.get("kind") != "ProceduralBlock":
            continue
        found = []
        assignments(member.get("body"), found)
        for assignment in found:
            left = symbol_name(assignment.get("left"))
            if left in local:
                local[left]["drivers"].append(
                    {
                        "via": f"{path}.{left} ({member.get('procedureKind', 'procedure')})",
                        "kind": "procedural",
                        "at": location(assignment) or location(member),
                    }
                )

    for name, entry in local.items():
        signals[entry["path"]] = entry


def chain(signals, signal_path, depth=6):
    """Follow the driver from a parent net into the child instance that drives it.

    Hops: parent net -> (instance output port) -> child net -> (continuous
    assign) -> child net -> terminal driver (a procedure or an assign).
    """
    out = []
    current = signal_path
    seen = set()
    while depth > 0 and current not in seen:
        seen.add(current)
        signal = signals.get(current)
        if signal is None:
            break
        port_hop = next(
            (
                driver
                for driver in signal.get("drivers", [])
                if driver.get("kind") == "instance-port"
                and driver.get("direction") in ("out", "inout")
                and driver.get("via") in signals
            ),
            None,
        )
        if port_hop is not None:
            out.append(
                {
                    "from": current,
                    "to": port_hop["via"],
                    "kind": "instance-output-port",
                    "at": port_hop.get("at"),
                }
            )
            current = port_hop["via"]
            depth -= 1
            continue
        assign_hop = (signal.get("assign_from") or [None])[0]
        if assign_hop in signals:
            out.append(
                {
                    "from": current,
                    "to": assign_hop,
                    "kind": "continuous-assign",
                    "at": (signal.get("drivers") or [{}])[0].get("at"),
                }
            )
            current = assign_hop
            depth -= 1
            continue
        out.append({"from": current, "drivers": signal.get("drivers", [])})
        break
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--slang", required=True)
    parser.add_argument("--fixture", required=True)
    parser.add_argument("--top", default="top")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    sources = sorted(
        os.path.join(args.fixture, name)
        for name in os.listdir(args.fixture)
        if name.endswith((".sv", ".v", ".svh"))
    )
    ast_path = args.out + ".ast.json"
    command = [
        args.slang,
        "--top",
        args.top,
        "--ast-json",
        ast_path,
        "--ast-json-source-info",
        *sources,
    ]
    started = time.monotonic()
    before = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    completed = subprocess.run(command, capture_output=True, text=True, check=False)
    elapsed_ms = round((time.monotonic() - started) * 1000)
    peak_kb = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    if completed.returncode != 0:
        print(completed.stderr, file=sys.stderr)
        raise SystemExit(f"slang exited {completed.returncode}")

    document = json.load(open(ast_path))
    index_bodies(document)
    root = next(
        member
        for member in document["design"]["members"]
        if member.get("kind") == "Instance"
    )

    hierarchy = [
        {
            "path": root.get("name"),
            "module": (root.get("body") or {}).get("name"),
            "instantiation": location(root),
        }
    ]
    walk_instances(root, root.get("name"), hierarchy)

    signals = {}
    collect_scope(root, root.get("name"), signals)

    # The fixture's signal of interest: driven inside a child, loaded in the parent.
    interesting = {
        path: entry
        for path, entry in signals.items()
        if entry.get("drivers") and entry.get("loads")
    }
    report = {
        "tool": {
            "command": command,
            "exit_code": completed.returncode,
            "stderr": completed.stderr.strip(),
            "elapsed_ms": elapsed_ms,
            "peak_rss_kb": peak_kb,
            "peak_rss_mb": round(peak_kb / 1024, 1),
            "rss_note": "resource.getrusage(RUSAGE_CHILDREN).ru_maxrss (KiB on Linux), "
            "the maximum over all children of this probe",
            "limits": {"max_elapsed_s": 30, "max_peak_rss_mb": 1024},
            "passes_limits": elapsed_ms <= 30000 and peak_kb <= 1024 * 1024,
        },
        "hierarchy": hierarchy,
        "signals": signals,
        "signals_with_drivers_and_loads": sorted(interesting),
        "driver_chain_of_interest": {
            "top.link": chain(signals, "top.link"),
        },
    }
    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "w") as handle:
        json.dump(report, handle, indent=1, sort_keys=False)
    print(json.dumps(report, indent=1)[:4000])


if __name__ == "__main__":
    main()
