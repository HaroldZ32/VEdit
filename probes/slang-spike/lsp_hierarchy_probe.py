#!/usr/bin/env python3
"""Capture slang-server's hierarchy + driver/load LSP shapes on the spike fixture.

Sequence: initialize (answering the server's own `slang/setConfig` notification),
`initialized`, `textDocument/didOpen` for the fixture files, then a list of
`workspace/executeCommand` calls. Every request and raw response is printed, so
the output is the evidence for T7's contract.

Usage:
  lsp_hierarchy_probe.py --server <slang-server> --root <fixture dir> [--wrapper "docker run -i …"]
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
import time


def encode(message):
    body = json.dumps(message).encode()
    return b"Content-Length: %d\r\n\r\n" % len(body) + body


def read_message(stream):
    headers = {}
    while True:
        line = stream.readline()
        if not line:
            return None
        if line in (b"\r\n", b"\n"):
            break
        key, _, value = line.decode().partition(":")
        headers[key.strip().lower()] = value.strip()
    return json.loads(stream.read(int(headers.get("content-length", "0"))))


class Client:
    def __init__(self, command, cwd=None):
        self.process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd=cwd,
        )
        self.next_id = 1
        self.notifications = []

    def notify(self, method, params):
        message = {"jsonrpc": "2.0", "method": method, "params": params}
        print("--> " + json.dumps(message)[:400])
        self.process.stdin.write(encode(message))
        self.process.stdin.flush()

    def request(self, method, params, timeout=20.0):
        request_id = self.next_id
        self.next_id += 1
        message = {"jsonrpc": "2.0", "id": request_id, "method": method, "params": params}
        print("--> " + json.dumps(message)[:600])
        self.process.stdin.write(encode(message))
        self.process.stdin.flush()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            incoming = read_message(self.process.stdout)
            if incoming is None:
                break
            if incoming.get("method") and "id" in incoming:
                # A server->client REQUEST: answer it so the server is not blocked.
                reply = {"jsonrpc": "2.0", "id": incoming["id"], "result": None}
                self.process.stdin.write(encode(reply))
                self.process.stdin.flush()
                self.notifications.append(incoming)
                continue
            if incoming.get("method"):
                self.notifications.append(incoming)
                print("<-- notification " + json.dumps(incoming)[:2000])
                continue
            if incoming.get("id") == request_id:
                return incoming
        return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--root", required=True)
    parser.add_argument("--wrapper", default="")
    parser.add_argument("--top", default="top")
    parser.add_argument("--signal", default="top.link")
    args = parser.parse_args()

    command = (args.wrapper.split() if args.wrapper else []) + [args.server]
    # Run the server FROM the workspace root: this is how the product starts it,
    # and it is what makes relative build-file entries resolve.
    client = Client(command, cwd=None if args.wrapper else os.path.abspath(args.root))
    root_uri = "file://" + os.path.abspath(args.root)
    files = sorted(
        os.path.join(os.path.abspath(args.root), name)
        for name in os.listdir(args.root)
        if name.endswith(".sv")
    )

    started = time.monotonic()
    initialize = client.request(
        "initialize",
        {
            "processId": None,
            "clientInfo": {"name": "vedit-spike", "version": "0"},
            "rootUri": root_uri,
            "capabilities": {},
            "workspaceFolders": [{"uri": root_uri, "name": "fixture"}],
        },
    )
    elapsed = time.monotonic() - started
    print("<-- " + json.dumps(initialize, indent=1)[:6000])
    print("handshake_seconds=%.3f" % elapsed)
    if initialize is None or "result" not in initialize:
        raise SystemExit("initialize did not complete")
    client.notify("initialized", {})

    for path in files:
        uri = "file://" + path
        with open(path) as handle:
            text = handle.read()
        client.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": uri,
                    "languageId": "systemverilog",
                    "version": 1,
                    "text": text,
                }
            },
        )

    # slang-server elaborates from a build file (or a top file). Write one with
    # ABSOLUTE paths next to the fixture so the run is reproducible anywhere.
    # The .f must NOT be written into the fixture (it would pollute a pinned
    # directory). Its ENTRIES stay relative to the workspace root, which is what
    # the server resolves against; the file itself can live anywhere.
    build_dir = os.environ.get("SPIKE_BUILD_FILE_DIR") or tempfile.gettempdir()
    os.makedirs(build_dir, exist_ok=True)
    build_file = tempfile.NamedTemporaryFile(
        "w", prefix="vedit-spike-", suffix=".f", delete=False, dir=build_dir
    )
    for path in files:
        # Relative to the workspace root, like a real project's .f.
        build_file.write(os.path.relpath(path, os.path.abspath(args.root)) + "\n")
    build_file.close()
    print("build_file=" + build_file.name + " contents=" + json.dumps(open(build_file.name).read()))

    calls = [
        ("slang.setBuildFile", [build_file.name]),
        ("slang.setTopLevel", [args.top]),
        ("slang.getScope", [""]),
        ("slang.getScopes", [""]),
        ("slang.getScopesByModule", []),
        ("slang.searchHierarchy", [""]),
        ("slang.getInstancesOfModule", ["mid"]),
        ("slang.getDriversWithLocation", [args.signal]),
        ("slang.getLoadsWithLocation", [args.signal]),
    ]
    results = {}
    design_build_seconds = None
    for method, arguments in calls:
        call_started = time.monotonic()
        response = client.request(
            "workspace/executeCommand", {"command": method, "arguments": arguments}
        )
        if method == "slang.setBuildFile":
            design_build_seconds = time.monotonic() - call_started
        print("== %s -> %s" % (method, json.dumps(response, indent=1)[:6000]))
        results[method] = response
        if method == "slang.setBuildFile":
            # The design is built off the request thread; poll until it answers.
            for attempt in range(30):
                probe = client.request(
                    "workspace/executeCommand",
                    {"command": "slang.getScopesByModule", "arguments": []},
                )
                payload = (probe or {}).get("result") or []
                if payload:
                    print("design_ready_after_ms=%d" % (attempt * 100))
                    results["slang.getScopesByModule"] = probe
                    break
                time.sleep(0.1)

    # The server's own peak RSS (VmHWM) is the number the 1 GiB bound applies to.
    peak_rss_kb = None
    try:
        with open(f"/proc/{client.process.pid}/status") as status:
            for line in status:
                if line.startswith("VmHWM:"):
                    peak_rss_kb = int(line.split()[1])
    except OSError:
        peak_rss_kb = None
    print(
        "server_measurements="
        + json.dumps(
            {
                "handshake_seconds": round(elapsed, 3),
                "design_build_seconds": round(design_build_seconds, 3)
                if design_build_seconds is not None
                else None,
                "server_peak_rss_kb": peak_rss_kb,
                "server_peak_rss_mb": round(peak_rss_kb / 1024, 1) if peak_rss_kb else None,
                "limits": {"max_elapsed_s": 30, "max_peak_rss_mb": 1024},
                "passes_limits": peak_rss_kb is None
                or (peak_rss_kb <= 1024 * 1024
                    and (design_build_seconds or 0) <= 30),
            }
        )
    )

    client.request("shutdown", None)
    client.notify("exit", None)
    try:
        client.process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        client.process.kill()

    # Keep the machine-readable copy next to the console log, never in probes/.
    dump_path = os.environ.get("SPIKE_HIERARCHY_JSON")
    if dump_path:
        with open(dump_path, "w") as handle:
            json.dump({"initialize": initialize, "commands": results}, handle, indent=1)
    print("notifications_seen=" + json.dumps([n.get("method") for n in client.notifications]))


if __name__ == "__main__":
    main()
