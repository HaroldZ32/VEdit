#!/usr/bin/env python3
"""T50: measure a language server's advertised capabilities with a real
initialize handshake (JSON-RPC over stdio, Content-Length framing).

Why this exists: docs/capability-matrix.md is the human-readable table, and its
rows must come from a MEASUREMENT, not from a recollection. T30's review found
the verible `codeAction` row contradicted what the live server advertises; this
script is the re-measurement tool so the row can be corrected (and re-checked)
without trusting anybody's memory.

  python3 capability_probe.py <server-binary> [--root <dir>] [--timeout 20]

Prints one `CAP-ROW <key> <value>` line per advertised capability and a
`CAP-KEYS <n>` summary, then exits 0. Unknown/unsupported rows are simply absent.
"""
import argparse
import json
import os
import subprocess
import sys
import threading
import time


def encode(message):
    body = json.dumps(message).encode("utf-8")
    return b"Content-Length: %d\r\n\r\n%s" % (len(body), body)


class Reader:
    def __init__(self, stream):
        self.stream = stream
        self.lock = threading.Lock()

    def read_message(self, timeout):
        deadline = time.time() + timeout
        headers = {}
        while True:
            if time.time() > deadline:
                return None
            line = self.stream.readline()
            if not line:
                return None
            if line in (b"\r\n", b"\n"):
                break
            key, _, value = line.decode("utf-8", "replace").partition(":")
            headers[key.strip().lower()] = value.strip()
        length = int(headers.get("content-length", "0"))
        if length <= 0:
            return None
        body = self.stream.read(length)
        try:
            return json.loads(body.decode("utf-8", "replace"))
        except json.JSONDecodeError:
            return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--root", default=os.getcwd())
    parser.add_argument("--timeout", type=float, default=20.0)
    args = parser.parse_args()

    env = dict(os.environ)
    env.setdefault("HOME", "/tmp")
    process = subprocess.Popen(
        [args.binary],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        env=env,
    )
    reader = Reader(process.stdout)

    initialize = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {
            "processId": os.getpid(),
            "rootUri": "file://" + os.path.abspath(args.root),
            "capabilities": {},
        },
    }
    process.stdin.write(encode(initialize))
    process.stdin.flush()

    deadline = time.time() + args.timeout
    capabilities = None
    while time.time() < deadline:
        message = reader.read_message(max(1.0, deadline - time.time()))
        if message is None:
            break
        if message.get("id") == 1 and "result" in message:
            capabilities = message["result"].get("capabilities", {})
            break
        # Servers may send notifications before the initialize response (exactly
        # the slang-server behaviour that kills the Rust client, T3): ignore them.
    if capabilities is None:
        print("CAP-FAIL no-initialize-response", file=sys.stderr)
        try:
            process.kill()
        except OSError:
            pass
        return 1

    for key in sorted(capabilities):
        value = capabilities[key]
        rendered = json.dumps(value, sort_keys=True) if isinstance(value, (dict, list)) else value
        print("CAP-ROW %s %s" % (key, rendered))
    print("CAP-KEYS %d" % len(capabilities))
    def advertised(key):
        """A capability counts as advertised only when its VALUE says so: several
        servers publish the key as False (verible sends hoverProvider False), and
        "the key exists" is not the same as "the server serves it"."""
        if key not in capabilities:
            return "no"
        value = capabilities[key]
        if value is False or value is None:
            return "no"
        return "yes"

    for wanted in ("codeActionProvider", "callHierarchyProvider", "hoverProvider",
                   "workspaceSymbolProvider", "documentSymbolProvider", "referencesProvider",
                   "renameProvider", "documentFormattingProvider", "documentRangeFormattingProvider",
                   "completionProvider", "inlayHintProvider", "signatureHelpProvider",
                   "executeCommandProvider", "definitionProvider", "documentHighlightProvider"):
        print("CAP-LOOKUP %s %s" % (wanted, advertised(wanted)))

    try:
        process.stdin.write(encode({"jsonrpc": "2.0", "id": 2, "method": "shutdown", "params": None}))
        process.stdin.write(encode({"jsonrpc": "2.0", "method": "exit"}))
        process.stdin.flush()
    except (BrokenPipeError, OSError):
        pass
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
    return 0


if __name__ == "__main__":
    sys.exit(main())
