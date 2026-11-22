"""Cut a flat (rectilinear) view out of a 360-degree Panoramax frame, for a realism pose.

usage: python lab/realism/pano-view.py PANORAMAX_ID OUT.jpg [--yaw 0] [--pitch 0] [--hfov 70] [--width 1600] [--height 900]
The yaw is relative to the frame's own heading (its view:azimuth), so 0 looks along the way the camera travelled.
Needs Python 3 with NumPy and Pillow. Photographs are never committed (lab/realism/README.md).
"""
import argparse
import io
import json
import math
import urllib.request

import numpy as np
from PIL import Image

HEADERS = {"User-Agent": "OSDS-realism/1.0 (game development; realism score)"}


def fetch(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS), timeout=120) as r:
        return r.read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("id")
    ap.add_argument("out")
    ap.add_argument("--yaw", type=float, default=0.0)
    ap.add_argument("--pitch", type=float, default=0.0)
    ap.add_argument("--hfov", type=float, default=70.0)
    ap.add_argument("--width", type=int, default=1600)
    ap.add_argument("--height", type=int, default=900)
    a = ap.parse_args()
    meta = json.loads(fetch(f"https://api.panoramax.xyz/api/search?ids={a.id}"))["features"][0]
    data = None
    for key in ("hd", "sd", "thumb"):  # the full-size frame first; some hosts refuse it, then the 2048 px one
        if key in meta["assets"]:
            try:
                data = fetch(meta["assets"][key]["href"])
                Image.open(io.BytesIO(data)).verify()
                break
            except Exception as ex:
                print(f"{key} failed: {ex}")
    try:
        img = Image.open(io.BytesIO(data)).convert("RGB")
    except Exception as ex:
        raise SystemExit(f"no usable picture for {a.id}: {ex}")
    src = np.asarray(img, dtype=np.float32)
    sh, sw = src.shape[:2]
    w, h = a.width, a.height
    f = (w / 2) / math.tan(math.radians(a.hfov) / 2)
    x = (np.arange(w) - (w - 1) / 2)[None, :].repeat(h, 0)
    y = ((h - 1) / 2 - np.arange(h))[:, None].repeat(w, 1)
    z = np.full((h, w), f)
    p, q = math.radians(a.pitch), math.radians(a.yaw)
    y2 = y * math.cos(p) + z * math.sin(p)
    z2 = -y * math.sin(p) + z * math.cos(p)
    x3 = x * math.cos(q) + z2 * math.sin(q)
    z3 = -x * math.sin(q) + z2 * math.cos(q)
    lon = np.arctan2(x3, z3)  # 0 = the centre of the equirectangular frame
    lat = np.arctan2(y2, np.hypot(x3, z3))
    u = (lon / (2 * math.pi) + 0.5) * (sw - 1)
    v = (0.5 - lat / math.pi) * (sh - 1)
    u0, v0 = np.floor(u).astype(int), np.floor(v).astype(int)
    fu, fv = (u - u0)[..., None], (v - v0)[..., None]
    u1, v1 = np.clip(u0 + 1, 0, sw - 1), np.clip(v0 + 1, 0, sh - 1)
    u0, v0 = np.clip(u0, 0, sw - 1), np.clip(v0, 0, sh - 1)
    out = (src[v0, u0] * (1 - fu) * (1 - fv) + src[v0, u1] * fu * (1 - fv) + src[v1, u0] * (1 - fu) * fv + src[v1, u1] * fu * fv)
    Image.fromarray(np.clip(out + 0.5, 0, 255).astype(np.uint8)).save(a.out, quality=92)
    print(f"saved {a.out} from {sw}x{sh}")


if __name__ == "__main__":
    main()
