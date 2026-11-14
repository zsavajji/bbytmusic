#!/bin/sh
# Builds and runs the libFuzzer targets of engine/tests/fuzz. Usage: fuzz-engine.sh [seconds per target, 60]
# Needs Homebrew's llvm (Apple's clang has no libFuzzer) and engine/third_party/yyjson
# (scripts/fetch-third-party.sh). Everything lands in build/fuzz/; exit 1 if any target crashed.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SECS=${1:-60}
CXX=/opt/homebrew/opt/llvm/bin/clang++
CC=/opt/homebrew/opt/llvm/bin/clang
E=$ROOT/engine; S=$E/src; F=$E/tests/fixtures
B=$ROOT/build/fuzz
SAN="-g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined"
INC="-I$S -I$E/tests/fuzz -I$E/third_party/yyjson"
[ -x "$CXX" ] || { echo "fuzz-engine: $CXX missing (brew install llvm)" >&2; exit 2; }
[ -f "$E/third_party/yyjson/yyjson.c" ] || { echo "fuzz-engine: run scripts/fetch-third-party.sh first" >&2; exit 2; }
mkdir -p "$B/crashes"

JSON="$S/util/json.cpp"
# name | sources | extra link flags | max_len
targets="
webm_demux|$S/media/webm_demux.cpp||65536
webm_index|$S/stream/webm_index.cpp||16384
ump|$S/stream/ump.cpp||4096
pb|$S/proto/pb.cpp||1024
np_protocol|$S/platform/np_protocol.cpp $JSON||1024
player_response|$S/stream/resolve.cpp $S/stream/clients.cpp $S/net/http.cpp $S/util/log.cpp $JSON|-lcurl|65536
innertube_json|$S/innertube/search.cpp $S/innertube/next.cpp $S/innertube/items.cpp $S/innertube/client.cpp $S/net/http.cpp $S/util/log.cpp $JSON|-lcurl|65536
"

# yyjson is C: compile it once, without the sanitizers' fuzzing instrumentation getting in the way
$CC -g -O1 -w -fsanitize=address,undefined -I"$E/third_party/yyjson" -c "$E/third_party/yyjson/yyjson.c" -o "$B/yyjson.o" || exit 2

seed() {  # name file...: copies the files into the corpus (a chunk-size byte first where the target wants one)
	name=$1; shift
	d=$B/corpus/$name; mkdir -p "$d"
	for f in "$@"; do [ -f "$f" ] && cp "$f" "$d/$(basename "$f")"; done
}
prefixed() {  # name prefix-byte-octal file...
	name=$1; pre=$2; shift 2
	d=$B/corpus/$name; mkdir -p "$d"
	for f in "$@"; do { printf "$pre"; cat "$f"; } > "$d/$(basename "$f").$pre.seed"; done
}

crashed=0
echo "$targets" | while IFS='|' read -r name srcs libs maxlen; do
	[ -n "$name" ] || continue
	exe=$B/${name}_fuzz
	echo "build $name" >&2
	# shellcheck disable=SC2086
	$CXX -std=c++20 $SAN $INC $E/tests/fuzz/${name}_fuzz.cpp $srcs "$B/yyjson.o" $libs -o "$exe" || { echo "$name: BUILD FAILED"; echo build >> "$B/.failed"; continue; }
	case $name in
	webm_demux) prefixed $name '\000' $F/media/tone.webm; prefixed $name '\100' $F/media/tone.webm; prefixed $name '\007' $F/media/tone.webm ;;
	webm_index) prefixed $name '\001' $F/media/tone.webm; head -c 4000 "$F/media/tone.webm" > "$B/head.tmp"; prefixed $name '\377' "$B/head.tmp" ;;
	ump) d=$B/corpus/ump; mkdir -p $d
		printf '\000\024\003abc\025\002xy\026\000' > $d/s1       # chunk=whole: MEDIA_HEADER, MEDIA, MEDIA_END
		printf '\003\024\003abc\025\002xy\200\200\200\200\200' > $d/s2 ;;
	pb) d=$B/corpus/pb; mkdir -p $d
		printf '\010\226\001\022\007\010\001\022\003abc\035\000\000\200\077' > $d/s1   # varint, nested message, fixed32
		printf '\011\001\000\000\000\000\000\000\000\032\000' > $d/s2 ;;
	np_protocol) d=$B/corpus/np_protocol; mkdir -p $d
		printf '@control\nmsg::track\ndat::play\nid::1\n' > $d/s1
		printf 'res::register\nerr::bad thing\nid::1\n' > $d/s2
		printf 'msg::revoke\n' > $d/s3
		printf 'msg::track\ndat::sendData\n' > $d/s4 ;;
	player_response) prefixed $name '\000' $F/stream/player_visionos.json; prefixed $name '\001' $F/stream/player_visionos.json ;;
	innertube_json) seed $name $F/innertube/*.json ;;
	esac
	mkdir -p "$B/corpus/$name"
	log=$B/$name.log
	( cd "$B/crashes" && "$exe" -max_total_time="$SECS" -max_len="$maxlen" -timeout=10 -rss_limit_mb=2048 \
		-artifact_prefix="$B/crashes/${name}-" "$B/corpus/$name" > "$log" 2>&1 )
	rc=$?
	execs=$(grep -E '^#[0-9]+' "$log" | tail -1 | sed -E 's/^#([0-9]+).*/\1/')
	if [ $rc -ne 0 ]; then
		echo "$name: CRASHED after ${execs:-0} execs (see $log, input in $B/crashes/)"
		echo crash >> "$B/.failed"
	else
		echo "$name: ${execs:-?} execs, no crash"
	fi
done
[ -f "$B/.failed" ] && { rm -f "$B/.failed"; exit 1; }
exit 0
