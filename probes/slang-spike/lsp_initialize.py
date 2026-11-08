#!/usr/bin/env python3
"""Drive one real `initialize` handshake against a language server over stdio.

The server may run on the host or inside a container: this script is the CLIENT
(pure stdio), so `docker run -i … slang-server` can be the server process while
the probe stays on the host.

It prints the JSON-RPC requests and responses verbatim, plus the wall time of
the handshake, and exits non-zero when the server never answers.

Usage:
  lsp_initialize.py --server <path> --root <dir> [--wrapper "docker run -i …"]
"""
import argparse
import json
import subprocess
import sys
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
    length = int(headers.get("content-length", "0"))
    return json.loads(stream.read(length))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--root", default="/tmp")
    parser.add_argument(
        "--root-uri",
        default="",
        help="workspace URI as the SERVER sees it (e.g. file:///fixture inside a container)",
    )
    parser.add_argument("--wrapper", default="")
    parser.add_argument("--timeout", type=float, default=20.0)
    args = parser.parse_args()

    if args.wrapper:
        command = args.wrapper.split() + [args.server]
    else:
        command = [args.server]
    root_uri = args.root_uri or ("file://" + args.root)

    started = time.monotonic()
    process = subprocess.Popen(
        command,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    initialize = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {
            "processId": None,
            "clientInfo": {"name": "vedit-spike", "version": "0"},
            "rootUri": root_uri,
            "capabilities": {},
            "workspaceFolders": None,
        },
    }
    print("--> " + json.dumps(initialize))
    process.stdin.write(encode(initialize))
    process.stdin.flush()

    # The server sends its own `slang/setConfig` notification BEFORE answering;
    # skip notifications until the response with our id arrives.
    notifications = []
    response = None
    while True:
        incoming = read_message(process.stdout)
        if incoming is None:
            break
        if incoming.get("id") == 1:
            response = incoming
            break
        notifications.append(incoming)
        print("<-- notification " + json.dumps(incoming)[:3000])
    elapsed = time.monotonic() - started
    if response is None:
        error = process.stderr.read().decode(errors="replace")
        print("<-- NO RESPONSE", file=sys.stderr)
        print(error, file=sys.stderr)
        process.kill()
        raise SystemExit(1)
    print("<-- " + json.dumps(response, indent=1))

    result = response.get("result") or {}
    capabilities = result.get("capabilities") or {}
    print("handshake_seconds=%.3f" % elapsed)
    print("serverInfo=" + json.dumps(result.get("serverInfo")))
    print("capabilities=" + json.dumps(sorted(capabilities.keys())))
    print("notifications_before_response=" + json.dumps([n.get("method") for n in notifications]))

    initialized = {"jsonrpc": "2.0", "method": "initialized", "params": {}}
    process.stdin.write(encode(initialized))
    process.stdin.flush()

    shutdown = {"jsonrpc": "2.0", "id": 2, "method": "shutdown", "params": None}
    process.stdin.write(encode(shutdown))
    process.stdin.flush()
    shutdown_response = read_message(process.stdout)
    print("<-- " + json.dumps(shutdown_response))
    process.stdin.write(encode({"jsonrpc": "2.0", "method": "exit"}))
    process.stdin.flush()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
    if not capabilities:
        raise SystemExit("the server answered initialize without capabilities")
    raise SystemExit(0)


if __name__ == "__main__":
    main()
