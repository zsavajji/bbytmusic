#!/usr/bin/env python3
"""Step 2.12: what BBMusic costs on the Q10, read over ssh (nothing on the phone is changed).

    scripts/measure-phone.py                  # a snapshot, saved to build/measure/<time>.json
    scripts/measure-phone.py build/measure/X.json   # a snapshot, and what changed since X

A snapshot: the battery's charge and whether it is charging, and for the UI (bbmusic) and the
engine (bbmusicd) the process id, CPU seconds used so far (utime + stime) and private data. Since
an earlier snapshot of the same processes: CPU as a share of one core, and the charge lost per
hour (meaningful only if the phone was unplugged in between). Always: the time from each `play`
the UI sent to the engine's next "state playing" (tap to sound, the tap itself not counted).
"""
import datetime as dt
import json
import os
import re
import subprocess
import sys
import time

PHONE = os.environ.get("PHONE", "bb10rootx")
DATA = "/accounts/1000/appdata/com.zsavajji.bbmusic.testRel_jji_bbmusic7322f61a/data"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "measure")


def ssh(cmd):
    return subprocess.run(["ssh", "-o", "BatchMode=yes", PHONE, cmd], capture_output=True, text=True,
                          timeout=60).stdout


def size_kb(s):   # pidin's "76K", "1260K", "8192", "2M"
    m = re.fullmatch(r"(\d+)([KM]?)", s)
    if not m:
        return 0
    n = int(m.group(1))
    return n * 1024 if m.group(2) == "M" else n if m.group(2) == "K" else n // 1024


def processes():
    # Both are listed under the app's id; the executable in each one's memory map says which.
    pids = [int(l.split()[0]) for l in ssh('pidin -F "%a %N" ar').splitlines()
            if "com.zsavajji.bbmusic" in l and l.split()[0].isdigit()]
    snap = {}
    for pid in pids:
        mem = ssh(f"pidin -p {pid} mem").splitlines()
        names = [l.split()[0] for l in mem[1:] if l.split() and l.split()[0] in ("bbmusic", "bbmusicd")]
        if not names:
            continue
        times = ssh(f"pidin -p {pid} times").splitlines()
        cols = times[-1].split() if len(times) > 1 else []
        cpu = float(cols[-4]) + float(cols[-3]) if len(cols) >= 4 else 0.0
        # The first thread's row carries the process's code and data sizes.
        row = mem[1].split() if len(mem) > 1 else []
        data = size_kb(row[6]) if len(row) > 6 else 0
        snap[names[0]] = {"pid": pid, "cpu_s": cpu, "data_kb": data}
    return snap


def battery():
    status = ssh("cat /pps/system/BattMgr/status")
    charge = re.search(r'"StateOfCharge":(\d+)', status)
    charger = re.search(r'"ChargerStatus":"(\w+)"', status)
    return {"charge": int(charge.group(1)) if charge else None, "charger": charger.group(1) if charger else None}


def stamp(line):
    try:
        return dt.datetime.strptime(line[:23], "%Y-%m-%d %H:%M:%S.%f")
    except ValueError:
        return None


def tap_to_sound():
    ui = [(stamp(l), l) for l in ssh(f"cat {DATA}/bbmusic-ui.log").splitlines()]
    eng = [(stamp(l), l) for l in ssh(f"cat {DATA}/bbmusicd.log").splitlines()]
    plays = [t for t, l in ui if t and l[24:26] == "> " and re.search(r'"cmd"\s*:\s*"play"', l)]
    sounds = [t for t, l in eng if t and "state playing" in l]
    out = []
    for p in plays:
        after = [s for s in sounds if s >= p]
        if after and (after[0] - p).total_seconds() < 30:
            out.append((p.strftime("%m-%d %H:%M:%S"), round((after[0] - p).total_seconds() * 1000)))
    return out


def main():
    now = time.time()
    snap = {"time": now, "battery": battery(), "procs": processes()}
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, dt.datetime.fromtimestamp(now).strftime("%Y%m%d-%H%M%S") + ".json")
    with open(path, "w") as f:
        json.dump(snap, f, indent=1)
    print(f"snapshot {path}")
    print(f"battery {snap['battery']['charge']}%, charger {snap['battery']['charger']}")
    for name, p in sorted(snap["procs"].items()):
        print(f"{name}: pid {p['pid']}, {p['cpu_s']:.1f} s of CPU so far, data {p['data_kb'] / 1024:.1f} MB")
    if not snap["procs"]:
        print("the app is not running")
    if len(sys.argv) > 1:
        with open(sys.argv[1]) as f:
            old = json.load(f)
        hours = (now - old["time"]) / 3600
        print(f"since {sys.argv[1]} ({hours * 60:.0f} min):")
        for name, p in sorted(snap["procs"].items()):
            o = old["procs"].get(name)
            if not o or o["pid"] != p["pid"]:
                print(f"  {name}: restarted in between, no CPU share")
                continue
            print(f"  {name}: {100 * (p['cpu_s'] - o['cpu_s']) / (hours * 3600):.1f} % of a core")
        a, b = old["battery"]["charge"], snap["battery"]["charge"]
        if a is not None and b is not None and hours > 0:
            print(f"  battery {a}% -> {b}%: {(a - b) / hours:.1f} % per hour"
                  + ("" if old["battery"]["charger"] != "PLUGGED" else " (it was plugged in at the start)"))
    taps = tap_to_sound()
    print("tap to sound (UI's play -> engine's state playing), ms:")
    for when, ms in taps:
        print(f"  {when}  {ms}")


if __name__ == "__main__":
    main()
