#!/usr/bin/env bash
# T11 evidence: side-by-side comparison of the real `verible-verilog-lint` CLI
# and the shipped project-batch ABI for the SAME files.
#
# Run inside the pinned image from the checkout root:
#   docker run --rm -v "$PWD":/vedit-build -w /vedit-build vedit-qt6-build:local \
#     bash evidence/w5-backend/t11-cli-vs-batch.sh
#
# Both sides read the same fixture (the w5-probe fixture, verbatim, plus a
# valid tab file that exercises the stderr-only path). Units differ by design:
# the CLI prints 1-based BYTE columns, the C ABI carries 0-based UTF-16
# columns; the log prints both so the conversion is visible, not assumed.
set -uo pipefail

cd /vedit-build || exit 1
LINT=.mpd/recon/verible/bin/verible-verilog-lint
DUMP=target/tmp/w5-harness/t11_batch_dump
FIX=/tmp/t11-fixture

if [[ ! -x "$LINT" ]]; then
    echo "MISSING-CLI $LINT"
    exit 1
fi
if [[ ! -x "$DUMP" ]]; then
    echo "MISSING-BATCH-DUMP $DUMP"
    exit 1
fi

rm -rf "$FIX"
mkdir -p "$FIX"

# The w5-probe fixture, byte for byte (probe text in w5_probe.cpp).
printf 'module top;\n  logic a b;\n\tlogic tabbed;\n  wire w = a really_long_unknown_signal_name_that_makes_this_line_long_enough_to_trip_the_line_length_rule_1234567890;\nendmodule\n' \
    > "$FIX/undeclared.sv"
# A syntactically valid file: the only finding is the `no-tabs` lint rule, which
# verible writes to stderr (the path T138 already covered).
printf 'module top;\n\tlogic a;\nendmodule\n' > "$FIX/tabs.sv"

compare_one() {
    local label="$1" file="$2"
    local out="$FIX/$label.cli.out" err="$FIX/$label.cli.err"

    echo "=== $label ($file) ==="
    "$LINT" "$file" > "$out" 2> "$err"
    local cli_exit=$?
    echo "-- CLI (real binary) exit=$cli_exit"
    echo "-- CLI stdout --"
    cat "$out"
    echo "-- CLI stderr --"
    cat "$err"
    echo "-- BATCH (shipped C ABI, engine=1) --"
    "$DUMP" "$LINT" "$file" | tee "$FIX/$label.batch.out"
    local batch_exit=${PIPESTATUS[0]}

    local cli_n
    cli_n=$(cat "$out" "$err" | grep -cE ':[0-9]+:[0-9]+(-[0-9]+)?(-[0-9]+:[0-9]+)?: ')
    local batch_n
    batch_n=$(awk -F'count=' '/^BATCH-POLL/ {print $2}' "$FIX/$label.batch.out")
    echo "COMPARE label=$label cli-exit=$cli_exit batch-exit=$batch_exit cli-findings=$cli_n batch-findings=${batch_n:-none} equal=$([[ "$cli_n" = "${batch_n:-x}" ]] && echo 1 || echo 0)"
    echo
}

compare_one undeclared "$FIX/undeclared.sv"
compare_one tabs "$FIX/tabs.sv"
