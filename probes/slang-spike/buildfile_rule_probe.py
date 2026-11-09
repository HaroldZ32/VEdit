#!/usr/bin/env python3
"""Measure the slang-server build-file rule (what must be relative, and where).

Three runs on the same fixture, each with the server's cwd = the project root:

  1. `.f` OUTSIDE the project root (absolute path to the file),
     entries project-root-relative            -> expect correct location URIs;
  2. `.f` outside, entries ABSOLUTE           -> expect the degraded-parent URIs;
  3. `.f` inside the project root, entries relative -> correct (the original setup).

The output is the rule, measured: the FILE may live anywhere; the ENTRIES must be
project-root-relative; the server's cwd must be the project root and `rootUri`
must point there.

Usage: buildfile_rule_probe.py --server <slang-server> --root <fixture>
"""
import argparse
import json
import os
import subprocess
import tempfile


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


class Server:
    def __init__(self, server, cwd):
        self.process = subprocess.Popen(
            [server],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd=cwd,
        )
        self.next_id = 1

    def call(self, method, params):
        request_id = self.next_id
        self.next_id += 1
        self.process.stdin.write(
            encode({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params})
        )
        self.process.stdin.flush()
        while True:
            incoming = read_message(self.process.stdout)
            if incoming is None:
                raise SystemExit(f"{method}: the server closed the pipe")
            if incoming.get("id") == request_id:
                return incoming
            if incoming.get("method") and "id" in incoming:
                self.process.stdin.write(
                    encode({"jsonrpc": "2.0", "id": incoming["id"], "result": None})
                )
                self.process.stdin.flush()

    def notify(self, method, params):
        self.process.stdin.write(encode({"jsonrpc": "2.0", "method": method, "params": params}))
        self.process.stdin.flush()

    def close(self):
        self.call("shutdown", None)
        self.notify("exit", None)
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()


def run_case(server, root, build_file, label, cwd=None):
    client = Server(server, cwd=cwd or root)
    client.call(
        "initialize",
        {"processId": None, "rootUri": "file://" + root, "capabilities": {}},
    )
    client.notify("initialized", {})
    client.call("workspace/executeCommand", {"command": "slang.setBuildFile", "arguments": [build_file]})
    scopes = client.call(
        "workspace/executeCommand", {"command": "slang.getScopesByModule", "arguments": []}
    )
    drivers = client.call(
        "workspace/executeCommand",
        {"command": "slang.getDriversWithLocation", "arguments": ["top.link"]},
    )
    client.close()
    driver = (drivers.get("result") or [None])[0]
    uri = ((driver or {}).get("location") or {}).get("uri")
    return {
        "label": label,
        "build_file": build_file,
        "entries": open(build_file).read().split(),
        "modules_found": len(scopes.get("result") or []),
        "driver_uri": uri,
        "correct_uri": bool(uri) and os.path.dirname(uri.replace("file://", "")) == root,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--root", required=True)
    parser.add_argument("--out", default="")
    args = parser.parse_args()
    root = os.path.abspath(args.root)
    files = sorted(
        name for name in os.listdir(root) if name.endswith(".sv")
    )
    work = tempfile.mkdtemp(prefix="slang-buildfile-rule-")
    outside = os.path.join(work, "outside.f")
    with open(outside, "w") as handle:
        handle.write("".join(name + "\n" for name in files))
    outside_absolute = os.path.join(work, "outside-absolute.f")
    with open(outside_absolute, "w") as handle:
        handle.write("".join(os.path.join(root, name) + "\n" for name in files))
    inside = os.path.join(root, "inside.f")
    with open(inside, "w") as handle:
        handle.write("".join(name + "\n" for name in files))

    try:
        report = {
            "root": root,
            "cases": [
                run_case(args.server, root, outside, "cwd=root, f outside root, relative entries"),
                run_case(args.server, root, outside_absolute, "cwd=root, f outside root, ABSOLUTE entries"),
                run_case(args.server, root, inside, "cwd=root, f inside root, relative entries"),
                run_case(
                    args.server,
                    root,
                    outside,
                    "cwd=PARENT of root, f outside root, relative entries",
                    cwd=os.path.dirname(root),
                ),
            ],
        }
    finally:
        for path in (outside, outside_absolute, inside):
            try:
                os.unlink(path)
            except OSError:
                pass
        os.rmdir(work)
    print(json.dumps(report, indent=1))
    if args.out:
        with open(args.out, "w") as handle:
            json.dump(report, handle, indent=1)


if __name__ == "__main__":
    main()
