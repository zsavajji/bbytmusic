#!/bin/sh
# Builds build/bbmusic-gate.bar, the Phase 0 gate app (gateapp/): the headless engine
# bbmusicd-gate in the bbmusic-build image, the Cascades UI in the Native SDK 10.3.1 image, and
# the unsigned package of both. Each package gets the next build id (build/gate-buildid.txt).
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMAGE=${BB10_NDK_IMAGE:-yamsergey/bb10-ndk:0.6}
ASSETS=$ROOT/gateapp/app/assets
mkdir -p "$ROOT/build"

# The roots the engine's TLS check trusts, and a minute of pink noise plus a 440 Hz tone for it to play.
[ -s "$ASSETS/cacert.pem" ] || curl -fsSL https://curl.se/ca/cacert.pem -o "$ASSETS/cacert.pem"
[ -s "$ASSETS/test.webm" ] || ffmpeg -loglevel error -y \
	-f lavfi -i anoisesrc=color=pink:sample_rate=48000:duration=60:amplitude=0.4 \
	-f lavfi -i sine=frequency=440:sample_rate=48000:duration=60 \
	-filter_complex "[0:a][1:a]amerge=inputs=2,pan=stereo|c0=c0+0.3*c1|c1=0.8*c0+0.3*c1[a]" \
	-map "[a]" -c:a libopus -b:a 128k "$ASSETS/test.webm"

# The engine. zlib by its .a, never -lz: the phone's libz.so.2 exports a libgcc unwinder that
# hijacks C++ exceptions (docker/Dockerfile).
docker run --rm -v "$ROOT:/src" -w /src bbmusic-build sh -euc '
	bb10-clang++ -O2 -DCURL_STATICLIB -I/opt/bbmusic/include gateapp/engine/*.cpp -o build/bbmusicd-gate \
		-L/opt/bbmusic/lib -lcurl -lwolfssl -lopus /opt/bb10-sdk/target/qnx6/armle-v7/usr/lib/libz.a \
		-lsocket -lasound -laudio_manager -lbps
	unwinders=$(readelf --dyn-syms -W build/bbmusicd-gate | grep -ci " UND .*unwind" || true)
	if [ "$unwinders" != 0 ]; then
		echo "build-gate.sh: bbmusicd-gate imports $unwinders unwinder symbols" >&2
		exit 1
	fi
	ls -l build/bbmusicd-gate'

[ -f "$ROOT/build/gate-buildid.txt" ] || echo 1 >"$ROOT/build/gate-buildid.txt"
docker run --rm -v "$ROOT:/src" -w /src "$IMAGE" bash -c '
	source /root/bbndk/bbndk-env_10_3_1_995.sh >/dev/null
	set -eu
	mkdir -p build/gate-ui
	# lupdate parses QML with Qt 4.8, as the phone does; a QML error quits the app at start.
	if lupdate gateapp/app/assets/*.qml -ts /tmp/qml-check.ts 2>&1 | grep -A2 "error:"; then
		echo "build-gate.sh: QML that Qt 4.8 cannot parse" >&2
		exit 1
	fi
	(cd build/gate-ui && qmake -spec blackberry-armv7le-qcc CONFIG+=device ../../gateapp/app/gate.pro >/dev/null &&
		make -j"$(nproc)" >/dev/null)
	ls -l build/gate-ui/app/bbmusic-gate
	cd gateapp/app && blackberry-nativepackager -package ../../build/bbmusic-gate.bar \
		-buildIdFile ../../build/gate-buildid.txt bar-descriptor.xml -configuration Device-Release
	rm -f assets/.assets.index'
unzip -p "$ROOT/build/bbmusic-gate.bar" META-INF/MANIFEST.MF | grep -E '^Package-(Id|Version):'
unzip -l "$ROOT/build/bbmusic-gate.bar"
