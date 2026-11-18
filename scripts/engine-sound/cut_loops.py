#!/usr/bin/env python3
"""Cut engine recordings into the game's loop banks, and measure them.

OSDS plays a recorded engine as loops: one short loop per steady engine speed, on throttle
and off, inside the car and outside (docs/recording-your-car.md says how to record them).
This script turns the recordings into that format: data/sound/engines/<name>/bank.json and
one 16-bit mono WAV per loop. Every loop is a whole number of engine cycles (720 crank
degrees), cut where the end matches the start and crossfaded over 20 ms, so it repeats
without a click. Loops keep their recorded loudness (one gain for the whole bank), so the
engine gets louder with revs and load the way the recording did.

Needs Python 3.9+, numpy and ffmpeg on the PATH (ffmpeg reads phone formats: m4a, mp3, wav).

Commands
  steady    A folder of steady-rpm recordings named <mic>_<load>_<rpm>.<ext>
            (exterior_on_3000.m4a, cabin_off_2500.wav, ...). The rpm in the name is the
            target; the script measures the real speed from the firing frequency.
  sweep     One rev-up or pull recording: tracks the engine speed through it and cuts a
            short loop at each rpm step, evened out to that exact speed.
  analyze   Order levels of a recording at a known or tracked speed (JSON), the reference
            the game's spectrogram test compares each car's voice with.
  spectrogram  A PNG spectrogram with the firing-order lines drawn over it.

Examples
  python scripts/engine-sound/cut_loops.py steady ~/miata-recordings --cylinders 4 \
      --out data/sound/engines/nb2001 --name "2001 Miata (owner)" --licence "CC BY 4.0" \
      --credit "Recorded by the OSDS project owner"
  python scripts/engine-sound/cut_loops.py sweep pull.wav --cylinders 8 --start-rpm 1500 \
      --mic exterior --load on --steps 1500:6000:500 --out data/sound/engines/test
  python scripts/engine-sound/cut_loops.py analyze idle.wav --cylinders 4 --rpm 850
  python scripts/engine-sound/cut_loops.py spectrogram pull.wav --cylinders 8 --png pull.png
"""
from __future__ import annotations

import argparse
import json
import math
import os
import re
import struct
import subprocess
import sys
import zlib
from pathlib import Path

import numpy as np

RATE = 44100
CROSSFADE_S = 0.020


# --- Audio in and out ---------------------------------------------------------------------

def read_audio(path: str, rate: int = RATE) -> np.ndarray:
    """Any format ffmpeg reads -> mono float32 at rate."""
    out = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", str(path), "-ac", "1", "-ar", str(rate),
                          "-f", "f32le", "-"], check=True, capture_output=True).stdout
    return np.frombuffer(out, dtype=np.float32).astype(np.float64)


def write_wav(path: Path, x: np.ndarray, rate: int = RATE) -> None:
    pcm = np.clip(np.round(x * 32767.0), -32768, 32767).astype("<i2").tobytes()
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt ")
        f.write(struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16))
        f.write(b"data" + struct.pack("<I", len(pcm)) + pcm)


def high_pass(x: np.ndarray, hz: float = 25.0, rate: int = RATE) -> np.ndarray:
    """Removes rumble and DC below about hz (the signal minus its moving average)."""
    n = max(3, int(rate / hz))
    c = np.cumsum(np.concatenate([[0.0], x]))
    avg = (c[n:] - c[:-n]) / n
    avg = np.concatenate([np.full(n // 2, avg[0]), avg, np.full(len(x) - len(avg) - n // 2, avg[-1])])
    return x - avg


# --- Engine speed -------------------------------------------------------------------------

def magnitude_at(x: np.ndarray, hz: np.ndarray, rate: int = RATE) -> np.ndarray:
    """Amplitude of the Hann-windowed block at arbitrary frequencies (zero-padded FFT, interpolated)."""
    w = np.hanning(len(x))
    n = 1 << int(math.ceil(math.log2(len(x) * 4)))
    spec = np.abs(np.fft.rfft(x * w, n)) / (w.sum() / 2)
    return np.interp(np.asarray(hz, dtype=float) * n / rate, np.arange(len(spec)), spec)


def comb_score(block: np.ndarray, firing_hz: np.ndarray, orders: int = 8) -> np.ndarray:
    """How strongly a harmonic comb at each candidate firing frequency is present."""
    w = np.hanning(len(block))
    n = 1 << int(math.ceil(math.log2(len(block) * 4)))
    spec = np.abs(np.fft.rfft(block * w, n)) / (w.sum() / 2)
    bins = np.arange(len(spec))
    score = np.zeros(len(firing_hz))
    for k in range(1, orders + 1):
        f = firing_hz * k
        m = np.where(f < 0.45 * RATE, np.interp(f * n / RATE, bins, spec), 0.0)
        score += np.log1p(m * 1000.0) / k ** 0.5
    return score


def measure_rpm(x: np.ndarray, cylinders: int, guess_rpm: float, spread: float = 0.15) -> float:
    """The steady speed near guess_rpm (+-spread), from the firing comb over the whole block."""
    block = x[: min(len(x), RATE * 2)]
    firing = guess_rpm * cylinders / 120.0
    cand = np.linspace(firing * (1 - spread), firing * (1 + spread), 241)
    s = comb_score(block, cand)
    i = int(np.argmax(s))
    if 0 < i < len(cand) - 1:  # parabolic refinement
        d = (s[i - 1] - s[i + 1]) / (2 * (s[i - 1] - 2 * s[i] + s[i + 1]) + 1e-12)
        f = cand[i] + d * (cand[1] - cand[0])
    else:
        f = cand[i]
    return f * 120.0 / cylinders


def track_rpm(x: np.ndarray, cylinders: int, start_rpm: float, hop_s: float = 0.05, block_s: float = 0.16,
              max_step: float = 0.08) -> tuple[np.ndarray, np.ndarray]:
    """Engine speed every hop_s through a recording whose speed changes (a rev-up)."""
    hop, size = int(hop_s * RATE), int(block_s * RATE)
    times, rpms = [], []
    rpm = start_rpm
    for at in range(0, len(x) - size, hop):
        block = x[at: at + size]
        firing = rpm * cylinders / 120.0
        cand = np.linspace(firing * (1 - max_step), firing * (1 + max_step), 81)
        s = comb_score(block, cand, orders=6)
        rpm = cand[int(np.argmax(s))] * 120.0 / cylinders
        times.append((at + size / 2) / RATE)
        rpms.append(rpm)
    return np.array(times), np.array(rpms)


# --- Loops --------------------------------------------------------------------------------

def best_loop(x: np.ndarray, rpm: float, seconds: float) -> np.ndarray:
    """A seamless loop of whole engine cycles near `seconds` long from a steady stretch."""
    cycle = 120.0 / rpm * RATE  # samples per 720 degrees
    cycles = max(2, int(round(seconds * RATE / cycle)))
    length = int(round(cycles * cycle))
    fade = int(CROSSFADE_S * RATE)
    if len(x) < length + 2 * fade + 16:
        raise ValueError(f"recording too short for a {seconds:.2f} s loop at {rpm:.0f} rpm")
    # Steadiest, loudest-without-clipping stretch: the start with the least level change.
    starts = np.arange(fade, len(x) - length - 2 * fade, max(1, RATE // 20))
    win = int(0.05 * RATE)
    env = np.sqrt(np.convolve(x ** 2, np.ones(win) / win, mode="same"))
    def badness(s: int) -> float:
        e = env[s: s + length]
        clip = np.mean(np.abs(x[s: s + length]) > 0.98)
        return float(np.std(e) / (np.mean(e) + 1e-9) + 10 * clip)
    start = int(min(starts, key=badness)) if len(starts) else fade
    # Fine-tune the length (+-2 %) so the material after the end matches the start.
    ref = x[start: start + fade]
    best, best_len = -2.0, length
    for n in range(int(length * 0.98), int(length * 1.02) + 1):
        seg = x[start + n: start + n + fade]
        if len(seg) < fade:
            break
        c = float(np.dot(ref, seg) / (np.linalg.norm(ref) * np.linalg.norm(seg) + 1e-12))
        if c > best:
            best, best_len = c, n
    y = x[start: start + best_len].copy()
    w = np.sin(np.linspace(0, np.pi / 2, fade)) ** 2
    y[:fade] = w * x[start: start + fade] + (1 - w) * x[start + best_len: start + best_len + fade]
    return y


def flatten_sweep(x: np.ndarray, times: np.ndarray, rpms: np.ndarray, target: float, seconds: float) -> np.ndarray:
    """Around the moment the engine passed `target`, resample so every cycle is at `target` rpm."""
    t_hit = float(np.interp(target, rpms, times)) if np.all(np.diff(rpms) >= -50) else float(times[np.argmin(np.abs(rpms - target))])
    t = np.arange(len(x)) / RATE
    rpm_t = np.interp(t, times, rpms)
    phase = np.cumsum(rpm_t / 120.0) / RATE  # engine cycles since the start
    centre = float(np.interp(t_hit, t, phase))
    cycles_wanted = seconds * target / 120.0 + 2 * CROSSFADE_S * target / 120.0 + 1
    grid = centre - cycles_wanted / 2 + np.arange(int(cycles_wanted * 120.0 / target * RATE)) * (target / 120.0) / RATE
    src = np.interp(grid, phase, t) * RATE
    flat = np.interp(src, np.arange(len(x)), x)
    return best_loop(flat, target, seconds)


def write_bank(out: Path, loops: list[dict], name: str, credit: str, licence: str, source: str, headroom_db: float = -3.0) -> None:
    out.mkdir(parents=True, exist_ok=True)
    peak = max(float(np.max(np.abs(l["samples"]))) for l in loops)
    gain = 10 ** (headroom_db / 20) / max(peak, 1e-9)  # one gain for the whole bank
    manifest = {"name": name, "credit": credit, "licence": licence, "source": source, "gain_db": 0.0,
                "sample_rate": RATE, "loops": []}
    for l in sorted(loops, key=lambda l: (l["mic"], l["load"], l["rpm"])):
        file = f"{l['mic']}_{l['load']}_{int(round(l['rpm']))}.wav"
        write_wav(out / file, l["samples"] * gain)
        manifest["loops"].append({"file": file, "rpm": round(l["rpm"], 1), "load": l["load"], "mic": l["mic"],
                                  "seconds": round(len(l["samples"]) / RATE, 3), "from": l.get("from", "")})
    (out / "bank.json").write_text(json.dumps(manifest, indent=1) + "\n", encoding="utf-8")
    print(f"wrote {len(loops)} loops to {out}")


NAME = re.compile(r"^(cabin|exterior|inside|outside)_(on|off)_(\d{3,5})$", re.I)


def cmd_steady(a) -> None:
    loops = []
    for f in sorted(Path(a.folder).iterdir()):
        m = NAME.match(f.stem)
        if not m:
            continue
        mic = "cabin" if m.group(1).lower() in ("cabin", "inside") else "exterior"
        x = high_pass(read_audio(f))
        x = x[int(0.5 * RATE): len(x) - int(0.5 * RATE)]
        rpm = measure_rpm(x, a.cylinders, float(m.group(3)))
        if abs(rpm / float(m.group(3)) - 1) > 0.12:
            print(f"  {f.name}: measured {rpm:.0f} rpm, far from {m.group(3)}; check the cylinder count", file=sys.stderr)
        loops.append({"mic": mic, "load": m.group(2).lower(), "rpm": rpm, "samples": best_loop(x, rpm, a.seconds), "from": f.name})
        print(f"  {f.name}: {rpm:.0f} rpm")
    if not loops:
        sys.exit("no files named like exterior_on_3000.m4a")
    write_bank(Path(a.out), loops, a.name, a.credit, a.licence, a.source)


def parse_steps(text: str) -> list[float]:
    lo, hi, step = (float(v) for v in text.split(":"))
    return list(np.arange(lo, hi + 1e-6, step))


def cmd_sweep(a) -> None:
    x = high_pass(read_audio(a.file))
    if a.trim:
        lo, hi = (float(v) for v in a.trim.split(":"))
        x = x[int(lo * RATE): int(hi * RATE)]
    times, rpms = track_rpm(x, a.cylinders, a.start_rpm)
    print(f"tracked {rpms.min():.0f}-{rpms.max():.0f} rpm over {times[-1]:.1f} s")
    loops = []
    for target in parse_steps(a.steps):
        if target < rpms.min() - 50 or target > rpms.max() + 50:
            print(f"  {target:.0f} rpm: not reached, skipped")
            continue
        loops.append({"mic": a.mic, "load": a.load, "rpm": target, "from": Path(a.file).name,
                      "samples": flatten_sweep(x, times, rpms, target, a.seconds)})
        print(f"  {target:.0f} rpm: cut")
    if a.out:
        out = Path(a.out)
        if (out / "bank.json").exists() and a.append:
            old = json.loads((out / "bank.json").read_text(encoding="utf-8"))
            for l in old["loops"]:
                if (l["mic"], l["load"]) != (a.mic, a.load):
                    loops.append({**l, "samples": read_audio(out / l["file"])})
        write_bank(out, loops, a.name, a.credit, a.licence, a.source)


def order_levels(x: np.ndarray, rpm: float, orders: list[float]) -> list[float]:
    rev_hz = rpm / 60.0
    block = x[: min(len(x), RATE * 2)]
    m = magnitude_at(block, np.array([o * rev_hz for o in orders]))
    ref = max(float(m.max()), 1e-12)
    return [round(max(20 * math.log10(max(v, 1e-12) / ref), -45.0), 1) for v in m]


def cmd_analyze(a) -> None:
    x = high_pass(read_audio(a.file))
    if a.trim:
        lo, hi = (float(v) for v in a.trim.split(":"))
        x = x[int(lo * RATE): int(hi * RATE)]
    rpm = measure_rpm(x, a.cylinders, a.rpm) if a.rpm else None
    if rpm is None:
        sys.exit("give --rpm (a guess within 15 %)")
    orders = [k * 0.5 for k in range(1, 2 * a.max_order + 1)]
    levels = order_levels(x, rpm, orders)
    rms_db = 20 * math.log10(float(np.sqrt(np.mean(x ** 2))) + 1e-12)
    result = {"file": Path(a.file).name, "cylinders": a.cylinders, "rpm": round(rpm, 1),
              "firing_order": a.cylinders / 2, "orders": orders, "order_db": levels, "rms_dbfs": round(rms_db, 2)}
    text = json.dumps(result, indent=1)
    if a.json:
        Path(a.json).write_text(text + "\n", encoding="utf-8")
    print(text)


def write_png(path: Path, rgb: np.ndarray) -> None:
    h, w, _ = rgb.shape
    raw = b"".join(b"\x00" + rgb[y].astype(np.uint8).tobytes() for y in range(h))
    def chunk(t: bytes, d: bytes) -> bytes:
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def cmd_spectrogram(a) -> None:
    x = read_audio(a.file)
    size, top = 16384, a.max_hz
    hop = max(512, (len(x) - size) // 1600)
    frames = [np.abs(np.fft.rfft(x[i: i + size] * np.hanning(size))) for i in range(0, len(x) - size, hop)]
    s = 20 * np.log10(np.array(frames).T + 1e-9)
    bins = int(top / (RATE / size))
    s = s[:bins][::-1]
    s = np.clip((s - (s.max() - 70)) / 70, 0, 1)
    img = np.stack([s * 255, s ** 2 * 230, (1 - s) * 60 * (s > 0.05)], axis=-1)
    if a.cylinders and a.start_rpm:
        times, rpms = track_rpm(x, a.cylinders, a.start_rpm)
        for col in range(img.shape[1]):
            rpm = float(np.interp((col * hop + size / 2) / RATE, times, rpms))
            for k in (1, 2):
                row = bins - 1 - int(k * rpm * a.cylinders / 120.0 / (RATE / size))
                if 0 <= row < bins:
                    img[row, col] = (80, 200, 255)
    img = np.repeat(img, max(1, 400 // img.shape[0]), axis=0)
    write_png(Path(a.png), img)
    print(f"wrote {a.png} ({img.shape[1]} x {img.shape[0]})")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    def bank_args(q):
        q.add_argument("--out", required=False)
        q.add_argument("--name", default="")
        q.add_argument("--credit", default="")
        q.add_argument("--licence", default="")
        q.add_argument("--source", default="")
        q.add_argument("--seconds", type=float, default=1.2, help="loop length (whole cycles near this)")
    q = sub.add_parser("steady"); q.add_argument("folder"); q.add_argument("--cylinders", type=int, required=True); bank_args(q)
    q = sub.add_parser("sweep"); q.add_argument("file"); q.add_argument("--cylinders", type=int, required=True)
    q.add_argument("--start-rpm", type=float, required=True); q.add_argument("--steps", default="1000:7000:500")
    q.add_argument("--mic", choices=["cabin", "exterior"], default="exterior"); q.add_argument("--load", choices=["on", "off"], default="on")
    q.add_argument("--trim", help="start:end seconds"); q.add_argument("--append", action="store_true", help="keep the bank's other mic/load sets")
    bank_args(q)
    q.set_defaults(seconds=0.35)
    q = sub.add_parser("analyze"); q.add_argument("file"); q.add_argument("--cylinders", type=int, required=True)
    q.add_argument("--rpm", type=float); q.add_argument("--trim"); q.add_argument("--max-order", type=int, default=10); q.add_argument("--json")
    q = sub.add_parser("spectrogram"); q.add_argument("file"); q.add_argument("--png", required=True)
    q.add_argument("--cylinders", type=int); q.add_argument("--start-rpm", type=float); q.add_argument("--max-hz", type=float, default=2000)
    a = p.parse_args()
    {"steady": cmd_steady, "sweep": cmd_sweep, "analyze": cmd_analyze, "spectrogram": cmd_spectrogram}[a.cmd](a)


if __name__ == "__main__":
    main()
