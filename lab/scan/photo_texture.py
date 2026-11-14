"""Photo-true texture synthesis (proposed PHTX, v2.0.4): for each land-cover class of a tile, a clean,
class-pure texture made only from this place's own aerial photo, so the meso scale (0.5-30 m: grass
patches, dirt spots, wear, crop rows, roof grain) is true to the place. The fine scale (mm-cm) stays
with the CC0 detail kit on top.

    python -I lab/scan/photo_texture.py --lat 41.7318 --lon -93.6001 --out C:/aiwork/phtx

1. Photo: USDA NAIP via imagery.geoplatform.gov (public domain), 1024 px over the z16 tile (0.45-0.6 m).
2. Classes: the aerial_student eye (data/ai/scan-eye-student.onnx) on the tile; a cell is "sure" at p >= 0.5.
3. Patches: 24 px squares fully inside the sure mask of one class, with no tree/roof/road/water inside.
4. Synthesis: image quilting (Efros and Freeman 2001) with a minimum-error boundary cut, seeded by
   the tile and class, so a place always gives the same texture.
Cost per class and the patch counts go to phtx-<x>-<y>.json.
"""
import argparse, io, json, math, os, sys, time

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scan_data as sd  # noqa: E402
import restore_pages as rp  # noqa: E402

P, O = 24, 6  # patch size and overlap, source pixels


def eye_probs(img512, model, threads):
    import onnxruntime as ort
    so = ort.SessionOptions(); so.intra_op_num_threads = threads
    s = ort.InferenceSession(model, so, providers=["CPUExecutionProvider"])
    return s.run(None, {"image": (img512.astype(np.float32) / 255).transpose(2, 0, 1)[None]})[0][0]


def patches(img, mask, stride=4):
    out = []
    h, w = mask.shape
    for y in range(0, h - P, stride):
        for x in range(0, w - P, stride):
            if mask[y:y + P, x:x + P].all():
                out.append(img[y:y + P, x:x + P])
    return np.stack(out).astype(np.float32) if out else None


def min_cut(err):
    """Minimum-error vertical seam through err (H x W): for each row the column where the new patch starts."""
    h, w = err.shape
    cost = err.copy()
    for i in range(1, h):
        left = np.r_[np.inf, cost[i - 1, :-1]]
        right = np.r_[cost[i - 1, 1:], np.inf]
        cost[i] += np.minimum(np.minimum(left, cost[i - 1]), right)
    seam = np.zeros(h, int)
    seam[-1] = int(np.argmin(cost[-1]))
    for i in range(h - 2, -1, -1):
        j = seam[i + 1]
        lo, hi = max(0, j - 1), min(w, j + 2)
        seam[i] = lo + int(np.argmin(cost[i, lo:hi]))
    return seam


def quilt(lib, size, rng):
    step = P - O
    n = (size - O) // step + 1
    out = np.zeros((n * step + O, n * step + O, 3), np.float32)
    flat = lib.reshape(len(lib), -1)
    for r in range(n):
        for c in range(n):
            y, x = r * step, c * step
            if r == 0 and c == 0:
                out[:P, :P] = lib[rng.integers(len(lib))]
                continue
            err = np.zeros(len(lib), np.float64)
            if c > 0:
                err += ((lib[:, :, :O] - out[y:y + P, x:x + O]) ** 2).sum((1, 2, 3))
            if r > 0:
                err += ((lib[:, :O, :] - out[y:y + O, x:x + P]) ** 2).sum((1, 2, 3))
            ok = np.flatnonzero(err <= err.min() * 1.15 + 1e-6)
            pick = lib[ok[rng.integers(len(ok))]].copy()
            m = np.ones((P, P), bool)
            if c > 0:
                seam = min_cut(((pick[:, :O] - out[y:y + P, x:x + O]) ** 2).sum(-1))
                for i in range(P):
                    m[i, :seam[i]] = False
            if r > 0:
                seam = min_cut(((pick[:O, :] - out[y:y + O, x:x + P]) ** 2).sum(-1).T)
                for j in range(P):
                    m[:seam[j], j] = False
            region = out[y:y + P, x:x + P]
            region[m] = pick[m]
    return np.clip(out[:size, :size], 0, 255).astype(np.uint8)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lat", type=float, required=True)
    ap.add_argument("--lon", type=float, required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--size", type=int, default=512)
    ap.add_argument("--threads", type=int, default=2)
    ap.add_argument("--model", default=os.path.join(os.path.dirname(__file__), "..", "..", "data", "ai", "scan-eye-student.onnx"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    x, y = sd.tile_of(a.lat, a.lon)
    img = rp.naip(x, y, a.out)
    t = time.perf_counter()
    probs = eye_probs(np.asarray(Image.fromarray(img).resize((512, 512), Image.BILINEAR)), a.model, a.threads)
    t_eye = time.perf_counter() - t
    cls = probs.argmax(0)
    sure = probs.max(0) >= 0.5
    cls1024 = np.asarray(Image.fromarray(np.where(sure, cls, 255).astype(np.uint8)).resize((1024, 1024), Image.NEAREST))
    res = {"tile": [16, x, y], "eye_seconds": round(t_eye, 3), "classes": {}}
    sheet = []
    # Natural ground only: man-made surfaces (roads, parking, roofs) have structure (markings, seams) that
    # quilting copies into grids; they take CC0 materials tinted by TINT instead.
    for name in ("grass", "crop", "bare", "shrub", "tree"):
        c = sd.C[name]
        mask = cls1024 == c
        if mask.mean() < 0.01:
            continue
        lib = patches(img, mask)
        if lib is None or len(lib) < 30:
            continue
        if len(lib) > 3000:
            lib = lib[np.random.default_rng(1).choice(len(lib), 3000, replace=False)]
        rng = np.random.default_rng(x * 73856093 ^ y * 19349663 ^ c)
        t = time.perf_counter()
        tex = quilt(lib, a.size, rng)
        dt = time.perf_counter() - t
        Image.fromarray(tex).save(os.path.join(a.out, f"phtx-{x}-{y}-{name}.png"))
        # Before: the photo crop of the largest sure area of this class (with whatever else is in it).
        ys, xs = np.nonzero(mask)
        cy, cx = int(np.median(ys)), int(np.median(xs))
        y0, x0 = min(max(0, cy - 128), 1024 - 256), min(max(0, cx - 128), 1024 - 256)
        before = img[y0:y0 + 256, x0:x0 + 256]
        sheet.append(np.concatenate([np.asarray(Image.fromarray(before).resize((256, 256))), tex[:256, :256]], 1))
        res["classes"][name] = {"share": round(float(mask.mean()), 3), "patches": int(len(lib)), "seconds": round(dt, 2)}
    if sheet:
        Image.fromarray(np.concatenate(sheet, 0)).save(os.path.join(a.out, f"phtx-{x}-{y}-sheet.png"))
    json.dump(res, open(os.path.join(a.out, f"phtx-{x}-{y}.json"), "w"), indent=1)
    print(json.dumps(res))


if __name__ == "__main__":
    main()
