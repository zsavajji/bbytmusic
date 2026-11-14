# BBMusic plan: Phase 1, the engine on the Mac

Part of [PLAN.md](../../PLAN.md) §9. Read PLAN.md §3–§5 and
[phase0.md](phase0.md) first: Phase 0 measured what this phase builds on, and its probes
(`tools/gates/*.py`, `*.cpp`) are the reference each piece here is checked against.

Started 2026-11-14, run unattended at the user's request ("proceed unattended as much as you
can… commit every relevant step… 1 opus and 2 sonnet subagents"). The main session writes the
contracts, reviews each piece and commits it; sub-agents write the code, each in folders of
its own.

## Rules for every file in `engine/`

- **C++20, no Qt.** It must build for the Mac (clang, the system's libcurl, Homebrew's opus)
  and for BB10 (`bb10-clang++` in the `bbmusic-build` image: static libcurl, wolfSSL and opus
  in `/opt/bbmusic`). Nothing platform-specific outside `src/audio/qsa_*` and `src/platform/`.
- **At most about 500 lines a file**, one class or one job per file (the user's rule). Split
  before it gets there.
- **No exception leaves a module's public function**: errors come back as `bool` + `std::string
  &err`, or an `Error` in a result struct. Parsers of network bytes never trust a length: check
  every read against the buffer.
- **No libm call and no allocation per audio sample** (phase0.md, gate 0.4).
- **zlib by its `.a`, never `-lz`**, on BB10 (phase0.md, gate 0.3).
- Third-party code is fetched and pinned by sha256 (`scripts/fetch-third-party.sh`), not
  committed: yyjson for JSON. No other new dependency without asking.
- Every module has tests in `engine/tests/<module>/*_test.cpp`: plain `main()` programs using
  `tests/check.h`, run by `ctest`, built with ASan and UBSan on the Mac. Tests that need the
  network carry the ctest label `live` and are not part of the default run.
- Fixtures: synthetic ones are committed. Recorded InnerTube JSON may be committed when it was
  fetched anonymously. **Never commit media bytes or signed stream urls** (they are someone's
  music, and the urls carry an IP address).
- Sub-agents do not commit, do not touch files outside their folders, and do not touch the
  phone. They build in a build folder of their own (`build/engine-<name>`).

## Layout

```
engine/
├── CMakeLists.txt          one static library per folder of src/, with its dependencies
│                           declared; one test program per tests/<module>/*_test.cpp
├── src/
│   ├── util/               log, json (yyjson behind a small view), ebml (id and size readers),
│   │                       base64, hex/sha1, time
│   ├── net/                http: libcurl behind Request/Response, streamed bodies, CA bundle
│   ├── proto/              protobuf writer and reader, by hand
│   ├── stream/             resolve (the /player clients), webm_index (init bytes → cues),
│   │                       dash_source, ump, sabr_source, source.h
│   ├── media/              webm_demux (bytes → Opus packets), opus_decoder
│   ├── audio/              sink.h, wav_sink, null_sink; later qsa_sink (BB10)
│   ├── innertube/          client (context, visitor data), items (the renderers → Item),
│   │                       search, next
│   ├── player/             queue, player (source → demux → decoder → sink), later
│   ├── store/              SQLite, later
│   ├── server/             the NDJSON server on loopback, later
│   └── main.cpp            bbmusicd
└── tests/                  check.h, fixtures/, <module>/*_test.cpp
```

Dependencies, and nothing else: `net → util`; `stream → proto, net, util`; `media → util`;
`audio → util`; `innertube → net, util`; `player → stream, media, audio`; `store → util`;
`server → all`.

## Contracts between the pieces

Everything is in namespace `bbm`. These are fixed; a sub-agent that needs one changed says so
in its report instead of changing it.

```cpp
// stream/source.h — one track's audio as the container's own bytes
struct StreamInfo {               // what resolve() learned from /player
    std::string video_id, client;        // "VISIONOS"
    int itag = 0; std::string mime;      // 251, audio/webm; codecs="opus"
    int64_t content_length = 0, duration_ms = 0, last_modified = 0;
    int64_t init_end = 0, index_start = 0, index_end = 0;   // initRange.end, indexRange
    std::string url;                     // direct url, "" if the client gave none
    std::string sabr_url, ustreamer_config;   // "" if no SABR
    std::string user_agent, client_version; int client_id = 0;
    double loudness_db = 0;
};
struct Segment { int index = 0; int64_t start_ms = 0, duration_ms = 0; std::vector<uint8_t> bytes; };
class Source {
public:
    virtual ~Source() = default;
    virtual bool open(std::string &err) = 0;                 // fetches the init bytes and the index
    virtual const std::vector<uint8_t> &init() const = 0;    // the file's start, through the Cues
    virtual int segment_count() const = 0;                   // clusters in the file
    virtual int64_t duration_ms() const = 0;
    virtual int segment_at(int64_t ms) const = 0;            // the segment that holds ms
    virtual bool read(int index, Segment &out, std::string &err) = 0;   // blocks; any index, any order
    virtual void cancel() = 0;                               // from another thread: unblocks read()
};
// init() + read(0) + read(1) + … is the same file whichever Source gave it (gate 0.2).

// media: bytes in, packets out, then PCM
struct Packet { int64_t pts_ms = 0; const uint8_t *data = nullptr; size_t size = 0; };
// WebmDemuxer: feed(init bytes) once, then feed(segment bytes) in any order of whole segments;
// next(Packet&) hands packets out until the fed bytes are used up. Exposes channels, pre_skip.
// OpusDecoder: decode(Packet, std::vector<int16_t>& interleaved) at 48 kHz; reset() after a seek.

// audio/sink.h
class Sink {
public:
    virtual ~Sink() = default;
    virtual bool open(int rate, int channels, std::string &err) = 0;
    virtual bool write(const int16_t *interleaved, size_t frames, std::string &err) = 0;   // blocks
    virtual void close() = 0;
};

// innertube/items.h — all the UI ever sees of a renderer
struct Item {
    std::string kind;        // "song" "video" "album" "artist" "playlist" "station"
    std::string id;          // videoId, or a browse id for the other kinds
    std::string title, subtitle, thumb;   // subtitle: "Artist • Album" as YouTube gives it
    std::string artist, album, artist_id, album_id, playlist_id, params;
    int duration_s = 0; bool is_explicit = false;
};
```

## Steps and who builds them

| Step | What | Folder | Who |
|---|---|---|---|
| 1.1a | CMake, `check.h`, `util/`, the two build scripts, a `bbmusicd` that prints its version | `engine/` root, `util/`, `scripts/` | sonnet, first |
| 1.2 | `net/http` | `net/` | same |
| 1.5 | resolve: the client table of phase0.md, `/player`, `StreamInfo` | `stream/` | opus |
| 1.7 | `webm_index`, `dash_source` | `stream/` | opus |
| 1.8 | protobuf by hand, UMP parser | `proto/`, `stream/` | opus |
| 1.9 | `sabr_source` | `stream/` | opus |
| 1.6 | `webm_demux`, `opus_decoder`, `wav_sink`, `null_sink` | `media/`, `audio/` | sonnet A |
| 1.3 | InnerTube client: `WEB_REMIX` context, visitor data, the Item shape | `innertube/` | sonnet B |
| 1.4 | search: filters, continuations, suggestions | `innertube/` | sonnet B |
| 1.12 | `next`: the watch queue and radio | `innertube/` | sonnet B |
| 1.10 | segment cache on disk, with a fixed cap | `stream/` | after the first wave |
| 1.11 | player and queue | `player/` | after the first wave |
| 1.13 | store | `store/` | after the first wave |
| 1.1b | the NDJSON server and `main.cpp` | `server/` | after the first wave |
| 1.14 | fuzz targets, fixture recorder | `tests/fuzz/` | last |

What "done" means for a step: its tests pass under ASan/UBSan through
`scripts/test-engine.sh`; its `live` test, if it has one, passed once and the report says so
with the numbers; `scripts/build-engine.sh` still cross-builds for BB10; no file over about 500
lines.

## Decisions waiting for the user

Marked here as they come up; each has what was done meanwhile.

1. **Recorded InnerTube responses in the repo.** Tests for search and `next` need real
   responses. Meanwhile: fetched anonymously, committed under `engine/tests/fixtures/`. Say if
   you'd rather they stay out of git.
2. **yyjson** (MIT, one `.c` and one `.h`) as the JSON parser, fetched and pinned. PLAN.md named
   it; it is the only third-party source besides the three libraries in the Docker image.
3. **DASH before SABR.** While `VISIONOS` gives direct urls the player uses DASH and falls back
   to SABR. SABR is built and tested from the start, as you asked, but it is not the default.
   Say if you want SABR first.
4. **Branch.** Phase 1 is on `phase1`; `master` has the plan and Phase 0. Nothing is pushed
   (there is no remote).

## Results

### 1.1a and 1.2 — skeleton, util, net (commit 72f3649, 2026-11-14)

`scripts/test-engine.sh` passes on the Mac with ASan and UBSan; the live HTTP test (GET of
music.youtube.com, an InnerTube search, a streamed GET) passed in 2.4 s; `scripts/build-engine.sh`
cross-builds `bbmusicd` for BB10: 2.4 MB, no unwinder import. yyjson 0.13.0 is pinned by
sha256. Things to know when using it:

- Configure with `-G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug`: a bare `cmake -S engine -B …`
  picked a multi-config generator on this Mac and `ctest` then found no programs.
  `scripts/test-engine.sh` passes the build type; the generator is still the default there.
- Test names: `tests/<module>/<name>_test.cpp` is ctest `<module>.<name>`; `<module>_tests`
  builds one module's tests alone.
- `json_quote()` returns the string with its quotes. `Json::find(key)` is a depth-first search.
- On BB10 the link order is fixed in CMake: curl, wolfSSL, opus, then `libz.a` by path.
- Not run on the phone: this build's network code (gate 0.3 ran the same libraries).

### 1.6 — WebM demuxer, Opus decoder, sinks (commit d65af25, 2026-11-14)

A 5 s synthetic tone decodes to 240 648 frames at 438.8 zero-crossing pairs a second; feeding
it in chunks of 1, 7, 1000 and 65 536 bytes gives the same PCM; truncated, random and
single-byte-corrupted input never crashes under ASan. Things to know:

- `feed()` parses ahead to the first block, so `channels()` and `pre_skip()` are valid right
  after the init bytes. A malformed stream makes `feed()` fail until `reset()`.
- After a seek the player calls `WebmDemuxer::reset()`, `OpusDecoder::reset()`, then
  `set_skip()` with the pre-roll (`seek_preroll_ns()`, about 80 ms): `reset()` clears the skip.
- `Packet` lives in `media/webm_demux.h`.
- Not yet tried: a real YouTube file (Cues before the clusters, clusters of unknown size are
  covered only by a hand-made buffer), mono, short packets, BB10. The live test of step 1.11
  is where a real file first goes through it.
