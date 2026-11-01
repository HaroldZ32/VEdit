#!/usr/bin/env bash
# Negative verification for the hardened checks (T91).
#
# For every check that T89 flagged as a "soft PASS", this script breaks the
# condition ON A COPY extracted inside the test container, runs the FULL verify
# script against it, and quotes that check's own RESULT line. Repository files
# and the shipped tarball are never modified.
#
#   bash build/centos7-verify/negative-checks.sh
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TARBALL=$(ls "$ROOT"/build/centos7-verify/dist/vedit-centos7-verify-*.tar.gz | head -1)
IMAGE="docker.m.daocloud.io/library/centos:7"
NAME=$(basename "$TARBALL" .tar.gz)

case_run() { # <name> <breakage shell, runs inside /work> <check name substring>
    local name="$1" breakage="$2" check="$3" line
    line=$(docker run --rm -v "$ROOT/build/centos7-verify/dist":/art "$IMAGE" bash -lc '
        set -u
        rm -rf /work && mkdir -p /work
        tar xzf "/art/'"$NAME"'.tar.gz" -C /work
        cd "/work/'"$NAME"'"
        '"$breakage"'
        bash verify-centos7.sh /tmp/r.txt >/dev/null 2>&1
        grep -a "'"$check"'" /tmp/r.txt | head -1
    ' 2>&1 | grep -a '^RESULT' | head -1)
    printf '## CASE %s\n   breakage: %s\n   observed: %s\n\n' "$name" "$breakage" "${line:-<check line not found>}"
}

echo "# Negative verification for the hardened checks (T91)"
echo "# collected: $(date -Is)  HEAD=$(git rev-parse HEAD)"
echo "# tarball:   $(basename "$TARBALL")  sha256=$(sha256sum "$TARBALL" | cut -d' ' -f1)"
echo "# method:    extract the SHIPPED tarball inside a stock centos:7 container, break ONE"
echo "#            condition there, run the full script, quote the check's own RESULT line."
echo "#            Repository files and the tarball are never modified (proof at the end)."
echo
case_run verible-wrong-binary \
    'cp /bin/true AppDir/bin/verible-verilog-lint' 'toolchain.verible'
case_run slang-not-a-binary \
    'printf "this is not a compiler\n" > AppDir/bin/slang; chmod +x AppDir/bin/slang' 'toolchain.slang'
case_run startup-exit-nonzero \
    'sed -i "s|run_app --project \"\$SAMPLE\" --hold 1500|run_app --project \"\$SAMPLE\" --hold 1500 --not-a-real-option|" verify-centos7.sh' 'startup.exit'
case_run lint-count-wrong \
    'sed -i "s/EXPECTED_DIAGS=3/EXPECTED_DIAGS=4/" verify-centos7.sh' 'lint.bundled'
case_run lint-product-wrong-fixture \
    'sed -i "s|FIXTURE=\"\$HERE/samples/diagnostics-fixture\"|FIXTURE=\"\$HERE/samples/verilog-demo\"|" verify-centos7.sh' 'lint.product'
echo "# restore proof: the repository tree is unchanged by the cases above"
echo "\$ sha256sum build/centos7-verify/verify-centos7.sh"
sha256sum "$ROOT/build/centos7-verify/verify-centos7.sh"
echo "\$ git status --porcelain build/centos7-verify/ packaging/runtime/ THIRD_PARTY_NOTICES.md evidence/ | grep -v '^??' | head -8"
git -C "$ROOT" status --porcelain build/centos7-verify/ packaging/runtime/ THIRD_PARTY_NOTICES.md evidence/ | grep -v '^??' | head -8
