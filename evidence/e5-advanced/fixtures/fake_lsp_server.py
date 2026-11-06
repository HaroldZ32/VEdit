#!/usr/bin/env python3
"""A minimal LSP server used ONLY as a protocol fixture for the T16 evidence.

WHY THIS EXISTS: AC-3.5 requires hover to make a REAL `textDocument/hover`
request and render the answer whenever the backend's `initialize` result
advertises `hoverProvider: true`. No backend in this workspace can do that:

  * `verible-verilog-ls` advertises `hoverProvider: false` (measured), so it is
    the "greyed with its real reason" side of the rule;
  * `slang-server` 0.3.1 needs GLIBC >= 2.27 (docs/BUILD.md §13) and cannot run
    in the pinned CentOS-7-compatible image.

This fixture speaks the wire protocol for real: the product's vd-lsp client
spawns it, sends `initialize` / `textDocument/hover` / `textDocument/signatureHelp`
and parses the answers. That proves the client request path, the C ABI mapping and
the rendering. It does NOT claim slang hover works - the report says exactly that.

Every received request method is appended to $FAKE_LSP_LOG (when set), so the
evidence log shows the product really asked.
"""

import json
import os
import sys

LOG_PATH = os.environ.get("FAKE_LSP_LOG", "")


def log(line):
    if not LOG_PATH:
        return
    with open(LOG_PATH, "a", encoding="utf-8") as handle:
        handle.write(line + "\n")


def read_message():
    headers = {}
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            return None
        if line in (b"\r\n", b"\n"):
            break
        key, _, value = line.decode("utf-8", "replace").partition(":")
        headers[key.strip().lower()] = value.strip()
    length = int(headers.get("content-length", "0"))
    if length <= 0:
        return None
    return json.loads(sys.stdin.buffer.read(length).decode("utf-8"))


def send(payload):
    body = json.dumps(payload).encode("utf-8")
    sys.stdout.buffer.write(b"Content-Length: %d\r\n\r\n" % len(body))
    sys.stdout.buffer.write(body)
    sys.stdout.buffer.flush()


def main():
    hovers = 0
    signatures = 0
    log("fake-lsp: started")
    while True:
        message = read_message()
        if message is None:
            break
        method = message.get("method")
        request_id = message.get("id")
        if method:
            log("fake-lsp: method=%s id=%s" % (method, request_id))
        if method == "initialize":
            send(
                {
                    "jsonrpc": "2.0",
                    "id": request_id,
                    "result": {
                        "capabilities": {
                            "textDocumentSync": 1,
                            "hoverProvider": True,
                            "signatureHelpProvider": {"triggerCharacters": ["(", ","]},
                            "definitionProvider": True,
                            "referencesProvider": True,
                            "documentSymbolProvider": True,
                        },
                        "serverInfo": {"name": "vedit-fake-lsp", "version": "1.0"},
                    },
                }
            )
        elif method == "textDocument/hover":
            hovers += 1
            send(
                {
                    "jsonrpc": "2.0",
                    "id": request_id,
                    "result": {
                        "contents": {
                            "kind": "markdown",
                            "value": "**fake hover**\n\nlogic [7:0] data\n\n(hover #%d)" % hovers,
                        },
                        "range": {
                            "start": {"line": 0, "character": 0},
                            "end": {"line": 0, "character": 5},
                        },
                    },
                }
            )
        elif method == "textDocument/signatureHelp":
            signatures += 1
            send(
                {
                    "jsonrpc": "2.0",
                    "id": request_id,
                    "result": {
                        "signatures": [
                            {
                                "label": "foo(int a, int b)",
                                "parameters": [{"label": "int a"}, {"label": "int b"}],
                            }
                        ],
                        "activeSignature": 0,
                        "activeParameter": 1,
                    },
                }
            )
        elif request_id is not None:
            # Every other request (documentSymbol, shutdown, ...) gets an empty
            # answer; the fixture only has to keep the session alive.
            send({"jsonrpc": "2.0", "id": request_id, "result": None})
        if method == "exit":
            break
    log("fake-lsp: exiting")


if __name__ == "__main__":
    main()
