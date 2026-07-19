#!/usr/bin/env bash
# Build a loadable XLOC2 firmware core module (phz_core-<label>.so/.dylib)
# from any Phazerville source tree — the vendored one, a local checkout, or
# any git repo/ref — and drop it into the app's cores folder.
#
# Usage:
#   build-core.sh [--src <dir> | --repo <git-url> --ref <ref>]
#                 [--out <dir>] [--name <label>] [-- <extra cmake args>]
#
#   --src <dir>    firmware source root containing software/src
#                  (default: the vendored firmware/ tree). Used as-is: the
#                  emulator patches are NOT applied to a --src tree.
#   --repo <url>   git URL (or local path) to clone shallowly; patches/*.patch
#                  are applied with `git apply --3way` before building.
#   --ref <ref>    branch / tag / commit to build (required with --repo).
#   --out <dir>    where to copy the finished module
#                  (default: the app's <stateDir>/cores folder).
#   --name <label> label for the output file, phz_core-<label>.<ext>
#                  (default: the ref, or git-describe of --src).
#   -- ...         anything after -- is passed to the cmake configure step
#                  (e.g. -- -DCMAKE_OSX_ARCHITECTURES=arm64).
#
# The build reuses a per-label build directory (build-core/<label>) so
# repeated builds of the same source are incremental.
set -euo pipefail

usage() { sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
die() { echo "build-core.sh: error: $*" >&2; exit 1; }

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="" REPO="" REF="" OUT="" NAME=""
EXTRA_CMAKE=()

while [ $# -gt 0 ]; do
  case "$1" in
    --src)  SRC="$2"; shift 2 ;;
    --repo) REPO="$2"; shift 2 ;;
    --ref)  REF="$2"; shift 2 ;;
    --out)  OUT="$2"; shift 2 ;;
    --name) NAME="$2"; shift 2 ;;
    --)     shift; EXTRA_CMAKE=("$@"); break ;;
    -h|--help) usage ;;
    *) die "unknown argument: $1 (see --help)" ;;
  esac
done

[ -n "$REPO" ] && [ -n "$SRC" ] && die "--repo and --src are mutually exclusive"
[ -n "$REPO" ] && [ -z "$REF" ] && die "--repo requires --ref"

case "$(uname -s)" in
  Darwin) EXT=".dylib"; DEFAULT_STATE="$HOME/Library/Application Support/Calsynth/XLOC2" ;;
  *)      EXT=".so";    DEFAULT_STATE="$HOME/.config/Calsynth/XLOC2" ;;
esac

TMPDIR_CLONE=""
cleanup() { [ -n "$TMPDIR_CLONE" ] && rm -rf "$TMPDIR_CLONE"; }
trap cleanup EXIT

LABEL="$NAME"

if [ -n "$REPO" ]; then
  TMPDIR_CLONE="$(mktemp -d /tmp/xloc2-core-src.XXXXXX)"
  echo "== Fetching $REPO @ $REF"
  git -C "$TMPDIR_CLONE" init -q
  git -C "$TMPDIR_CLONE" remote add origin "$REPO"
  if ! git -C "$TMPDIR_CLONE" fetch -q --depth 1 origin "$REF" 2>/dev/null; then
    echo "   (shallow fetch of '$REF' failed; fetching full history)"
    git -C "$TMPDIR_CLONE" fetch -q origin || die "cannot fetch $REPO"
  fi
  git -C "$TMPDIR_CLONE" checkout -q FETCH_HEAD 2>/dev/null \
    || git -C "$TMPDIR_CLONE" checkout -q "$REF" \
    || die "cannot checkout '$REF'"

  echo "== Applying emulator patches"
  FAILED=0
  for p in "$ROOT"/patches/*.patch; do
    if git -C "$TMPDIR_CLONE" apply --3way "$p" 2>/tmp/xloc2-patch-err.$$; then
      echo "   applied $(basename "$p")"
    else
      echo "   FAILED  $(basename "$p"):" >&2
      sed 's/^/     /' /tmp/xloc2-patch-err.$$ >&2
      FAILED=1
    fi
  done
  rm -f /tmp/xloc2-patch-err.$$
  [ "$FAILED" -ne 0 ] && die \
    "some patches did not apply to $REPO@$REF — resolve upstream drift in patches/ first"

  SRC="$TMPDIR_CLONE"
  [ -z "$LABEL" ] && LABEL="$REF"
else
  SRC="${SRC:-$ROOT/firmware}"
  [ -d "$SRC" ] || die "--src $SRC does not exist"
  SRC="$(cd "$SRC" && pwd)"
  if [ -z "$LABEL" ]; then
    if [ "$SRC" = "$ROOT/firmware" ]; then
      LABEL="vendored"
    elif [ -e "$SRC/.git" ]; then
      LABEL="$(git -C "$SRC" describe --tags --always --dirty 2>/dev/null || true)"
    fi
    [ -z "$LABEL" ] && LABEL="$(basename "$SRC")"
  fi
fi

[ -d "$SRC/software/src" ] || die "$SRC does not contain software/src"

# sanitize the label for use in a file name
LABEL="$(echo "$LABEL" | tr '/ :' '---' | tr -cd 'A-Za-z0-9._-')"
CORE_NAME="phz_core-$LABEL"
BUILD="$ROOT/build-core/$LABEL"
OUT="${OUT:-$DEFAULT_STATE/cores}"

echo "== Configuring ($BUILD)"
cmake -S "$ROOT" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DXLOC2_BUILD_APP=OFF \
  -DXLOC2_FW_DIR="$SRC" \
  -DXLOC2_CORE_NAME="$CORE_NAME" \
  -DXLOC2_CORE_REF="$LABEL" \
  ${EXTRA_CMAKE[@]+"${EXTRA_CMAKE[@]}"} >/dev/null

JOBS="$( (command -v nproc >/dev/null && nproc) || sysctl -n hw.ncpu 2>/dev/null || echo 2)"
echo "== Building $CORE_NAME$EXT (-j$JOBS)"
cmake --build "$BUILD" --target phz_core -j"$JOBS"

mkdir -p "$OUT"
cp "$BUILD/$CORE_NAME$EXT" "$OUT/"
echo "== Done: $OUT/$CORE_NAME$EXT"
