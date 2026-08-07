#!/usr/bin/env python3
"""A/B encoder comparison harness.

Encodes every .wav in a corpus with encoder A and encoder B across a quality
range and compares: SHA-256, byte equality, file size, and (optionally)
decoded PCM. Used for the errorL/errorR investigation.

Usage: ab_compare.py <encA> <encB> <corpus_dir> [--qualities 0..10]
"""

import hashlib
import os
import subprocess
import sys

QUALITIES = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10]


def sh256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def encode(enc, wav, mpc, q):
    r = subprocess.run([enc, "--silent", "--overwrite", "--quality", str(q),
                        wav, mpc], capture_output=True, text=True)
    return r.returncode


def main():
    encA, encB, corpus = sys.argv[1], sys.argv[2], sys.argv[3]
    quals = QUALITIES
    if "--qualities" in sys.argv:
        i = sys.argv.index("--qualities")
        quals = [int(x) for x in sys.argv[i + 1].split(",")]

    wavs = sorted(f for f in os.listdir(corpus) if f.endswith(".wav"))
    diffs = 0
    total = 0
    for q in quals:
        for w in wavs:
            wav = os.path.join(corpus, w)
            mpcA = "/tmp/ab_A.mpc"
            mpcB = "/tmp/ab_B.mpc"
            ra = encode(encA, wav, mpcA, q)
            rb = encode(encB, wav, mpcB, q)
            total += 1
            if ra != 0 or rb != 0:
                print("ENC FAIL q%d %s (A=%d B=%d)" % (q, w, ra, rb))
                diffs += 1
                continue
            ha, hb = sh256(mpcA), sh256(mpcB)
            sa, sb = os.path.getsize(mpcA), os.path.getsize(mpcB)
            if ha != hb or sa != sb:
                print("DIFF q%d %s  A=%s(%d) B=%s(%d)" % (q, w, ha[:16], sa, hb[:16], sb))
                diffs += 1
    print("\n== %d/%d A/B encoder outputs differ ==" % (diffs, total))
    return 0 if diffs == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
