# AC-14 (FIM real endpoint) — real-network proof via a path-rewriting proxy

Owner: Deep Worker · date 2026-10-30 · verdict: **AUTO (with an explicit boundary)**.
Scope: prove that `vd-fim`'s real `HttpTransport` implements the OpenAI-compatible
FIM shape end-to-end against the real DeepSeek FIM service. **No product code was
changed**; the adaptation lives entirely in this evidence directory.

## Protocol facts (checked against the official docs)

- Base URL: `https://api.deepseek.com/beta` (the docs say the `/beta` base must be
  used for FIM), operation `POST /completions`.
- Request body: `model` (required), `prompt` (required), `suffix`, `max_tokens`,
  plus optional `echo`/`logprobs`; supported models: `deepseek-flash`,
  `deepseek-v4-pro`.
- Auth: `Authorization: Bearer <key>`.
- Response: `choices[0].text` — the shape vd-fim already parses.

`vd-fim`'s `OpenAi` backend posts `POST <base>/v1/completions` with
`{model,prompt,suffix,max_tokens}`. The only difference is the **path** and the
auth header location: the key belongs to the proxy, not to vd-fim.

## How the test works

```
harness (real vd-fim HttpTransport)
   -> POST http://127.0.0.1:<local-port>/v1/completions   (vd-fim's shape)
   -> proxy.py: rewrite path to /beta/completions, add Authorization: Bearer
   -> POST https://api.deepseek.com/beta/completions      (real TLS, real service)
   -> response passed back unchanged; vd-fim parses choices[0].text
```

Files: `harness/` (standalone Rust crate, path-dependency on `crates/vd-fim`,
its own `[workspace]` so the product workspace is untouched), `proxy.py`,
`run.sh`, sanitized logs under `results/`.

## Results (real network, 2026-10-30)

| case | result | observed |
|---|---|---|
| positive | **PASS** | non-empty completion `text_len=5` (`1'b1;`), `latency_ms=3572`, model `deepseek-flash`; upstream `HTTP 200`, response keys `id,choices,created,model,system_fingerprint,object,usage` |
| cancel (proxy delays the first upstream reply by 3 s; `start` → `cancel`, then a fresh request) | **PASS** | `stale_delivered=false`, `fresh_ok=true` (`fresh_text_len=9`): the late reply of the cancelled generation was dropped and a newer result still arrived |
| degradation (closed port) | **PASS** | `Failed` reason `FIM 连接被拒绝`, 9 chars ≤ 80, `contains_url_or_header=false`, 50 ms |

Raw sanitized logs: `results/{positive,cancel,degraded}.log`,
`results/proxy-{positive,cancel}.log` (request/response keys and sizes only; the
local port is a placeholder).

## Key discipline (self-checked)

- The key is read at runtime from `~/.dsh/.credentials.yaml` into a shell
  variable, passed to `proxy.py` **through the environment only** (never on the
  command line, never written to disk).
- The scan asserts, without printing the literal:
  `key_present=true`, `key_length=35`, `key_occurrences_in_evidence_tree=0`,
  `key_occurrences_in_proxy_logs=0`, `key_in_proxy_cmdline=0`,
  `auth_header=Bearer <redacted>` (`results/key-scan.txt`).
- No token appears in this directory, in the proxy logs, or in the harness logs.

## Boundary statement (required, do not drop)

**Validated: vd-fim's protocol implementation and the OpenAI-compatible FIM
shape. DeepSeek's official FIM lives at `/beta/completions`; the test reaches it
through an evidence-only local path-rewriting proxy. The product itself does not
build in that path rewrite**, so out of the box VEdit still sends
`/v1/completions` and would need a proxy (or a product change) to talk to
DeepSeek's FIM endpoint directly. This evidence does not claim that DeepSeek FIM
works without the proxy, nor does it make any statement about other providers.

## Reproduce

```bash
bash evidence/fim/real-endpoint/run.sh   # requires the DeepSeek key in ~/.dsh/.credentials.yaml
```

The script rebuilds the evidence harness in the pinned image, runs the three
cases against the real service, and finishes with the key-leak scan.
