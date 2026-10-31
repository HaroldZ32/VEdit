#!/usr/bin/env bash
# Reproducible acceptance check for the repository-owned Verilog fixture
# (C13 / AC-S1.5).
#
#   bash scripts/verify-fixture.sh
#   bash scripts/verify-fixture.sh --evidence evidence/fixture
#
# The check needs no toolchain, no network and no build: it expands the
# fixture's filelists with the semantics the project model implements
# (crates/vd-config/src/filelist.rs) and compares the result with the checked-in
# expectations under tests/fixtures/verilog-project/expected/. It is
# idempotent — only --evidence writes files, and every write overwrites — and
# exits non-zero on any failure.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
FIXTURE="$ROOT/tests/fixtures/verilog-project"
EVIDENCE=""
MAX_DEPTH=16

usage() {
  cat <<'EOF'
usage: verify-fixture.sh [--fixture DIR] [--evidence DIR]

  --fixture DIR    fixture project root (default: tests/fixtures/verilog-project)
  --evidence DIR   also write manifest.txt, expected.txt and verify-output.txt
EOF
}

while [ $# -gt 0 ]; do
  case $1 in
    --fixture)
      [ $# -ge 2 ] || { echo "verify-fixture: --fixture needs a directory" >&2; exit 2; }
      FIXTURE=$2
      shift 2
      ;;
    --evidence)
      [ $# -ge 2 ] || { echo "verify-fixture: --evidence needs a directory" >&2; exit 2; }
      EVIDENCE=$2
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "verify-fixture: unknown argument '$1'" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [ ! -d "$FIXTURE" ]; then
  echo "verify-fixture: fixture directory not found: $FIXTURE" >&2
  exit 2
fi
FIXTURE=$(cd "$FIXTURE" && pwd -P)
if [ ! -f "$FIXTURE/project.f" ]; then
  echo "verify-fixture: $FIXTURE/project.f is missing" >&2
  exit 2
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

FILES_LIST="$TMP/files.txt"
FILE_KEYS="$TMP/file.keys"
INCDIRS_LIST="$TMP/incdirs.txt"
DEFINES_LIST="$TMP/defines.txt"
SKIPPED_LIST="$TMP/skipped-tokens.txt"
DUPLICATES_LIST="$TMP/duplicates.txt"
NOTES="$TMP/notes.txt"
REPORT="$TMP/report.txt"
: > "$FILES_LIST"
: > "$FILE_KEYS"
: > "$INCDIRS_LIST"
: > "$DEFINES_LIST"
: > "$SKIPPED_LIST"
: > "$DUPLICATES_LIST"
: > "$NOTES"

PASSES=0
FAILS=0
SKIPS=0

report() { # STATUS LABEL [DETAIL]
  local status=$1 label=$2 detail=${3:-}
  case $status in
    PASS) PASSES=$((PASSES + 1)) ;;
    FAIL) FAILS=$((FAILS + 1)) ;;
    SKIP) SKIPS=$((SKIPS + 1)) ;;
  esac
  printf '%-4s %s\n' "$status" "$label"
  if [ -n "$detail" ]; then
    printf '%s\n' "$detail" | sed 's/^/       /'
  fi
}

note() { # diagnostic that must make the run fail
  printf '%s\n' "$1" >> "$NOTES"
  FAILS=$((FAILS + 1))
  printf 'FAIL %s\n' "$1"
}

section() {
  printf '\n%s\n' "$1"
}

trim() {
  local s=$1
  s=${s#"${s%%[![:space:]]*}"}
  s=${s%"${s##*[![:space:]]}"}
  printf '%s' "$s"
}

# Mirror of strip_comment() in crates/vd-config/src/filelist.rs: `//` starts a
# comment anywhere, `#` only at line start or after whitespace.
strip_comment() {
  local text=$1 before
  text=${text%%//*}
  before=${text%%#*}
  if [ "$before" != "$text" ]; then
    if [ -z "$before" ] || [[ ${before: -1} == [[:space:]] ]]; then
      text=$before
    fi
  fi
  printf '%s' "$text"
}

resolve() { # BASE PATH -> BASE/PATH, absolute paths kept as-is
  local base=$1 path=$2
  case $path in
    /*) printf '%s' "$path" ;;
    *) printf '%s/%s' "$base" "$path" ;;
  esac
}

canon() {
  if command -v realpath >/dev/null 2>&1; then
    realpath -m -- "$1" 2>/dev/null || printf '%s' "$1"
  else
    printf '%s' "$1"
  fi
}

# Fixture-root-relative normalized path, so ../rtl/x.sv and rtl/x.sv read the
# same in the manifest and in expected/.
display_path() {
  local real
  real=$(canon "$1")
  case $real in
    "$FIXTURE"/*) printf '%s' "${real#"$FIXTURE"/}" ;;
    *) printf '%s' "$real" ;;
  esac
}

# Repository-relative path, for labels and evidence references.
repo_path() {
  case $1 in
    "$ROOT"/*) printf '%s' "${1#"$ROOT"/}" ;;
    *) printf '%s' "$1" ;;
  esac
}

add_unique() { # LIST VALUE
  local target=$1 value=$2
  if grep -Fxq -e "$value" -- "$target" 2>/dev/null; then
    return 0
  fi
  printf '%s\n' "$value" >> "$target"
}

parse_define() { # RAW -> NAME or NAME=VALUE on stdout, exit 1 when malformed
  local raw name value=""
  raw=$(trim "$1")
  [ -n "$raw" ] || return 1
  if [[ $raw == *=* ]]; then
    name=$(trim "${raw%%=*}")
    value=$(trim "${raw#*=}")
  else
    name=$raw
  fi
  [[ $name =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]] || return 1
  if [[ $raw == *=* ]]; then
    printf '%s=%s' "$name" "$value"
  else
    printf '%s' "$name"
  fi
}

VISITING=()

expand_filelist() { # FILE DEPTH
  local file=$1 depth=$2 base raw text rest nested nested_path resolved part
  local canonical lineno=0
  local -a parts=()

  if [ "$depth" -gt "$MAX_DEPTH" ]; then
    note "nested -f recursion deeper than $MAX_DEPTH at $(display_path "$file"); stopped"
    return 0
  fi
  canonical=$(canon "$file")
  for part in ${VISITING[@]+"${VISITING[@]}"}; do
    if [ "$part" = "$canonical" ]; then
      note "nested -f cycle back into $(display_path "$file"); skipped"
      return 0
    fi
  done
  if [ ! -f "$file" ] || [ ! -r "$file" ]; then
    note "filelist is not a readable file: $(display_path "$file")"
    return 0
  fi

  base=$(dirname -- "$file")
  VISITING+=("$canonical")
  while IFS= read -r raw || [ -n "$raw" ]; do
    lineno=$((lineno + 1))
    text=$(trim "$(strip_comment "$raw")")
    if [ -z "$text" ]; then
      continue
    fi

    case $text in
      '+incdir+'*)
        parts=()
        IFS='+' read -r -a parts <<< "${text#+incdir+}"
        for part in ${parts[@]+"${parts[@]}"}; do
          part=$(trim "$part")
          [ -n "$part" ] || continue
          add_unique "$INCDIRS_LIST" "$(display_path "$(resolve "$base" "$part")")"
        done
        continue
        ;;
      '+define+'*)
        parts=()
        IFS='+' read -r -a parts <<< "${text#+define+}"
        for part in ${parts[@]+"${parts[@]}"}; do
          part=$(trim "$part")
          [ -n "$part" ] || continue
          if resolved=$(parse_define "$part"); then
            add_unique "$DEFINES_LIST" "$resolved"
          else
            note "$(display_path "$file"):$lineno malformed +define+ token '$part'; skipped"
          fi
        done
        continue
        ;;
    esac

    case $text in
      -f*|-F*)
        rest=${text:2}
        if [[ $rest == [[:space:]]* ]]; then
          nested=$(trim "$rest")
          if [ -n "$nested" ]; then
            nested_path=$(resolve "$base" "$nested")
            if [ -f "$nested_path" ]; then
              expand_filelist "$nested_path" $((depth + 1))
            else
              note "$(display_path "$file"):$lineno nested -f target '$nested' does not exist"
            fi
            continue
          fi
        fi
        ;;
    esac

    case $text in
      '+'*|'-'*)
        # A token written for another simulator: warned about and skipped, never
        # fatal. This mirrors WarningCode::FilelistUnknownToken.
        add_unique "$SKIPPED_LIST" "${text%%[[:space:]]*}"
        continue
        ;;
    esac

    resolved=$(resolve "$base" "$text")
    if grep -Fxq -e "$resolved" -- "$FILE_KEYS" 2>/dev/null; then
      add_unique "$DUPLICATES_LIST" "$(display_path "$resolved")"
    else
      printf '%s\n' "$resolved" >> "$FILE_KEYS"
      printf '%s\n' "$(display_path "$resolved")" >> "$FILES_LIST"
    fi
  done < "$file"
  unset "VISITING[$((${#VISITING[@]} - 1))]"
}

print_list() { # TITLE FILE
  local title=$1 file=$2 count
  count=$(wc -l < "$file")
  count=${count// /}
  printf '%s (%s):\n' "$title" "$count"
  if [ "$count" -gt 0 ]; then
    sed 's/^/  /' "$file"
  else
    printf '  <none>\n'
  fi
}

expectation_label() {
  case $1 in
    files.txt) printf 'entries' ;;
    incdirs.txt) printf 'include dirs' ;;
    defines.txt) printf 'defines' ;;
    skipped-tokens.txt) printf 'skipped tokens' ;;
    duplicates.txt) printf 'duplicate entries' ;;
    *) printf '%s' "${1%.txt}" ;;
  esac
}

compare_list() { # LABEL EXPECTED-FILE COMPUTED-FILE
  local label=$1 expected=$2 computed=$3 diffout count
  if [ ! -f "$expected" ]; then
    report FAIL "$label (expected file missing: $(display_path "$expected"))"
    return 0
  fi
  diffout=$(diff -u --label "expected/$(basename -- "$expected")" --label "computed/$(basename -- "$computed")" "$expected" "$computed" || true)
  count=$(wc -l < "$computed")
  count=${count// /}
  if [ -z "$diffout" ]; then
    report PASS "$label matches $(display_path "$expected") ($count items)"
  else
    report FAIL "$label does not match $(display_path "$expected")" "$diffout"
  fi
}

require_files() {
  local rel missing=""
  local -a required=(
    README.md
    project.f
    filelists/tb.f
    filelists/nested/tb_aux.f
    filelists/extra.f
    rtl/alu_pkg.sv
    rtl/alu.sv
    rtl/counter.v
    rtl/mem.sv
    rtl/top.sv
    rtl/include/rtl_defs.vh
    tb/tb_top.sv
    tb/tb_aux.sv
    tb/include/tb_defs.vh
    inc/defs.vh
    inc/generated/width.vh
    expected/files.txt
    expected/incdirs.txt
    expected/defines.txt
    expected/skipped-tokens.txt
    expected/duplicates.txt
  )
  for rel in "${required[@]}"; do
    if [ ! -f "$FIXTURE/$rel" ]; then
      missing+="missing: $rel"$'\n'
    fi
  done
  if [ -z "$missing" ]; then
    report PASS "fixture tree contains the ${#required[@]} required files"
  else
    report FAIL "fixture tree is incomplete" "$missing"
  fi
}

check_incdirs_exist() {
  local dir missing=""
  if [ ! -s "$INCDIRS_LIST" ]; then
    report FAIL "project.f declares no +incdir+ directory"
    return 0
  fi
  while IFS= read -r dir; do
    [ -n "$dir" ] || continue
    if [ ! -d "$FIXTURE/$dir" ]; then
      missing+="not a directory: $dir"$'\n'
    fi
  done < "$INCDIRS_LIST"
  if [ -z "$missing" ]; then
    report PASS "every +incdir+ resolves to a directory"
  else
    report FAIL "+incdir+ entries do not resolve" "$missing"
  fi
}

# The product auto-discovers `*.f` in the project root, so the fixture must keep
# exactly one project-level filelist there.
check_root_filelists() {
  local count
  count=$(find "$FIXTURE" -maxdepth 1 -type f -name '*.f' | wc -l)
  count=${count// /}
  if [ "$count" -eq 1 ] && [ -f "$FIXTURE/project.f" ]; then
    report PASS "project.f is the only filelist at the fixture root (auto-discovery is unambiguous)"
  else
    report FAIL "the fixture root must hold exactly one .f filelist" "found $count *.f file(s); expected 1 (project.f)"
  fi
}

check_entries_exist() {
  local entry missing=""
  while IFS= read -r entry; do
    [ -n "$entry" ] || continue
    if [ ! -f "$FIXTURE/$entry" ]; then
      missing+="resolved but absent: $entry"$'\n'
    fi
  done < "$FILES_LIST"
  if [ -z "$missing" ]; then
    report PASS "every resolved source entry exists on disk"
  else
    report FAIL "resolved source entries are missing" "$missing"
  fi
}

check_source_coverage() {
  local file rel orphans=""
  while IFS= read -r file; do
    [ -n "$file" ] || continue
    rel=$(display_path "$file")
    if ! grep -Fxq -e "$rel" -- "$FILES_LIST" 2>/dev/null; then
      orphans+="not in any filelist: $rel"$'\n'
    fi
  done < <(find "$FIXTURE/rtl" "$FIXTURE/tb" -type f \( -name '*.v' -o -name '*.sv' \) | sort)
  if [ -z "$orphans" ]; then
    report PASS "every .v/.sv under rtl/ and tb/ is reachable from project.f"
  else
    report FAIL "source files are not covered by the filelists" "$orphans"
  fi
}

check_includes() {
  local entry header dir found missing=""
  while IFS= read -r entry; do
    [ -n "$entry" ] || continue
    while IFS= read -r header; do
      [ -n "$header" ] || continue
      found=0
      while IFS= read -r dir; do
        [ -n "$dir" ] || continue
        if [ -f "$FIXTURE/$dir/$header" ]; then
          found=1
          break
        fi
      done < "$INCDIRS_LIST"
      if [ "$found" -eq 0 ]; then
        missing+="$entry includes $header but no +incdir+ provides it"$'\n'
      fi
    done < <(grep -oE '`include[[:space:]]*"[^"]+"' "$FIXTURE/$entry" 2>/dev/null | sed -E 's/.*"([^"]+)"/\1/' || true)
  done < "$FILES_LIST"
  if [ -z "$missing" ]; then
    report PASS "every \`include resolves through the resolved +incdir+ list"
  else
    report FAIL "an included header is not reachable" "$missing"
  fi
}

check_defines_used() {
  local define name entry found unused=""
  while IFS= read -r define; do
    [ -n "$define" ] || continue
    name=${define%%=*}
    found=0
    while IFS= read -r entry; do
      [ -n "$entry" ] || continue
      if grep -qw -e "$name" -- "$FIXTURE/$entry"; then
        found=1
        break
      fi
    done < "$FILES_LIST"
    if [ "$found" -eq 0 ]; then
      unused+="define never referenced by a source: $name"$'\n'
    fi
  done < "$DEFINES_LIST"
  if [ -z "$unused" ]; then
    report PASS "every +define+ name is referenced by the sources"
  else
    report FAIL "a +define+ name is dead" "$unused"
  fi
}

expect_grep() { # FILE PATTERN DESCRIPTION
  local file=$1 pattern=$2 description=$3
  if grep -Eq -- "$pattern" "$FIXTURE/$file"; then
    report PASS "$description"
  else
    report FAIL "$description" "pattern /$pattern/ not found in $file"
  fi
}

check_hierarchy() {
  expect_grep rtl/top.sv '^module top' "module top is declared"
  expect_grep rtl/top.sv '^[[:space:]]*alu u_alu' "top instantiates alu as u_alu"
  expect_grep rtl/top.sv '^[[:space:]]*counter #\(\.WIDTH_P\(WIDTH_P\)\) u_counter' "top instantiates counter as u_counter"
  expect_grep rtl/top.sv '^[[:space:]]*mem u_mem' "top instantiates mem as u_mem"
  expect_grep tb/tb_top.sv '^module tb_top' "module tb_top is declared"
  expect_grep tb/tb_top.sv '^[[:space:]]*top #\(\.WIDTH_P\(WIDTH_P\)\) u_top' "tb_top instantiates top as u_top"
  expect_grep rtl/alu.sv 'import alu_pkg::\*;' "alu imports the shared package"
  expect_grep rtl/top.sv 'alu_pkg::alu_op_t' "top uses the package type"
  expect_grep tb/tb_top.sv 'alu_pkg::ALU_ADD' "tb_top uses a package enumeration value"
}

check_verible() {
  local file failures=""
  if ! command -v verible-verilog-syntax >/dev/null 2>&1; then
    report SKIP "verible-verilog-syntax not installed: Verilog syntax parse not checked"
    return 0
  fi
  while IFS= read -r file; do
    [ -n "$file" ] || continue
    if ! verible-verilog-syntax --printtree -- "$FIXTURE/$file" >/dev/null 2>&1; then
      failures+="syntax error: $file"$'\n'
    fi
  done < "$FILES_LIST"
  if [ -z "$failures" ]; then
    report PASS "verible-verilog-syntax parses every resolved source"
  else
    report FAIL "verible-verilog-syntax rejected a fixture source" "$failures"
  fi
}

print_computed() {
  section "[computed from project.f]"
  print_list "entries" "$FILES_LIST"
  print_list "include dirs" "$INCDIRS_LIST"
  print_list "defines" "$DEFINES_LIST"
  print_list "skipped tokens" "$SKIPPED_LIST"
  print_list "duplicate entries" "$DUPLICATES_LIST"
}

print_expectations() {
  local name
  section "[expectations — tests/fixtures/verilog-project/expected]"
  for name in files.txt incdirs.txt defines.txt skipped-tokens.txt duplicates.txt; do
    printf '%s (%s):\n' "$(expectation_label "$name")" "expected/$name"
    if [ -f "$FIXTURE/expected/$name" ]; then
      sed 's/^/  /' "$FIXTURE/expected/$name"
    else
      printf '  <missing>\n'
    fi
  done
}

main() {
  section "VEdit acceptance fixture check (C13 / AC-S1.5)"
  printf 'fixture: %s\n' "$(repo_path "$FIXTURE")"
  printf 'top filelist: project.f\n'

  expand_filelist "$FIXTURE/project.f" 0
  print_computed
  print_expectations

  section "[checks]"
  require_files
  check_root_filelists
  check_incdirs_exist
  compare_list "entries" "$FIXTURE/expected/files.txt" "$FILES_LIST"
  compare_list "include dirs" "$FIXTURE/expected/incdirs.txt" "$INCDIRS_LIST"
  compare_list "defines" "$FIXTURE/expected/defines.txt" "$DEFINES_LIST"
  compare_list "skipped tokens" "$FIXTURE/expected/skipped-tokens.txt" "$SKIPPED_LIST"
  compare_list "duplicate entries" "$FIXTURE/expected/duplicates.txt" "$DUPLICATES_LIST"
  check_entries_exist
  check_source_coverage
  check_includes
  check_defines_used
  check_hierarchy
  check_verible

  section "[summary]"
  printf '%s passed, %s failed, %s skipped\n' "$PASSES" "$FAILS" "$SKIPS"
  if [ -s "$NOTES" ]; then
    printf '\nnotes:\n'
    sed 's/^/  /' "$NOTES"
  fi
}

evidence_header() {
  local head status
  head=$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || printf '<not a git checkout>')
  status=$(git -C "$ROOT" status --porcelain 2>/dev/null || true)
  printf 'generated: %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf 'git_head: %s\n' "$head"
  printf 'command: bash scripts/verify-fixture.sh --evidence %s\n' "$(repo_path "$EVIDENCE")"
  if [ -z "$status" ]; then
    printf 'git_status_porcelain: <clean>\n'
  else
    printf 'git_status_porcelain:\n%s\n' "$status"
  fi
  printf -- '---\n'
}

file_row() { # PATH -> "<sha256>  <bytes>  <path>"  (size via stat, portable enough for this repo)
  local path=$1 size sha
  size=$(stat -c%s -- "$FIXTURE/$path")
  sha=$(sha256sum -- "$FIXTURE/$path" | awk '{print $1}')
  printf '%s  %8s  %s\n' "$sha" "$size" "$path"
}

write_evidence() {
  local manifest="$EVIDENCE/manifest.txt"
  local expected="$EVIDENCE/expected.txt"
  local output="$EVIDENCE/verify-output.txt"
  local name path

  mkdir -p "$EVIDENCE"
  {
    evidence_header
    printf 'fixture: %s\n\n' "$(repo_path "$FIXTURE")"
    printf '[files — sha256, bytes, fixture-relative path]\n'
    while IFS= read -r path; do
      [ -n "$path" ] || continue
      file_row "$path"
    done < <(cd "$FIXTURE" && find . -type f \( -name '*.f' -o -name '*.v' -o -name '*.sv' -o -name '*.vh' \) | sed 's|^\./||' | sort)
    printf '\n[resolved entries — in filelist order]\n'
    sed 's/^/  /' "$FILES_LIST"
    printf '\n[include dirs]\n'
    sed 's/^/  /' "$INCDIRS_LIST"
    printf '\n[defines]\n'
    sed 's/^/  /' "$DEFINES_LIST"
    printf '\n[skipped tokens]\n'
    sed 's/^/  /' "$SKIPPED_LIST"
    printf '\n[duplicate entries]\n'
    if [ -s "$DUPLICATES_LIST" ]; then sed 's/^/  /' "$DUPLICATES_LIST"; else printf '  <none>\n'; fi
  } > "$manifest"

  {
    evidence_header
    for name in files.txt incdirs.txt defines.txt skipped-tokens.txt duplicates.txt; do
      printf '== expected/%s ==\n' "$name"
      if [ -f "$FIXTURE/expected/$name" ]; then
        cat "$FIXTURE/expected/$name"
      else
        printf '<missing>\n'
      fi
      printf '\n'
    done
  } > "$expected"

  {
    evidence_header
    cat "$REPORT"
  } > "$output"

  printf 'evidence written: %s\n' "$(repo_path "$EVIDENCE")/manifest.txt"
  printf 'evidence written: %s\n' "$(repo_path "$EVIDENCE")/expected.txt"
  printf 'evidence written: %s\n' "$(repo_path "$EVIDENCE")/verify-output.txt"
}

set +e
main > "$REPORT" 2>&1
MAIN_RC=$?
set -e
cat "$REPORT"

# The exit code comes from the summary the report ends with, so an aborted check
# can never look like a pass.
FAILED=$(grep -E '^[0-9]+ passed, [0-9]+ failed, [0-9]+ skipped$' "$REPORT" | tail -n 1 | awk '{print $3}')
if [ -z "$FAILED" ]; then
  printf 'FAIL the check aborted before writing its summary (exit %s)\n' "$MAIN_RC"
  FAILED=1
elif [ "$MAIN_RC" -ne 0 ] && [ "$FAILED" -eq 0 ]; then
  printf 'FAIL the check exited %s without reporting a failed check\n' "$MAIN_RC"
  FAILED=1
fi

if [ -n "$EVIDENCE" ]; then
  EVIDENCE=$(mkdir -p "$EVIDENCE" && cd "$EVIDENCE" && pwd -P)
  write_evidence
fi

if [ "$FAILED" -gt 0 ]; then
  exit 1
fi
exit 0
