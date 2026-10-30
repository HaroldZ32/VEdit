#!/usr/bin/env python3
"""Evidence-only path-rewriting proxy for the FIM real-endpoint test.

vd-fim's OpenAi backend posts to  POST <base>/v1/completions  with
{model,prompt,suffix,max_tokens}. DeepSeek's FIM API lives at
POST https://api.deepseek.com/beta/completions. This proxy accepts the former,
rewrites the path, adds the bearer header from the process environment and
returns the upstream response unchanged.

The key is read only from DEEPSEEK_API_KEY, never logged, never written.
"""
import json
import os
import sys
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

UPSTREAM = os.environ.get("FIM_UPSTREAM", "https://api.deepseek.com/beta/completions")
KEY = os.environ.get("DEEPSEEK_API_KEY", "")
DELAY_FIRST = float(os.environ.get("PROXY_DELAY_FIRST", "0"))
PORT_FILE = os.environ.get("PORT_FILE", "")

if not KEY:
    sys.stderr.write("proxy: DEEPSEEK_API_KEY is not set\n")
    sys.exit(2)

STATE = {"count": 0}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_args):
        return

    def do_POST(self):
        if self.path != "/v1/completions":
            self.send_response(404)
            self.end_headers()
            return
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        STATE["count"] += 1
        number = STATE["count"]
        try:
            request_keys = ",".join(json.loads(body).keys())
        except Exception:
            request_keys = "<non-json>"
        delayed = number == 1 and DELAY_FIRST > 0
        if delayed:
            time.sleep(DELAY_FIRST)
        request = urllib.request.Request(
            UPSTREAM,
            data=body,
            method="POST",
            headers={
                "Content-Type": "application/json",
                "Authorization": "Bearer " + KEY,
            },
        )
        try:
            with urllib.request.urlopen(request, timeout=90) as response:
                payload = response.read()
                status = response.status
        except urllib.error.HTTPError as error:
            payload = error.read()
            status = error.code
        except Exception:
            self.send_response(502)
            self.send_header("Content-Length", "0")
            self.end_headers()
            print(f"PROXY {number} upstream_error", flush=True)
            return
        try:
            keys = ",".join(json.loads(payload).keys())
        except Exception:
            keys = "<non-json>"
        print(
            f"PROXY {number} req_bytes={len(body)} req_keys={request_keys} "
            f"status={status} resp_bytes={len(payload)} resp_keys={keys} delayed={delayed}",
            flush=True,
        )
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
if PORT_FILE:
    with open(PORT_FILE, "w", encoding="utf-8") as handle:
        handle.write(str(server.server_port))
print(f"LISTENING {server.server_port}", flush=True)
server.serve_forever()
