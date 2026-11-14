#!/bin/sh
# Builds build/bbmusic.bar: the engine (scripts/build-engine.sh, skipped with SKIP_ENGINE=1 when
# build/engine-bb10/bbmusicd exists), the Cascades UI in the Native SDK 10.3.1 image, and the
# unsigned package of both. Each package gets the next build id (build/app-buildid.txt).
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
# The roots the engine's TLS trusts (BB10's own store is from 2013): Mozilla's bundle, fetched,
# not committed. The engine reads app/native/assets/cacert.pem.
CA=$ROOT/app/assets/cacert.pem
[ -s "$CA" ] || { [ -s "$ROOT/gateapp/app/assets/cacert.pem" ] && cp "$ROOT/gateapp/app/assets/cacert.pem" "$CA"; } \
	|| curl -fsSL https://curl.se/ca/cacert.pem -o "$CA"
IMAGE=${BB10_NDK_IMAGE:-yamsergey/bb10-ndk:0.6}
mkdir -p "$ROOT/build"

if [ "${SKIP_ENGINE:-0}" = 1 ] && [ -x "$ROOT/build/engine-bb10/bbmusicd" ]; then
	echo "build-app.sh: using the existing build/engine-bb10/bbmusicd"
else
	"$ROOT/scripts/build-engine.sh"
fi

[ -f "$ROOT/build/app-buildid.txt" ] || echo 1 >"$ROOT/build/app-buildid.txt"
docker run --rm -v "$ROOT:/src" -w /src "$IMAGE" bash -c '
	source /root/bbndk/bbndk-env_10_3_1_995.sh >/dev/null
	set -eu
	mkdir -p build/app-ui
	# lupdate parses QML with Qt 4.8, as the phone does; a QML error quits the app at start.
	if lupdate app/assets/*.qml -ts /tmp/qml-check.ts 2>&1 | grep -A2 "error:"; then
		echo "build-app.sh: QML that Qt 4.8 cannot parse" >&2
		exit 1
	fi
	(cd build/app-ui && qmake -spec blackberry-armv7le-qcc CONFIG+=device ../../app/bbmusic.pro >/dev/null &&
		make -j"$(nproc)" >/dev/null)
	ls -l build/app-ui/app/bbmusic
	cd app && blackberry-nativepackager -package ../build/bbmusic.bar \
		-buildIdFile ../build/app-buildid.txt bar-descriptor.xml -configuration Device-Release
	rm -f assets/.assets.index'
unzip -p "$ROOT/build/bbmusic.bar" META-INF/MANIFEST.MF | grep -E '^Package-(Id|Version):'
unzip -l "$ROOT/build/bbmusic.bar"
