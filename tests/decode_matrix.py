#!/usr/bin/env python3
"""Decoder cross-compatibility matrix.

Encodes corpus files with a given encoder, decodes with both decoders,
and compares: decoded WAV PCM (byte-exact) and `-i` stream info.

Usage: decode_matrix.py <encoder> <decoderA> <decoderB> <corpus_dir> [--qualities 0,5,10]
"""

import hashlib
import os
import subprocess
import sys

QUALITIES = [0, 5, 10]


def sh256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def run(args):
    return subprocess.run(args, capture_output=True, text=True)


def main():
    enc, decA, decB, corpus = sys.argv[1:5]
    quals = QUALITIES
    if "--qualities" in sys.argv:
        i = sys.argv.index("--qualities")
        quals = [int(x) for x in sys.argv[i + 1].split(",")]

    wavs = sorted(f for f in os.listdir(corpus) if f.endswith(".wav"))
    total = same = fail = 0
    for q in quals:
        for w in wavs:
            wav = os.path.join(corpus, w)
            mpc = "/tmp/matrix.mpc"
            r = run([enc, "--silent", "--overwrite", "--quality", str(q), wav, mpc])
            if r.returncode != 0:
                print("ENC FAIL %s" % w); continue
            pcmA, pcmB = "/tmp/matrixA.wav", "/tmp/matrixB.wav"
            ra = run([decA, mpc, pcmA])
            rb = run([decB, mpc, pcmB])
            total += 1
            if ra.returncode != 0 or rb.returncode != 0:
                print("DEC FAIL q%d %s (A=%d B=%d)" % (q, w, ra.returncode, rb.returncode))
                fail += 1
                continue
            if sh256(pcmA) == sh256(pcmB) and os.path.getsize(pcmA) == os.path.getsize(pcmB):
                same += 1
            else:
                print("PCM DIFF q%d %s A=%s(%d) B=%s(%d)" % (
                    q, w, sh256(pcmA)[:16], os.path.getsize(pcmA), sh256(pcmB)[:16], os.path.getsize(pcmB)))
    print("\n== decoder cross-compat: %d/%d decoded PCM identical ==" % (same, total))


if __name__ == "__main__":
    main()
