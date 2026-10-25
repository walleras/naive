#!/bin/sh
# build.sh -- drive the SeSaML build without make (none installed).
#
# Two conformance tiers, per the project's locked decision:
#   CORE  : strict ISO C89, -pedantic-errors -- the "it really is 1990 C" claim.
#   WIN32 : same C89 dialect but relaxed pedantry, because <windows.h> is not
#           itself strict-C89-clean. Confined to sm_win32*.c only.
#
# Usage:  sh build/build.sh [test|sml|clean]
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC="$ROOT/src"
TST="$ROOT/tests"
OUT="$ROOT/build"
CC=${CC:-gcc}

CORE_FLAGS="-std=c89 -pedantic-errors -Wall -Wextra -Werror -I$SRC"

CORE_SRCS="$SRC/sm_common.c $SRC/sm_value.c $SRC/sm_schema.c $SRC/sm_set.c \
$SRC/sm_lex.c $SRC/sm_expr.c $SRC/sm_func.c"

target=${1:-test}

case "$target" in
  clean)
    rm -f "$OUT"/*.o "$OUT"/test_engine "$OUT"/test_engine.exe
    echo "cleaned"
    ;;
  test)
    # The strict-C89 *compile* is the conformance proof and runs fine here.
    # Some volumes (e.g. the N: drive this project lives on) block *executing*
    # binaries, so emit test exes to the system temp dir and run them there.
    RUNDIR=${TEMP:-${TMPDIR:-/tmp}}
    rc=0
    for t in "$TST"/test_*.c; do
      name=$(basename "$t" .c)
      echo "== compiling core (strict C89) + $name =="
      $CC $CORE_FLAGS $CORE_SRCS "$SRC/sm_doc.c" \
          "$SRC/sm_parse.c" "$SRC/sm_server.c" "$t" -o "$RUNDIR/sm_$name.exe"
      echo "== running $name (from $RUNDIR) =="
      "$RUNDIR/sm_$name.exe" "$ROOT" || rc=1
      echo
    done
    exit $rc
    ;;
  sml)
    # The .sml page viewer. Strict ISO C89 for engine + parser + viewer; only
    # the Win32 backend uses the relaxed gnu89 dialect.
    echo "== compiling engine + parser + viewer (strict C89) =="
    STRICT_SRCS="$CORE_SRCS $SRC/sm_doc.c $SRC/sm_parse.c $SRC/sm_server.c $SRC/sml_main.c"
    OBJ=""
    for f in $STRICT_SRCS; do
      o="$OUT/$(basename "$f" .c).o"
      $CC $CORE_FLAGS -c "$f" -o "$o"
      OBJ="$OBJ $o"
    done
    echo "== compiling Win32 backend (gnu89, relaxed) =="
    $CC -std=gnu89 -Wall -I"$SRC" -c "$SRC/sm_win32.c" -o "$OUT/sm_win32.o"
    echo "== linking =="
    $CC $OBJ "$OUT/sm_win32.o" -o "$OUT/sesaml.exe" -mwindows -lgdi32 -luser32 -lshell32
    echo "built $OUT/sesaml.exe  (run: sesaml examples/hello.sml)"
    ;;
  *)
    echo "usage: sh build/build.sh [test|sml|clean]" >&2
    exit 2
    ;;
esac
