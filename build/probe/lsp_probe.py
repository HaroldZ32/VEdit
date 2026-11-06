#!/usr/bin/env python3
"""Capability + behaviour probe for a language server, over raw stdio JSON-RPC."""
import json, os, select, subprocess, sys, time

ROOT = os.path.dirname(os.path.abspath(__file__)) + "/slang"
BIN = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "..", "..", "..", "packaging/runtime/out/AppDir/bin/verible-verilog-ls")
BIN = os.path.abspath(BIN)

class Client:
    def __init__(self, binary, root):
        env = {k: v for k, v in os.environ.items() if k != "LD_LIBRARY_PATH"}
        self.p = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, env=env, cwd=root)
        self.buf = b""
        self.notes = []
    def send(self, obj):
        data = json.dumps(obj).encode()
        self.p.stdin.write(b"Content-Length: %d\r\n\r\n" % len(data) + data)
        self.p.stdin.flush()
    def _read_one(self, timeout):
        end = time.time() + timeout
        while time.time() < end:
            if b"\r\n\r\n" in self.buf:
                head, rest = self.buf.split(b"\r\n\r\n", 1)
                n = 0
                for line in head.split(b"\r\n"):
                    if line.lower().startswith(b"content-length:"):
                        n = int(line.split(b":", 1)[1])
                if len(rest) >= n:
                    msg = json.loads(rest[:n].decode("utf-8", "replace"))
                    self.buf = rest[n:]
                    return msg
            r, _, _ = select.select([self.p.stdout], [], [], 0.2)
            if r:
                chunk = os.read(self.p.stdout.fileno(), 1 << 16)
                if not chunk:
                    return None
                self.buf += chunk
        return None
    def wait_id(self, ident, timeout=10.0):
        end = time.time() + timeout
        while time.time() < end:
            msg = self._read_one(max(0.1, end - time.time()))
            if msg is None:
                return None
            if msg.get("id") == ident and ("result" in msg or "error" in msg):
                return msg
            self.notes.append(msg.get("method") or ("id=" + str(msg.get("id"))))
        return None

def summarize(msg):
    if msg is None:
        return "NO RESPONSE (timeout)"
    if "error" in msg:
        return "ERROR " + json.dumps(msg["error"])
    return "RESULT " + json.dumps(msg["result"])[:600]

def main():
    c = Client(BIN, ROOT)
    c.send({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
        "processId": None, "rootUri": "file://" + ROOT,
        "capabilities": {"textDocument": {"rename": {"prepareSupport": True}}},
        "workspaceFolders": [{"uri": "file://" + ROOT, "name": "probe"}]}})
    init = c.wait_id(1, 20)
    print("== initialize:", "ok" if init and "result" in init else summarize(init))
    caps = ((init or {}).get("result") or {}).get("capabilities") or {}
    for k in ("textDocumentSync", "renameProvider", "prepareProvider", "codeActionProvider",
              "hoverProvider", "definitionProvider", "referencesProvider",
              "documentSymbolProvider", "documentFormattingProvider",
              "documentRangeFormattingProvider"):
        if k in caps or k in ("renameProvider", "codeActionProvider", "hoverProvider"):
            print("   %-30s %s" % (k, json.dumps(caps.get(k))))
    c.send({"jsonrpc": "2.0", "method": "initialized", "params": {}})
    path = os.path.join(ROOT, "top.sv")
    uri = "file://" + path
    text = open(path).read()
    c.send({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {
        "uri": uri, "languageId": "systemverilog", "version": 1, "text": text}}})
    time.sleep(1.5)
    td = {"textDocument": {"uri": uri}}
    cases = [
        ("definition on instance 'leaf'", 2, {"method": "textDocument/definition", "params": {**td, "position": {"line": 11, "character": 2}}}),
        ("references on 'w'", 3, {"method": "textDocument/references", "params": {**td, "position": {"line": 13, "character": 13}, "context": {"includeDeclaration": True}}}),
        ("hover on 'leaf'", 4, {"method": "textDocument/hover", "params": {**td, "position": {"line": 11, "character": 2}}}),
        ("action: symbol on 'top'", 5, {"method": "textDocument/documentSymbol", "params": td}),
        ("action: prepareRename 'w'", 6, {"method": "textDocument/prepareRename", "params": {**td, "position": {"line": 13, "character": 13}}}),
        ("action: rename 'w' -> w2", 7, {"method": "textDocument/rename", "params": {**td, "position": {"line": 13, "character": 13}, "newName": "w2"}}),
        ("action: codeAction line 12", 8, {"method": "textDocument/codeAction", "params": {**td, "range": {"start": {"line": 11, "character": 0}, "end": {"line": 11, "character": 30}}, "context": {"diagnostics": []}}}),
        ("action: formatting", 9, {"method": "textDocument/formatting", "params": {**td, "options": {"tabSize": 2, "insertSpaces": True}}}),
    ]
    for label, ident, req in cases:
        req["jsonrpc"] = "2.0"
        req["id"] = ident
        c.send(req)
        print("== %-34s %s" % (label, summarize(c.wait_id(ident, 12))))
    try:
        c.send({"jsonrpc": "2.0", "id": 99, "method": "shutdown", "params": None})
        c.wait_id(99, 5)
        c.send({"jsonrpc": "2.0", "method": "exit", "params": None})
    except Exception:
        pass
    time.sleep(0.3)
    err = c.p.stderr.read(4000).decode("utf-8", "replace") if c.p.poll() is not None else ""
    if err.strip():
        print("== server stderr (tail) ==")
        print(err.strip()[-1500:])
    print("== notifications seen:", sorted(set(c.notes)))

main()
