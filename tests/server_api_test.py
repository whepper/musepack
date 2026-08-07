#!/usr/bin/env python3
"""Phase 4 musicpack-server integration tests.

Two modes:

  setup <ref-mpc> <ref-flac> <tmpdir>
      Builds a small real library under <tmpdir>/lib from the reference
      fixtures: the mpc + flac packages, a second edition of the mpc album
      (same release group, distinct release), a two-disc package, a
      malformed package, and a symlink-escape attempt.

  run <base-url> <libdir>
      Exercises the HTTP API v1: health, album/release/track/artist
      endpoints, collector hierarchy, pagination, errors, direct streaming
      with HTTP Range (byte-identity vs the source files), HEAD, concurrency,
      and security boundaries (traversal, missing files, symlink escapes).

Exits non-zero on the first failure. Uses only the stdlib.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import urllib.error
import urllib.request

API = "/api/v1"


# --------------------------------------------------------------------------
# setup
# --------------------------------------------------------------------------

def write_json(path, obj):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=1)


def file_sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def setup(ref_mpc, ref_flac, tmpdir):
    lib = os.path.join(tmpdir, "lib")
    shutil.rmtree(lib, ignore_errors=True)
    os.makedirs(lib)

    # 1. mpc + flac packages
    shutil.copytree(ref_mpc, os.path.join(lib, "Compilation.mpack"))
    shutil.copytree(ref_flac, os.path.join(lib, "Classical.mpack"))

    # 2. second edition of the mpc album (same group, different release)
    second = os.path.join(lib, "Compilation-1987.mpack")
    shutil.copytree(ref_mpc, second)
    with open(os.path.join(second, "manifest.json"), encoding="utf-8") as f:
        m = json.load(f)
    m["release"]["edition"] = "1987 Original CD"
    m["release"]["releaseDate"] = "1987-06-15"
    m["release"]["country"] = "DE"
    write_json(os.path.join(second, "manifest.json"), m)

    # 3. two-disc package: disc 2 reuses the same codec but must reference
    #    distinct audio objects (v1 rejects duplicate referenced paths), so
    #    copy the audio files under new names with refreshed sha256.
    twodisc = os.path.join(lib, "TwoDisc.mpack")
    shutil.copytree(ref_mpc, twodisc)
    with open(os.path.join(twodisc, "manifest.json"), encoding="utf-8") as f:
        m = json.load(f)
    m["album"]["title"] = "Two Disc Extravaganza"
    m["album"]["originalReleaseDate"] = "2001-01-01"
    m["release"]["edition"] = "2CD"
    d2_tracks = []
    for i, (num, title) in enumerate([(1, "Side Two One"), (2, "Side Two Two")]):
        src = os.path.join(twodisc, m["media"][0]["tracks"][i]["audio"]["path"])
        newpath = f"audio/0{i + 5} - {title}.mpc"
        dst = os.path.join(twodisc, newpath)
        shutil.copyfile(src, dst)
        d2_tracks.append({
            "track": num,
            "title": title,
            "audio": {"path": newpath, "sha256": file_sha(dst)},
        })
    m["media"].append({"disc": 2, "format": "CD", "tracks": d2_tracks})
    write_json(os.path.join(twodisc, "manifest.json"), m)

    # 4. malformed package
    bad = os.path.join(lib, "Broken.mpack")
    os.makedirs(bad)
    with open(os.path.join(bad, "manifest.json"), "w", encoding="utf-8") as f:
        f.write("{ this is not json ")

    # 5. symlink-escape attempt: valid manifest, but the audio object at a
    #    contained path is a symlink pointing outside the package. The server
    #    must reject it at resolve time (scan -> warning, stream -> 503).
    esc = os.path.join(lib, "Escape.mpack")
    shutil.copytree(ref_mpc, esc)
    with open(os.path.join(esc, "manifest.json"), encoding="utf-8") as f:
        m = json.load(f)
    m["release"]["edition"] = "Escape Edition"
    write_json(os.path.join(esc, "manifest.json"), m)
    target = os.path.join(tmpdir, "escaped.bin")
    with open(target, "wb") as f:
        f.write(b"not a real mpc file")
    escaped_audio = os.path.join(
        esc, "audio", "01 - Alphaville - Big in Japan.mpc")
    os.remove(escaped_audio)
    os.symlink(target, escaped_audio)

    with open(os.path.join(tmpdir, "libdir"), "w") as f:
        f.write(lib)


# --------------------------------------------------------------------------
# http helpers
# --------------------------------------------------------------------------

def get(base, path, headers=None, method=None):
    req = urllib.request.Request(base + path, headers=headers or {})
    if method:
        req.get_method = lambda: method
    try:
        with urllib.request.urlopen(req) as r:
            return r.status, dict(r.headers), r.read()
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers), e.read()


def sha(data):
    return hashlib.sha256(data).hexdigest()


class T:
    """Tiny assertion helper."""

    def __init__(self):
        self.failures = 0
        self.passed = 0

    def ok(self, cond, name):
        if cond:
            self.passed += 1
        else:
            self.failures += 1
            print("FAIL", name)


# --------------------------------------------------------------------------
# run
# --------------------------------------------------------------------------

def run(base, libdir, t):
    import time

    # ---- health
    st, h, body = get(base, API + "/health")
    t.ok(st == 200, "health 200")
    t.ok(json.loads(body)["status"] == "ok", "health status ok")
    t.ok(json.loads(body)["apiVersion"] == "v1", "health api version")

    # ---- albums list + ordering + pagination
    st, _, body = get(base, API + "/albums")
    albums = json.loads(body)
    t.ok(st == 200 and albums["total"] == 3, "albums total 3")
    titles = [a["title"] for a in albums["albums"]]
    # ordered by album artist, then title: the two "Alphaville" albums come
    # before the "Synthetic Chamber Orchestra" album
    t.ok(titles == ["Synthetic Test Compilation", "Two Disc Extravaganza",
                    "Synthetic Classical Compilation"],
         "albums deterministically ordered")
    st, _, body = get(base, API + "/albums?limit=1&offset=1")
    page = json.loads(body)
    t.ok(st == 200 and len(page["albums"]) == 1 and page["total"] == 3,
         "albums pagination")

    # ---- collector hierarchy: Compilation group has three releases
    comp = next(a for a in albums["albums"]
                if a["title"] == "Synthetic Test Compilation")
    st, _, body = get(base, API + f"/albums/{comp['id']}")
    detail = json.loads(body)
    t.ok(st == 200, "album detail 200")
    editions = sorted(r.get("edition", "") for r in detail["releases"])
    t.ok("1987 Original CD" in editions and "2016 Digital Remaster" in editions
         and "Escape Edition" in editions,
         "editions not collapsed")
    media = detail["releases"][0]["media"]
    t.ok(media == ["Digital"], "release media formats")

    # ---- release detail: tracks, codec, audio url, artwork
    release_id = next(r["id"] for r in detail["releases"]
                      if r.get("edition") == "2016 Digital Remaster")
    st, _, body = get(base, API + f"/releases/{release_id}")
    rel = json.loads(body)
    t.ok(st == 200, "release detail 200")
    track = rel["media"][0]["tracks"][0]
    t.ok(track["codec"]["codec"] == "musepack-sv8", "mpc codec musepack-sv8")
    t.ok(track["codec"]["mimeType"] == "audio/musepack", "mpc mime")
    t.ok(track["codec"]["sampleRate"] == 44100, "mpc sample rate")
    t.ok(track["audio"]["url"].startswith("/api/v1/tracks/"), "audio url")
    t.ok(any(a["kind"] == "artwork" for a in rel["artwork"]), "artwork asset")
    t.ok(rel["packageStatus"] == "valid", "package status valid")

    # ---- two-disc release
    td = next(a for a in albums["albums"] if a["title"] == "Two Disc Extravaganza")
    _, _, body = get(base, API + f"/albums/{td['id']}")
    td_detail = json.loads(body)
    st, _, body = get(base, API + f"/releases/{td_detail['releases'][0]['id']}")
    td_rel = json.loads(body)
    t.ok(st == 200 and len(td_rel["media"]) == 2, "two-disc release has 2 media")
    t.ok(td_rel["media"][1]["disc"] == 2, "second media is disc 2")

    # ---- track detail
    tid = track["id"]
    st, _, body = get(base, API + f"/tracks/{tid}")
    tr = json.loads(body)
    t.ok(st == 200 and tr["id"] == tid, "track detail 200")
    t.ok(tr["context"]["albumTitle"] == "Synthetic Test Compilation",
         "track album context")

    # ---- artists
    st, _, body = get(base, API + "/artists")
    artists = json.loads(body)
    t.ok(st == 200 and len(artists["artists"]) >= 2, "artists list")
    aid = artists["artists"][0]["id"]
    st, _, body = get(base, API + f"/artists/{aid}")
    t.ok(st == 200 and "albums" in json.loads(body), "artist detail")

    # ---- errors
    st, _, _ = get(base, API + "/tracks/abc")
    t.ok(st == 400, "malformed id -> 400")
    st, _, _ = get(base, API + "/tracks/999999")
    t.ok(st == 404, "unknown id -> 404")
    st, _, _ = get(base, API + "/releases/999999")
    t.ok(st == 404, "unknown release -> 404")
    st, _, _ = get(base, API + "/nope")
    t.ok(st == 404, "unknown endpoint -> 404")
    st, _, _ = get(base, "/api/v1/tracks/../../../etc/passwd")
    t.ok(st == 404 or st == 400, "traversal attempt rejected")
    st, _, body = get(base, "/etc/passwd")
    t.ok(st == 404 and b"root:" not in body, "arbitrary path not served")

    # ---- streaming: byte identity (one mpc + one flac track)
    flac_release = None
    _, _, b2 = get(base, API + f"/albums/{next(a for a in albums['albums']
                   if a['title'] == 'Synthetic Classical Compilation')['id']}")
    flac_release = json.loads(b2)["releases"][0]["id"]
    _, _, b2 = get(base, API + f"/releases/{flac_release}")
    flac_track = json.loads(b2)["media"][0]["tracks"][0]

    for label, want_size, track_id in (
            ("mpc", 28288, tid),
            ("flac", 138222, flac_track["id"])):
        st, _, body = get(base, API + f"/tracks/{track_id}/audio")
        t.ok(st == 200, f"{label} full GET 200")
        _, _, b2 = get(base, API + f"/tracks/{track_id}")
        manifest_sha = json.loads(b2)["audio"]["sha256"]
        t.ok(sha(body) == manifest_sha, f"{label} bytes hash to manifest sha256")
        t.ok(len(body) == want_size, f"{label} full size")

    mpc_url = API + f"/tracks/{tid}/audio"
    # first / last byte
    st, h, body = get(base, mpc_url, {"Range": "bytes=0-0"})
    t.ok(st == 206 and h.get("Content-Range") == f"bytes 0-0/{28288}" and
         h.get("Content-Length") == "1", "range first byte 206")
    st, h, body = get(base, mpc_url, {"Range": "bytes=-1"})
    t.ok(st == 206 and int(h.get("Content-Range").split("/")[1]) == 28288 and
         len(body) == 1, "range last byte via suffix")
    # open-ended + suffix
    st, h, body = get(base, mpc_url, {"Range": "bytes=28280-"})
    t.ok(st == 206 and len(body) == 8, "open-ended range")
    st, h, body = get(base, mpc_url, {"Range": "bytes=-16"})
    t.ok(st == 206 and len(body) == 16, "suffix range")
    # whole-file range
    st, h, body = get(base, mpc_url, {"Range": "bytes=0-99999999"})
    t.ok(st == 206 and len(body) == 28288, "whole-file range 206")
    # invalid / beyond EOF / oversized -> 416
    for r in ("bytes=999999-", "bytes=0-1,5-6", "bytes=18446744073709551616-"):
        st, h, _ = get(base, mpc_url, {"Range": r})
        t.ok(st == 416 and h.get("Content-Range") == "bytes */28288",
             f"unsatisfiable range {r} -> 416")
    # range byte identity: concatenated disjoint ranges match the file
    ranges = [("bytes=0-999", 0, 1000), ("bytes=10000-10999", 10000, 1000),
              ("bytes=28280-", 28280, 8)]
    concat = b""
    for r, off, ln in ranges:
        _, _, b = get(base, mpc_url, {"Range": r})
        concat += b
    with open(os.path.join(libdir, "Compilation.mpack", "audio",
                           "01 - Alphaville - Big in Japan.mpc"), "rb") as f:
        src = f.read()
    expect = src[0:1000] + src[10000:11000] + src[28280:]
    t.ok(concat == expect, "range responses byte-identical to source slices")

    # ---- HEAD
    st, h, body = get(base, mpc_url, method="HEAD")
    t.ok(st == 200 and h.get("Accept-Ranges") == "bytes" and
         h.get("Content-Length") == "28288" and body == b"",
         "HEAD returns headers, no body")

    # ---- assets (artwork)
    art = rel["artwork"][0]
    st, h, body = get(base, API + f"/assets/{art['id']}")
    with open(os.path.join(libdir, "Compilation.mpack", "artwork", "front.jpg"),
              "rb") as f:
        t.ok(st == 200 and body == f.read() and
             h.get("Content-Type") == "image/jpeg", "artwork asset bytes")
    st, h, body = get(base, API + f"/assets/{art['id']}", {"Range": "bytes=0-9"})
    t.ok(st == 206 and len(body) == 10, "artwork range")

    # ---- missing source file -> 503
    deleted = os.path.join(libdir, "Compilation.mpack", "audio",
                           "02 - Bleachers - The Van.mpc")
    if os.path.exists(deleted):
        os.remove(deleted)
        _, _, b2 = get(base, API + f"/releases/{rel['id']}")
        trk2 = json.loads(b2)["media"][0]["tracks"][1]
        st, _, _ = get(base, API + f"/tracks/{trk2['id']}/audio")
        t.ok(st == 503, "missing source file -> 503")

    # ---- symlink-escape package: valid manifest, symlinked audio outside
    _, _, b2 = get(base, API + "/albums")
    esc_albums = [a for a in json.loads(b2)["albums"]
                  if a["title"] == "Synthetic Test Compilation"]
    _, _, b3 = get(base, API + f"/albums/{esc_albums[0]['id']}")
    rels = json.loads(b3)["releases"]
    esc_rel = next(r for r in rels if r.get("packageStatus") == "warning")
    _, _, b4 = get(base, API + f"/releases/{esc_rel['id']}")
    esc_track = json.loads(b4)["media"][0]["tracks"][0]
    st, _, _ = get(base, API + f"/tracks/{esc_track['id']}/audio")
    t.ok(st == 503, "symlink escape stream blocked")

    # ---- concurrency: parallel range reads
    import threading

    results = []
    def fetch():
        _, _, b = get(base, mpc_url, {"Range": "bytes=10000-11000"})
        results.append(b)
    threads = [threading.Thread(target=fetch) for _ in range(4)]
    for th in threads:
        th.start()
    for th in threads:
        th.join()
    t.ok(all(b == src[10000:11001] for b in results) and len(results) == 4,
         "concurrent range reads consistent")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    mode = sys.argv[1]
    if mode == "setup":
        setup(sys.argv[2], sys.argv[3], sys.argv[4])
        return 0
    if mode == "run":
        t = T()
        run(sys.argv[2], sys.argv[3], t)
        print(f"server_api_test: {t.passed} passed, {t.failures} failed")
        return 0 if t.failures == 0 else 1
    print("unknown mode", mode)
    return 2


if __name__ == "__main__":
    sys.exit(main())
