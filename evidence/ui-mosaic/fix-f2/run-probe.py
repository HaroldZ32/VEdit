#!/usr/bin/env python3
"""UI-Mosaic F2 evidence: build and run the repeatable wiring assertion.

The probe (theme_seam_probe.cpp) is NOT part of the product build — the task may
not add ctest targets and existing tests must not be modified, so the assertion
lives here as an evidence tool. It links the REAL libraries just built from the
changed sources (libvedit_app_shell.a + libvedit_editor_core.a), using the exact
compile flags and library list CMake recorded for the existing editor-pane
activation test (target/qt6-build/compile_commands.json + build.ninja), so the
probe is compiled with the same -Wall -Wextra -Wpedantic -Werror policy.

Run INSIDE the pinned image, from /vedit-build:

    docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
        python3 evidence/ui-mosaic/fix-f2/run-probe.py
"""
import json
import os
import shlex
import subprocess
import sys

ROOT = os.environ.get("VEDIT_ROOT", "/vedit-build")
OUT = os.path.join(ROOT, "evidence/ui-mosaic/fix-f2")
BUILD = os.path.join(ROOT, "target/qt6-build")
PROBE_SRC = os.path.join(OUT, "theme_seam_probe.cpp")
PROBE_OBJ = os.path.join(OUT, "theme_seam_probe.o")
PROBE_EXE = os.path.join(OUT, "theme_seam_probe")
FIXTURE = os.path.join(OUT, "fixture/syntax-mosaic.v")
REFERENCE_TU = "/src/app/editor_area_activation_test.cpp"
REFERENCE_TARGET = "build src/app/vedit-editor-pane-activation-tests:"


def fail(message):
    print(f"F2-PROBE BUILD FAIL: {message}", file=sys.stderr)
    return 1


def main():
    if not os.path.exists(os.path.join(BUILD, "compile_commands.json")):
        return fail("no build tree at target/qt6-build")
    with open(os.path.join(BUILD, "compile_commands.json"), encoding="utf-8") as handle:
        database = json.load(handle)
    entry = next((e for e in database if e["file"].endswith(REFERENCE_TU)), None)
    if entry is None:
        return fail(f"no compile entry for {REFERENCE_TU}")
    tokens = shlex.split(entry["command"])
    # Everything before `-o <object>` is the flag set; the tail is `-c <source>`.
    flags = tokens[: tokens.index("-o")]
    compile_cmd = flags + ["-c", PROBE_SRC, "-o", PROBE_OBJ]
    print("+", " ".join(compile_cmd))
    subprocess.run(compile_cmd, check=True, cwd=ROOT)

    with open(os.path.join(BUILD, "build.ninja"), encoding="utf-8") as handle:
        lines = handle.read().splitlines()
    start = next((i for i, line in enumerate(lines) if line.startswith(REFERENCE_TARGET)), None)
    if start is None:
        return fail(f"no link rule for {REFERENCE_TARGET}")
    link_libraries = None
    link_flags = ""
    compile_flags = ""
    for line in lines[start + 1 : start + 40]:
        if line.startswith("  LINK_LIBRARIES = "):
            link_libraries = line.split("= ", 1)[1]
        elif line.startswith("  LINK_FLAGS = "):
            link_flags = line.split("= ", 1)[1]
        elif line.startswith("  FLAGS = "):
            compile_flags = line.split("= ", 1)[1]
        elif not line.strip():
            break
    if not link_libraries:
        return fail("no LINK_LIBRARIES for the reference target")

    link_cmd = [tokens[0]] + shlex.split(compile_flags) + [PROBE_OBJ, "-o", PROBE_EXE]
    link_cmd += [f for f in shlex.split(link_flags) if "--dependency-file" not in f]
    link_cmd += shlex.split(link_libraries)
    print("+ link", " ".join(link_cmd))
    # CMake records the archives relative to the BUILD directory, so the link
    # has to run there (the probe object/exe paths are absolute).
    subprocess.run(link_cmd, check=True, cwd=BUILD)

    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = "offscreen"
    env.setdefault("HOME", "/root")
    print("+ run", PROBE_EXE, FIXTURE)
    return subprocess.run([PROBE_EXE, FIXTURE], cwd=ROOT, env=env).returncode


if __name__ == "__main__":
    sys.exit(main())
