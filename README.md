<p align="center">
  <img src="app/assets/images/app_icon.png" width="120" alt="BBMusic icon">
</p>

<h1 align="center">BBMusic</h1>

<p align="center">
  <b>An unofficial, native YouTube Music client for BlackBerry 10</b><br>
  Made for the Q10 and the Classic. Works on the Passport and the rest of the BB10 family.
</p>

<p align="center">
  <img alt="Version" src="https://img.shields.io/badge/version-1.0-FF0000">
  <img alt="Platform" src="https://img.shields.io/badge/platform-BlackBerry%2010.3-222222">
  <img alt="UI" src="https://img.shields.io/badge/UI-Cascades%20%2F%20Qt%204.8-41CD52">
  <img alt="Engine" src="https://img.shields.io/badge/engine-C%2B%2B20-00599C">
  <img alt="License" src="https://img.shields.io/badge/license-GPLv3-blue">
</p>

---

BBMusic plays YouTube Music on a BlackBerry 10 phone. You can search, browse albums, artists,
playlists and podcasts, and play them in an app that looks and behaves like the stock BB10 music
player. Music keeps playing after you close the app, and you control it from the system's volume
overlay, the Active Frame, a headset or a Bluetooth car stereo.

> [!NOTE]
> BBMusic is an unofficial client, made independently. It is not affiliated with or endorsed by YouTube or Google.
> Using it may break YouTube's terms of service. Use it at your own risk.

## Features

**Listening**
- Search with suggestions, filtered by All, Songs, Artists and Podcasts
- Home and Explore feeds, and a Recent page with what you have played
- Album, playlist and artist pages with covers, including long playlists
- Podcasts: episodes by genre, a show's page, episodes in search
- Every track opens into a radio queue, so the music doesn't stop
- Favorites kept on the phone: each one is downloaded and plays offline, no account needed
- Optional YouTube sign-in with a code (as a TV does it): your playlists, liked songs, albums and
  artists, and Like from the player

**Playback**
- Gapless Opus playback with seek, started from the on-disk cache in under 0.2 s
- A Now Playing page laid out like the stock player, and a mini-player strip on every page
- A queue you can rearrange, shuffle, sort and clear; Play Next, Add to Queue, Start Radio
- Repeat, Repeat One and Shuffle
- An equalizer with ten presets
- SponsorBlock: skip the non-music parts of a music video (off by default)
- The queue comes back after a restart; playback rides out a network drop

**BlackBerry 10 integration**
- Music keeps playing with the app minimised or closed
- The volume overlay's play, pause, next and previous buttons, with the cover
- An Active Frame with the art, title, artist and time
- Bluetooth (AVRCP): title, artist and position on the car's screen; Bluetooth off pauses
- Keyboard shortcuts: Space, N, P on the player, and more on each page
- Dark theme with YouTube's red, sized for the 720 × 720 screens of the Q10 and the Classic
- Settings for the YouTube account, the content's country, the song cache size and SponsorBlock

## How it works

```
                     one .bar, one sandbox
 ┌─────────────────────────┐            ┌───────────────────────────┐
 │ bbmusic   (UI)          │  loopback  │ bbmusicd   (engine)       │
 │ Cascades · Qt 4.8 · C++ │◄──────────►│ C++20 · headless service  │◄──► YouTube Music
 │                         │  TCP+token │                           │
 └─────────────────────────┘            └─────────────┬─────────────┘
                                                      │
                                    libasound ◄───────┴───► data/music.db
```

BBMusic ships as one `.bar` with two processes:

- **`bbmusicd`, the engine**, written in C++20 and cross-compiled for BB10 (QNX on ARMv7) with
  the bb10-cxx20 clang toolchain, with no Qt in it. It speaks YouTube's InnerTube
  API to `music.youtube.com` over libcurl and wolfSSL (TLS 1.3, with its own root certificates,
  since BB10's are from 2013), fetches audio by DASH ranges or SABR/UMP, decodes Opus with
  libopus and writes PCM to the speaker itself. It runs as a BB10 headless service, so the music
  outlives the UI, and holds the system's now-playing service. It keeps the queue, the history,
  the favorites and the account in SQLite, and the songs in a cache on disk.
- **`bbmusic`, the UI**, a native Cascades app (Qt 4.8, C++ and QML). It starts the engine and
  talks to it over a token-protected loopback socket as NDJSON ([docs/protocol.md](docs/protocol.md)).

The engine stops by itself after ten minutes with nothing playing and no UI connected. It builds
and runs on the Mac too, where its tests run under ASan and UBSan, and its parsers of network
bytes (UMP, WebM, protobuf) have fuzz targets.

## Building

You need Docker, the local image `bb10-cxx20` and `yamsergey/bb10-ndk:0.6`.

```sh
docker build -t bbmusic-build docker   # once: the engine's toolchain and libraries
./scripts/test-engine.sh               # the engine's tests, on the PC
./scripts/build-engine.sh              # build/engine-bb10/bbmusicd for BB10
./scripts/build-app.sh                 # build/bbmusic.bar: engine + UI, unsigned
```

`build-app.sh` fetches Mozilla's root certificates, checks the QML against Qt 4.8 and packages
both processes. The resulting `.bar` is unsigned: install it on a BB10 phone in Development Mode.

## License

Copyright © 2026 Andrea Faggin.

BBMusic is free software: you can redistribute it and/or modify it under the terms of the
[GNU General Public License v3.0](LICENSE). Its protocol code is ported from
[YTMusicWP](https://github.com/Yasukoisreal/YTMusicWP), which is GPL-3.0.

The libraries keep their own licenses: libcurl is under the curl license, wolfSSL is GPL-3.0,
libopus is BSD, and yyjson is MIT.
