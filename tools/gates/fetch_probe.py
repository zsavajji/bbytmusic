#!/usr/bin/env python3
"""Gate 0.2 (PLAN.md §9): one track fetched twice, by DASH byte ranges and by SABR/UMP.

Both results are WebM/Opus files decoded to WAV by ffmpeg; the gate passes when the two WAVs
have the same length and a seek to 2:00 lands at 2:00 on both paths. This is a probe, not the
engine: it is the reference the C++ port (steps 1.6-1.9) is checked against.

    python3 tools/gates/fetch_probe.py --out /tmp/gate02 [videoId]

Schema: LuanRT/googlevideo protos (video_playback_abr_request, media_header, ...), read
2026-11-14. Never commit --out: urls and responses carry your IP.
"""
import argparse, base64, json, os, struct, subprocess, sys, time, urllib.error, urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from player_matrix import CLIENTS, find_song, post, walk  # noqa: E402

ITAG = 251
SEEK_MS = 120000


# ---------- protobuf, by hand ----------
def varint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def f_varint(field, n):
    return varint(field << 3) + varint(n)


def f_bytes(field, data):
    if isinstance(data, str):
        data = data.encode()
    return varint(field << 3 | 2) + varint(len(data)) + data


def f_float(field, x):
    return varint(field << 3 | 5) + struct.pack("<f", x)


def decode(buf):
    """{field: [value, ...]}; varints as int, length-delimited as bytes."""
    out, pos = {}, 0
    while pos < len(buf):
        key, pos = read_varint(buf, pos)
        field, wire = key >> 3, key & 7
        if wire == 0:
            val, pos = read_varint(buf, pos)
        elif wire == 2:
            n, pos = read_varint(buf, pos)
            val, pos = bytes(buf[pos:pos + n]), pos + n
        elif wire == 5:
            val, pos = buf[pos:pos + 4], pos + 4
        elif wire == 1:
            val, pos = buf[pos:pos + 8], pos + 8
        else:
            raise ValueError("wire type %d" % wire)
        out.setdefault(field, []).append(val)
    return out


def read_varint(buf, pos):
    n = shift = 0
    while True:
        b = buf[pos]
        pos += 1
        n |= (b & 0x7F) << shift
        shift += 7
        if not b & 0x80:
            return n, pos


def one(msg, field, default=0):
    return msg[field][0] if field in msg else default


# ---------- UMP ----------
def ump_varint(buf, pos):
    b0 = buf[pos]
    if b0 < 128:
        return b0, pos + 1
    if b0 < 192:
        return (b0 & 0x3F) + 64 * buf[pos + 1], pos + 2
    if b0 < 224:
        return (b0 & 0x1F) + 32 * (buf[pos + 1] + 256 * buf[pos + 2]), pos + 3
    if b0 < 240:
        return (b0 & 0x0F) + 16 * (buf[pos + 1] + 256 * (buf[pos + 2] + 256 * buf[pos + 3])), pos + 4
    return struct.unpack_from("<I", buf, pos + 1)[0], pos + 5


def ump_parts(buf):
    pos = 0
    while pos < len(buf):
        kind, pos = ump_varint(buf, pos)
        size, pos = ump_varint(buf, pos)
        yield kind, buf[pos:pos + size]
        pos += size


# ---------- WebM (EBML) ----------
def ebml_id(buf, pos):
    n = 8 - buf[pos].bit_length() + 1
    return int.from_bytes(buf[pos:pos + n], "big"), pos + n


def ebml_size(buf, pos):
    n = 8 - buf[pos].bit_length() + 1
    val = int.from_bytes(buf[pos:pos + n], "big") & ((1 << (7 * n)) - 1)
    return (None if val == (1 << (7 * n)) - 1 else val), pos + n


def webm_cues(head):
    """[(time_ms, absolute byte offset of the cluster)] from a file's init + index bytes."""
    pos, seg_start, scale, cues = 0, None, 1000000, []
    end = len(head)
    while pos < end:
        eid, pos = ebml_id(head, pos)
        size, pos = ebml_size(head, pos)
        if eid == 0x18538067:  # Segment: descend
            seg_start = pos
            continue
        if eid == 0x1549A966:  # Info
            p = pos
            while p < pos + size:
                cid, p = ebml_id(head, p)
                csz, p = ebml_size(head, p)
                if cid == 0x2AD7B1:
                    scale = int.from_bytes(head[p:p + csz], "big")
                p += csz
        if eid == 0x1C53BB6B:  # Cues
            p = pos
            while p < pos + size:
                cid, p = ebml_id(head, p)
                csz, p = ebml_size(head, p)
                if cid == 0xBB:  # CuePoint
                    t = off = None
                    q = p
                    while q < p + csz:
                        did, q = ebml_id(head, q)
                        dsz, q = ebml_size(head, q)
                        if did == 0xB3:
                            t = int.from_bytes(head[q:q + dsz], "big")
                        elif did == 0xB7:  # CueTrackPositions
                            r = q
                            while r < q + dsz:
                                fid, r = ebml_id(head, r)
                                fsz, r = ebml_size(head, r)
                                if fid == 0xF1:
                                    off = int.from_bytes(head[r:r + fsz], "big")
                                r += fsz
                        q += dsz
                    cues.append((t * scale // 1000000, seg_start + off))
                p += csz
        pos += size
    return cues


# ---------- HTTP ----------
def http(url, ua, headers=None, body=None):
    req = urllib.request.Request(url, data=body, headers=dict({"User-Agent": ua}, **(headers or {})))
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.read()


def resolve(video, label="visionos"):
    client = next(c for c in CLIENTS if c[0] == label)
    _, name, version, cid, host, ua, extra = client
    _, visitor, _ = find_song("test")
    ctx = dict({"clientName": name, "clientVersion": version, "hl": "en", "gl": "US", "userAgent": ua,
                "visitorData": visitor}, **extra)
    status, data = post(host, "player", {
        "context": {"client": ctx}, "videoId": video, "contentCheckOk": True, "racyCheckOk": True},
        {"User-Agent": ua, "X-YouTube-Client-Name": str(cid), "X-YouTube-Client-Version": version,
         "X-Goog-Visitor-Id": visitor, "Origin": "https://" + host})
    sd = data.get("streamingData") or {}
    fmt = next((f for f in sd.get("adaptiveFormats", []) if f.get("itag") == ITAG), None)
    if not fmt:
        sys.exit("no itag %d: http %s %s" % (ITAG, status, data.get("playabilityStatus")))
    return {"client": client, "fmt": fmt, "sabr_url": sd.get("serverAbrStreamingUrl"),
            "ustreamer": next(walk(data.get("playerConfig") or {}, "videoPlaybackUstreamerConfig"), "")}


# ---------- DASH ----------
def dash_fetch(info, out, start_ms=0):
    fmt, ua = info["fmt"], info["client"][5]
    total = int(fmt["contentLength"])
    index_end = int(fmt["indexRange"]["end"])
    head = http(fmt["url"], ua, {"Range": "bytes=0-%d" % index_end})
    cues = webm_cues(head)
    first = index_end + 1
    if start_ms:
        first = max(off for t, off in cues if t <= start_ms)
    requests, body, pos = 1, bytearray(), first
    while pos < total:
        last = min(pos + (1 << 20), total) - 1
        body += http(fmt["url"], ua, {"Range": "bytes=%d-%d" % (pos, last)})
        requests, pos = requests + 1, last + 1
    with open(out, "wb") as f:
        f.write(head)
        f.write(body)
    return {"bytes": len(head) + len(body), "requests": requests, "cues": len(cues)}


# ---------- SABR ----------
def format_id(fmt):
    msg = f_varint(1, fmt["itag"]) + f_varint(2, int(fmt["lastModified"]))
    return msg + (f_bytes(3, fmt["xtags"]) if fmt.get("xtags") else b"")


def sabr_request(info, player_ms, buffered, cookie, initialized):
    _, _, version, cid, _, _, extra = info["client"]
    fid = format_id(info["fmt"])
    state = (f_varint(21, 360) + f_varint(28, player_ms) + f_varint(34, 1) + f_float(35, 1.0)
             + f_varint(40, 1))  # 40: enabled track types, 1 = audio only
    body = f_bytes(1, state)
    if initialized:
        body += f_bytes(2, fid)
    if buffered:
        first, last, start_ms, dur_ms = buffered
        body += f_bytes(3, f_bytes(1, fid) + f_varint(2, start_ms) + f_varint(3, dur_ms)
                        + f_varint(4, first) + f_varint(5, last))
    body += f_bytes(5, base64.urlsafe_b64decode(info["ustreamer"] + "=" * (-len(info["ustreamer"]) % 4)))
    body += f_bytes(16, fid)
    client = f_varint(16, cid) + f_bytes(17, version)
    if extra.get("osName"):
        client += f_bytes(18, extra["osName"]) + f_bytes(19, extra["osVersion"])
    ctx = f_bytes(1, client) + (f_bytes(3, cookie) if cookie else b"")
    return body + f_bytes(19, ctx)


def sabr_fetch(info, out, start_ms=0, max_requests=400, max_segments=None):
    ua, url = info["client"][5], info["sabr_url"]
    init, segments, headers, pending = None, {}, {}, {}
    cookie, end_seg, initialized, empties, log = None, None, False, 0, []
    player_ms, rn = start_ms, 0
    while rn < max_requests:
        rn += 1
        buffered = None
        if segments:
            seqs = sorted(segments)
            first = segments[seqs[0]]
            last = segments[seqs[-1]]
            buffered = (seqs[0], seqs[-1], first["start"], last["start"] + last["dur"] - first["start"])
            player_ms = last["start"] + last["dur"]
        body = sabr_request(info, player_ms, buffered, cookie, initialized)
        try:
            raw = http("%s&rn=%d" % (url, rn), ua, {
                "Content-Type": "application/x-protobuf", "Accept": "application/vnd.yt-ump",
                "Accept-Encoding": "identity"}, body)
        except urllib.error.HTTPError as e:
            log.append("rn=%d http %d" % (rn, e.code))
            break
        got, backoff = 0, 0
        for kind, data in ump_parts(raw):
            if kind == 20:  # MEDIA_HEADER
                h = decode(data)
                headers[one(h, 1)] = h
                pending[one(h, 1)] = bytearray()
            elif kind == 21:  # MEDIA: header id, then bytes
                pending.setdefault(data[0], bytearray()).extend(data[1:])
            elif kind == 22:  # MEDIA_END
                h, payload = headers.pop(data[0], {}), bytes(pending.pop(data[0], b""))
                if one(h, 3) != ITAG:
                    continue
                if one(h, 8):
                    init, initialized = payload, True
                elif one(h, 9) not in segments:
                    # WebM headers carry no start_ms/duration_ms (11, 12): only time_range (15).
                    tr = decode(one(h, 15, b""))
                    scale = one(tr, 3, 1000)
                    segments[one(h, 9)] = {"start": one(h, 11) or one(tr, 1) * 1000 // scale,
                                           "dur": one(h, 12) or one(tr, 2) * 1000 // scale, "data": payload}
                    got += 1
            elif kind == 35:  # NEXT_REQUEST_POLICY
                p = decode(data)
                backoff, cookie = one(p, 4), one(p, 7, cookie)
            elif kind == 42:  # FORMAT_INITIALIZATION_METADATA
                m = decode(data)
                if one(decode(one(m, 2, b"")), 1) == ITAG:
                    end_seg = one(m, 4)
            elif kind == 43:  # SABR_REDIRECT
                url = one(decode(data), 1).decode()
                log.append("rn=%d redirect" % rn)
            elif kind == 44:
                e = decode(data)
                log.append("rn=%d SABR_ERROR %s code %s" % (rn, one(e, 1, b"").decode(), one(e, 2)))
            elif kind == 45:
                s = decode(data)
                log.append("rn=%d SABR_SEEK to %d/%d" % (rn, one(s, 1), one(s, 2)))
            elif kind == 46:
                log.append("rn=%d RELOAD_PLAYER_RESPONSE" % rn)
            elif kind == 58:
                status = one(decode(data), 1)
                if status != 1:
                    log.append("rn=%d STREAM_PROTECTION_STATUS %d" % (rn, status))
            # 47 playback start policy, 49 bandwidth sampling hint, 51 selectable formats,
            # 52 request identifier, 53 request cancellation policy, 57 SABR context update
            elif kind not in (47, 49, 51, 52, 53, 57):
                log.append("rn=%d part %d (%dB)" % (rn, kind, len(data)))
        empties = 0 if got else empties + 1
        if (end_seg and segments and max(segments) >= end_seg) or empties >= 5:
            break
        if max_segments and len(segments) >= max_segments:
            break
        if backoff:
            time.sleep(backoff / 1000.0)
    seqs = sorted(segments)
    with open(out, "wb") as f:
        f.write(init or b"")
        for s in seqs:
            f.write(segments[s]["data"])
    gaps = [s for a, s in zip(seqs, seqs[1:]) if s != a + 1]
    return {"bytes": len(init or b"") + sum(len(segments[s]["data"]) for s in seqs), "requests": rn,
            "segments": len(seqs), "first": seqs[0] if seqs else None, "last": seqs[-1] if seqs else None,
            "end_seg": end_seg, "gaps": gaps, "first_start_ms": segments[seqs[0]]["start"] if seqs else None,
            "log": log[:12]}


# ---------- decode and compare ----------
def to_wav(src):
    wav = src.rsplit(".", 1)[0] + ".wav"
    r = subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", src, wav], capture_output=True, text=True)
    dur = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", wav],
                         capture_output=True, text=True).stdout.strip()
    return float(dur or 0), r.stderr.strip()[:200]


def first_pts(src):
    r = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries", "packet=pts_time",
                        "-read_intervals", "%+#1", "-of", "csv=p=0", src], capture_output=True, text=True)
    return r.stdout.strip().split("\n")[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video", nargs="?", default="dQw4w9WgXcQ")
    ap.add_argument("--out", required=True)
    ap.add_argument("--client", default="visionos")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    path = lambda name: os.path.join(args.out, "%s.%s" % (args.video, name))  # noqa: E731
    info = resolve(args.video, args.client)
    fmt = info["fmt"]
    print("format: itag %d, %s, %s bytes, %s ms, url %s, sabr %s" % (
        fmt["itag"], fmt["mimeType"], fmt.get("contentLength"), fmt.get("approxDurationMs"),
        "yes" if "url" in fmt else "NO", "yes" if info["sabr_url"] and info["ustreamer"] else "NO"))
    results = {}
    if "url" in fmt:
        t = time.time()
        results["dash"] = dash_fetch(info, path("dash.webm"))
        results["dash"]["seconds"] = round(time.time() - t, 1)
        results["dash"]["wav_s"], results["dash"]["ffmpeg"] = to_wav(path("dash.webm"))
        dash_fetch(info, path("dash-seek.webm"), SEEK_MS)
        results["dash"]["seek_pts"] = first_pts(path("dash-seek.webm"))
    if info["sabr_url"] and info["ustreamer"]:
        t = time.time()
        results["sabr"] = sabr_fetch(info, path("sabr.webm"))
        results["sabr"]["seconds"] = round(time.time() - t, 1)
        results["sabr"]["wav_s"], results["sabr"]["ffmpeg"] = to_wav(path("sabr.webm"))
        seek = sabr_fetch(info, path("sabr-seek.webm"), SEEK_MS, max_segments=3)
        results["sabr"]["seek_first_start_ms"] = seek["first_start_ms"]
        results["sabr"]["seek_pts"] = first_pts(path("sabr-seek.webm"))
        results["sabr"]["seek_log"] = seek["log"]
    print(json.dumps(results, indent=1))


if __name__ == "__main__":
    sys.exit(main())
