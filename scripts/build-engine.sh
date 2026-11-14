#!/bin/sh
# Cross-builds the engine for BB10 in the bbmusic-build image into build/engine-bb10, then fails
# if the binary imports a libgcc unwinder (the -lz trap, phase0.md gate 0.3).
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
"$ROOT/scripts/fetch-third-party.sh"
docker run --rm -v "$ROOT:/src" -w /src bbmusic-build sh -euc '
	cmake -S engine -B build/engine-bb10 -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE=/opt/bb10-cxx20/share/bb10-cxx20.cmake >/dev/null
	cmake --build build/engine-bb10 -j"$(nproc)"
	unwinders=$(readelf --dyn-syms -W build/engine-bb10/bbmusicd | grep -ci " UND .*unwind" || true)
	if [ "$unwinders" != 0 ]; then
		echo "build-engine.sh: bbmusicd imports $unwinders unwinder symbols" >&2
		exit 1
	fi
	ls -l build/engine-bb10/bbmusicd'
