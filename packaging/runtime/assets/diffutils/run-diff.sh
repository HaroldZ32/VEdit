#!/bin/sh
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
appdir=$(CDPATH= cd -- "$here/../../../.." && pwd -P)
exec "$appdir/ld-linux-x86-64.so.2" --library-path "$appdir/lib" "$here/diff.bin" "$@"
