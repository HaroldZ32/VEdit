#!/usr/bin/env bash
# T59 / C17 v2 — extract the comparable metrics of v1, v2 run A and v2 run B
# into one TSV, so the comparison table in 20-compare-v1-v2.md is mechanical
# (no hand-copied numbers).
#
#   bash evidence/r2-c17/v2/scripts/extract-metrics.sh > evidence/r2-c17/v2/20-metrics.tsv
set -u
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)
V1="$ROOT/evidence/r2-c17"

emit() { # $1=label $2=log file $3=mode(plain|verilog)
  local label=$1 file=$2 mode=$3
  [ -f "$file" ] || { echo -e "$label\t$mode\tMISSING\t-\t-" ; return; }
  awk -v label="$label" -v mode="$mode" '
    /^MEASURE / {
      name=$2
      p50=""; p95=""
      for (i=3;i<=NF;i++) {
        if ($i ~ /^p50_us=/) { split($i,a,"="); p50=a[2] }
        if ($i ~ /^p95_us=/) { split($i,a,"="); p95=a[2] }
      }
      if (p50 != "") printf "%s\t%s\t%s\t%s\t%s\n", label, mode, name, p50, (p95==""?"-":p95)
    }
  ' "$file"
}

{
  echo -e "source\tmode\tmetric\tp50_us\tp95_us"
  emit v1 "$V1/02-run.log" plain
  emit v2A "$V1/v2/02-run.log" plain
  emit v2B "$V1/v2/rerun/02-run.log" plain
  emit v1 "$V1/02b-run-verilog.log" verilog
  emit v2A "$V1/v2/02b-run-verilog.log" verilog
  emit v2B "$V1/v2/rerun/02b-run-verilog.log" verilog
} | awk -F'\t' 'NR==1 || $3 !~ /^grab_baseline_no_overlay$/ || 1'
