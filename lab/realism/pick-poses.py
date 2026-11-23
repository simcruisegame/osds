"""Find licensed Panoramax street photographs for realism poses (lab/realism/README.md).

usage:
  python lab/realism/pick-poses.py near LAT LON [HALF_DEG]
      Frames near a point, nearest first: id | metres | lat lon | azimuth | fov | UTC | local solar hour | licence | author | camera.
  python lab/realism/pick-poses.py random --seed 30 --count 8 [--exclude-near poses.json]
      The seeded Random US poses: a seeded draw of points from the Random US areas
      (scripts/RandomPlaces.ps1) and data/test-places.json; for each, the nearest daylight
      CC-licensed frame on Panoramax within about 5 km. Places without one, and kinds already
      drawn (--per-kind), are skipped in the seeded order, so the same seed always gives the same poses. Prints pose entries as JSON.

A 360-degree frame becomes a pose looking along the camera's travel (its azimuth); realism.ps1
cuts the matching view out of the panorama with pano-view.py. Photographs are never committed.
"""
import argparse
import json
import math
import random
import sys
import urllib.parse
import urllib.request

API = "https://api.panoramax.xyz/api/search"
HEADERS = {"User-Agent": "OSDS-realism/1.0 (game development; realism score)"}

# The Random US areas of scripts/RandomPlaces.ps1 (south, north, west, east).
AREAS = {
    "desert": [(32.3, 33.8, -114.5, -111.0), (35.5, 36.6, -116.0, -114.6), (34.5, 35.3, -108.5, -106.5)],
    "mountains": [(38.6, 40.0, -107.0, -105.5), (35.2, 36.0, -84.0, -82.5), (45.5, 46.8, -114.0, -112.0)],
    "plains": [(41.5, 42.8, -95.5, -92.0), (37.8, 39.0, -100.0, -97.0), (40.3, 41.5, -99.5, -96.8)],
    "forest": [(44.0, 45.3, -71.5, -69.5), (44.5, 46.5, -123.5, -122.3), (46.5, 47.5, -94.5, -92.5)],
    "coast": [(35.0, 37.5, -122.4, -120.6), (27.0, 28.6, -82.8, -82.3), (39.2, 40.2, -74.3, -74.0)],
    "suburb": [(33.85, 34.1, -84.6, -84.15), (32.95, 33.2, -96.95, -96.6), (41.75, 42.0, -88.2, -87.95)],
    "small-town": [(40.0, 40.6, -83.5, -82.5), (44.0, 44.6, -73.0, -72.4), (29.9, 30.4, -99.2, -98.5)],
    "city": [(40.70, 40.80, -74.02, -73.93), (41.86, 41.92, -87.65, -87.61), (37.74, 37.80, -122.47, -122.39)],
    "bridge": [(24.62, 24.76, -81.40, -80.90), (45.78, 45.86, -84.76, -84.70), (29.95, 30.30, -90.15, -90.05)],
    "gravel": [(40.40, 40.80, -96.90, -96.40), (43.80, 44.40, -103.80, -103.20), (46.60, 47.20, -100.50, -99.80)],
    "switchback": [(37.90, 38.05, -107.72, -107.62), (38.80, 38.92, -105.10, -105.00), (35.45, 35.52, -83.98, -83.90)],
}
LICENCE = {"CC-BY-4.0": "CC BY 4.0", "CC-BY-SA-4.0": "CC BY-SA 4.0", "CC0": "CC0", "etalab-2.0": "Etalab Open Licence 2.0",
           "CC-BY-SA-2.0": "CC BY-SA 2.0", "CC-BY-2.0": "CC BY 2.0"}
# Fixed places beyond the three first poses (lummus, dragon, bixby): place name, kind, lat, lon. Chosen for
# Panoramax coverage and for spread: farm gravel, Midwest/Mountain suburbs and towns, big cities, desert, snow country.
FIXED = [
    ("nebraska", "farm gravel", 40.6, -96.65),
    ("detroit", "city grid", 42.35, -83.06),
    ("bronx", "dense city", 40.8448, -73.8648),
    ("seattle", "steep city", 47.6062, -122.3321),
    ("ankeny", "suburb", 41.7318, -93.6001),
    ("rawlins", "interstate", 41.7911, -107.2387),
    ("gilbert", "desert suburb", 33.3528, -111.789),
    ("anchorage", "north city", 61.2181, -149.9003),
    ("mojave", "desert rural", 36.05, -115.3),
    ("co-front-range", "mountain town", 39.3, -106.1),
    ("chicago", "downtown", 41.8757, -87.6305),
    ("nyc-lower-east", "dense city", 40.7120, -73.9899),
    ("nevada-desert", "desert rural", 36.05, -115.3),
    ("central-coast", "coast highway", 36.3, -121.9),
]
CC = ("CC-BY-4.0", "CC-BY-SA-4.0", "CC0", "etalab-2.0", "CC-BY-SA-2.0", "CC-BY-2.0")


def search(lat, lon, half, limit=100):
    q = {"bbox": f"{lon - half},{lat - half},{lon + half},{lat + half}", "limit": limit}
    with urllib.request.urlopen(urllib.request.Request(API + "?" + urllib.parse.urlencode(q), headers=HEADERS), timeout=60) as r:
        return json.load(r)["features"]


def metres(lat1, lon1, lat2, lon2):
    k = math.cos(math.radians((lat1 + lat2) / 2))
    return 111320.0 * math.hypot(lat2 - lat1, (lon2 - lon1) * k)


def describe(f, lat, lon):
    p = f["properties"]
    flon, flat = f["geometry"]["coordinates"]
    io = p.get("pers:interior_orientation") or {}
    hh, mm = int(p["datetime"][11:13]), int(p["datetime"][14:16])
    solar = ((hh + mm / 60 + flon / 15) % 24)
    ex = p.get("exif") or {}
    try:
        aspect = float(ex.get("Exif.Image.ImageWidth") or ex.get("Exif.Photo.PixelXDimension")) / float(
            ex.get("Exif.Image.ImageLength") or ex.get("Exif.Photo.PixelYDimension"))
    except (TypeError, ValueError, ZeroDivisionError):
        aspect = 0.0
    fov = io.get("field_of_view")
    if fov is None and abs(aspect - 2.0) < 0.02:
        fov = 360  # an equirectangular panorama without a field-of-view tag
    elif fov is None and aspect > 1.0:
        try:  # a flat frame without a field-of-view tag: from the 35 mm focal length, else a phone's usual 65 degrees
            f35 = float(str(ex.get("Exif.Photo.FocalLengthIn35mmFilm")).split("/")[0])
            fov = round(math.degrees(2 * math.atan(18.0 / f35)), 1) if f35 > 0 else 65.0
        except ValueError:
            fov = 65.0
    author = next((s["name"] for s in reversed(f.get("providers") or []) if "producer" in s.get("roles", [])), "")
    dims = io.get("sensor_array_dimensions") or []
    portrait = 0 < aspect < 1.0  # a portrait photo is a pedestrian snapshot, not a street view
    return {"collection": f.get("collection"), "portrait": portrait, "id": f["id"], "m": metres(lat, lon, flat, flon), "lat": flat, "lon": flon, "az": p.get("view:azimuth"),
            "fov": fov, "utc": p["datetime"][:19] + "Z", "solar": solar, "licence": p.get("license", ""),
            "author": author, "camera": f"{io.get('camera_manufacturer', '')} {io.get('camera_model', '')}".strip(),
            "size": io.get("sensor_array_dimensions")}


def usable(d):
    # Daylight (09-16 local solar time), a heading, a free licence.
    return not d["portrait"] and d["az"] is not None and d["fov"] is not None and 9.0 <= d["solar"] <= 16.0 and d["licence"] in CC


def nearest(lat, lon, halves=(0.005, 0.02, 0.05, 0.12, 0.3)):
    for half in halves:
        found = [describe(f, lat, lon) for f in search(lat, lon, half)]
        found = [d for d in found if usable(d)]
        # A street sequence (a drive or ride) has many frames from one collection close together;
        # a lone snapshot (a bike rack, a doorway) does not.
        per = {}
        for d in found:
            per[d["collection"]] = per.get(d["collection"], 0) + 1
        found = sorted((d for d in found if per[d["collection"]] >= 6), key=lambda d: d["m"])
        if found:
            return found[0]
    return None


def pose_from(d, name, kind):
    is360 = (d["fov"] or 360) >= 180
    hfov = 70.0 if is360 else float(d["fov"])
    pose = {"name": name, "lat": round(d["lat"], 7), "lon": round(d["lon"], 7), "heading": round(d["az"]), "hfov": hfov,
            "utc": d["utc"], "width": 1600, "height": 900, "world": "helper", "snap": "road",
            "photo": {"source": "panoramax", "id": d["id"], "url": f"https://panoramax.xyz/#focus=pic&pic={d['id']}",
                      "author": d["author"], "licence": LICENCE.get(d["licence"], d["licence"])},
            "notes": f"{kind}. {d['camera']}, {'360-degree frame cut to 70 deg along the travel' if is360 else 'flat frame'}."}
    if is360:
        pose["photo"]["view"] = {"yaw": 0.0, "pitch": 0.0, "hfov": hfov}
    return pose


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    n = sub.add_parser("near")
    n.add_argument("lat", type=float)
    n.add_argument("lon", type=float)
    n.add_argument("half", type=float, nargs="?", default=0.01)
    f = sub.add_parser("fixed")
    f.add_argument("--only", default="", help="comma list of fixed place names")
    r = sub.add_parser("random")
    r.add_argument("--seed", type=int, default=30)
    r.add_argument("--count", type=int, default=8)
    r.add_argument("--places", default="data/test-places.json")
    r.add_argument("--tries", type=int, default=80)
    r.add_argument("--exclude", default="", help="a poses.json whose frames (and places within 2 km) are not drawn again")
    r.add_argument("--per-kind", type=int, default=1, help="at most this many poses of one place kind")
    a = ap.parse_args()

    if a.cmd == "near":
        for d in sorted((describe(f, a.lat, a.lon) for f in search(a.lat, a.lon, a.half)), key=lambda d: d["m"])[:40]:
            print(" | ".join(str(x) for x in [d["id"], f"{d['m']:.0f} m", f"{d['lat']:.6f} {d['lon']:.6f}", d["az"], d["fov"],
                                               d["utc"], f"{d['solar']:.1f} h", d["licence"], d["author"], d["camera"]]))
        return

    if a.cmd == "fixed":
        out = []
        for name, kind, lat, lon in FIXED:
            if a.only and name not in a.only.split(","):
                continue
            d = nearest(lat, lon)
            if not d:
                print(f"none near {name}", file=sys.stderr)
                continue
            print(f"{name}: {d['m']:.0f} m from the place, {d['utc']} ({d['solar']:.1f} h solar)", file=sys.stderr)
            out.append(pose_from(d, name, f"Fixed place ({kind}), the nearest daylight Panoramax frame to {lat},{lon}"))
        print(json.dumps(out, indent=2))
        return

    rng = random.Random(a.seed)
    candidates = []
    try:
        with open(a.places, encoding="utf-8") as fh:
            candidates += [(p["kind"], p["lat"], p["lon"], p["key"]) for p in json.load(fh)]
    except OSError:
        pass
    for kind, boxes in AREAS.items():
        for s, nn, w, e in boxes:
            for _ in range(2):
                candidates.append((kind, rng.uniform(s, nn), rng.uniform(w, e), kind))
    rng.shuffle(candidates)
    poses, seen, kinds = [], set(), {}
    taken = []
    if a.exclude:
        with open(a.exclude, encoding="utf-8") as fh:
            for q in json.load(fh)["poses"]:
                taken.append((q["lat"], q["lon"]))
                if q.get("photo", {}).get("id"):
                    seen.add(q["photo"]["id"])
    for kind, lat, lon, key in candidates[: a.tries]:
        if kinds.get(kind, 0) >= a.per_kind:
            continue
        try:
            d = nearest(lat, lon)
        except Exception as ex:  # a slow or failed request skips the place, deterministically enough for a pick
            print(f"skip {key}: {ex}", file=sys.stderr)
            continue
        if d and any(metres(d["lat"], d["lon"], tl, tn) < 2000 for tl, tn in taken):
            d = None
        if not d or d["id"] in seen:
            print(f"none near {key} {lat:.4f},{lon:.4f}", file=sys.stderr)
            continue
        seen.add(d["id"])
        kinds[kind] = kinds.get(kind, 0) + 1
        name = f"r{a.seed}-{len(poses) + 1}-{key.replace('us-', '')}"
        print(f"{name}: {d['m']:.0f} m from the drawn point, {d['utc']} ({d['solar']:.1f} h solar)", file=sys.stderr)
        taken.append((d["lat"], d["lon"]))
        poses.append(pose_from(d, name, f"Random US place ({kind}, seed {a.seed}), the nearest daylight Panoramax frame to {lat:.4f},{lon:.4f}"))
        if len(poses) >= a.count:
            break
    print(json.dumps(poses, indent=2))


if __name__ == "__main__":
    main()
