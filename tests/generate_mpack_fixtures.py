#!/usr/bin/env python3
"""Generate the committed `.mpack` reference packages under tests/reference/.

Deterministic (seeded) generation. Two packages prove codec independence:

  test-musicpack-album.mpack/   Musepack album (4 tracks)
  test-flac-album.mpack/        FLAC album (3 tracks)

Each contains multi-value artist metadata, artwork, lyrics, a booklet (MPC
package) and extras. Loudness is measured by the `musicpack` CLI (libmusepack
for .mpc, ffmpeg decode for .flac).

Usage:
  python3 tests/generate_mpack_fixtures.py \
      --mpcenc build/mpcenc/mpcenc \
      --musicpack build/musicpack/musicpack \
      [--ffmpeg ffmpeg] [--out tests/reference]
"""

import argparse
import math
import os
import random
import struct
import subprocess
import sys
import wave
import zlib


DURATION = 1.0


def synth(freq_l, freq_r, rate, seed):
    rng = random.Random(seed)
    n = int(rate * DURATION)
    out = []
    for i in range(n):
        t = i / rate
        l = (0.45 * math.sin(2 * math.pi * freq_l * t)
             + 0.12 * math.sin(2 * math.pi * freq_l * 2.7 * t)
             + 0.03 * rng.uniform(-1, 1))
        r = (0.45 * math.sin(2 * math.pi * freq_r * t)
             + 0.12 * math.sin(2 * math.pi * freq_r * 1.9 * t)
             + 0.03 * rng.uniform(-1, 1))
        out.append(l)
        out.append(r)
    return out


def write_wav(path, samples, rate):
    w = wave.open(path, "wb")
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(rate)
    frames = bytearray()
    for v in samples:
        iv = max(-32768, min(32767, int(v * 32767)))
        frames += struct.pack("<h", iv)
    w.writeframes(bytes(frames))
    w.close()


def make_png(path, rgb, size=8):
    """A tiny valid PNG (solid color)."""
    raw = b"".join(b"\x00" + bytes(rgb) * size for _ in range(size))
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", zlib.compress(raw)))
        f.write(chunk(b"IEND", b""))


def make_pdf(path):
    """A minimal valid one-page PDF."""
    objects = []
    objects.append(b"<< /Type /Catalog /Pages 2 0 R >>")
    objects.append(b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>")
    objects.append(b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                   b"/Contents 5 0 R /Resources << /Font << /F1 4 0 R >> >> >>")
    objects.append(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
    content = b"BT /F1 24 Tf 72 720 Td (MusicPack test booklet) Tj ET"
    objects.append(b"<< /Length %d >>\nstream\n%s\nendstream" % (len(content), content))
    out = bytearray(b"%PDF-1.4\n")
    offsets = []
    for i, obj in enumerate(objects, 1):
        offsets.append(len(out))
        out += b"%d 0 obj\n" % i
        out += obj + b"\nendobj\n"
    xref = len(out)
    out += b"xref\n0 %d\n" % (len(objects) + 1)
    out += b"0000000000 65535 f \n"
    for off in offsets:
        out += b"%010d 00000 n \n" % off
    out += b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (
        len(objects) + 1, xref)
    with open(path, "wb") as f:
        f.write(bytes(out))


def run(cmd, what):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print("FAIL %s: %s" % (what, r.stderr), file=sys.stderr)
        sys.exit(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mpcenc", required=True)
    ap.add_argument("--musicpack", required=True)
    ap.add_argument("--ffmpeg", default="ffmpeg")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    outdir = args.out or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "reference")
    os.makedirs(outdir, exist_ok=True)

    mpc_specs = [  # name, freq_l, freq_r, seed
        ("Alphaville - Big in Japan", 440, 550, 11),
        ("Bleachers - The Van", 330, 660, 12),
        ("Synthwave - Night Drive", 220, 440, 13),
        ("Test Artist - Fourth Track", 150, 700, 14),
    ]
    flac_specs = [  # name, freq_l, freq_r, seed
        ("Classical Piece No 1", 260, 520, 21),
        ("Classical Piece No 2", 330, 440, 22),
        ("Classical Piece No 3", 175, 350, 23),
    ]

    tmp = os.path.join(outdir, "_mpack_tmp")
    if os.path.exists(tmp):
        for root, _, fns in os.walk(tmp, topdown=False):
            for fn in fns:
                os.remove(os.path.join(root, fn))
            os.rmdir(root)
    os.makedirs(os.path.join(tmp, "mpc_src"), exist_ok=True)
    os.makedirs(os.path.join(tmp, "flac_src"), exist_ok=True)

    # --- Musepack source album -------------------------------------------
    src = os.path.join(tmp, "mpc_src")
    for i, (name, fl, fr, seed) in enumerate(mpc_specs, 1):
        wav = os.path.join(tmp, "_s.mp3" if False else "_s.wav")
        write_wav(wav, synth(fl, fr, 44100, seed), 44100)
        mpc = os.path.join(src, "%02d - %s.mpc" % (i, name))
        run([args.mpcenc, "--silent", "--overwrite", "--quality", "5", wav, mpc],
            "encode %s" % name)
    make_png(os.path.join(src, "folder.jpg"), (30, 60, 120))
    make_pdf(os.path.join(src, "booklet.pdf"))
    with open(os.path.join(src, "01 - Big in Japan.lrc"), "w") as f:
        f.write("[00:00.00]Big in Japan - sample lyrics\n")
    with open(os.path.join(src, "02 - The Van.lrc"), "w") as f:
        f.write("[00:00.00]The Van - sample lyrics\n")
    with open(os.path.join(src, "notes.txt"), "w") as f:
        f.write("MusicPack test package notes.\n")

    pkg1 = os.path.join(outdir, "test-musicpack-album.mpack")
    if os.path.exists(pkg1):
        subprocess.run(["rm", "-rf", pkg1])
    run([args.musicpack, "import", "-o", pkg1,
         "-t", "Synthetic Test Compilation",
         "-a", "Alphaville",
         "-a", "Bleachers",
         src], "import musicpack album")

    # --- FLAC source album ----------------------------------------------
    src2 = os.path.join(tmp, "flac_src")
    for i, (name, fl, fr, seed) in enumerate(flac_specs, 1):
        wav = os.path.join(tmp, "_f.wav")
        write_wav(wav, synth(fl, fr, 44100, seed), 44100)
        flac = os.path.join(src2, "%02d - %s.flac" % (i, name))
        run([args.ffmpeg, "-y", "-loglevel", "error", "-i", wav, "-c:a", "flac", flac],
            "encode %s" % name)
    make_png(os.path.join(src2, "folder.jpg"), (90, 40, 20))
    with open(os.path.join(src2, "01 - Piece One.lrc"), "w") as f:
        f.write("[00:00.00]Classical Piece No 1\n")

    pkg2 = os.path.join(outdir, "test-flac-album.mpack")
    if os.path.exists(pkg2):
        subprocess.run(["rm", "-rf", pkg2])
    run([args.musicpack, "import", "-o", pkg2,
         "-t", "Synthetic Classical Compilation",
         "-a", "Synthetic Chamber Orchestra",
         src2], "import flac album")

    subprocess.run(["rm", "-rf", tmp])
    print("generated %s" % pkg1)
    print("generated %s" % pkg2)


if __name__ == "__main__":
    main()
