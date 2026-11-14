#!/bin/sh
# Fetches the third-party sources the engine compiles, pinned by sha256, into engine/third_party/.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
YYJSON_TAG=0.13.0
YYJSON_C_SHA=d2d58ef0a3b2267862dc363832c4f3185fd375aea933d5d3419d1a940d6ece2e
YYJSON_H_SHA=ef803cda5c06b8962face6dfa39c3284b3bcf3e73f4d7317664690cc779679ca

sha() { if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }

fetch() {  # dir url sha file
	dest=$1/$4
	mkdir -p "$1"
	[ -f "$dest" ] && [ "$(sha "$dest")" = "$3" ] && return 0
	curl -fsSL "$2" -o "$dest.tmp"
	if [ "$(sha "$dest.tmp")" != "$3" ]; then
		rm -f "$dest.tmp"
		echo "fetch-third-party: $4 does not match its pinned sha256" >&2
		exit 1
	fi
	mv "$dest.tmp" "$dest"
}

Y=$ROOT/engine/third_party/yyjson
base=https://raw.githubusercontent.com/ibireme/yyjson/$YYJSON_TAG/src
fetch "$Y" "$base/yyjson.c" "$YYJSON_C_SHA" yyjson.c
fetch "$Y" "$base/yyjson.h" "$YYJSON_H_SHA" yyjson.h
