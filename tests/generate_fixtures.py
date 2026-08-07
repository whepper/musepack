#!/usr/bin/env python3
"""Generate Musepack test fixtures and golden decode output.

Deterministic (seeded) generation so fixtures are reproducible.

Pipeline:
  1. synthesize reference WAV files (32/37.8/44.1/48 kHz, stereo)
  2. encode each WAV with the reference mpcenc  -> <name>.mpc
  3. decode each .mpc with the reference mpcdec -> <name>.wav (golden)

The committed artifact pair is (<name>.mpc, <name>.wav). The regression
harness (run_tests.sh) re-decodes the .mpc with a freshly built mpcdec and
compares the result to the golden .wav, sample for sample.

Usage:
  python3 tests/generate_fixtures.py --mpcenc PATH --mpcdec PATH [--out DIR] [--list]
"""

import argparse
import math
import os
import random
import struct
import subprocess
import sys
import wave


SAMPLE_RATES = [44100, 48000, 37800, 32000]
DURATION = 1.0  # seconds, keep fixtures small


def synth(freq_l, freq_r, rate, dur, seed):
    """Deterministic stereo signal: sum of sines + small amount of noise."""
    rng = random.Random(seed)
    n = int(rate * dur)
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


def write_wav(path, samples, rate, nch=2, sampwidth=2):
    w = wave.open(path, "wb")
    w.setnchannels(nch)
    w.setsampwidth(sampwidth)
    w.setframerate(rate)
    fmt = "<h" if sampwidth == 2 else "<i"
    maxv = 1 << (sampwidth * 8 - 1)
    frames = bytearray()
    for v in samples:
        iv = int(v * (maxv - 1))
        iv = max(-maxv, min(maxv - 1, iv))
        frames += struct.pack(fmt, iv)
    w.writeframes(bytes(frames))
    w.close()


def read_pcm_wav(path):
    w = wave.open(path, "rb")
    assert w.getsampwidth() == 2, "golden must be 16-bit"
    nch = w.getnchannels()
    frames = w.readframes(w.getnframes())
    vals = struct.unpack("<%dh" % (len(frames) // 2), frames)
    # interleaved -> list of (l, r) tuples preserved in order
    return w.getframerate(), nch, vals


def enc_args(mpcenc, wav, mpc, quality):
    return [mpcenc, "--silent", "--overwrite", "--quality", str(quality), wav, mpc]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mpcenc", required=True)
    ap.add_argument("--mpcdec", required=True)
    ap.add_argument("--out", default=None)
    ap.add_argument("--list", action="store_true",
                    help="print fixture names and exit")
    args = ap.parse_args()

    outdir = args.out or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "fixtures")

    specs = [
        # name, sample_rate, freq_l, freq_r, quality, seed
        ("sine44-q5", 44100, 440, 550, 5.0, 1),
        ("sine44-q7", 44100, 440, 550, 7.0, 2),
        ("sine48-q6", 48000, 330, 660, 6.0, 3),
        ("sine37-q4", 37800, 200, 500, 4.0, 4),
        ("sine32-q8", 32000, 150, 700, 8.0, 5),
    ]

    if args.list:
        for name, *_ in specs:
            print(name)
        return

    os.makedirs(outdir, exist_ok=True)
    tmp_wav = os.path.join(outdir, "_src.wav")

    for name, rate, fl, fr, q, seed in specs:
        samples = synth(fl, fr, rate, DURATION, seed)
        write_wav(tmp_wav, samples, rate)

        mpc = os.path.join(outdir, name + ".mpc")
        wav = os.path.join(outdir, name + ".wav")

        r = subprocess.run(enc_args(args.mpcenc, tmp_wav, mpc, q),
                           capture_output=True, text=True)
        if r.returncode != 0:
            print("ENC FAIL %s: %s" % (name, r.stderr), file=sys.stderr)
            sys.exit(1)

        r = subprocess.run([args.mpcdec, mpc, wav],
                           capture_output=True, text=True)
        if r.returncode != 0:
            print("DEC FAIL %s: %s" % (name, r.stderr), file=sys.stderr)
            sys.exit(1)

        rate_g, nch_g, _ = read_pcm_wav(wav)
        assert rate_g == rate and nch_g == 2, \
            "golden %s wrong format: %dHz %dch" % (name, rate_g, nch_g)
        print("generated %s (%d samples @ %dHz)" % (name, len(samples) // 2, rate))

    os.remove(tmp_wav)


if __name__ == "__main__":
    main()
