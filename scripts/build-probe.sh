#!/bin/sh
# Cross-builds bb10_probe for BB10 in the bbmusic-build image into build/engine-bb10, fails if
# it imports a libgcc unwinder (the -lz trap, phase0.md gate 0.3), and prints its size.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
"$ROOT/scripts/fetch-third-party.sh"
docker run --rm -v "$ROOT:/src" -w /src bbmusic-build sh -euc '
	cmake -S engine -B build/engine-bb10 -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE=/opt/bb10-cxx20/share/bb10-cxx20.cmake >/dev/null
	cmake --build build/engine-bb10 --target bb10_probe -j"$(nproc)"
	unwinders=$(readelf --dyn-syms -W build/engine-bb10/bb10_probe | grep -ci " UND .*unwind" || true)
	if [ "$unwinders" != 0 ]; then
		echo "build-probe.sh: bb10_probe imports $unwinders unwinder symbols" >&2
		exit 1
	fi
	ls -l build/engine-bb10/bb10_probe'
