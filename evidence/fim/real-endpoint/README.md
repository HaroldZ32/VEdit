# AC-14 (FIM real endpoint) — automated-proof attempt and negative evidence

Owner: Deep Worker · date 2026-10-30 · status: **no local endpoint; AC-14 stays MANUAL**.
Scope: find a local completion endpoint exposed by the running DSH harness, then
drive the real `vd-fim` `HttpTransport` through positive / cancellation /
degradation cases. No token or key was read, printed or stored; every concrete
port/URL in this directory is replaced by a placeholder.

## 1) What was probed (item by item)

### 1.1 Harness environment (`$DSH_*`)

```
$ env | grep -i '^DSH' | sed -E 's/=.*//'
DSH_HOME         SET len=21
DSH_PROFILE      SET len=7
DSH_PROFILE_DIR  SET len=38
DSH_SESSION_ID   SET len=36
DSH_SHELL        SET len=1

$ env | grep -iE 'API_KEY|BASE_URL|API_BASE|OPENAI|DEEPSEEK|OLLAMA|LLM' | sed -E 's/=.*//'
(no output)
```

**Negative evidence, stated as a conclusion:** the harness exports **no API base
URL, no port and no token** to this session — only filesystem/profile facts. This
is evidence that *the local DSH does not expose a FIM-shaped completion endpoint
to the editor*, not merely that "we did not find one".

### 1.2 Every local listener, classified

| class | endpoint (placeholder) | requests sent | observed response (status + key headers/body, sanitized) | verdict |
|---|---|---|---|---|
| HTTP outbound proxy ×3 | `127.0.0.1:<proxy-port-1..3>` | `GET /`; `GET /v1/models`; `POST /v1/completions`; `POST /v1/chat/completions`; `POST /infill`; h2c preface | `400 Bad Request`, `Connection: close`, **`Proxy-Connection: close`**, `Content-Length: 0`; h2c → peer sends non-SETTINGS data | not an API; origin-form is rejected by a forward-proxy parser |
| `dsh web` UI | `127.0.0.1:<dsh-web-port>` | `GET /`; `GET /v1/models`; the three POST paths | `401 Unauthorized`, body `dsh web authentication required; reopen the URL printed by dsh web.`; `/v1/models` → 404; POST paths → 405 | agent-chat UI, token-gated; **not** a completion endpoint; token was not read or used |
| unrelated SPA | `127.0.0.1:<spa-port>` | `GET /`; `/api/health`, `/api/models`, `/api/v1/models`, `/openapi.json`, `/docs`, `/health`, `/v1/models`, `/api/sessions` | `/` → `200 text/html` (894 B); every other path → `404`, body `404 page not found` | unrelated local web app |
| small dev servers ×3 | `127.0.0.1:<dev-port-1..3>` | `GET /`; `/mcp`, `/sse`, `/health`, `/v1/models` | `404` on every request | neither MCP-over-HTTP nor a model server |
| local model defaults | `127.0.0.1:11434` (Ollama), `:8080`, `:8000`, `:5000`, `:1234` | `GET /v1/models` | connection refused | no local model server |

The full command-level log (with the same placeholders) is `probe.txt`.

### 1.3 Protocols `vd-fim` can speak (and why none matched)

`crates/vd-fim` implements exactly two shapes:

- **OpenAI-compatible FIM:** `POST <base>/v1/completions` with `prompt` + `suffix`;
- **llama.cpp:** `POST <base>/infill` with `input_prefix` + `input_suffix`.

No discovered listener returned a model list or a completion for either shape, so
the positive and cancellation cases have no object to run against.

## 2) Result

| endpoint | protocol | model | positive completion | cancel | degradation | evidence |
|---|---|---|---|---|---|---|
| `127.0.0.1:<local listeners>` | none of the two supported shapes | — | not possible | not run | dead-port short `Failed` is covered by the existing vd-fim tests; not re-run against a real endpoint | `probe.txt` |

**Conclusion: the local harness environment has no FIM-compatible endpoint;
AC-14 remains MANUAL.** Only the degradation path is machine-checkable today,
and the existing vd-fim suites already cover it. No test that could only skip was
added — a skipped test is not a verification.

**This probe is a local harness-environment observation, not a public service,
and does not constitute an endorsement of any provider's FIM compatibility.**

## 3) What would make AC-14 automatable

An operator-supplied endpoint of either shape above (optionally with a key
injected via the environment, never written to disk). Then a test under
`crates/vd-fim/tests/` can run the three cases with the real `HttpTransport`:
positive (non-empty text + latency), cancel (`start` → `cancel`, a late reply is
dropped by generation mismatch and cannot overwrite a newer result), and
degradation (closed port → `Failed` with a ≤80-character reason containing no
key, header or full URL). That is a source change and would need a new freeze.
