#!/usr/bin/env python3
"""Minimal stdio LSP client for the T20 Metals spike (S4/S5).

Launches a language-server command, performs initialize/initialized, answers the
server's client-directed requests generically, records every JSON-RPC frame it
sees, and shuts the server down. The purpose is to observe what a REAL editor
session does offline - especially whether Metals' supported-Scala-version HTTP
scrape (which lives in the `--version` CLI path, `scala.meta.metals.Main`) is
ever reached - and to keep the transcript as evidence.

Usage:
  lsp_session.py --seconds 45 --root /tmp/ws --log /tmp/session.log -- <cmd...>
"""

import argparse
import json
import os
import subprocess
import sys
import time


def frame(obj):
    body = json.dumps(obj).encode("utf-8")
    return b"Content-Length: %d\r\n\r\n" % len(body) + body


class Reader:
    def __init__(self, stream):
        self.stream = stream
        self.buf = b""

    def next_message(self, timeout):
        # Parse one Content-Length framed JSON-RPC message, blocking up to timeout.
        deadline = time.monotonic() + timeout
        while True:
            if b"\r\n\r\n" in self.buf:
                head, rest = self.buf.split(b"\r\n\r\n", 1)
                length = None
                for line in head.split(b"\r\n"):
                    if line.lower().startswith(b"content-length:"):
                        length = int(line.split(b":", 1)[1].strip())
                if length is not None and len(rest) >= length:
                    body, self.buf = rest[:length], rest[length:]
                    return json.loads(body.decode("utf-8"))
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            import select

            ready, _, _ = select.select([self.stream], [], [], remaining)
            if not ready:
                return None
            chunk = os.read(self.stream.fileno(), 65536)
            if not chunk:
                return None
            self.buf += chunk


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--seconds", type=float, default=45.0)
    parser.add_argument("--root", required=True)
    parser.add_argument("--log", required=True)
    parser.add_argument("cmd", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.cmd
    if command and command[0] == "--":
        command = command[1:]

    root_uri = "file://" + os.path.abspath(args.root)
    log = open(args.log, "w", encoding="utf-8")

    def record(direction, message):
        log.write("%s %s\n" % (direction, json.dumps(message, ensure_ascii=False)))
        log.flush()

    stderr_path = args.log + ".stderr"
    stderr = open(stderr_path, "w", encoding="utf-8")
    proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=stderr, bufsize=0)
    reader = Reader(proc.stdout)

    def send(message):
        record("-->", message)
        proc.stdin.write(frame(message))
        proc.stdin.flush()

    send({
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {
            "processId": os.getpid(),
            "rootUri": root_uri,
            "workspaceFolders": [{"uri": root_uri, "name": "spike"}],
            "capabilities": {
                "workspace": {"configuration": True, "workspaceFolders": True},
                "window": {"workDoneProgress": True, "showMessage": {}},
                "textDocument": {"diagnostic": {}, "publishDiagnostics": {}},
            },
            "initializationOptions": {},
        },
    })

    initialized = False
    deadline = time.monotonic() + args.seconds
    while time.monotonic() < deadline:
        message = reader.next_message(min(2.0, max(0.0, deadline - time.monotonic())))
        if message is None:
            if proc.poll() is not None and not initialized:
                break
            continue
        record("<--", message)
        if message.get("id") == 1 and "result" in message and not initialized:
            send({"jsonrpc": "2.0", "method": "initialized", "params": {}})
            initialized = True
        elif "id" in message and "method" in message:
            method = message.get("method", "")
            if method == "workspace/configuration":
                items = message.get("params", {}).get("items", [])
                result = [None for _ in items]
            elif method == "window/showMessageRequest":
                result = None
            elif method == "window/showDocument":
                result = {"success": True}
            elif method == "workspace/applyEdit":
                result = {"applied": False}
            else:
                result = None
            send({"jsonrpc": "2.0", "id": message["id"], "result": result})

    try:
        send({"jsonrpc": "2.0", "id": 99, "method": "shutdown", "params": None})
        time.sleep(0.5)
        send({"jsonrpc": "2.0", "method": "exit", "params": None})
    except (BrokenPipeError, ValueError):
        pass
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=10)
    log.close()
    stderr.close()
    print("SESSION exit=%s initialized=%s log=%s stderr=%s"
          % (proc.returncode, initialized, args.log, stderr_path))
    return 0 if initialized else 2


if __name__ == "__main__":
    sys.exit(main())
