#!/bin/sh
# Host build of the engine with ASan/UBSan, then ctest. `scripts/test-engine.sh x` builds in
# build/engine-x (default build/engine-host). LIVE=1 also runs the tests that need the network.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=$ROOT/build/engine-${1:-host}
"$ROOT/scripts/fetch-third-party.sh"
export PKG_CONFIG_PATH=${PKG_CONFIG_PATH:-}:/opt/homebrew/lib/pkgconfig:/usr/local/lib/pkgconfig
cmake -S "$ROOT/engine" -B "$B" -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build "$B" -j"$(getconf _NPROCESSORS_ONLN)"
if [ "${LIVE:-0}" = 1 ]; then
	ctest --test-dir "$B" --output-on-failure
else
	ctest --test-dir "$B" --output-on-failure -LE live
fi
