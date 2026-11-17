#!/bin/sh
# Builds the experimental Alicorn-owned host window.
#   host-spike/build_host.sh [alicorn-source] [acetate-build-dir] [output]
# Needs Odin, SDL3 and the acetate-host-backend library from a build configured
# with -Daudacity_host_spike=ON. The Alicorn checkout must be the pinned source
# recorded in src/alicorn/README.md.
set -eu
HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
ALICORN=${1:-${AUDACITY_ALICORN_SOURCE:-$HOME/dev/alicorn-pin}}
BUILD=${2:-$HOME/dev/acetate-build}
OUT=${3:-$BUILD/host-spike/acetate-host}
REPO_ROOT=$ALICORN
. "$ALICORN/tools/common.sh"
ODIN=$(alicorn_resolve_odin "")
FLAGS=$(alicorn_accesskit_linker_flags)
LIB=$BUILD/host-spike/lib
FLAGS="$FLAGS -L$LIB -Wl,-rpath,$LIB"
cd "$ALICORN"
alicorn_odin_with_linker_flags "$ODIN" "$FLAGS" build "$HERE/app" \
	"-collection:alicorn=$ALICORN" "-out:$OUT"
