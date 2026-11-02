# T137 — real file-open entry points (positional, File → Open, desktop MIME)

## What was missing

`main.cpp` had **no `addPositionalArgument`** and the shell had **no File → Open**
command, so a file could only be opened by double-clicking the file tree. This
also meant every headless “open file” check called `pane->openFile`/the C ABI
instead of a user path.

## Implemented

- **Positional arguments**: `vedit-app <file>...` and
  `vedit-app --project <dir> <file>...`. All paths are validated first; a
  missing path or a directory prints one readable line to stderr and exits **2**
  without constructing the window. Valid files open through the installed
  editor provider; the first file uses the active pane, each additional file
  is opened in a **new horizontal split** (active = last).
- **File → Open…**: `file.open`, **Ctrl+O** (no existing binding; the global
  shortcut scan shows no duplicates). The handler calls
  `QFileDialog::getOpenFileName` with a Verilog/SystemVerilog filter and then the
  same `ApplicationShell::openPaths()` used by positional arguments. The dialog
  path is also exercisable headlessly through `setOpenFileDialogProvider()` (test
  seam; default is the real QFileDialog).
- **Desktop/MIME**: `packaging/runtime/assets/vedit.desktop` now declares
  `MimeType=text/x-svsrc;text/x-verilog;text/x-systemverilog;text/x-verilog-src;text/x-systemverilog-src;text/verilog;`
  (the host maps `.sv` to `text/x-svsrc`, `.v` to `text/x-verilog`) and keeps
  `Exec=AppRun %F`, which forwards file arguments to the new positional path.
- **Open Recent** was left out (optional in the brief); no state format change.

## Verification path (the important sentence)

**All acceptance checks below were run through the packaged binary's real entry
point** — a test AppImage built from the current AppDir, executed with
`APPIMAGE_EXTRACT_AND_RUN=1 /test.AppImage <file>` — not through
`pane->openFile` or the C ABI. The test image is
`target/t137-image/VEdit-0.1.1-x86_64.AppImage` (110,447,096 B, sha256
`4016d70f…`), explicitly **not the delivery AppImage**.

## Results

`02-packaged.log` (stock centos:7, packaged test image):

```
APPIMAGE_EXTRACT_AND_RUN=1 ... /test.AppImage /fixtures/hello.sv --hold 600
open.paths: opened 1 file(s): /fixtures/hello.sv          exit 0

... /test.AppImage --project /fixtures /fixtures/second.v --hold 600
open.paths: opened 1 file(s): /fixtures/second.v          exit 0

... /test.AppImage /no-such-t137.sv
vedit-app: cannot open /no-such-t137.sv: no such file or directory   exit 2

... /test.AppImage --selfcheck
APP-SHELL-SELFTEST ok=1 checks=245 failures=0
```

`01-cli.log` shows the same paths on the unpackaged product binary, including
the two-file invocation (`open.paths: opened 2 file(s)`).

`--selfcheck` also asserts:
`open.command.exists`, `open.command.in-file-menu`,
`open.command.shortcut-ctrl-o`, `open.missing.rejected`,
`open.multiple.pane-count`, `open.multiple.paths`,
`open.dialog.action-opens`, plus the existing T129/T130 close/sync checks.

Desktop file inside the test AppImage (extracted) carries the MIME list and
`Exec=AppRun %F` (`02-packaged.log`).

**Double-click caveat**: the AppImage now advertises the MIME association and the
binary accepts forwarded paths; an actual file-manager double-click was **not**
executed in this headless container (no desktop session). The association itself
is present and `.sv`/`.v` map to the advertised types on the host.

## Gates

- root ctest: **31/31 passed**, `CTEST-EXIT=0`.
- `bash build/gates/typecheck.sh`: **PASS**, `TYPECHECK-EXIT=0`.
- No `src/workspace/diagnostics/**` or `src/syntax/**` changes; no delivery
  AppImage rebuild (the test image lives under `target/`).

## Files

`src/app/main.cpp` (positional parser + validation + open after session),
`src/app/app_shell.{h,cpp}` (openPaths, File→Open command, dialog provider),
`src/app/editor_area.{h,cpp}` (reuse open/close APIs), `packaging/runtime/assets/vedit.desktop`
(MIME), `evidence/file-open/**`.
