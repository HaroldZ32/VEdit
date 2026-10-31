# Terminal panel (`src/terminal`, task T35)

A Lapce-style terminal panel: one **local** shell session on a real pty, docked as
the shell's `terminal` panel, with the process exit status always visible, and no
remote transport at all.

## Why not QTermWidget

The contract allows "QTermWidget 或等价". The pinned build image ships neither
`qtermwidget` nor `libvterm` (`evidence/term-panel/00-pins.txt`), and rebuilding
the image to add them would mean editing `scripts/` — outside this lane. QTermWidget
is also **GPL-2.0-or-later** while VEdit is MIT, so linking it into the product
would change the licence of the whole binary.

The panel therefore carries its own small equivalent, built only on the POSIX pty
API that glibc already provides (`posix_openpt`/`grantpt`/`unlockpt`/`ptsname`,
`fork`, `setsid`, `TIOCSCTTY`, `TIOCSWINSZ`) plus Qt6 Widgets. Nothing new is
linked, nothing in `scripts/` changed, and the licence stays MIT.

## Files

| file | role |
|---|---|
| `vt_screen.h/.cpp` | Qt-free VT100/xterm screen model: cell grid, scrollback ring, cursor, SGR/CSI/OSC parser, alt screen, cursor-position/device-attribute replies |
| `pty_session.h/.cpp` | one child process on a pty: non-blocking reads via `QSocketNotifier`, buffered writes, `TIOCSWINSZ` resize, `waitpid` reaping, exit code / termination signal |
| `terminal_widget.h/.cpp` | painting (256-colour palette, bold/italic/underline/strike/inverse, block cursor, selection), keyboard encoding, wheel scrollback, copy (Ctrl+Shift+C) / paste (Ctrl+Shift+V) |
| `terminal_panel.h/.cpp` | the panel: header (shell path, Restart/Clear/Close), exit banner, status line, factory entry points |
| `seam_check.cpp` | compile-only proof that `terminalPanelFactory()` fits `PanelRegistry::setFactory` |
| `terminal_selftest.cpp` | `--selfcheck` (headless, 101 checks) and `--screenshots <dir>` |

## Wiring it into the application shell

The shell inserts panels through `PanelRegistry::setFactory(id, factory)`
(`evidence/app-shell/06-seams.md` §2). Two lines are needed outside this lane —
one in the root `CMakeLists.txt`, one where the shell is created (`src/app/main.cpp`):

```cmake
# CMakeLists.txt
add_subdirectory(src/terminal)
```

```cpp
// src/app/main.cpp, after the shell exists
#include "terminal/terminal_panel.h"
...
shell->panels()->setFactory(QStringLiteral("terminal"),
                            vedit::terminal::terminalPanelFactory());
```

Both live in `src/app/` and the root build, which this lane must not write, so they
are handed to the assembly step. `seam_check.cpp` already proves the call
type-checks: the factory's type is exactly `std::function<QWidget *()>`, which is
`PanelSpec::factory`.

## Behaviour

* **Local only.** No socket, no listener, no SSH (product non-goal 4). The binary
  links Qt6 Core/Gui/Widgets — `readelf -d` shows no `libQt6Network`
  (`evidence/term-panel/04-elf-needed.txt`), and the self-check also passes inside
  `unshare -n` with no network namespace at all (`05-no-network.txt`).
* **Exit status visible.** `PtySession::exitDescription()` reports
  `exited with code N` / `killed by signal N (SIGNAME)`. The panel shows it twice:
  the status line (`Exited · code 42`) and the banner above the screen
  (`Shell exited with code 42. Press Enter or Restart to start a new session.`).
* **Dockable, closable, rebuildable.** The panel is a plain `QWidget`, so the shell
  owns the `QDockWidget`; Close hides the dock (the panel emits `closeRequested()`),
  Restart — or Enter on a finished session — tears the pty down and starts a new
  shell in the same panel, clearing the screen.
* **Terminal size follows the widget.** `TIOCSWINSZ` is updated on every resize
  (`stty size` inside the shell agrees; `panel.dock.pty-size-follows-widget`).
* **Emulated subset.** Printable UTF-8, CR/LF/BS/HT, CSI A B C D E F G H f d J K L
  M P X S T @ m n c h l r s u b, SGR 0–9/21–29/30–37/39/40–47/49/90–97/100–107,
  `38;5;n`, `38;2;r;g;b`, private modes 1/6/7/25/47/1047/1049/2004, OSC 0/2 titles,
  DSR/DA replies, alt screen, scrollback with a configurable ring size. Not
  implemented: mouse reporting, DEC line drawing (DEC special graphics), sixel,
  bidi, ligatures — none of them needed by a shell or by the panel's acceptance.

## Build and verify

Standalone (what `evidence/term-panel/` records, inside the pinned image):

```sh
cmake -S src/terminal -B target/term-build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build target/term-build --parallel "$(nproc)"
ctest --test-dir target/term-build --output-on-failure
```

The build is warnings-as-errors (`-Wall -Wextra -Wpedantic -Werror`), like the M0
smoke target and `src/app`. `ctest` runs `vedit-terminal-selftest --selfcheck`
under `QT_QPA_PLATFORM=offscreen`; the screenshots need a real platform
(`QT_QPA_PLATFORM=xcb` under Xvfb, `QT_OPENGL=software`):

```sh
DISPLAY=:78 QT_QPA_PLATFORM=xcb QT_OPENGL=software \
  target/term-build/vedit-terminal-selftest --screenshots evidence/term-panel
```
