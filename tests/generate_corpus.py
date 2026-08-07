#!/usr/bin/env python3
"""Generate deterministic adversarial stereo WAV corpus for codec audits.

Covers the signal categories required by the compatibility audit:
mono/stereo, 32/44.1/48 kHz, silence, quiet, full-scale, impulses,
transients, tonal, noise, high-dynamic, very short, odd counts, L/R
asymmetry, phase-inverted, clipping/full-scale edges, plus the
errorL/errorR-specific adversarial stereo pairs (left/right divergence).

All signals are synthetic and deterministic (fixed seed). No copyrighted
material. Run with a Python 3 that has the `wave` module (stdlib).

Usage: generate_corpus.py <outdir>
"""

import math
import os
import random
import struct
import sys
import wave

RATES = [44100, 48000, 32000]


def write_wav(path, samples, rate, nch=2):
    """samples: list of (l, r) float tuples in [-1, 1]."""
    w = wave.open(path, "wb")
    w.setnchannels(nch)
    w.setsampwidth(2)
    w.setframerate(rate)
    frames = bytearray()
    for s in samples:
        for v in s:
            iv = int(round(max(-1.0, min(1.0, v)) * 32767))
            iv = max(-32768, min(32767, iv))
            frames += struct.pack("<h", iv)
    w.writeframes(bytes(frames))
    w.close()


def sine(freq, t):
    return math.sin(2 * math.pi * freq * t)


def make_signal(kind, rate, dur, seed=1234):
    rng = random.Random(seed)
    n = int(rate * dur)
    out = []

    for i in range(n):
        t = i / rate
        if kind == "stereo_sine":
            l = 0.5 * sine(440, t)
            r = 0.5 * sine(550, t)
        elif kind == "mono_sine":
            l = r = 0.5 * sine(440, t)
        elif kind == "l_sine_r_silence":
            l = 0.5 * sine(440, t)
            r = 0.0
        elif kind == "l_silence_r_sine":
            l = 0.0
            r = 0.5 * sine(440, t)
        elif kind == "l_freq_a_r_freq_b":
            l = 0.4 * sine(440, t)
            r = 0.4 * sine(2200, t)
        elif kind == "l_sine_r_noise":
            l = 0.5 * sine(440, t)
            r = 0.3 * rng.uniform(-1, 1)
        elif kind == "l_impulse_r_tone":
            l = 0.9 if (i % rate) < 8 else 0.0
            r = 0.3 * sine(880, t)
        elif kind == "impulses_offset":
            l = 0.9 if (i % rate) < 8 else 0.0
            r = 0.9 if ((i + 64) % rate) < 8 else 0.0
        elif kind == "amp_asymmetry":
            l = 0.9 * sine(440, t)
            r = 0.1 * sine(440, t)
        elif kind == "phase_inverted":
            l = 0.5 * sine(440, t)
            r = -0.5 * sine(440, t)
        elif kind == "uncorrelated_noise":
            l = 0.6 * rng.uniform(-1, 1)
            r = 0.6 * rng.uniform(-1, 1)
        elif kind == "silence":
            l = r = 0.0
        elif kind == "quiet":
            l = 0.001 * sine(440, t)
            r = 0.001 * sine(550, t)
        elif kind == "full_scale":
            l = 1.0 * sine(440, t)
            r = 1.0 * sine(550, t)
        elif kind == "impulse":
            l = r = 1.0 if i == 0 else 0.0
        elif kind == "transient_attack":
            env = 1.0 if i < rate // 8 else 0.2
            l = r = env * 0.9 * sine(440, t)
        elif kind == "high_dynamic":
            env = 1.0 if (i // (rate // 2)) % 2 == 0 else 0.02
            l = env * 0.8 * sine(440, t)
            r = env * 0.8 * sine(440, t)
        elif kind == "clipping_edge":
            l = 1.3 * sine(440, t)  # will clip at write_wav
            r = 1.3 * sine(550, t)
        elif kind == "chirp":
            f = 100 + (2000 - 100) * (t / dur)
            l = r = 0.5 * math.sin(2 * math.pi * f * t)
        else:
            raise ValueError(kind)

        out.append((l, r))

    return out


SIGNALS = [
    # adversarial stereo (errorL/errorR divergence)
    ("l_sine_r_silence", 2.0),
    ("l_silence_r_sine", 2.0),
    ("l_freq_a_r_freq_b", 2.0),
    ("l_sine_r_noise", 2.0),
    ("l_impulse_r_tone", 2.0),
    ("impulses_offset", 2.0),
    ("amp_asymmetry", 2.0),
    ("phase_inverted", 2.0),
    ("uncorrelated_noise", 2.0),
    # general corpus
    ("stereo_sine", 2.0),
    ("mono_sine", 2.0),
    ("silence", 2.0),
    ("quiet", 2.0),
    ("full_scale", 2.0),
    ("impulse", 2.0),
    ("transient_attack", 2.0),
    ("high_dynamic", 2.0),
    ("clipping_edge", 2.0),
    ("chirp", 2.0),
    # odd sample counts
    ("odd_stereo_sine", 2.0),
    # very short
    ("stereo_sine_short", 0.25),
]


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "corpus"
    os.makedirs(outdir, exist_ok=True)
    for kind, dur in SIGNALS:
        for rate in RATES:
            odd = kind.startswith("odd_")
            base = kind.replace("odd_", "").replace("_short", "")
            samples = make_signal(base, rate, dur)
            if odd:
                samples.append((0.0, 0.0))  # odd sample count
            name = "%s_%d" % (kind, rate)
            write_wav(os.path.join(outdir, name + ".wav"), samples, rate)
            print(name)
    print("done -> %s" % outdir)


if __name__ == "__main__":
    main()
