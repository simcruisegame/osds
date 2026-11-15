"""Road surface head for the aerial eye (first material head; library keys for the road surface).

    python -I lab/scan/road_surface.py labels --data C:/aiwork/scan-data      # OSM surface per road pixel
    python -I lab/scan/road_surface.py train  --data C:/aiwork/scan-data      # head on the student's features
    python -I lab/scan/road_surface.py eval   --data C:/aiwork/scan-data

Labels: OpenStreetMap highway ways with a surface=* tag (ODbL, (c) OpenStreetMap contributors), from the
Overpass API (one polite query per 3 x 3 tile place, cached), rasterised along each way at its class width.
Classes (library keys road/<kind>): asphalt, concrete, paving (paving_stones, sett, bricks),
chipseal, gravel (gravel, fine_gravel, compacted, pebblestone), dirt (dirt, ground, earth, mud, sand, grass).
The head reads the lite student's 1/4-resolution decoder features (96 channels), so it adds about 0.03 M
weights and a few ms per tile; per way the game takes the mean over the way's pixels.
"""
import argparse, glob, json, math, os, sys, time, urllib.parse, urllib.request

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scan_data as sd  # noqa: E402

KINDS = ["asphalt", "concrete", "paving", "chipseal", "gravel", "dirt"]
SURFACE = {"asphalt": 0, "concrete": 1, "concrete:plates": 1, "concrete:lanes": 1, "paving_stones": 2, "sett": 2,
           "bricks": 2, "cobblestone": 2, "unhewn_cobblestone": 2, "chipseal": 3, "gravel": 4, "fine_gravel": 4,
           "compacted": 4, "pebblestone": 4, "dirt": 5, "ground": 5, "earth": 5, "mud": 5, "sand": 5, "grass": 5}
WIDTH = {"motorway": 22, "trunk": 16, "primary": 13, "secondary": 11, "tertiary": 9, "unclassified": 7,
         "residential": 7, "service": 4.5, "track": 3.5, "living_street": 6}
OVERPASS = "https://overpass-api.de/api/interpreter"


def overpass(bbox, cache):
    if os.path.exists(cache):
        return json.load(open(cache))
    s, w, n, e = bbox
    q = f'[out:json][timeout:60];way["highway"]["surface"]({s},{w},{n},{e});out geom;'
    req = urllib.request.Request(OVERPASS, data=urllib.parse.urlencode({"data": q}).encode(), headers=sd.UA)
    for k in range(4):
        try:
            data = json.load(urllib.request.urlopen(req, timeout=120))
            os.makedirs(os.path.dirname(cache), exist_ok=True)
            json.dump(data, open(cache, "w"))
            time.sleep(2.0)  # polite: Overpass asks for few requests per second
            return data
        except Exception as ex:  # noqa: BLE001
            print("overpass retry", k, ex, flush=True)
            time.sleep(15 * (k + 1))
    return {"elements": []}


def tile_lonlat_bbox(x, y, z=16):
    n = 2 ** z
    lon0, lon1 = x / n * 360 - 180, (x + 1) / n * 360 - 180
    lat = lambda yy: math.degrees(math.atan(math.sinh(math.pi * (1 - 2 * yy / n))))
    return lat(y + 1), lon0, lat(y), lon1


def label_tile(x, y, ways, size=512):
    """Road surface kind per pixel (255 = none) on the tile's 512 grid."""
    img = Image.new("L", (size, size), 255)
    d = ImageDraw.Draw(img)
    n = 2 ** 16
    latm = math.degrees(math.atan(math.sinh(math.pi * (1 - 2 * (y + 0.5) / n))))
    mpp = 2 * sd.ORIGIN / n / size * math.cos(math.radians(latm))
    for w in ways:
        k = SURFACE.get(w.get("tags", {}).get("surface", ""))
        if k is None or "geometry" not in w:
            continue
        width = WIDTH.get(w["tags"].get("highway", ""), 0)
        if width <= 0:
            continue
        pts = []
        for p in w["geometry"]:
            u = ((p["lon"] + 180) / 360 * n - x) * size
            v = ((1 - math.asinh(math.tan(math.radians(p["lat"]))) / math.pi) / 2 * n - y) * size
            pts.append((u, v))
        # the core of the road only (70 % of its width): the edge pixels mix verge and road
        d.line(pts, fill=k, width=max(1, int(round(0.7 * width / mpp))))
    return np.asarray(img)


def cmd_labels(a):
    rows = {}
    for f in glob.glob(os.path.join(a.data, "index-*.json")):
        for r in json.load(open(f)):
            rows.setdefault(r["place"], []).append(r)
    os.makedirs(os.path.join(a.data, "road"), exist_ok=True)
    counts = np.zeros(len(KINDS), np.int64)
    for place, tiles in rows.items():
        xs, ys = [t["x"] for t in tiles], [t["y"] for t in tiles]
        s, w, _, _ = tile_lonlat_bbox(min(xs), max(ys))
        _, _, n, e = tile_lonlat_bbox(max(xs), min(ys))
        data = overpass((s, w, n, e), os.path.join(a.data, "overpass", place + ".json"))
        ways = [el for el in data.get("elements", []) if el.get("type") == "way"]
        for t in tiles:
            lab = label_tile(t["x"], t["y"], ways)
            np.save(os.path.join(a.data, "road", t["file"].replace(".npz", ".npy")), lab)
            counts += np.bincount(lab[lab != 255].ravel(), minlength=len(KINDS))[:len(KINDS)]
        print(place, len(ways), "ways", flush=True)
    print("pixels per kind", dict(zip(KINDS, counts.tolist())))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd")
    ap.add_argument("--data", required=True)
    a = ap.parse_args()
    {"labels": cmd_labels}[a.cmd](a)


if __name__ == "__main__":
    main()
