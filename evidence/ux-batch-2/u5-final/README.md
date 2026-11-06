# U5 — packaging self-check covers all five syntax definitions (fail-closed)

Task: `CELL-20261105-UX-BATCH2-HANDOVER.md` §3.U5.
Write scope: `packaging/runtime/selfcheck.sh` + this directory.
Result: **done** — one file changed (`packaging/runtime/selfcheck.sh`).

## 1. Defect

`packaging/runtime/selfcheck.sh` asserted the loaded definitions with three
greps over the product's `syntax-product:` line — two XML file names plus one
boolean:

```bash
SYNTAX_LINE=$(printf '%s\n' "$OUT" | grep -m1 'syntax-product:')
if printf '%s' "$SYNTAX_LINE" | grep -q 'own-xml=.*vedit-verilog.xml' \
   && printf '%s' "$SYNTAX_LINE" | grep -q 'own-xml=.*vedit-systemverilog.xml' \
   && printf '%s' "$SYNTAX_LINE" | grep -q 'verilog=1'; then
  echo "CHECK PASS  our syntax definitions loaded (both XMLs, order-insensitive)"
```

The package ships **five** definitions — Verilog, SystemVerilog, VHDL, Markdown,
Tcl (`usr/share/vedit/ksh-whitelist.txt`, `resources/syntax/`) — so a package
that had lost VHDL, Markdown and Tcl still passed the self-check. The product's
own `--selfcheck` does not cover them either (`APP-SHELL-SELFTEST` /
`EDIT-CORE-SELFTEST` contain no `construct.vhdl|markdown|tcl` checks; the
highlight-mode construction suite is a separate ctest binary, not part of the
delivered runtime check).

## 2. Fix (`packaging/runtime/selfcheck.sh`, one block + the header line)

```bash
SYNTAX_DEFINITIONS=(vedit-verilog.xml vedit-systemverilog.xml vedit-vhdl.xml
                    vedit-markdown.xml vedit-tcl.xml)
# own-xml= lists what the loader really loaded, in QDirIterator order (not
# sorted), so membership is tested per entry instead of by surrounding text.
OWN_XML=$(printf '%s\n' "$SYNTAX_LINE" | sed -n 's/.*own-xml=\(\[[^]]*\]\).*/\1/p')
missing_syntax=""
for definition in "${SYNTAX_DEFINITIONS[@]}"; do
  printf '%s' "$OWN_XML" | grep -qE "(^|\[|,)${definition}(,|\])" \
    || missing_syntax="$missing_syntax $definition"
done
if [ -n "$missing_syntax" ]; then
  echo "CHECK FAIL  own XML definitions missing:${missing_syntax} (own-xml=${OWN_XML:-<none>})"
  fail=1
elif printf '%s' "$SYNTAX_LINE" | grep -q 'verilog=1' \
  && printf '%s' "$SYNTAX_LINE" | grep -q 'systemverilog=1'; then
  echo "CHECK PASS  our syntax definitions loaded (all five XMLs, order-insensitive)"
else
  echo "CHECK FAIL  all five XMLs loaded but the Verilog/SystemVerilog name lookup failed"
  fail=1
fi
```

Fail-closed: any definition absent from `own-xml=` prints the missing file names
and sets `fail=1`, so the script ends in `SELFCHECK FAIL; exit 1`. Order does not
matter (`own-xml=` is `QDirIterator` order, observed
`tcl, systemverilog, markdown, verilog` in the negative run). The header comment
line 8 was updated to say "ALL FIVE".

`packaging/runtime/AppRun` was **not** touched (mtime 2026-11-05 02:43, before
this session; sha256 `925ab1500e1504412a2cfa8a492d8140453f803dc3081702fa4412edb62406c3`).
The script only *executes* it.

## 3. Verification

### AppDir used (built from the current tree, not hand-made)

```
$ bash packaging/runtime/assemble-appdir.sh --out target/u5-appdir     (in the pinned image)
[assemble] active whitelist files: 5 (no syntax-bundled/)
APPDIR_WHITELIST=Markdown,SystemVerilog,Tcl,VHDL,Verilog
```

Content: `vedit-{markdown,systemverilog,tcl,verilog,vhdl}.xml`.
Provenance + AppRun hash: `00-appdir-provenance.log`.

### (1) All five present ⇒ the script PASSES — exit 0

```
$ bash packaging/runtime/selfcheck.sh --appdir target/u5-appdir
...
CHECK PASS  our syntax definitions loaded (all five XMLs, order-insensitive)
...
SELFCHECK PASS
POSITIVE exit=0
```

Full log: `01-all-five-present-passes.log` (every check PASS, incl.
`APP-SHELL-SELFTEST ok=1`, both `ksh_guard` locale cases, bundled toolchain,
fonts, packaged loader/libc in `/proc/<pid>/maps`).

### (2) One newly covered XML removed ⇒ the script FAILS — exit 1

Negative control on a **copy**, never on the working tree:

```
$ cp -a target/u5-appdir target/u5-appdir-neg
$ rm target/u5-appdir-neg/usr/share/org.kde.syntax-highlighting/syntax/vedit-vhdl.xml
$ printf 'Markdown,SystemVerilog,Tcl,Verilog\n' > .../usr/share/vedit/ksh-whitelist.txt
$ bash packaging/runtime/selfcheck.sh --appdir target/u5-appdir-neg
syntax-product: ... definitions=5 own-xml=[vedit-tcl.xml,vedit-systemverilog.xml,vedit-markdown.xml,vedit-verilog.xml] ...
app_exit=0
CHECK PASS  APP-SHELL-SELFTEST ok=1
CHECK FAIL  own XML definitions missing: vedit-vhdl.xml (own-xml=[vedit-tcl.xml,vedit-systemverilog.xml,vedit-markdown.xml,vedit-verilog.xml])
CHECK PASS  KTextEditor kernel
SELFCHECK FAIL
NEGATIVE exit=1
```

Full log: `02-missing-vhdl-fails.log`. The whitelist was aligned with the
directory so `AppRun`'s `ksh_guard` still passes and the product still starts
(`app_exit=0`, `APP-SHELL-SELFTEST ok=1`) — the failure is attributable **only**
to the new coverage check, not to the guard or a dead product.

### (3) The OLD check on the same negative AppDir ⇒ PASS (the defect, proven)

The same script with only the syntax block reverted to the two-XML version
(temporary copy, run then deleted; nothing left behind):

```
$ bash <copy-with-old-block> --appdir target/u5-appdir-neg
CHECK PASS  our syntax definitions loaded (both XMLs, order-insensitive)
SELFCHECK PASS
old-script exit=0
```

Full log: `03-old-two-xml-check-would-pass.log` — an AppDir without VHDL passed
the previous self-check, which is exactly the reported defect.

## 4. Summary

| AppDir | check | result |
|---|---|---|
| five definitions | new (five XMLs) | `CHECK PASS` / `SELFCHECK PASS` / exit **0** |
| four definitions (VHDL removed) | new (five XMLs) | `CHECK FAIL … missing: vedit-vhdl.xml` / exit **1** |
| four definitions (VHDL removed) | old (two XMLs) | `CHECK PASS` / exit **0** (defect) |

## 5. Notes

- `assemble-appdir.sh` already refuses to build a package that lacks any of the
  five (`REQUIRED_SYNTAX`), so this self-check is the runtime defence for an
  AppDir that is damaged in transit, hand-edited, or assembled by another path —
  exactly the case the old two-grep check waved through.
- Nothing in the working tree was broken for the negative control: it lives in
  `target/u5-appdir-neg/` (build output). Reproduce with the three commands in
  §3(2).
- `bash -n packaging/runtime/selfcheck.sh` → OK (no output).
