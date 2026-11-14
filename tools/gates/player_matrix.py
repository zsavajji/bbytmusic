#!/usr/bin/env python3
"""Gate 0.1 (PLAN.md §9): what /youtubei/v1/player returns per client today.

Anonymous, no PO token. For each client and each test video it reports whether the audio
formats carry direct urls (DASH path), only a SABR url, or nothing, and whether a direct url
really serves bytes. Raw responses go to --out (never commit them: the urls carry your IP).

    python3 tools/gates/player_matrix.py --out /tmp/gate01 [videoId ...]
"""
import argparse, json, os, sys, urllib.error, urllib.request

WEB_UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
          "Chrome/140.0.0.0 Safari/537.36")
SAFARI_UA = ("Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) "
             "Version/26.0 Safari/605.1.15")
# label, clientName, clientVersion, X-YouTube-Client-Name, host, user agent, extra client fields
# Versions: yt-dlp master's INNERTUBE_CLIENTS on 2026-11-14, plus YTMusicWP's and bbtube's.
CLIENTS = [
    ("android_vr", "ANDROID_VR", "1.65.10", 28, "www.youtube.com",
     "com.google.android.apps.youtube.vr.oculus/1.65.10 (Linux; U; Android 12L; eureka-user Build/SQ3A.220605.009.A1) gzip",
     {"deviceMake": "Oculus", "deviceModel": "Quest 3", "androidSdkVersion": 32, "osName": "Android", "osVersion": "12L"}),
    ("android", "ANDROID", "21.26.364", 3, "www.youtube.com",
     "com.google.android.youtube/21.26.364 (Linux; U; Android 11) gzip",
     {"androidSdkVersion": 30, "osName": "Android", "osVersion": "11"}),
    ("ios", "IOS", "21.26.4", 5, "www.youtube.com",
     "com.google.ios.youtube/21.26.4 (iPhone16,2; U; CPU iOS 18_3_2 like Mac OS X;)",
     {"deviceMake": "Apple", "deviceModel": "iPhone16,2", "osName": "iPhone", "osVersion": "18.3.2.22D82"}),
    ("visionos", "VISIONOS", "1.02", 101, "www.youtube.com", SAFARI_UA,
     {"deviceMake": "Apple", "deviceModel": "RealityDevice17,1", "osName": "visionOS", "osVersion": "26.5.23O471"}),
    ("tv", "TVHTML5", "7.20260707.07.00", 7, "www.youtube.com",
     "Mozilla/5.0 (ChromiumStylePlatform) Cobalt/25.lts.30.1034943-gold (unlike Gecko), Unknown_TV_Unknown_0/Unknown (Unknown, Unknown)",
     {}),
    ("tv_downgraded", "TVHTML5", "5.20260707", 7, "www.youtube.com",
     "Mozilla/5.0 (ChromiumStylePlatform) Cobalt/Version", {}),
    ("web_embedded", "WEB_EMBEDDED_PLAYER", "2.20260708.00.00", 56, "www.youtube.com", SAFARI_UA, {}),
    ("web_music", "WEB_REMIX", "1.20260707.12.00", 67, "music.youtube.com", WEB_UA, {}),
    ("web", "WEB", "2.20260708.00.00", 1, "www.youtube.com", WEB_UA, {}),
    ("mweb", "MWEB", "2.20260708.05.00", 2, "www.youtube.com",
     "Mozilla/5.0 (iPad; CPU OS 16_7_10 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.6 Mobile/15E148 Safari/604.1",
     {}),
]


def post(host, endpoint, body, headers):
    req = urllib.request.Request(
        "https://%s/youtubei/v1/%s?prettyPrint=false" % (host, endpoint),
        data=json.dumps(body).encode(), method="POST",
        headers=dict({"Content-Type": "application/json", "Accept-Encoding": "identity"}, **headers))
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        return e.code, {}


def walk(node, key):
    """Every value stored under `key`, anywhere in the tree."""
    if isinstance(node, dict):
        for k, v in node.items():
            if k == key:
                yield v
            yield from walk(v, key)
    elif isinstance(node, list):
        for v in node:
            yield from walk(v, key)


def find_song(query):
    """A YouTube Music 'song' (art track) for `query`, and the visitor data of the session."""
    status, data = post("music.youtube.com", "search",
                        {"context": {"client": {"clientName": "WEB_REMIX", "clientVersion": "1.20260707.12.00",
                                                "hl": "en", "gl": "US"}}, "query": query},
                        {"User-Agent": WEB_UA, "Origin": "https://music.youtube.com"})
    visitor = (data.get("responseContext") or {}).get("visitorData", "")
    for ep in walk(data, "watchEndpoint"):
        kind = next(walk(ep, "musicVideoType"), "")
        if kind == "MUSIC_VIDEO_TYPE_ATV" and ep.get("videoId"):
            return ep["videoId"], visitor, status
    return None, visitor, status


def fetch_range(url, ua, first, last):
    """HTTP status of a ranged GET, by header and, if that fails, by the &range= parameter."""
    for target, hdr in ((url, {"Range": "bytes=%d-%d" % (first, last)}),
                        ("%s&range=%d-%d" % (url, first, last), {})):
        try:
            req = urllib.request.Request(target, headers=dict({"User-Agent": ua}, **hdr))
            with urllib.request.urlopen(req, timeout=20) as r:
                return "%d/%dB" % (r.status, len(r.read()))
        except urllib.error.HTTPError as e:
            last_err = str(e.code)
        except Exception as e:  # noqa: BLE001 - a probe: report anything
            last_err = type(e).__name__
    return last_err


def probe(client, video, visitor, out):
    label, name, version, cid, host, ua, extra = client
    ctx = dict({"clientName": name, "clientVersion": version, "hl": "en", "gl": "US", "userAgent": ua}, **extra)
    if visitor:
        ctx["visitorData"] = visitor
    headers = {"User-Agent": ua, "X-YouTube-Client-Name": str(cid), "X-YouTube-Client-Version": version,
               "Origin": "https://" + host}
    if visitor:
        headers["X-Goog-Visitor-Id"] = visitor
    status, data = post(host, "player", {
        "context": {"client": ctx}, "videoId": video, "contentCheckOk": True, "racyCheckOk": True,
        "playbackContext": {"contentPlaybackContext": {"html5Preference": "HTML5_PREF_WANTS"}}}, headers)
    if out:
        with open(os.path.join(out, "%s.%s.json" % (video, label)), "w") as f:
            json.dump(data, f)
    row = {"client": label, "http": status}
    play = data.get("playabilityStatus") or {}
    row["status"] = play.get("status", "-") + (": " + play["reason"][:40] if play.get("reason") else "")
    sd = data.get("streamingData") or {}
    audio = [f for f in sd.get("adaptiveFormats", []) if f.get("mimeType", "").startswith("audio/")]
    row["audio itags"] = ",".join(str(f["itag"]) for f in audio) or "-"
    row["url"] = sum(1 for f in audio if "url" in f)
    row["cipher"] = sum(1 for f in audio if "signatureCipher" in f or "cipher" in f)
    ustreamer = next(walk(data.get("playerConfig") or {}, "videoPlaybackUstreamerConfig"), "")
    row["sabr"] = "yes" if sd.get("serverAbrStreamingUrl") and ustreamer else (
        "url only" if sd.get("serverAbrStreamingUrl") else "-")
    row["hls"] = "yes" if sd.get("hlsManifestUrl") else "-"
    row["itag18"] = "url" if any(f.get("itag") == 18 and "url" in f for f in sd.get("formats", [])) else "-"
    # Does a direct url serve bytes? The first 64 KB, then 64 KB from 1 MB in (PO-token
    # enforcement has been seen to start after the first chunk).
    row["251 head"] = row["251 at 1MB"] = "-"
    best = next((f for f in audio if f.get("itag") == 251 and "url" in f), None) or next(
        (f for f in audio if "url" in f), None)
    if best:
        row["251 head"] = "i%d %s" % (best["itag"], fetch_range(best["url"], ua, 0, 65535))
        if int(best.get("contentLength", 0)) > 1200000:
            row["251 at 1MB"] = fetch_range(best["url"], ua, 1048576, 1114111)
        row["ranges"] = "yes" if "initRange" in best and "indexRange" in best else "no"
    return row


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("videos", nargs="*", default=["dQw4w9WgXcQ"])
    ap.add_argument("--song", default="Never Gonna Give You Up Rick Astley",
                    help="search query used to find an art-track song to add to the test videos")
    ap.add_argument("--out", help="directory for the raw responses")
    args = ap.parse_args()
    if args.out:
        os.makedirs(args.out, exist_ok=True)
    song, visitor, status = find_song(args.song)
    print("search: http %s, visitorData %s, art track %s" % (status, "yes" if visitor else "NO", song))
    videos = args.videos + ([song] if song and song not in args.videos else [])
    cols = ["client", "http", "status", "audio itags", "url", "cipher", "sabr", "hls", "itag18",
            "251 head", "251 at 1MB", "ranges"]
    for video in videos:
        print("\n### %s\n" % video)
        print("| " + " | ".join(cols) + " |")
        print("|" + "---|" * len(cols))
        for client in CLIENTS:
            row = probe(client, video, visitor, args.out)
            print("| " + " | ".join(str(row.get(c, "-")) for c in cols) + " |", flush=True)


if __name__ == "__main__":
    sys.exit(main())
