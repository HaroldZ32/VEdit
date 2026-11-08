# `.flist` is not a filelist — one source for the extension set

Batch `ux-batch-2`, leftover lane `leftovers-flist`. The open planner
(`src/workspace/project/project_filelist.cpp`) had its own extension list that
accepted `.flist`; the file dialog (`src/common/file_dialog_filters.h`) advertises
`*.f *.F` only, and the Rust parser accepts extension `f` only
(`crates/vd-config/src/project.rs` → `extension.eq_ignore_ascii_case("f")`).
Three answers to one question. The planner now consumes the shared answer.

## 1. The shared source we now consume

`src/common/file_dialog_filters.h` (RM-16) — the same header the open and
import dialogs already use:

```cpp
inline QStringList filelistExtensions() { return QStringList{QStringLiteral("f")}; }
inline QStringList filelistGlobs()      { return QStringList{QStringLiteral("*.f"), QStringLiteral("*.F")}; }
inline bool isFilelistPath(const QString &path);   // extension `f`, case-insensitive
```

The planner calls `vedit::common::isFilelistPath(path)`. No include-path change
was needed: `src/workspace/project/CMakeLists.txt:93-95` already carries
`${CMAKE_CURRENT_SOURCE_DIR}/../../common` (verified, unchanged).

## 2. Planner: before / after

Before (`project_filelist.cpp`) — a private list reproducing the guess:

```cpp
const QString suffix = info.suffix();
if (suffix.compare(QStringLiteral("f"), Qt::CaseInsensitive) != 0
    && suffix.compare(QStringLiteral("flist"), Qt::CaseInsensitive) != 0) {
    return plan;
}
```

After:

```cpp
if (!vedit::common::isFilelistPath(path)) {
    return plan;
}
```

Behaviour: `.flist` is **not** planned as a filelist (no root taken, no import);
`.f`, `.F` still are, unchanged (`/p/design.f` → root `/p`; `/p/DESIGN.F` with a
project open → import into the current root).

`project_filelist.h` carried two comments claiming `.flist` is a filelist
(`FIX 2: opening a .f / .F / .flist filelist…` and the `isFilelist` field). Both
were corrected to `.f` / `.F` plus the ruling; comment-only, no behaviour change.

## 3. The inverted assertion

`src/workspace/project/project_selftest.cpp`, section 6c — case renamed
`filelist.plan.flist` → `filelist.plan.flist-not-a-filelist` so the log line
cannot be misread as the old claim:

```cpp
// Ruling: `.flist` is not a filelist extension; only `f` case-insensitive,
// decided by the parser (crates/vd-config/src/project.rs). The planner
// takes the set from src/common/file_dialog_filters.h, the same source
// the file dialogs offer, so the long spelling plans as a plain document.
const auto flist =
    vedit::workspace::project::planOpenedFilelist(QStringLiteral("/p/run.flist"), true);
check("filelist.plan.flist-not-a-filelist",
      !flist.isFilelist && !flist.needsRoot && flist.absolutePath.isEmpty());
```

The positive cases `filelist.plan.lowercase-f` (`/p/design.f`, no project →
root `/p`) and `filelist.plan.uppercase-f` (`/p/DESIGN.F`, project open → import,
no root) are untouched.

One more assertion rested on the same premise: the end-to-end check
`filelist-open.second-filelist-kept` used `/lists/extra.flist` as its *second
filelist*. Its assertion is unchanged and not weakened (a second filelist in
another directory is imported into the CURRENT root, the root does not change,
both filelists are kept); only the fixture spelling became a real filelist,
`extra.F` — which additionally exercises the upper-case spelling end to end.
The run below shows that case failing on the old fixture and passing on the
corrected one.

## 4. Doc diff (`docs/USER_GUIDE.md`)

Three sentences, surrounding wording kept:

```diff
-… 过滤器只列 `*.f` / `*.F`；`.flist` 长拼写不在过滤器里，但仍可通过 `All Files (*)` 选中并打开，导入行为相同。
+… 过滤器只列 `*.f` / `*.F`；`.flist` 长拼写不在过滤器里，可通过 `All Files (*)` 当作普通文本文件打开，但**不会**按 filelist 导入。

-打开一个 `.f` / `.F`（`.flist` 长拼写用对话框的 `All Files (*)` 选中）：
+打开一个 `.f` / `.F`（`.flist` 长拼写只能用对话框的 `All Files (*)` 当普通文本打开，不会按 filelist 导入）：

-- `*.f` / `*.F`：filelist（对话框默认只筛这两种；`.flist` 拼写通过 `All Files (*)` 打开后仍按 filelist 导入）；
+- `*.f` / `*.F`：filelist（对话框只筛这两种；`.flist` 拼写可用 `All Files (*)` 当普通文本打开，但**不**按 filelist 导入）；
```

## 5. Observed output (image `vedit-qt6-build:local`, workspace at `/vedit-build`, tree `target/qt6-build`)

Build (no include-path change needed; `-Werror` clean):

```console
$ ninja -C target/qt6-build vedit-workspace-project-selftest vedit-file-dialog-filters-selftest
[44/57] Building CXX object src/app/workspace-lane-project/CMakeFiles/vedit_workspace_project.dir/project_filelist.cpp.o
[50/57] Linking CXX executable src/app/vedit-file-dialog-filters-selftest
[57/57] Linking CXX executable src/app/workspace-lane-project/vedit-workspace-project-selftest
```

First run, before the e2e fixture was corrected — the inverted unit case already
passed, the stale `.flist` premise in the e2e block failed (console excerpt; that
log was overwritten by the green rerun):

```console
CHECK filelist.plan.flist-not-a-filelist PASS
CHECK filelist-open.second-filelist-kept FAIL
PROJECT-MODEL-SELFTEST ok=0 checks=153 failures=1
50% tests passed, 1 tests failed out of 2
```

After the fixture correction:

```console
$ target/qt6-build/src/app/workspace-lane-project/vedit-workspace-project-selftest --selfcheck tests/fixtures/verilog-project
CHECK filelist.plan.lowercase-f PASS
CHECK filelist.plan.uppercase-f PASS
CHECK filelist.plan.flist-not-a-filelist PASS
CHECK filelist.plan.non-filelist PASS
CHECK filelist.plan.empty PASS
CHECK filelist-open.second-filelist-kept PASS
PROJECT-MODEL-SELFTEST ok=1 checks=153 failures=0
```

```console
$ target/qt6-build/src/app/vedit-file-dialog-filters-selftest
CHECK PASS shared.extensions-are-f  f
CHECK PASS shared.globs-f-and-F  *.f,*.F
CHECK PASS shared.not-filelist-flist
VE-FILE-DIALOG-FILTERS-SELFTEST checks=46 failures=0
```

`ctest -R "vedit-workspace-project-selftest|vedit-file-dialog-filters-selftest"`:

```console
1/2 Test #14: vedit-file-dialog-filters-selftest ...   Passed    0.06 sec
2/2 Test #40: vedit-workspace-project-selftest .....   Passed    0.53 sec
100% tests passed, 0 tests failed out of 2
```

Raw logs beside this file: `ctest.log`, `project-selftest.log`,
`dialog-filters-selftest.log`.

## 6. Last stale `.flist` claim in the tree, fixed

`src/app/app_shell.h:215` said the lane may "import a `.f`/`.F`/`.flist` filelist
as the project". Corrected in place (comment only, no rebuild, no test rerun):

```diff
     // shell never learns a panel or a project. The project lane uses it to
-    // import a `.f`/`.F`/`.flist` filelist as the project.
+    // import a `.f` / `.F` filelist as the project; which extensions count is
+    // decided by isFilelistPath() in src/common/file_dialog_filters.h — `f`,
+    // case-insensitive, so a `.flist` is not a filelist.
```

Proof that no claim "`.flist` IS a filelist" survives under `src/`
(`grep -rn "flist" src/`, saved verbatim as `src-flist-grep.log`, 21 lines).
Every remaining hit is one of:

- the ruling itself: `project_filelist.cpp:19`, `project_filelist.h:5`,
  `project_selftest.cpp:239,555`, `app_shell.h:217`;
- negative assertions: `filelist.plan.flist-not-a-filelist` (+ its
  `planOpenedFilelist("/p/run.flist")` fixture), `shared.not-filelist-flist`
  with `!isFilelistPath("top.flist")`, `open.entryN.no-guessed-flist`,
  `open.filter.no-guessed-flist`;
- the history of why the guess was removed: `common/file_dialog_filters.h:8,18`,
  `app_shell.cpp:48`, `file_dialog_filters_selftest.cpp:10,172,202`,
  `main.cpp:800` — these quote the REMOVED `*.flist` literal on purpose.

The only live product string that still contains `.flist` is therefore a
negative test input; the dialogs' filter tables contain none.

Not run for this comment: no build, no tests (nothing compiled depends on it).

## 7. Not run

The full gate suite, translations, any commit (the captain commits).
