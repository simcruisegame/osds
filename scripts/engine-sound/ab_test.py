#!/usr/bin/env python3
"""A blind A/B test: can you tell the real engine from the game's?

make   Cuts matched clips from a real recording and from the game's render (the WAVs that
       the OSDS.Audio.CarSweeps test writes to Saved/Audio), levels them to the same
       loudness, and writes N trials: trial_01_A.wav / trial_01_B.wav ... in random order.
       The answer key goes to key.json; do not open it until you have listened.
score  Reads your answers (a text file, one letter per line: the clip you think is REAL)
       and prints how often you picked the real one. Under 60 % over 10+ trials means the
       game's sound passes (the 2.2 plan's target).

  python scripts/engine-sound/ab_test.py make --real idle.wav:2:4.5 --game Saved/Audio/nb2001-outside.wav:0.2:1.4 --trials 10 --out ab
  python scripts/engine-sound/ab_test.py score ab answers.txt
"""
from __future__ import annotations

import argparse
import json
import random
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cut_loops as cl  # noqa: E402


def clip(spec: str) -> np.ndarray:
    path, start, end = spec.rsplit(":", 2)
    x = cl.read_audio(path)
    x = x[int(float(start) * cl.RATE): int(float(end) * cl.RATE)]
    x = cl.high_pass(x)
    fade = int(0.03 * cl.RATE)
    ramp = np.linspace(0, 1, fade)
    x[:fade] *= ramp
    x[-fade:] *= ramp[::-1]
    return x / (np.sqrt(np.mean(x ** 2)) + 1e-12) * 0.1  # same loudness (RMS)


def cmd_make(a) -> None:
    real, game = clip(a.real), clip(a.game)
    n = min(len(real), len(game))
    real, game = real[:n], game[:n]
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    key = {}
    for t in range(1, a.trials + 1):
        real_is_a = random.random() < 0.5
        cl.write_wav(out / f"trial_{t:02d}_A.wav", real if real_is_a else game)
        cl.write_wav(out / f"trial_{t:02d}_B.wav", game if real_is_a else real)
        key[t] = "A" if real_is_a else "B"
    (out / "key.json").write_text(json.dumps({"real": a.real, "game": a.game, "answers": key}, indent=1), encoding="utf-8")
    print(f"{a.trials} trials in {out}. Listen to each pair and write the letter you think is REAL, one per line.")


def cmd_score(a) -> None:
    key = json.loads((Path(a.folder) / "key.json").read_text(encoding="utf-8"))["answers"]
    picks = [l.strip().upper() for l in Path(a.answers).read_text(encoding="utf-8").splitlines() if l.strip()]
    right = sum(1 for i, p in enumerate(picks, 1) if key.get(str(i)) == p)
    share = right / max(len(picks), 1)
    print(f"picked the real engine {right} of {len(picks)} times ({100 * share:.0f} %): "
          + ("the game passes (under 60 %)" if share < 0.6 else "the real one is still easy to tell"))


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    q = sub.add_parser("make"); q.add_argument("--real", required=True, help="file:start:end seconds")
    q.add_argument("--game", required=True, help="file:start:end seconds"); q.add_argument("--trials", type=int, default=10); q.add_argument("--out", required=True)
    q = sub.add_parser("score"); q.add_argument("folder"); q.add_argument("answers")
    a = p.parse_args()
    {"make": cmd_make, "score": cmd_score}[a.cmd](a)


if __name__ == "__main__":
    main()
