#!/usr/bin/env python3
"""Gate 0.7c (docs/plan/phase4.md): the TV sign-in (device code), and what its token can do.

The OAuth client is YouTube TV's own, read from www.youtube.com/tv as SmartTube does
(MediaServiceCore, ClientData.java): nothing of it is kept in this file.

    python3 tools/gates/tv_auth.py code      # asks for a code, prints it, exits
    python3 tools/gates/tv_auth.py           # waits for the sign-in if a code is pending, then
                                             # refreshes twice and runs the read-only checks
    python3 tools/gates/tv_auth.py --writes  # also: like and unlike, a private playlist made,
                                             # filled, emptied and deleted
    python3 tools/gates/tv_auth.py --revoke  # revokes the token and forgets it

--out (default debug/tv, which git ignores) holds the token and the raw responses: the token is
the account's key and the responses are the account's library. Never commit them.
"""
import argparse, json, os, re, sys, time, urllib.error, urllib.parse, urllib.request, uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from player_matrix import walk  # noqa: E402

TV_UA = ("Mozilla/5.0 (ChromiumStylePlatform) Cobalt/25.lts.30.1034943-gold (unlike Gecko), "
         "Unknown_TV_Unknown_0/Unknown (Unknown, Unknown)")
TV_VERSION = "7.20260707.07.00"
SCOPE = "http://gdata.youtube.com https://www.googleapis.com/auth/youtube-paid-content"
VIDEO = "dQw4w9WgXcQ"


def http(url, body=None, headers=None):
    """(status, parsed JSON or text). A JSON body makes it a POST."""
    headers = dict({"User-Agent": TV_UA, "Accept-Encoding": "identity"}, **(headers or {}))
    data = None
    if body is not None:
        data = json.dumps(body).encode()
        headers["Content-Type"] = "application/json"
    try:
        with urllib.request.urlopen(urllib.request.Request(url, data=data, headers=headers), timeout=30) as r:
            status, raw = r.status, r.read()
    except urllib.error.HTTPError as e:
        status, raw = e.code, e.read()
    try:
        return status, json.loads(raw)
    except ValueError:
        return status, raw.decode("utf-8", "replace")


def tv_client():
    """YouTube TV's OAuth client id and secret, from its own JavaScript."""
    _, page = http("https://www.youtube.com/tv")
    m = re.search(r'id="base-js" src="(.*?)"', page) or re.search(r"\.src = '(.*?m=base)'", page)
    if not m:
        sys.exit("no base-js in www.youtube.com/tv")
    url = urllib.parse.urljoin("https://www.youtube.com", m.group(1).replace("\\/", "/"))
    _, js = http(url)
    # The first match is the one that works (SmartTube: the later ones answer 401).
    m = re.search(r'clientId:"([-\w]+\.apps\.googleusercontent\.com)",\n?[$\w]+:"(\w+)"', js)
    if not m:
        sys.exit("no clientId in " + url)
    return m.group(1), m.group(2)


def load(path):
    return json.load(open(path)) if os.path.exists(path) else None


def save(path, data):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        json.dump(data, f)


def ask_code(out):
    cid, secret = tv_client()
    status, r = http("https://www.youtube.com/o/oauth2/device/code",
                     {"client_id": cid, "device_id": str(uuid.uuid4()), "model_name": "ytlr::", "scope": SCOPE})
    if status != 200 or "user_code" not in r:
        sys.exit("device/code: %s %s" % (status, r))
    save(os.path.join(out, "code.json"), dict(r, client_id=cid, client_secret=secret, asked=time.time()))
    print("CODE %s at %s (valid %d min, poll every %s s)" % (
        r["user_code"], r.get("verification_url"), r.get("expires_in", 0) // 60, r.get("interval")))


def wait_token(out):
    code = load(os.path.join(out, "code.json")) or sys.exit("no token and no pending code: run `tv_auth.py code`")
    interval, end = code.get("interval", 5), code["asked"] + code.get("expires_in", 1800)
    while time.time() < end:
        time.sleep(interval)
        status, r = http("https://www.youtube.com/o/oauth2/token",
                         {"code": code["device_code"], "client_id": code["client_id"],
                          "client_secret": code["client_secret"],
                          "grant_type": "http://oauth.net/grant_type/device/1.0"})
        err = r.get("error") if isinstance(r, dict) else str(r)[:80]
        if err == "slow_down":
            interval += 5
        elif err and err != "authorization_pending":
            sys.exit("token: %s %s" % (status, err))
        elif not err:
            tok = dict(r, client_id=code["client_id"], client_secret=code["client_secret"], at=time.time())
            save(os.path.join(out, "token.json"), tok)
            os.remove(os.path.join(out, "code.json"))
            print("signed in: expires_in %s, refresh token %s, scope %s" % (
                r.get("expires_in"), "yes" if r.get("refresh_token") else "NO", r.get("scope")))
            return tok
    sys.exit("the code expired")


def refresh(tok):
    status, r = http("https://www.youtube.com/o/oauth2/token",
                     {"refresh_token": tok["refresh_token"], "client_id": tok["client_id"],
                      "client_secret": tok["client_secret"], "grant_type": "refresh_token"})
    return status, r if isinstance(r, dict) else {"error": str(r)[:80]}


def yt(host, endpoint, access, fields):
    body = dict({"context": {"client": {"clientName": "TVHTML5", "clientVersion": TV_VERSION, "hl": "en",
                                        "gl": "US", "userAgent": TV_UA}}}, **fields)
    return http("https://%s/youtubei/v1/%s?prettyPrint=false" % (host, endpoint), body,
                {"Authorization": "Bearer " + access, "Origin": "https://" + host,
                 "X-YouTube-Client-Name": "7", "X-YouTube-Client-Version": TV_VERSION})


def row(what, status, note=""):
    print("| %s | %s | %s |" % (what, status, note), flush=True)


def checks(out, tok, writes):
    print("\n| Check | HTTP | Result |\n|---|---|---|")
    # Refresh twice: does the refresh token stay, and do both access tokens work at once (two apps)?
    s1, r1 = refresh(tok)
    s2, r2 = refresh(tok)
    row("refresh, twice", "%s, %s" % (s1, s2), "expires_in %s; a new refresh token: %s; the access tokens differ: %s" % (
        r1.get("expires_in"), "YES" if r1.get("refresh_token") or r2.get("refresh_token") else "no",
        r1.get("access_token") != r2.get("access_token")))
    if not r2.get("access_token"):
        sys.exit("refresh failed: %s %s" % (r1.get("error"), r2.get("error")))
    first, access = r1.get("access_token"), r2["access_token"]

    def call(name, host, endpoint, fields, token=None):
        status, data = yt(host, endpoint, token or access, fields)
        save(os.path.join(out, name + ".json"), data)
        return status, data if isinstance(data, dict) else {}

    for name, token in (("accounts_list.first", first), ("accounts_list", access)):
        status, d = call(name, "www.youtube.com", "account/accounts_list", {
            "accountReadMask": {"returnOwner": True, "returnBrandAccounts": True, "returnPersonaAccounts": False}},
            token)
        accounts = list(walk(d, "accountItem"))
        row(name.replace("_", " "), status, "%d account(s), name: %s, photo: %s" % (
            len(accounts), "yes" if next(walk(d, "accountName"), None) else "no",
            "yes" if next(walk(d, "accountPhoto"), None) else "no"))
    for host, bid in (("music.youtube.com", "FEmusic_liked_playlists"), ("music.youtube.com", "VLLM"),
                      ("www.youtube.com", "VLLM"), ("www.youtube.com", "FEplaylist_aggregation"),
                      ("www.youtube.com", "FElibrary"), ("music.youtube.com", "FEmusic_home")):
        status, d = call("browse.%s.%s" % (host.split(".")[0], bid), host, "browse", {"browseId": bid})
        logged = next(walk(d, "loggedOut"), None)
        row("browse %s, %s" % (bid, host), status, "tiles %d, logged out: %s, top keys: %s" % (
            sum(1 for _ in walk(d, "tileRenderer")), logged, ",".join(sorted(d.get("contents", {}).keys()))))
    status, d = call("player", "www.youtube.com", "player", {
        "videoId": VIDEO, "contentCheckOk": True, "racyCheckOk": True})
    sd = d.get("streamingData") or {}
    audio = [f for f in sd.get("adaptiveFormats", []) if f.get("mimeType", "").startswith("audio/")]
    row("player, TVHTML5 with the token", status, "%s; audio itags %s; url %d, cipher %d, sabr %s" % (
        (d.get("playabilityStatus") or {}).get("status"), ",".join(str(f["itag"]) for f in audio) or "-",
        sum(1 for f in audio if "url" in f), sum(1 for f in audio if "signatureCipher" in f),
        "yes" if sd.get("serverAbrStreamingUrl") else "-"))
    if not writes:
        return
    www = "www.youtube.com"
    status, _ = call("like", www, "like/like", {"target": {"videoId": VIDEO}})
    row("like/like", status)
    status, _ = call("removelike", www, "like/removelike", {"target": {"videoId": VIDEO}})
    row("like/removelike", status)
    status, d = call("playlist_create", www, "playlist/create", {
        "title": "bbmusic gate 0.7c (delete me)", "privacyStatus": "PRIVATE", "videoIds": []})
    pid = d.get("playlistId")
    row("playlist/create, private", status, "id: %s" % ("yes" if pid else "NO"))
    if pid:
        status, d = call("playlist_add", www, "browse/edit_playlist", {
            "playlistId": pid, "actions": [{"action": "ACTION_ADD_VIDEO", "addedVideoId": VIDEO}]})
        row("edit_playlist, add", status, str(d.get("status")))
        status, d = call("playlist_remove", www, "browse/edit_playlist", {
            "playlistId": pid, "actions": [{"action": "ACTION_REMOVE_VIDEO_BY_VIDEO_ID", "removedVideoId": VIDEO}]})
        row("edit_playlist, remove", status, str(d.get("status")))
        status, _ = call("playlist_delete", www, "playlist/delete", {"playlistId": pid})
        row("playlist/delete", status, "" if status == 200 else "DELETE IT BY HAND: " + pid)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("step", nargs="?", choices=["code"])
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../debug/tv"))
    ap.add_argument("--writes", action="store_true")
    ap.add_argument("--revoke", action="store_true")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    if args.step == "code":
        return ask_code(args.out)
    tok = load(os.path.join(args.out, "token.json")) or wait_token(args.out)
    if args.revoke:
        status, r = http("https://oauth2.googleapis.com/revoke?token=" + urllib.parse.quote(tok["refresh_token"]), {})
        print("revoke: %s %s" % (status, r))
        s, r = refresh(tok)
        print("refresh after it: %s %s" % (s, r.get("error")))
        os.remove(os.path.join(args.out, "token.json"))
        return
    checks(args.out, tok, args.writes)


if __name__ == "__main__":
    sys.exit(main())
