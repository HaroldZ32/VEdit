#!/usr/bin/env python3
"""Run a command, report wall time and the child's peak RSS (A-12 measurement).

    python3 measure.py -- <command> [args...]
"""
import resource
import subprocess
import sys
import time

args = sys.argv[1:]
if args and args[0] == "--":
    args = args[1:]
if not args:
    raise SystemExit("usage: measure.py -- <command> [args...]")

started = time.monotonic()
code = subprocess.run(args).returncode
elapsed = time.monotonic() - started
peak_kib = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
print("MEASURE exit=%d wall_s=%.3f peak_rss_mib=%.1f" % (code, elapsed, peak_kib / 1024.0))
sys.exit(code)
