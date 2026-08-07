#!/usr/bin/env python3
"""Compare two 16-bit stereo WAV files with a small tolerance.

The Musepack decoder's float output can differ in the last bit across libm
implementations (macOS vs glibc), which occasionally flips a 16-bit PCM sample
by +-1. This comparator accepts outputs that are sample-exact or within a
tiny tolerance, while still flagging any real regression (which produces
large deviations).

Usage: wavcmp_tol.py <reference.wav> <candidate.wav> [--max-abs N]
Exit status: 0 if close, 1 otherwise.
"""

import struct
import sys
import wave


def read_samples(path):
    with wave.open(path, "rb") as w:
        assert w.getsampwidth() == 2, "expected 16-bit PCM"
        nch = w.getnchannels()
        frames = w.readframes(w.getnframes())
        vals = struct.unpack("<%dh" % (len(frames) // 2), frames)
    return nch, vals


def main():
    max_abs = 2
    args = [a for a in sys.argv[1:] if not a.startswith("--max-abs")]
    for i, a in enumerate(sys.argv[1:]):
        if a == "--max-abs":
            max_abs = int(sys.argv[1:][i + 1])
    if len(args) != 2:
        print("usage: wavcmp_tol.py ref.wav cand.wav [--max-abs N]", file=sys.stderr)
        return 2

    nch_a, a = read_samples(args[0])
    nch_b, b = read_samples(args[1])
    if nch_a != nch_b:
        print("FAIL: channel count mismatch (%d vs %d)" % (nch_a, nch_b))
        return 1
    if len(a) != len(b):
        print("FAIL: sample count mismatch (%d vs %d)" % (len(a), len(b)))
        return 1

    worst = 0
    bad = 0
    for x, y in zip(a, b):
        d = abs(x - y)
        if d > worst:
            worst = d
        if d > max_abs:
            bad += 1

    if bad == 0:
        print("PASS (worst diff %d, max allowed %d)" % (worst, max_abs))
        return 0
    print("FAIL: %d samples deviate by more than %d (worst %d)"
          % (bad, max_abs, worst))
    return 1


if __name__ == "__main__":
    sys.exit(main())
