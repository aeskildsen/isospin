#!/usr/bin/env python3
# Copyright (C) 2026 Anders Eskildsen
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Compare two renders of tools/ab: level, brightness, stereo and the null residual.

Usage:
    compare.py a.wav b.wav [--windows N]

Prints for each file:
  - peak and RMS of each channel;
  - brightness: the power-weighted spectral centroid of the mono sum, for the
    whole file and for N equal parts of it (the power weighting makes it
    insensitive to a low noise floor);
  - the correlation of the two channels (for two channels).
Then the null residual: the RMS of a - b relative to the RMS of a, at the lag
from -4 to 4 samples that gives the smallest residual. It is meaningful only
when both sides are deterministic (no random values, or the same ones).
Needs numpy.
"""
import argparse
import struct
import sys
import wave

import numpy as np


def read(path: str) -> tuple[int, np.ndarray]:
    """Reads a 32-bit float WAV file (scsynth's NRT output) as frames x channels.
    The wave module of Python reads only integer PCM, thus the chunks are
    read here."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        sys.exit(f"compare: {path} is not a WAV file")
    pos, rate, channels, fmt, bits = 12, None, None, None, None
    while pos + 8 <= len(data):
        tag, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if tag == b"fmt ":
            fmt, channels, rate = struct.unpack("<HHI", body[:8])
            bits = struct.unpack("<H", body[14:16])[0]
        elif tag == b"data":
            if fmt not in (3, 0xFFFE) or bits != 32:
                sys.exit(f"compare: {path}: only 32-bit float WAV files")
            return rate, np.frombuffer(body, dtype="<f4").reshape(-1, channels).astype(np.float64)
        pos += 8 + size + (size & 1)
    sys.exit(f"compare: {path} has no data chunk")


def centroid(x: np.ndarray, rate: int) -> float:
    power = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    total = power.sum()
    return float((power * np.fft.rfftfreq(len(x), 1 / rate)).sum() / total) if total > 0 else 0.0


def rms(x: np.ndarray) -> float:
    return float(np.sqrt(np.mean(x ** 2)))


def describe(name: str, rate: int, x: np.ndarray, windows: int) -> None:
    channels = " ".join(f"ch{c} peak {np.abs(x[:, c]).max():.4f} rms {rms(x[:, c]):.4f}"
                        for c in range(x.shape[1]))
    print(f"{name}: {channels}")
    mono = x.sum(axis=1)
    parts = np.array_split(mono, windows)
    profile = " ".join(f"{centroid(p, rate):.0f}" for p in parts)
    print(f"  brightness (Hz) {centroid(mono, rate):.0f}; over time: {profile}")
    if x.shape[1] == 2:
        print(f"  L/R correlation {np.corrcoef(x[:, 0], x[:, 1])[0, 1]:.3f}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("a")
    parser.add_argument("b")
    parser.add_argument("--windows", type=int, default=8)
    args = parser.parse_args()

    rate_a, a = read(args.a)
    rate_b, b = read(args.b)
    if rate_a != rate_b or a.shape[1] != b.shape[1]:
        sys.exit("compare: the files have different sample rates or channel counts")
    describe("a", rate_a, a, args.windows)
    describe("b", rate_b, b, args.windows)

    n = min(len(a), len(b)) - 8
    ref = rms(a[4:4 + n])
    if ref == 0:
        print("null: a is silent")
        return
    best = min(range(-4, 5), key=lambda lag: rms(a[4:4 + n] - b[4 + lag:4 + lag + n]))
    residual = rms(a[4:4 + n] - b[4 + best:4 + best + n])
    exact = best == 0 and np.array_equal(a[:n], b[:n])
    level = "-inf" if residual == 0 else f"{20 * np.log10(residual / ref):.1f}"
    print(f"null: residual {level} dB relative to a, with b {best} samples later than a"
          + ("; the files are the same, bit for bit" if exact else ""))


if __name__ == "__main__":
    main()
