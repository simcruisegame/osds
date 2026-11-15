#!/usr/bin/env python3
"""learn_roadside.py - learn roadside-thing priors from OpenStreetMap in well-mapped US counties.

World knowledge graph V0 (OSDS / SimCruise innovation lane, 14-15 Nov 2026).
Runs on the PC (the cloud container cannot reach the OSM servers). Needs Python 3.10+, numpy, requests.

What it does, per county in counties.json:
  1. Downloads roads and roadside things from the Overpass API, one small tile at a time, politely
     (one request at a time, a pause between requests, back-off on HTTP 429/504, resume from cache).
  2. Projects to local metres, builds a 1 km road-density grid (context 'dens'), and gives every road
     segment its context: road class, density bin, Census division.
  3. Gives each mapped thing (things.json, learn 'osm') to its nearest road segment.
  4. Completeness gate per thing and county: the share of road length in the 'expected_in' density bins
     whose 3x3 km neighbourhood has at least one mapped item. A county teaches a thing only when the
     score >= min_score. A county that passes teaches with ALL its cells, so true zeros stay zeros.
  5. Learns: rate per 100 m by context with shrinkage to the parent context (empirical Bayes);
     gaps along long ways (min / p05 / p25 / p50 / p75 / CV); lumpiness (Fano factor in 200 m windows);
     offsets from the centreline; fence share per side and fence type; culvert vs bridge share at stream
     crossings; stop-sign share per junction approach; bench at bus stops; street lights on poles.
  6. Held-out counties are measured the same way but never pooled; T1 compares the pooled prior with them.

Outputs (out dir): roadside_priors.csv, roadside_spacing.csv, roadside_offsets.csv, fence_share.csv,
culvert_share.csv, stop_share.csv, cooccur.csv, county_completeness.csv, t1_heldout.csv, run_log.json.
All outputs are a derivative database of OpenStreetMap: ODbL 1.0, credit
"Map data (c) OpenStreetMap contributors". Never write personal data (no user names, no changeset ids).

Usage:
  python learn_roadside.py --selftest                       # no network; synthetic county; checks the maths
  python learn_roadside.py --out out --max-train 20         # full run (hours on a slow link; resumable)
  python learn_roadside.py --only 55025,31109 --out out     # a few counties
  python learn_roadside.py --offline --out out              # use only the cache, no network
Slow internet: --tile-deg 0.1 (smaller requests), --pause 20; a stopped run continues where it stopped.
Slow PC: --fence-step 40 (fewer fence stations); memory peak is about 2-4 GB for the biggest counties.
"""
import argparse
import csv
import json
import math
import os
import sys
import time
from collections import defaultdict

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
EARTH_R = 6371008.8
OVERPASS_URL = "https://overpass-api.de/api/interpreter"
USER_AGENT = "OSDS-world-graph-learner/0.1 (open source driving simulator; one request at a time)"
CREDIT = "Map data (c) OpenStreetMap contributors, ODbL 1.0"

CLASS_MAP = {
    "motorway": "motorway", "motorway_link": "motorway", "trunk": "trunk", "trunk_link": "trunk",
    "primary": "primary", "primary_link": "primary", "secondary": "secondary", "secondary_link": "secondary",
    "tertiary": "tertiary", "tertiary_link": "tertiary", "unclassified": "unclassified",
    "residential": "residential", "living_street": "residential", "service": "service",
}
CLASS_RANK = {"motorway": 7, "trunk": 6, "primary": 5, "secondary": 4, "tertiary": 3,
              "unclassified": 2, "residential": 1, "service": 0, "driveway": -1}
CLASS_WIDTH_M = {"motorway": 11.0, "trunk": 9.0, "primary": 8.0, "secondary": 7.0, "tertiary": 6.5,
                 "unclassified": 5.5, "residential": 6.5, "service": 4.0}
SKIP_SERVICE = {"parking_aisle", "drive-through", "emergency_access"}

CELL_M = 1000.0      # density and completeness grid
WIN_M = 200.0        # lumpiness window
MIN_WAY_FOR_GAPS_M = 500.0


# ----------------------------------------------------------------------------------------------
# Small helpers
# ----------------------------------------------------------------------------------------------

def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def load_json(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def tag_match(tags, spec):
    """spec: {"key": "value" | ["v1","v2"] | "*"}; all keys must match."""
    for k, v in spec.items():
        if k not in tags:
            return False
        if v == "*":
            continue
        if isinstance(v, list):
            if tags[k] not in v:
                return False
        elif tags[k] != v:
            return False
    return True


def thing_matches(tags, osm):
    if not any(tag_match(tags, m) for m in osm.get("match", [])):
        return False
    for k, bad in osm.get("exclude", {}).items():
        if tags.get(k) in bad:
            return False
    return True


def dens_bin(km_per_km2, edges):
    b = 1
    for i, e in enumerate(edges):
        if km_per_km2 >= e:
            b = i + 1
    return "dens:%d" % b


class LocalProj:
    """Equirectangular projection about the county centre; error < 0.5 % over 100 km."""

    def __init__(self, lat0, lon0):
        self.lat0, self.lon0 = lat0, lon0
        self.kx = EARTH_R * math.cos(math.radians(lat0)) * math.pi / 180.0
        self.ky = EARTH_R * math.pi / 180.0

    def xy(self, lat, lon):
        return (np.asarray(lon) - self.lon0) * self.kx, (np.asarray(lat) - self.lat0) * self.ky

    def latlon(self, x, y):
        return np.asarray(y) / self.ky + self.lat0, np.asarray(x) / self.kx + self.lon0


class SegIndex:
    """Uniform grid hash of line segments for nearest and range queries."""

    def __init__(self, x0, y0, x1, y1, cell=50.0):
        self.x0, self.y0, self.x1, self.y1 = x0, y0, x1, y1
        self.cell = cell
        buckets = defaultdict(list)
        cx0 = np.floor(np.minimum(x0, x1) / cell).astype(np.int64)
        cx1 = np.floor(np.maximum(x0, x1) / cell).astype(np.int64)
        cy0 = np.floor(np.minimum(y0, y1) / cell).astype(np.int64)
        cy1 = np.floor(np.maximum(y0, y1) / cell).astype(np.int64)
        for i in range(len(x0)):
            for gx in range(cx0[i], cx1[i] + 1):
                for gy in range(cy0[i], cy1[i] + 1):
                    buckets[(gx, gy)].append(i)
        self.b = {k: np.asarray(v, dtype=np.int64) for k, v in buckets.items()}

    def candidates(self, px, py, r):
        c = self.cell
        gx0, gx1 = int(math.floor((px - r) / c)), int(math.floor((px + r) / c))
        gy0, gy1 = int(math.floor((py - r) / c)), int(math.floor((py + r) / c))
        parts = [self.b[(gx, gy)] for gx in range(gx0, gx1 + 1) for gy in range(gy0, gy1 + 1) if (gx, gy) in self.b]
        if not parts:
            return np.empty(0, dtype=np.int64)
        return np.unique(np.concatenate(parts))

    def nearest(self, px, py, r):
        """Returns (seg id, distance, t along segment 0..1, signed side +1 left / -1 right) or None."""
        ids = self.candidates(px, py, r)
        if len(ids) == 0:
            return None
        x0, y0, x1, y1 = self.x0[ids], self.y0[ids], self.x1[ids], self.y1[ids]
        dx, dy = x1 - x0, y1 - y0
        l2 = dx * dx + dy * dy
        t = np.clip(((px - x0) * dx + (py - y0) * dy) / np.where(l2 > 0, l2, 1.0), 0.0, 1.0)
        qx, qy = x0 + t * dx, y0 + t * dy
        d = np.hypot(px - qx, py - qy)
        j = int(np.argmin(d))
        if d[j] > r:
            return None
        cross = dx[j] * (py - y0[j]) - dy[j] * (px - x0[j])
        return int(ids[j]), float(d[j]), float(t[j]), (1 if cross >= 0 else -1)


def seg_intersect(ax0, ay0, ax1, ay1, bx0, by0, bx1, by1):
    """Vectorised over b. True where segment a crosses segment b."""
    def orient(px, py, qx, qy, rx, ry):
        return (qx - px) * (ry - py) - (qy - py) * (rx - px)
    o1 = orient(ax0, ay0, ax1, ay1, bx0, by0)
    o2 = orient(ax0, ay0, ax1, ay1, bx1, by1)
    o3 = orient(bx0, by0, bx1, by1, ax0, ay0)
    o4 = orient(bx0, by0, bx1, by1, ax1, ay1)
    return (o1 * o2 < 0) & (o3 * o4 < 0)


# ----------------------------------------------------------------------------------------------
# Download (Overpass, polite) and cache
# ----------------------------------------------------------------------------------------------

class Overpass:
    def __init__(self, url, pause, offline, max_retries=6):
        self.url, self.pause, self.offline, self.max_retries = url, pause, offline, max_retries
        self.last = 0.0
        self.session = None

    def post(self, query, timeout=900):
        if self.offline:
            raise RuntimeError("offline mode and the answer is not in the cache")
        import requests
        if self.session is None:
            self.session = requests.Session()
            self.session.headers["User-Agent"] = USER_AGENT
        delay = self.pause
        for attempt in range(self.max_retries):
            wait = self.pause - (time.time() - self.last)
            if wait > 0:
                time.sleep(wait)
            self.last = time.time()
            try:
                r = self.session.post(self.url, data={"data": query}, timeout=timeout)
            except Exception as e:  # network error: back off
                log("  network error:", e, "- wait", delay, "s")
                time.sleep(delay)
                delay = min(delay * 2, 600)
                continue
            if r.status_code == 200:
                return r.json()
            if r.status_code in (429, 504, 503):
                log("  server busy (HTTP %d) - wait %d s" % (r.status_code, delay))
                time.sleep(delay)
                delay = min(delay * 2, 600)
                continue
            raise RuntimeError("Overpass HTTP %d: %s" % (r.status_code, r.text[:300]))
        raise RuntimeError("Overpass: too many retries")


def cached(path, fn):
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    data = fn()
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f)
    os.replace(tmp, path)
    return data


def county_boundary(op, cache_dir, fips, name, state):
    q1 = ('[out:json][timeout:120];rel["boundary"="administrative"]["admin_level"="6"]["nist:fips_code"="%s"];'
          'out ids tags bb;' % fips)
    data = cached(os.path.join(cache_dir, "boundary.json"), lambda: op.post(q1, 180))
    rels = [e for e in data.get("elements", []) if e.get("type") == "relation"]
    if not rels:
        q2 = ('[out:json][timeout:120];area["ISO3166-2"="US-%s"]->.s;'
              'rel(area.s)["boundary"="administrative"]["admin_level"="6"]["name"~"^%s( County| Parish| Borough| City and Borough| Census Area)?$"];'
              'out ids tags bb;' % (state, name))
        data = cached(os.path.join(cache_dir, "boundary_byname.json"), lambda: op.post(q2, 180))
        rels = [e for e in data.get("elements", []) if e.get("type") == "relation"]
    if not rels:
        raise RuntimeError("no OSM boundary found for %s %s, %s" % (fips, name, state))
    r = rels[0]
    b = r["bounds"]
    return r["id"], (b["minlat"], b["minlon"], b["maxlat"], b["maxlon"])


def tile_query(area_id, s, w, n, e):
    bb = "%.6f,%.6f,%.6f,%.6f" % (s, w, n, e)
    hw = "|".join(sorted(set(CLASS_MAP)))
    return (
        "[out:json][timeout:600][maxsize:536870912];"
        "area(%d)->.a;(" % area_id +
        'way["highway"~"^(%s)$"](%s)(area.a);' % (hw, bb) +
        'node["emergency"="fire_hydrant"](%s)(area.a);' % bb +
        'node["power"="pole"](%s)(area.a);' % bb +
        'node["man_made"="utility_pole"](%s)(area.a);' % bb +
        'node["highway"~"^(street_lamp|stop|give_way|bus_stop)$"](%s)(area.a);' % bb +
        'node["amenity"~"^(bench|waste_basket|post_box)$"](%s)(area.a);' % bb +
        'way["barrier"~"^(fence|hedge|wall|guard_rail)$"](%s)(area.a);' % bb +
        'way["waterway"~"^(stream|river|ditch|drain|canal)$"](%s)(area.a);' % bb +
        ");out tags geom qt;"
    )


def download_county(op, cache_root, c, tile_deg):
    fips = c["fips"]
    cdir = os.path.join(cache_root, fips)
    os.makedirs(cdir, exist_ok=True)
    rel_id, (s, w, n, e) = county_boundary(op, cdir, fips, c["name"], c["state"])
    area_id = 3600000000 + rel_id
    ny = max(1, int(math.ceil((n - s) / tile_deg)))
    nx = max(1, int(math.ceil((e - w) / tile_deg)))
    elements = {}
    stamp = None
    for iy in range(ny):
        for ix in range(nx):
            ts, tw = s + iy * (n - s) / ny, w + ix * (e - w) / nx
            tn, te = s + (iy + 1) * (n - s) / ny, w + (ix + 1) * (e - w) / nx
            path = os.path.join(cdir, "tile_%02d_%02d.json" % (iy, ix))
            new = not os.path.exists(path)
            if new:
                log("  %s tile %d/%d" % (fips, iy * nx + ix + 1, nx * ny))
            data = cached(path, lambda: op.post(tile_query(area_id, ts, tw, tn, te)))
            stamp = data.get("osm3s", {}).get("timestamp_osm_base", stamp)
            for el in data.get("elements", []):
                elements[(el["type"], el["id"])] = el
    return list(elements.values()), stamp


# ----------------------------------------------------------------------------------------------
# Build one county
# ----------------------------------------------------------------------------------------------

class County:
    def __init__(self, c, elements, things_cfg, ctx_cfg, fence_step):
        self.c = c
        self.div = "div:%d" % c["div"]
        nodes = [e for e in elements if e["type"] == "node" and "lat" in e]
        ways = [e for e in elements if e["type"] == "way" and e.get("geometry")]
        lat = [g["lat"] for wy in ways for g in wy["geometry"] if g] + [n["lat"] for n in nodes]
        lon = [g["lon"] for wy in ways for g in wy["geometry"] if g] + [n["lon"] for n in nodes]
        if not lat:
            raise RuntimeError("no data for county %s" % c["fips"])
        self.proj = LocalProj(0.5 * (min(lat) + max(lat)), 0.5 * (min(lon) + max(lon)))
        self.edges = ctx_cfg["dens_edges"]
        self._roads(ways)
        self._density()
        self._things(nodes, things_cfg)
        self.fence_step = fence_step
        self.barrier_ways = [wy for wy in ways if "barrier" in wy.get("tags", {})]
        self.water_ways = [wy for wy in ways if "waterway" in wy.get("tags", {})]
        self.nodes = nodes

    # roads -------------------------------------------------------------------------------------
    def _roads(self, ways):
        sx0, sy0, sx1, sy1, sway, ss0 = [], [], [], [], [], []
        self.way_class, self.way_len, self.way_tags, self.way_nodes = [], [], [], []
        self.drive_xy = []
        for wy in ways:
            t = wy.get("tags", {})
            hw = t.get("highway")
            if hw not in CLASS_MAP or t.get("area") == "yes":
                continue
            cls = CLASS_MAP[hw]
            if hw == "service":
                sv = t.get("service")
                if sv in SKIP_SERVICE:
                    continue
                if sv == "driveway":
                    cls = "driveway"
            g = [p for p in wy["geometry"] if p]
            if len(g) < 2:
                continue
            x, y = self.proj.xy([p["lat"] for p in g], [p["lon"] for p in g])
            seg = np.hypot(np.diff(x), np.diff(y))
            wi = len(self.way_class)
            self.way_class.append(cls)
            self.way_len.append(float(seg.sum()))
            self.way_tags.append(t)
            self.way_nodes.append(wy.get("nodes", []))
            if cls == "driveway":
                self.drive_xy.append((x[0], y[0], x[-1], y[-1]))
            s0 = np.concatenate([[0.0], np.cumsum(seg)[:-1]])
            sx0.append(x[:-1]); sy0.append(y[:-1]); sx1.append(x[1:]); sy1.append(y[1:])
            sway.append(np.full(len(seg), wi)); ss0.append(s0)
        cat = lambda a, dt=float: np.concatenate(a).astype(dt) if a else np.empty(0, dt)
        self.sx0, self.sy0, self.sx1, self.sy1 = cat(sx0), cat(sy0), cat(sx1), cat(sy1)
        self.sway, self.ss0 = cat(sway, np.int64), cat(ss0)
        self.slen = np.hypot(self.sx1 - self.sx0, self.sy1 - self.sy0)
        self.sclass = np.array([self.way_class[i] for i in self.sway]) if len(self.sway) else np.empty(0, "<U12")
        self.road_mask = self.sclass != "driveway"
        idx = np.where(self.road_mask)[0]
        self.road_ids = idx
        self.index = SegIndex(self.sx0[idx], self.sy0[idx], self.sx1[idx], self.sy1[idx], cell=50.0)

    def _density(self):
        mx = 0.5 * (self.sx0 + self.sx1)
        my = 0.5 * (self.sy0 + self.sy1)
        self.scell_x = np.floor(mx / CELL_M).astype(np.int64)
        self.scell_y = np.floor(my / CELL_M).astype(np.int64)
        dens = defaultdict(float)
        for i in np.where(self.road_mask & (self.sclass != "service"))[0]:
            dens[(self.scell_x[i], self.scell_y[i])] += self.slen[i] / 1000.0
        self.cell_dens = dens  # km of road per km2 (cell is 1 km2)
        self.sdens = np.array([dens_bin(dens.get((self.scell_x[i], self.scell_y[i]), 0.0), self.edges)
                               for i in range(len(self.slen))]) if len(self.slen) else np.empty(0, "<U8")

    # things ------------------------------------------------------------------------------------
    def _things(self, nodes, things_cfg):
        self.items = defaultdict(list)  # thing id -> list of (seg global id, dist, t, side, x, y)
        self.item_pts = defaultdict(list)  # thing id -> all (x, y) incl. off-road
        self.underground_hydrants = 0
        for nd in nodes:
            t = nd.get("tags", {})
            if t.get("emergency") == "fire_hydrant" and t.get("fire_hydrant:type") == "underground":
                self.underground_hydrants += 1
            for th in things_cfg:
                osm = th.get("osm")
                if not osm or osm.get("geom") != "node" or not th["learn"].startswith("osm"):
                    continue
                if not thing_matches(t, osm):
                    continue
                x, y = self.proj.xy(nd["lat"], nd["lon"])
                x, y = float(x), float(y)
                self.item_pts[th["id"]].append((x, y))
                hit = self.index.nearest(x, y, th.get("max_offset_m", 20))
                if hit:
                    j, d, tt, side = hit
                    self.items[th["id"]].append((int(self.road_ids[j]), d, tt, side, x, y))

    # completeness ------------------------------------------------------------------------------
    def completeness(self, th):
        exp = set(th.get("completeness", {}).get("expected_in", []))
        if not exp:
            return float("nan")
        pts = self.item_pts.get(th["id"], [])
        present = set((int(math.floor(x / CELL_M)), int(math.floor(y / CELL_M))) for x, y in pts)
        tot = got = 0.0
        for i in np.where(self.road_mask)[0]:
            if self.sdens[i] not in exp:
                continue
            cx, cy = self.scell_x[i], self.scell_y[i]
            tot += self.slen[i]
            if any((cx + dx, cy + dy) in present for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
                got += self.slen[i]
        return got / tot if tot > 0 else float("nan")

    # rates -------------------------------------------------------------------------------------
    def lengths_by_context(self):
        L = defaultdict(float)
        for i in np.where(self.road_mask)[0]:
            L[(self.sclass[i], self.sdens[i], self.div)] += self.slen[i] / 1000.0
        return L

    def counts_by_context(self, thing_id):
        C = defaultdict(int)
        for (sid, d, t, side, x, y) in self.items.get(thing_id, []):
            C[(self.sclass[sid], self.sdens[sid], self.div)] += 1
        return C

    # spacing and lumpiness ---------------------------------------------------------------------
    def spacing(self, thing_id):
        per_way = defaultdict(list)
        for (sid, d, t, side, x, y) in self.items.get(thing_id, []):
            w = int(self.sway[sid])
            per_way[w].append(self.ss0[sid] + t * self.slen[sid])
        # Gaps and lumpiness only on ways that carry a line of items (>= 5): a road either has a pole line or
        # not, and that yes/no is reported apart as the 'line share' (length share of long ways with items).
        gaps = defaultdict(list)
        fano_counts = defaultdict(list)
        line = defaultdict(lambda: [0.0, 0.0])  # key -> [length with a line, length of long ways]
        for w, L in enumerate(self.way_len):
            if L < MIN_WAY_FOR_GAPS_M or self.way_class[w] == "driveway":
                continue
            key = (self.way_class[w], self.div)
            pos = np.sort(np.asarray(per_way.get(w, [])))
            line[key][1] += L
            if len(pos) < 5:
                continue
            line[key][0] += L
            gaps[key].extend(np.diff(pos).tolist())
            lo, hi = pos[0], pos[-1]
            nwin = int((hi - lo) // WIN_M)
            if nwin >= 2:
                h, _ = np.histogram(pos, bins=nwin, range=(lo, lo + nwin * WIN_M))
                fano_counts[key].extend(h.tolist())
        return gaps, fano_counts, line

    def offsets(self, thing_id):
        out = defaultdict(list)
        for (sid, d, t, side, x, y) in self.items.get(thing_id, []):
            w = int(self.sway[sid])
            tags = self.way_tags[w]
            width = road_width(tags, self.sclass[sid])
            out[(self.sclass[sid], self.sdens[sid])].append((d, d - 0.5 * width, side))
        return out

    # fences ------------------------------------------------------------------------------------
    def fence_share(self):
        fx0, fy0, fx1, fy1, ftype = [], [], [], [], []
        for wy in self.barrier_ways:
            t = wy["tags"]
            b = t["barrier"]
            typ = "guard_rail" if b == "guard_rail" else ("hedge" if b == "hedge" else
                  ("wall" if b == "wall" else "fence:" + t.get("fence_type", "unknown")))
            g = [p for p in wy["geometry"] if p]
            if len(g) < 2:
                continue
            x, y = self.proj.xy([p["lat"] for p in g], [p["lon"] for p in g])
            fx0.append(x[:-1]); fy0.append(y[:-1]); fx1.append(x[1:]); fy1.append(y[1:])
            ftype += [typ] * (len(x) - 1)
        res_len = defaultdict(float)          # (class, dens, div, side) -> sampled km
        res_hit = defaultdict(lambda: defaultdict(float))  # same key -> type -> km
        if not fx0:
            for i in np.where(self.road_mask)[0]:
                for side in ("side:left", "side:right"):
                    res_len[(self.sclass[i], self.sdens[i], self.div, side)] += self.slen[i] / 1000.0
            return res_len, res_hit
        fx0, fy0, fx1, fy1 = map(np.concatenate, (fx0, fy0, fx1, fy1))
        fi = SegIndex(fx0, fy0, fx1, fy1, cell=50.0)
        step = self.fence_step
        for i in np.where(self.road_mask)[0]:
            L = self.slen[i]
            if L <= 0:
                continue
            ux, uy = (self.sx1[i] - self.sx0[i]) / L, (self.sy1[i] - self.sy0[i]) / L
            n = max(1, int(L // step))
            w_km = (L / n) / 1000.0
            for k in range(n):
                s = (k + 0.5) * L / n
                px, py = self.sx0[i] + ux * s, self.sy0[i] + uy * s
                ids = fi.candidates(px, py, 40.0)
                for side_sign, side in ((1, "side:left"), (-1, "side:right")):
                    key = (self.sclass[i], self.sdens[i], self.div, side)
                    res_len[key] += w_km
                    if len(ids) == 0:
                        continue
                    best = None
                    for j in ids:
                        ax, ay, bx, by = fx0[j], fy0[j], fx1[j], fy1[j]
                        dx, dy = bx - ax, by - ay
                        l2 = dx * dx + dy * dy
                        if l2 <= 0:
                            continue
                        # direction within 30 degrees of the road
                        if abs(dx * ux + dy * uy) / math.sqrt(l2) < 0.866:
                            continue
                        tt = min(1.0, max(0.0, ((px - ax) * dx + (py - ay) * dy) / l2))
                        qx, qy = ax + tt * dx, ay + tt * dy
                        d = math.hypot(qx - px, qy - py)
                        if d < 3.0 or d > 40.0:
                            continue
                        cross = ux * (qy - py) - uy * (qx - px)
                        if (cross >= 0) != (side_sign > 0):
                            continue
                        if best is None or d < best[0]:
                            best = (d, ftype[j])
                    if best:
                        res_hit[key][best[1]] += w_km
        return res_len, res_hit

    # culverts ----------------------------------------------------------------------------------
    def culvert_share(self):
        out = defaultdict(lambda: defaultdict(int))
        for wy in self.water_ways:
            t = wy["tags"]
            g = [p for p in wy["geometry"] if p]
            if len(g) < 2:
                continue
            x, y = self.proj.xy([p["lat"] for p in g], [p["lon"] for p in g])
            for k in range(len(x) - 1):
                ids = self.index.candidates(0.5 * (x[k] + x[k + 1]), 0.5 * (y[k] + y[k + 1]),
                                            0.5 * math.hypot(x[k + 1] - x[k], y[k + 1] - y[k]) + 60.0)
                if len(ids) == 0:
                    continue
                hit = seg_intersect(x[k], y[k], x[k + 1], y[k + 1],
                                    self.index.x0[ids], self.index.y0[ids], self.index.x1[ids], self.index.y1[ids])
                for j in ids[hit]:
                    gid = int(self.road_ids[j])
                    w = int(self.sway[gid])
                    rt = self.way_tags[w]
                    if t.get("tunnel") == "culvert" or t.get("culvert") == "yes":
                        kind = "culvert"
                    elif rt.get("bridge") not in (None, "no"):
                        kind = "bridge"
                    elif rt.get("ford") == "yes":
                        kind = "ford"
                    else:
                        kind = "untagged"
                    out[(self.sclass[gid], t.get("waterway"), self.div)][kind] += 1
        return out

    # stop signs --------------------------------------------------------------------------------
    def stop_share(self):
        node_ways = defaultdict(set)
        for w, nl in enumerate(self.way_nodes):
            if self.way_class[w] == "driveway":
                continue
            for nid in nl:
                node_ways[nid].add(w)
        junctions = {nid for nid, ws in node_ways.items() if len(ws) >= 2}
        stops = {}
        for nd in self.nodes:
            t = nd.get("tags", {})
            if t.get("highway") in ("stop", "give_way"):
                stops[nd["id"]] = "stop_all" if t.get("stop") == "all" else t.get("highway")
        approaches = defaultdict(int)
        signed = defaultdict(lambda: defaultdict(int))
        node_pos = {}
        for w, nl in enumerate(self.way_nodes):
            for k, nid in enumerate(nl):
                node_pos.setdefault((w, nid), k)
        for nid in junctions:
            ws = node_ways[nid]
            for w in ws:
                cls = self.way_class[w]
                other = max((self.way_class[o] for o in ws if o != w), key=lambda c: CLASS_RANK[c])
                nl = self.way_nodes[w]
                k = node_pos[(w, nid)]
                for direction in (-1, 1):
                    if not (0 <= k + direction < len(nl)):
                        continue
                    key = (cls, other, self.div)
                    approaches[key] += 1
                    # look up to 4 vertices back along this approach for a stop or give-way node
                    kind = stops.get(nid) if stops.get(nid) == "stop_all" else None
                    for m in range(1, 5):
                        kk = k + direction * m
                        if not (0 <= kk < len(nl)):
                            break
                        if nl[kk] in junctions:
                            break
                        if nl[kk] in stops:
                            kind = stops[nl[kk]]
                            break
                    if kind is None and nid in stops and stops[nid] != "stop_all":
                        kind = stops[nid] + "_at_node"
                    if kind:
                        signed[key][kind] += 1
        return approaches, signed

    # co-occurrence -----------------------------------------------------------------------------
    def cooccur(self):
        res = {}
        lamps = self.item_pts.get("obj:street_light", [])
        poles = self.item_pts.get("obj:utility_pole", [])
        if lamps and poles:
            P = np.asarray(poles)
            grid = defaultdict(list)
            for k, (x, y) in enumerate(P):
                grid[(int(x // 10), int(y // 10))].append(k)
            on = 0
            for x, y in lamps:
                gx, gy = int(x // 10), int(y // 10)
                near = [k for dx in (-1, 0, 1) for dy in (-1, 0, 1) for k in grid.get((gx + dx, gy + dy), [])]
                if near and np.min(np.hypot(P[near, 0] - x, P[near, 1] - y)) <= 2.0:
                    on += 1
            res["street_light_on_pole"] = (on, len(lamps))
        stops_bus = [(float(a), float(b), nd.get("tags", {}).get("bench"))
                     for nd in self.nodes if nd.get("tags", {}).get("highway") == "bus_stop"
                     for a, b in [self.proj.xy(nd["lat"], nd["lon"])]]
        benches = self.item_pts.get("obj:bench", [])
        if stops_bus:
            B = np.asarray(benches) if benches else np.empty((0, 2))
            n_b = 0
            for x, y, tagb in stops_bus:
                if tagb == "yes" or (len(B) and np.min(np.hypot(B[:, 0] - x, B[:, 1] - y)) <= 15.0):
                    n_b += 1
            res["bench_at_bus_stop"] = (n_b, len(stops_bus))
        if self.drive_xy:
            res["driveways_mapped"] = (len(self.drive_xy), int(sum(self.slen[self.road_mask]) / 100.0))
        return res


def road_width(tags, cls):
    w = tags.get("width")
    try:
        if w:
            return float(w.split()[0])
    except ValueError:
        pass
    try:
        lanes = int(tags.get("lanes", "0").split(";")[0])
        if lanes > 0:
            return 3.4 * lanes
    except ValueError:
        pass
    return CLASS_WIDTH_M.get(cls, 6.0)


# ----------------------------------------------------------------------------------------------
# Pool, shrink, write
# ----------------------------------------------------------------------------------------------

def shrink_rates(counts, lengths, k_km):
    """counts, lengths keyed (road, dens, div). Returns rows with raw and shrunk rate per km.
    Parent chain: (road,dens,div) -> (road,dens) -> (road) -> (all)."""
    def agg(level):
        C, L = defaultdict(float), defaultdict(float)
        for key, l in lengths.items():
            pk = key[:level]
            L[pk] += l
            C[pk] += counts.get(key, 0)
        return C, L
    levels = [agg(n) for n in range(0, 4)]
    lam = {}
    C0, L0 = levels[0]
    lam[()] = C0[()] / L0[()] if L0[()] > 0 else 0.0
    for n in range(1, 4):
        C, L = levels[n]
        for pk in L:
            parent = lam[pk[:-1]]
            lam[pk] = (C[pk] + k_km * parent) / (L[pk] + k_km)
    rows = []
    C3, L3 = levels[3]
    for key in sorted(L3):
        rows.append({"road": key[0], "dens": key[1], "div": key[2], "n": int(C3[key]),
                     "length_km": round(L3[key], 3),
                     "raw_per_100m": round(C3[key] / L3[key] / 10.0, 5) if L3[key] > 0 else "",
                     "shrunk_per_100m": round(lam[key] / 10.0, 5),
                     "parent_per_100m": round(lam[key[:2]] / 10.0, 5)})
    return rows, lam


def quantiles(a, qs):
    a = np.asarray(a, dtype=float)
    if len(a) == 0:
        return [""] * len(qs)
    return [round(float(np.quantile(a, q)), 2) for q in qs]


def write_csv(path, rows, fields):
    with open(path, "w", newline="", encoding="utf-8") as f:
        wtr = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        wtr.writeheader()
        for r in rows:
            wtr.writerow(r)


def learn(counties, things_cfg, ctx_cfg, get_elements, out_dir, fence_step, max_train):
    os.makedirs(out_dir, exist_ok=True)
    learned_things = [t for t in things_cfg if t["learn"].startswith("osm") and t.get("osm", {}).get("geom") == "node"]
    pool_counts = {t["id"]: defaultdict(int) for t in learned_things}
    pool_lengths = {t["id"]: defaultdict(float) for t in learned_things}
    pool_gaps = {t["id"]: defaultdict(list) for t in learned_things}
    pool_fano = {t["id"]: defaultdict(list) for t in learned_things}
    pool_off = {t["id"]: defaultdict(list) for t in learned_things}
    pool_line = {t["id"]: defaultdict(lambda: [0.0, 0.0]) for t in learned_things}
    held = {}
    fence_len, fence_hit = defaultdict(float), defaultdict(lambda: defaultdict(float))
    culv = defaultdict(lambda: defaultdict(int))
    appr, signed = defaultdict(int), defaultdict(lambda: defaultdict(int))
    co_rows, comp_rows = [], []
    n_train = 0
    stamps = {}
    fence_cfg = next((t for t in things_cfg if t["id"] == "obj:fence"), None)
    for c in counties:
        if c["role"] == "train" and max_train and n_train >= max_train:
            continue
        log("county %s %s, %s (%s)" % (c["fips"], c["name"], c["state"], c["role"]))
        try:
            elements, stamp = get_elements(c)
        except Exception as e:
            log("  skipped:", e)
            comp_rows.append({"fips": c["fips"], "name": c["name"], "role": c["role"], "thing": "*", "score": "", "used": "no", "note": str(e)[:200]})
            continue
        stamps[c["fips"]] = stamp
        cty = County(c, elements, things_cfg, ctx_cfg, fence_step)
        lengths = cty.lengths_by_context()
        used_any = False
        for th in learned_things:
            score = cty.completeness(th)
            gate = th.get("completeness", {}).get("min_score", 0.8)
            ok = (not math.isnan(score)) and score >= gate
            comp_rows.append({"fips": c["fips"], "name": c["name"], "role": c["role"], "thing": th["id"],
                              "score": "" if math.isnan(score) else round(score, 3),
                              "used": "yes" if (ok and c["role"] == "train") else ("test" if c["role"] == "heldout" else "no"),
                              "n_items": len(cty.items.get(th["id"], [])), "note": ""})
            if c["role"] == "heldout":
                held.setdefault(c["fips"], {})[th["id"]] = (cty.counts_by_context(th["id"]), lengths, score)
                continue
            if not ok:
                continue
            used_any = True
            for k, v in cty.counts_by_context(th["id"]).items():
                pool_counts[th["id"]][k] += v
            for k, v in lengths.items():
                pool_lengths[th["id"]][k] += v
            gaps, fano, line = cty.spacing(th["id"])
            for k, v in line.items():
                pool_line[th["id"]][k][0] += v[0]
                pool_line[th["id"]][k][1] += v[1]
            for k, v in gaps.items():
                pool_gaps[th["id"]][k].extend(v)
            for k, v in fano.items():
                pool_fano[th["id"]][k].extend(v)
            for k, v in cty.offsets(th["id"]).items():
                pool_off[th["id"]][k].extend(v)
        if c["role"] == "train":
            n_train += 1 if used_any else 0
            # fences: gate on mapped fence presence in rural cells
            if fence_cfg:
                pseudo = {"id": "obj:fence", "completeness": fence_cfg.get("completeness", {})}
                cty.item_pts["obj:fence"] = [(0.5 * (x0 + x1), 0.5 * (y0 + y1)) for wy in cty.barrier_ways
                                             for g in [[p for p in wy["geometry"] if p]] if len(g) >= 2
                                             for x0, y0 in [cty.proj.xy(g[0]["lat"], g[0]["lon"])]
                                             for x1, y1 in [cty.proj.xy(g[-1]["lat"], g[-1]["lon"])]]
                fs = cty.completeness(pseudo)
                fok = (not math.isnan(fs)) and fs >= pseudo["completeness"].get("min_score", 0.5)
                comp_rows.append({"fips": c["fips"], "name": c["name"], "role": c["role"], "thing": "obj:fence",
                                  "score": "" if math.isnan(fs) else round(fs, 3), "used": "yes" if fok else "no",
                                  "n_items": len(cty.barrier_ways), "note": ""})
                if fok:
                    fl, fh = cty.fence_share()
                    for k, v in fl.items():
                        fence_len[k] += v
                    for k, d in fh.items():
                        for typ, v in d.items():
                            fence_hit[k][typ] += v
            for k, d in cty.culvert_share().items():
                for kind, v in d.items():
                    culv[k][kind] += v
            a, sg = cty.stop_share()
            for k, v in a.items():
                appr[k] += v
            for k, d in sg.items():
                for kind, v in d.items():
                    signed[k][kind] += v
            for name, (num, den) in cty.cooccur().items():
                co_rows.append({"fips": c["fips"], "what": name, "num": num, "den": den,
                                "share": round(num / den, 4) if den else ""})
            if cty.underground_hydrants:
                co_rows.append({"fips": c["fips"], "what": "underground_hydrants_skipped", "num": cty.underground_hydrants, "den": "", "share": ""})

    # ---- write priors
    prior_rows, spacing_rows, off_rows, t1_rows = [], [], [], []
    lam_by_thing = {}
    for th in learned_things:
        tid = th["id"]
        if not pool_lengths[tid]:
            log("no training county passed the gate for", tid)
            continue
        rows, lam = shrink_rates(pool_counts[tid], pool_lengths[tid], th.get("shrink_k_km", 5))
        lam_by_thing[tid] = lam
        for r in rows:
            r["thing"] = tid
        prior_rows += rows
        for key in sorted(set(pool_gaps[tid]) | set(pool_fano[tid]) | set(pool_line[tid])):
            g = pool_gaps[tid].get(key, [])
            f = pool_fano[tid].get(key, [])
            mean_g = float(np.mean(g)) if g else 0.0
            q = quantiles(g, [0.0, 0.05, 0.25, 0.5, 0.75])
            # robust CV from the quartiles, so a few stray items (a pole given to a side road) do not inflate it
            cv = float((q[4] - q[2]) / (1.349 * q[3])) if g and q[3] else ""
            fano = float(np.var(f) / np.mean(f)) if f and np.mean(f) > 0 else ""
            ln = pool_line[tid].get(key, [0.0, 0.0])
            spacing_rows.append({"thing": tid, "road": key[0], "div": key[1], "n_gaps": len(g),
                                 "min_m": q[0], "p05_m": q[1], "p25_m": q[2], "p50_m": q[3], "p75_m": q[4],
                                 "cv": round(cv, 3) if cv != "" else "",
                                 "gamma_shape": round(1.0 / (cv * cv), 3) if cv not in ("", 0.0) else "",
                                 "fano_200m": round(fano, 3) if fano != "" else "", "n_windows": len(f),
                                 "line_share": round(ln[0] / ln[1], 4) if ln[1] > 0 else ""})
        for key, v in sorted(pool_off[tid].items()):
            arr = np.asarray(v)
            q_c = quantiles(arr[:, 0], [0.1, 0.5, 0.9])
            q_e = quantiles(arr[:, 1], [0.1, 0.5, 0.9])
            off_rows.append({"thing": tid, "road": key[0], "dens": key[1], "n": len(arr),
                             "centre_p10_m": q_c[0], "centre_p50_m": q_c[1], "centre_p90_m": q_c[2],
                             "edge_p10_m": q_e[0], "edge_p50_m": q_e[1], "edge_p90_m": q_e[2],
                             "left_share": round(float(np.mean(arr[:, 2] > 0)), 3)})
        # T1: held-out counties
        for fips, per in held.items():
            if tid not in per:
                continue
            counts, lengths, score = per[tid]
            pred = meas = 0.0
            dev = 0.0
            for key, l in lengths.items():
                lam_k = lam.get(key, lam.get(key[:2], lam.get(key[:1], lam[()])))
                mu = lam_k * l
                y = counts.get(key, 0)
                pred += mu
                meas += y
                if mu > 0:
                    dev += 2 * ((y * math.log(y / mu) if y > 0 else 0.0) - (y - mu))
            t1_rows.append({"thing": tid, "fips": fips, "completeness": "" if math.isnan(score) else round(score, 3),
                            "measured": int(meas), "predicted": round(pred, 1),
                            "ratio_pred_over_measured": round(pred / meas, 3) if meas > 0 else "",
                            "poisson_deviance_per_km": round(dev / max(1e-9, sum(lengths.values())), 4),
                            "note": "measured is only trustworthy when completeness >= the gate"})
    write_csv(os.path.join(out_dir, "roadside_priors.csv"), prior_rows,
              ["thing", "road", "dens", "div", "n", "length_km", "raw_per_100m", "shrunk_per_100m", "parent_per_100m"])
    write_csv(os.path.join(out_dir, "roadside_spacing.csv"), spacing_rows,
              ["thing", "road", "div", "n_gaps", "min_m", "p05_m", "p25_m", "p50_m", "p75_m", "cv", "gamma_shape", "fano_200m", "n_windows", "line_share"])
    write_csv(os.path.join(out_dir, "roadside_offsets.csv"), off_rows,
              ["thing", "road", "dens", "n", "centre_p10_m", "centre_p50_m", "centre_p90_m", "edge_p10_m", "edge_p50_m", "edge_p90_m", "left_share"])
    frows = []
    for key in sorted(fence_len):
        tot = fence_len[key]
        hits = fence_hit.get(key, {})
        row = {"road": key[0], "dens": key[1], "div": key[2], "side": key[3], "sampled_km": round(tot, 3),
               "any_share": round(sum(hits.values()) / tot, 4) if tot > 0 else ""}
        row["types"] = ";".join("%s=%.4f" % (k, v / tot) for k, v in sorted(hits.items(), key=lambda kv: -kv[1])) if tot > 0 else ""
        frows.append(row)
    write_csv(os.path.join(out_dir, "fence_share.csv"), frows, ["road", "dens", "div", "side", "sampled_km", "any_share", "types"])
    crows = []
    for key in sorted(culv):
        d = culv[key]
        n = sum(d.values())
        crows.append({"road": key[0], "waterway": key[1], "div": key[2], "n": n,
                      **{k + "_share": round(d.get(k, 0) / n, 4) for k in ("culvert", "bridge", "ford", "untagged")}})
    write_csv(os.path.join(out_dir, "culvert_share.csv"), crows,
              ["road", "waterway", "div", "n", "culvert_share", "bridge_share", "ford_share", "untagged_share"])
    srows = []
    for key in sorted(appr):
        n = appr[key]
        d = signed.get(key, {})
        srows.append({"approach": key[0], "crossing_max": key[1], "div": key[2], "approaches": n,
                      "stop_share": round((d.get("stop", 0) + d.get("stop_at_node", 0)) / n, 4),
                      "all_way_share": round(d.get("stop_all", 0) / n, 4),
                      "give_way_share": round((d.get("give_way", 0) + d.get("give_way_at_node", 0)) / n, 4)})
    write_csv(os.path.join(out_dir, "stop_share.csv"), srows,
              ["approach", "crossing_max", "div", "approaches", "stop_share", "all_way_share", "give_way_share"])
    write_csv(os.path.join(out_dir, "cooccur.csv"), co_rows, ["fips", "what", "num", "den", "share"])
    write_csv(os.path.join(out_dir, "county_completeness.csv"), comp_rows, ["fips", "name", "role", "thing", "score", "used", "n_items", "note"])
    write_csv(os.path.join(out_dir, "t1_heldout.csv"), t1_rows,
              ["thing", "fips", "completeness", "measured", "predicted", "ratio_pred_over_measured", "poisson_deviance_per_km", "note"])
    with open(os.path.join(out_dir, "run_log.json"), "w", encoding="utf-8") as f:
        json.dump({"tool": "learn_roadside.py", "graph_version": "world-graph-v0.1", "licence": "ODbL 1.0",
                   "credit": CREDIT, "osm_timestamps": stamps, "train_counties_used": n_train,
                   "time_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}, f, indent=1)
    with open(os.path.join(out_dir, "LICENCE.txt"), "w", encoding="utf-8") as f:
        f.write("These tables are a derivative database of OpenStreetMap data.\n"
                "%s. Licence: Open Database License 1.0, https://opendatacommons.org/licenses/odbl/1-0/\n" % CREDIT)
    return {"priors": prior_rows, "spacing": spacing_rows, "fence": frows, "culvert": crows, "stop": srows,
            "cooccur": co_rows, "completeness": comp_rows, "t1": t1_rows, "lam": lam_by_thing}


# ----------------------------------------------------------------------------------------------
# Self-test on a synthetic county (no network)
# ----------------------------------------------------------------------------------------------

def synthetic_elements(seed=7, hydrant_gap=120.0, pole_gap=45.0, drop_hydrants_east=False):
    """A 6 x 3 km county: west half a 100 m street grid (town), east half a 1 km rural grid.
    Town: hydrants every ~hydrant_gap m on alternate sides, lamps every 40 m, stop signs on N-S streets at every
    junction. Rural: poles every ~pole_gap m at 7 m offset on the north side of E-W roads, barbed wire fence along
    60 % of E-W road length at 15 m (south side), a stream crossing each N-S rural road through a culvert."""
    rng = np.random.default_rng(seed)
    proj = LocalProj(40.0, -96.0)
    els, nid, wid = [], [1], [1]

    def node(x, y, tags=None, keep=True):
        lat, lon = proj.latlon(x, y)
        e = {"type": "node", "id": nid[0], "lat": float(lat), "lon": float(lon)}
        if tags:
            e["tags"] = tags
        nid[0] += 1
        if keep and tags:
            els.append(e)
        return e

    def way(pts, tags, node_ids=None):
        geo = []
        for (x, y) in pts:
            lat, lon = proj.latlon(x, y)
            geo.append({"lat": float(lat), "lon": float(lon)})
        e = {"type": "way", "id": wid[0], "tags": tags, "geometry": geo, "nodes": node_ids or []}
        wid[0] += 1
        els.append(e)
        return e

    # town grid: x in [-3000, 0], y in [-1500, 1500], 100 m blocks
    xs = np.arange(-3000, 1, 100.0)
    ys = np.arange(-1500, 1501, 100.0)
    grid_id = {}
    for i, x in enumerate(xs):
        for j, y in enumerate(ys):
            grid_id[(i, j)] = 10_000_000 + i * 1000 + j
    stops = []
    for j, y in enumerate(ys):  # E-W residential streets, one way per block row
        pts = [(x, y) for x in xs]
        way(pts, {"highway": "residential"}, [grid_id[(i, j)] for i in range(len(xs))])
    for i, x in enumerate(xs):  # N-S streets: tertiary every 10th, else residential, split per block
        cls = "tertiary" if i % 10 == 0 else "residential"
        for j in range(len(ys) - 1):
            mid_id = 20_000_000 + i * 1000 + j
            pts = [(x, ys[j]), (x, ys[j] + 50.0), (x, ys[j + 1])]
            way(pts, {"highway": cls}, [grid_id[(i, j)], mid_id, grid_id[(i, j + 1)]])
    # stop nodes: on residential N-S streets, the vertex 50 m before the junction is the mid node; place a stop node
    # tag on it for the northbound approach to each junction (one in two approaches)
    for i, x in enumerate(xs):
        if i % 10 == 0:
            continue
        for j in range(len(ys) - 1):
            mid_id = 20_000_000 + i * 1000 + j
            lat, lon = proj.latlon(x, ys[j] + 50.0)
            els.append({"type": "node", "id": mid_id, "lat": float(lat), "lon": float(lon), "tags": {"highway": "stop"}})
            stops.append(mid_id)
    # hydrants along E-W town streets
    for j, y in enumerate(ys):
        s = -3000.0 + rng.uniform(0, hydrant_gap)
        k = 0
        while s < 0:
            if not (drop_hydrants_east and s > -2000):
                node(s, y + (5.0 if k % 2 == 0 else -5.0), {"emergency": "fire_hydrant", "fire_hydrant:type": "pillar"})
            s += hydrant_gap * rng.uniform(0.85, 1.15)
            k += 1
        for x in np.arange(-2980, 0, 40.0):
            node(x, y + 7.0, {"highway": "street_lamp"})
    # rural grid: x in [0, 3000], y in [-1500, 1500], 1 km
    rxs = np.arange(0, 3001, 1000.0)
    rys = np.arange(-1500, 1501, 1000.0)
    for y in rys:
        way([(x, y) for x in np.arange(0, 3001, 100.0)], {"highway": "unclassified"})
        s = rng.uniform(0, pole_gap)
        while s < 3000:
            node(s, y + 7.0, {"power": "pole"})
            s += pole_gap * rng.uniform(0.9, 1.1)
        way([(x, y - 15.0) for x in np.arange(0, 1800.1, 50.0)], {"barrier": "fence", "fence_type": "barbed_wire"})
    for x in rxs:
        way([(x, y) for y in np.arange(-1500, 1501, 100.0)], {"highway": "unclassified"})
        way([(x - 60.0, -1000.0), (x + 60.0, -950.0)], {"waterway": "stream", "tunnel": "culvert"})
    return els, {"town_km": None}


def selftest():
    things = load_json(os.path.join(HERE, "things.json"))
    ctx = {"dens_edges": next(f for f in things["contexts"]["fields"] if f["id"] == "dens")["bins_km_per_km2"]}
    els, _ = synthetic_elements()
    held_els, _ = synthetic_elements(seed=11)
    counties = [{"fips": "99001", "name": "Synthetic", "state": "NE", "div": 4, "role": "train"},
                {"fips": "99002", "name": "Synthetic held", "state": "NE", "div": 4, "role": "heldout"}]
    src = {"99001": els, "99002": held_els}
    out = os.path.join(HERE, "selftest_out")
    res = learn(counties, things["things"], ctx, lambda c: (src[c["fips"]], "synthetic"), out, 20.0, 0)
    ok = True

    def check(name, cond, detail):
        nonlocal ok
        print(("PASS " if cond else "FAIL ") + name + ": " + detail)
        ok = ok and cond

    pr = [r for r in res["priors"] if r["thing"] == "obj:fire_hydrant"]
    check("hydrant rows exist", len(pr) >= 1, "%d rows" % len(pr))
    n = sum(r["n"] for r in pr)
    expected = 31 * 3000 / 120.0  # 31 E-W streets x 3 km, one hydrant every ~120 m
    check("hydrant count", abs(n - expected) / expected < 0.05, "n=%d expected~%.0f" % (n, expected))
    sp = [r for r in res["spacing"] if r["thing"] == "obj:fire_hydrant" and r["road"] == "residential"]
    check("hydrant median gap ~120 m", bool(sp) and abs(sp[0]["p50_m"] - 120.0) < 15, str(sp[:1]))
    sp = [r for r in res["spacing"] if r["thing"] == "obj:utility_pole" and r["road"] == "unclassified"]
    check("pole median gap ~45 m", bool(sp) and abs(sp[0]["p50_m"] - 45.0) < 6, str(sp[:1]))
    check("pole gaps regular (fano < 0.5)", bool(sp) and sp[0]["fano_200m"] != "" and sp[0]["fano_200m"] < 0.5, str(sp[:1]))
    # 4 E-W rural roads carry poles, 4 N-S rural roads do not: line share about 0.5 of long-way length
    check("pole line share ~0.5", bool(sp) and sp[0]["line_share"] != "" and 0.35 < sp[0]["line_share"] < 0.65, str(sp[:1]))
    fr = [r for r in res["fence"] if r["road"] == "unclassified" and r["side"] == "side:right" and r["sampled_km"] > 1]
    # E-W rural roads run west to east, so the south side is the right side; fence covers 1.8 of 3 km on E-W roads
    if fr:
        best = max(fr, key=lambda r: r["any_share"] or 0)
        check("fence share on the right side", 0.15 < best["any_share"] < 0.6, str(best))
    else:
        check("fence rows exist", False, "none")
    cr = [r for r in res["culvert"] if r["road"] == "unclassified"]
    check("culverts found", bool(cr) and cr[0]["culvert_share"] > 0.99 and cr[0]["n"] >= 4, str(cr))
    st = [r for r in res["stop"] if r["approach"] == "residential" and r["crossing_max"] == "residential"]
    check("stop share on residential approaches", bool(st) and 0.2 < st[0]["stop_share"] < 0.6, str(st))
    cp = [r for r in res["completeness"] if r["thing"] == "obj:fire_hydrant" and r["fips"] == "99001"]
    check("hydrant completeness passes", bool(cp) and cp[0]["score"] != "" and cp[0]["score"] >= 0.8, str(cp))
    t1 = [r for r in res["t1"] if r["thing"] == "obj:fire_hydrant"]
    check("T1 prior predicts held-out county", bool(t1) and t1[0]["ratio_pred_over_measured"] != "" and abs(t1[0]["ratio_pred_over_measured"] - 1) < 0.1, str(t1))
    # completeness gate rejects a county where half the town has no mapped hydrants
    els2, _ = synthetic_elements(seed=3, drop_hydrants_east=True)
    c2 = County({"fips": "99003", "name": "Half mapped", "state": "NE", "div": 4, "role": "train"}, els2, things["things"], ctx, 20.0)
    th = next(t for t in things["things"] if t["id"] == "obj:fire_hydrant")
    sc = c2.completeness(th)
    check("half-mapped county fails the hydrant gate", sc < 0.8, "score=%.3f" % sc)
    print("SELFTEST", "OK" if ok else "FAILED")
    return 0 if ok else 1


# ----------------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--things", default=os.path.join(HERE, "things.json"))
    ap.add_argument("--counties", default=os.path.join(HERE, "counties.json"))
    ap.add_argument("--cache", default=os.path.join(HERE, "cache", "osm"))
    ap.add_argument("--out", default=os.path.join(HERE, "out"))
    ap.add_argument("--only", default="", help="comma list of county FIPS")
    ap.add_argument("--max-train", type=int, default=20, help="stop after this many training counties that passed a gate (0 = all)")
    ap.add_argument("--tile-deg", type=float, default=0.25)
    ap.add_argument("--pause", type=float, default=10.0, help="seconds between Overpass requests")
    ap.add_argument("--overpass", default=OVERPASS_URL)
    ap.add_argument("--offline", action="store_true")
    ap.add_argument("--fence-step", type=float, default=20.0)
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    things = load_json(a.things)
    ctx = {"dens_edges": next(f for f in things["contexts"]["fields"] if f["id"] == "dens")["bins_km_per_km2"]}
    cj = load_json(a.counties)
    keys = cj["fields"]
    counties = [dict(zip(keys, row)) for row in cj["counties"]]
    if a.only:
        want = set(a.only.split(","))
        counties = [c for c in counties if c["fips"] in want]
    op = Overpass(a.overpass, a.pause, a.offline)
    os.makedirs(a.cache, exist_ok=True)
    learn(counties, things["things"], ctx, lambda c: download_county(op, a.cache, c, a.tile_deg), a.out, a.fence_step, a.max_train)
    log("done; outputs in", a.out, "-", CREDIT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
