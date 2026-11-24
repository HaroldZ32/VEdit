# Duplicate module fixture (batch-20261118 B)

Two files declare the SAME module name `uart` with DIFFERENT ports. `slang`
12.1.0 reports `error: duplicate definition of 'uart'`, exits **1**, and still
writes a complete, parseable AST JSON — that is the case B-1…B-5 are about.

## What is captured here

* `ast.json` — the real `slang --ast-json --ast-json-source-info` document of
  `project/rtl/uart.v` + `project/sim/uart.v`, in that order. Its shape is the
  awkward part: the top-level `definitions` array carries BOTH declarations with
  their own `source_file`, while the design tree (and therefore the only
  `InstanceBody`) belongs to `sim/uart.v` — the file slang processed LAST.
* `slang-stderr.txt` — the child's stderr (the duplicate diagnostic and the
  "previous definition here" note).

## Capture command

```sh
cd crates/vd-deeplint/tests/fixtures/duplicate-module/project
<repo>/build/slang/out/slang --ast-json ../ast.json --ast-json-source-info \
    rtl/uart.v sim/uart.v 2> ../slang-stderr.txt
echo $?   # 1
```

The committed `ast.json` has the shape this command produces (both files spelled
relative to the project root, `rtl/uart.v` first). slang's AST `addr` numbers differ
per run; the tests never assert them, they assert the ADOPTED file position and the
warning text.

The capture is committed so the assertion of B-1/B-2 does not need the tool; the
`the_real_slang_binary_resolves_a_duplicate_module_deterministically` test
re-runs the tool over `project/` for the same claim against the real binary.
