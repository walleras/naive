#!/bin/sh
# build.sh -- drive the Naive build without make.
#
#   sh build/build.sh viewer      build the viewer (build/naive.exe)  [Windows + MinGW]
#   sh build/build.sh check    compile the portable core only; works on any OS
#   sh build/build.sh test     build and run tests/test_*.c, if a tests/ folder exists
#   sh build/build.sh clean    remove build products
#
# Two tiers:
#   CORE  : the engine, parser, interpreter and mock server -- portable ISO C99,
#           compiled with -pedantic-errors -Wall -Wextra -Werror.
#   WIN32 : sm_win32.c, the GDI backend, is the one module that includes
#           <windows.h>, so it gets relaxed pedantry (-std=gnu99).
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC="$ROOT/src"
TST="$ROOT/tests"
OUT="$ROOT/build"
CC=${CC:-gcc}

CORE_FLAGS="-std=c99 -pedantic-errors -Wall -Wextra -Werror -I$SRC"

CORE_SRCS="$SRC/sm_common.c $SRC/sm_value.c $SRC/sm_schema.c $SRC/sm_set.c \
$SRC/sm_lex.c $SRC/sm_expr.c $SRC/sm_func.c $SRC/sm_doc.c $SRC/sm_parse.c \
$SRC/sm_server.c"

target=${1:-viewer}

case "$target" in
  clean)
    rm -f "$OUT"/*.o "$OUT"/*.exe
    echo "cleaned"
    ;;
  check)
    echo "== compiling core (ISO C99, -pedantic-errors -Werror) =="
    for f in $CORE_SRCS "$SRC/naive_main.c"; do
      $CC $CORE_FLAGS -c "$f" -o "${TMPDIR:-${TEMP:-/tmp}}/sm_check_$$.o"
    done
    rm -f "${TMPDIR:-${TEMP:-/tmp}}/sm_check_$$.o"
    echo "core compiles clean"
    ;;
  test)
    if ! ls "$TST"/test_*.c >/dev/null 2>&1; then
      echo "no tests found in $TST (the tests/ folder is not part of the repository)"
      exit 0
    fi
    # Emit test exes to the system temp dir (some volumes block running binaries
    # that were built in place) and run them from there.
    RUNDIR=${TEMP:-${TMPDIR:-/tmp}}
    rc=0
    for t in "$TST"/test_*.c; do
      name=$(basename "$t" .c)
      echo "== compiling core (ISO C99) + $name =="
      $CC $CORE_FLAGS $CORE_SRCS "$t" -o "$RUNDIR/sm_$name.exe"
      echo "== running $name (from $RUNDIR) =="
      "$RUNDIR/sm_$name.exe" "$ROOT" || rc=1
      echo
    done
    exit $rc
    ;;
  viewer)
    # The .nve page viewer: portable C99 for engine + parser + viewer; only the
    # Win32 backend uses the relaxed gnu99 dialect.
    echo "== compiling engine + parser + viewer (ISO C99) =="
    OBJ=""
    for f in $CORE_SRCS "$SRC/naive_main.c"; do
      o="$OUT/$(basename "$f" .c).o"
      $CC $CORE_FLAGS -c "$f" -o "$o"
      OBJ="$OBJ $o"
    done
    echo "== compiling Win32 backend (gnu99, relaxed) =="
    $CC -std=gnu99 -Wall -I"$SRC" -c "$SRC/sm_win32.c" -o "$OUT/sm_win32.o"
    echo "== linking =="
    $CC $OBJ "$OUT/sm_win32.o" -o "$OUT/naive.exe" -mwindows -lgdi32 -luser32 -lshell32
    echo "built $OUT/naive.exe  (run: build/naive.exe examples/hello.nve)"
    ;;
  *)
    echo "usage: sh build/build.sh [viewer|check|test|clean]" >&2
    exit 2
    ;;
esac
