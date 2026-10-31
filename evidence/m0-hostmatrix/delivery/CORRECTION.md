# Correction — the `meta.txt` note is a stale hardcoded string from T12's script

The T12 runner (`tests/packaging/hostmatrix.sh`, **unmodified** per the T67 contract) writes a
hardcoded line into every `meta.txt`:

```
artifact_note: PROBE build (T11); delivery blocked pending T60 — mechanism verification only
```

For **this** run that line is wrong: the artifact is the T64/F2 **delivery** build
(self-built KSyntaxHighlighting with 0 embedded definitions, whitelist `SystemVerilog,Verilog`,
`verify-not-shipped` `pass=8 fail=0`, `DELIVERY_BLOCKED=no`). Each `meta.txt` therefore carries an
appended, authoritative line:

```
artifact_note_corrected: DELIVERY build (T64/F2) ...
```

The measured facts in the rest of `meta.txt` (glibc, host Qt count, window geometry, key=value
results, exit code) are unaffected. The T12 script was deliberately **not** edited — the contract
for T67 forbids it; the discrepancy is recorded here instead of silently rewriting evidence.
