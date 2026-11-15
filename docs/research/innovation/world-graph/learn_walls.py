#!/usr/bin/env python3
"""learn_walls.py - wall material, storeys, foundation, lot and porch priors by Census division and era.

World knowledge graph V0 (OSDS / SimCruise innovation lane, 15 Nov 2026). Runs on the PC.
Needs Python 3.10+, numpy, pandas, openpyxl (xlsx), xlrd (old .xls inside the 1999-2019 zips), requests.

Sources (both public domain, US federal works):
  * U.S. Census Bureau, Survey of Construction (SOC) public-use microdata, 1999-2025:
    https://www.census.gov/construction/chars/microdata.html  (soc25.xlsx ... soc20.xlsx, soc19.zip ... soc99.zip)
    One row = one sampled new house, with WEIGHT = the number of houses it stands for.
  * Optional: U.S. Army Corps of Engineers National Structure Inventory (USACE NSI), public base layer, through the
    NSI API, per county. Only the public fields bldgtype, found_type, found_ht, num_story, med_yr_blt, occtype are
    read. Raw responses are NEVER written to disk; only aggregate counts per county x decade leave memory.
    (World lane terms check, 14 Nov 23:53Z.)

Outputs (out dir):
  walls_soc.csv          share of principal wall material (WAL1) by division x era x detached/attached, with
                         shrinkage to the national share of the same era
  walls_secondary.csv    P(secondary material WAL2 | principal WAL1, division, era)  -> "brick front, vinyl sides"
  storeys_base.csv       storeys (1, 2+, split) and foundation (basement, crawl, slab) shares by division x era
  lot_house.csv          lot area and floor area quantiles, garage size, porch, deck, patio, modular shares
  nsi_aggregate.csv      (with --nsi) masonry / wood / manufactured / steel shares and foundation shares by county
                         x decade of med_yr_blt, residential only
  walls_prior.csv        the table the scan engine reads: P(visible wall material | division, era): learned SOC
                         rows for 1999+, expert rows from materials.json for older eras (confidence low), with the
                         NSI masonry share applied where present
  run_log.json           versions, files, row counts, credits

Usage:
  python learn_walls.py --selftest                 # no network: synthetic SOC table, checks the maths
  python learn_walls.py --out out                  # download SOC (about 60 MB in total, once), learn
  python learn_walls.py --out out --nsi 55025,31109 # also aggregate NSI for these counties
  python learn_walls.py --offline --out out        # cache only
"""
import argparse
import io
import json
import math
import os
import sys
import time
import zipfile
from collections import defaultdict

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SOC_URL = "https://www.census.gov/construction/chars/xls/"
NSI_API = "https://nsi.sec.usace.army.mil/nsiapi/structures?fips=%s"
USER_AGENT = "OSDS-world-graph-learner/0.1 (open source driving simulator; one request at a time)"
SOC_YEARS = list(range(1999, 2026))

WAL_CODES = {1: "wood", 2: "brick", 3: "aluminum", 4: "stucco", 5: "vinyl", 6: "block", 7: "stone",
             8: "fiber_cement", 9: "other"}
STOR_CODES = {1: "1", 2: "2plus", 3: "split"}
BASE_CODES = {1: "basement", 2: "crawl", 3: "slab", 4: "other"}
GAR_CODES = {1: "garage_1", 2: "garage_2", 3: "garage_3plus", 4: "other_or_none"}
ERAS = [(1999, 2004), (2005, 2009), (2010, 2014), (2015, 2019), (2020, 2025)]
NSI_PUBLIC_FIELDS = {"bldgtype", "found_type", "found_ht", "num_story", "med_yr_blt", "occtype"}


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def era_of(year):
    for a, b in ERAS:
        if a <= year <= b:
            return "%d-%d" % (a, b)
    return None


# ----------------------------------------------------------------------------------------------
# SOC download and read
# ----------------------------------------------------------------------------------------------

def soc_filename(year):
    yy = "%02d" % (year % 100)
    return "soc%s.xlsx" % yy if year >= 2020 else "soc%s.zip" % yy


def fetch(url, path, offline, pause=3.0):
    if os.path.exists(path):
        return path
    if offline:
        raise RuntimeError("offline and not cached: " + path)
    import requests
    time.sleep(pause)
    log("  download", url)
    r = requests.get(url, headers={"User-Agent": USER_AGENT}, timeout=300)
    if r.status_code != 200:
        raise RuntimeError("HTTP %d for %s" % (r.status_code, url))
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(r.content)
    os.replace(tmp, path)
    return path


def read_soc_file(path):
    import pandas as pd
    if path.endswith(".zip"):
        with zipfile.ZipFile(path) as z:
            names = [n for n in z.namelist() if n.lower().endswith((".xls", ".xlsx", ".csv"))]
            if not names:
                raise RuntimeError("no table inside " + path)
            data = z.read(names[0])
            name = names[0].lower()
        if name.endswith(".csv"):
            df = pd.read_csv(io.BytesIO(data))
        else:
            df = pd.read_excel(io.BytesIO(data), engine="xlrd" if name.endswith(".xls") else "openpyxl")
    else:
        df = pd.read_excel(path, engine="openpyxl")
    df.columns = [str(c).strip().upper() for c in df.columns]
    return df


def load_soc(cache, offline):
    import pandas as pd
    os.makedirs(cache, exist_ok=True)
    frames, files = [], []
    for y in SOC_YEARS:
        fn = soc_filename(y)
        try:
            p = fetch(SOC_URL + fn, os.path.join(cache, fn), offline)
            df = read_soc_file(p)
        except Exception as e:
            log("  SOC %d skipped: %s" % (y, e))
            continue
        need = {"DIV", "STRT", "WAL1", "WEIGHT"}
        miss = need - set(df.columns)
        if miss:
            log("  SOC %d skipped: missing columns %s (columns: %s)" % (y, sorted(miss), list(df.columns)[:40]))
            continue
        df["FILE_YEAR"] = y
        frames.append(df)
        files.append(fn)
    if not frames:
        raise RuntimeError("no SOC files could be read")
    return pd.concat(frames, ignore_index=True, sort=False), files


def prepare_soc(df):
    """Keep houses started in the file's own year (each case once), decode codes, add era."""
    import pandas as pd
    df = df.copy()
    for c in ("DIV", "STRT", "WAL1", "WAL2", "STOR", "BASE", "DET", "GAR", "PRCH", "DECK", "PATI", "MFGS", "AREA", "SQFS", "WEIGHT"):
        if c in df.columns:
            df[c] = pd.to_numeric(df[c], errors="coerce").fillna(0)
    df = df[df["STRT"] > 0]
    df["START_YEAR"] = (df["STRT"] // 100).astype(int)
    df = df[df["START_YEAR"] == df["FILE_YEAR"]]
    df = df[(df["DIV"] >= 1) & (df["DIV"] <= 9) & (df["WEIGHT"] > 0)]
    df["ERA"] = df["START_YEAR"].map(era_of)
    df = df[df["ERA"].notna()]
    df["WALL"] = df["WAL1"].astype(int).map(WAL_CODES)
    if "WAL2" in df.columns:
        w2 = df["WAL2"].astype(int)
        # WAL2: 0 = none or not reported; before 2009, 10 = none
        df["WALL2"] = w2.map(WAL_CODES).where(~w2.isin([0, 10]), "none")
    df["DETACHED"] = np.where(df.get("DET", 1) == 2, "attached", "detached")
    return df


# ----------------------------------------------------------------------------------------------
# Weighted shares with shrinkage
# ----------------------------------------------------------------------------------------------

def shares(df, by, col, k=30.0, parent_by=None):
    """Weighted share of each value of col inside groups 'by'. Shrinks to the parent group (parent_by) with a
    pseudo-sample of k houses: p = (n * p_group + k * p_parent) / (n + k), n = unweighted cases."""
    d = df[df[col].notna()]
    out = []
    parent = {}
    if parent_by is not None:
        for pk, g in d.groupby(parent_by):
            pk = pk if isinstance(pk, tuple) else (pk,)
            w = g.groupby(col)["WEIGHT"].sum()
            parent[pk] = (w / w.sum()).to_dict()
    for key, g in d.groupby(by):
        key = key if isinstance(key, tuple) else (key,)
        w = g.groupby(col)["WEIGHT"].sum()
        tot = w.sum()
        n = len(g)
        pk = tuple(key[by.index(c)] for c in parent_by) if parent_by is not None else None
        pp = parent.get(pk, {}) if pk is not None else {}
        values = set(w.index) | set(pp)
        for v in sorted(values, key=str):
            p_raw = float(w.get(v, 0.0) / tot) if tot > 0 else 0.0
            p = (n * p_raw + k * pp.get(v, p_raw)) / (n + k) if pp else p_raw
            row = dict(zip(by, key))
            row.update({col.lower(): v, "share": round(p, 4), "raw_share": round(p_raw, 4), "n": n,
                        "weighted_houses": int(round(tot))})
            out.append(row)
    return out


def wquantile(x, w, q):
    o = np.argsort(x)
    x, w = np.asarray(x)[o], np.asarray(w)[o]
    c = np.cumsum(w)
    if c[-1] <= 0:
        return float("nan")
    return float(np.interp(q * c[-1], c, x))


def lot_house(df):
    rows = []
    for (div, era), g in df.groupby(["DIV", "ERA"]):
        r = {"DIV": int(div), "ERA": era, "n": len(g)}
        for col, name in (("AREA", "lot_sqft"), ("SQFS", "floor_sqft")):
            if col in g.columns:
                gg = g[g[col] > 0]
                for q in (0.1, 0.5, 0.9):
                    r["%s_p%02d" % (name, int(q * 100))] = round(wquantile(gg[col], gg["WEIGHT"], q)) if len(gg) else ""
        W = g["WEIGHT"].sum()
        for col, yes in (("PRCH", 1), ("DECK", 1), ("PATI", 1)):
            if col in g.columns:
                gg = g[g[col] > 0]
                r[col.lower() + "_share"] = round(float(gg.loc[gg[col] == yes, "WEIGHT"].sum() / gg["WEIGHT"].sum()), 4) if len(gg) else ""
        if "GAR" in g.columns:
            gg = g[g["GAR"] > 0]
            for code, name in GAR_CODES.items():
                r[name + "_share"] = round(float(gg.loc[gg["GAR"] == code, "WEIGHT"].sum() / gg["WEIGHT"].sum()), 4) if len(gg) else ""
        if "MFGS" in g.columns:
            gg = g[g["MFGS"] > 0]
            r["modular_share"] = round(float(gg.loc[gg["MFGS"] == 1, "WEIGHT"].sum() / gg["WEIGHT"].sum()), 4) if len(gg) else ""
        rows.append(r)
    return rows


# ----------------------------------------------------------------------------------------------
# NSI: aggregate in memory, never store raw records
# ----------------------------------------------------------------------------------------------

def nsi_aggregate_county(fips, offline, pause=5.0):
    if offline:
        raise RuntimeError("NSI is run-time only; it is never cached, so offline mode cannot read it")
    import requests
    time.sleep(pause)
    log("  NSI county", fips)
    r = requests.get(NSI_API % fips, headers={"User-Agent": USER_AGENT}, timeout=600)
    if r.status_code != 200:
        raise RuntimeError("NSI HTTP %d" % r.status_code)
    feats = r.json().get("features", [])
    agg = aggregate_nsi_records(fips, (f.get("properties", {}) for f in feats))
    del feats, r  # raw response leaves memory here; nothing was written
    return agg


def aggregate_nsi_records(fips, records):
    counts = defaultdict(lambda: defaultdict(int))
    for p in records:
        p = {k: v for k, v in p.items() if k in NSI_PUBLIC_FIELDS}  # public fields only
        occ = str(p.get("occtype", ""))
        if not occ.startswith("RES"):
            continue
        yb = p.get("med_yr_blt")
        try:
            dec = "%ds" % (int(yb) // 10 * 10) if yb and int(yb) > 1700 else "unknown"
        except (TypeError, ValueError):
            dec = "unknown"
        key = (fips, dec)
        counts[key]["n"] += 1
        counts[key]["bldg_" + str(p.get("bldgtype", "?"))] += 1
        counts[key]["found_" + str(p.get("found_type", "?"))] += 1
        try:
            st = float(p.get("num_story") or 0)
            counts[key]["stories_" + ("1" if st < 1.5 else ("2" if st < 2.5 else "3plus"))] += 1
        except (TypeError, ValueError):
            pass
    rows = []
    for (f, dec), c in sorted(counts.items()):
        n = c["n"]
        row = {"fips": f, "decade": dec, "n": n}
        for k in sorted(c):
            if k != "n":
                row[k + "_share"] = round(c[k] / n, 4)
        rows.append(row)
    return rows


# ----------------------------------------------------------------------------------------------
# Combine into the scan-engine table
# ----------------------------------------------------------------------------------------------

def combine(walls_rows, materials, nsi_rows, counties_div):
    """walls_prior rows: division, era, material, share, source, confidence."""
    out = []
    for r in walls_rows:
        if r["DETACHED"] != "detached":
            continue
        out.append({"div": r["DIV"], "era": r["ERA"], "material": r["wall"], "share": r["share"], "source": "src:census_soc",
                    "confidence": "high" if r["n"] >= 200 else ("medium" if r["n"] >= 50 else "low"), "n": r["n"]})
    exp = materials["pre1999_visible_wall_expert"]
    region_of_div = {int(k): v for k, v in exp["division_to_region"].items()}
    # NSI masonry share per division x era (pooled over the given counties), used to rescale masonry vs frame
    nsi_mas = defaultdict(lambda: [0, 0])
    for r in nsi_rows or []:
        div = counties_div.get(r["fips"])
        if div is None or r["decade"] == "unknown":
            continue
        dec = int(r["decade"][:-1])
        era = next((e["era"] for e in exp["eras"] if e["from"] <= dec <= e["to"]), None)
        if era is None:
            continue
        nsi_mas[(div, era)][0] += r.get("bldg_M_share", 0.0) * r["n"]
        nsi_mas[(div, era)][1] += r["n"]
    masonry = set(materials["masonry_materials"])
    for div in range(1, 10):
        region = region_of_div[div]
        for e in exp["eras"]:
            base = dict(exp["table"][region][e["era"]])
            src, conf = "expert", "low"
            m = nsi_mas.get((div, e["era"]))
            if m and m[1] >= 500:
                target = m[0] / m[1]
                cur = sum(v for k, v in base.items() if k in masonry)
                if 0 < cur < 1:
                    for k in base:
                        base[k] *= (target / cur) if k in masonry else ((1 - target) / (1 - cur))
                    src = "expert+src:nsi(masonry share)"
            tot = sum(base.values())
            for k, v in base.items():
                out.append({"div": div, "era": e["era"], "material": k, "share": round(v / tot, 4), "source": src,
                            "confidence": conf, "n": m[1] if m else 0})
    return out


def write_csv(path, rows):
    import csv
    if not rows:
        open(path, "w").close()
        return
    fields = []
    for r in rows:
        for k in r:
            if k not in fields:
                fields.append(k)
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for r in rows:
            w.writerow(r)


def run(df_raw, files, materials, nsi_rows, counties_div, out):
    os.makedirs(out, exist_ok=True)
    df = prepare_soc(df_raw)
    walls = shares(df, ["DIV", "ERA", "DETACHED"], "WALL", k=30.0, parent_by=["ERA", "DETACHED"])
    sec = shares(df[df["WALL2"].notna()], ["DIV", "ERA", "WALL"], "WALL2", k=30.0, parent_by=["ERA", "WALL"]) if "WALL2" in df.columns else []
    df_s = df.assign(STOREYS=df["STOR"].astype(int).map(STOR_CODES)) if "STOR" in df.columns else None
    df_b = df.assign(FOUNDATION=df["BASE"].astype(int).map(BASE_CODES)) if "BASE" in df.columns else None
    sb = []
    if df_s is not None:
        sb += [dict(r, field="storeys") for r in shares(df_s, ["DIV", "ERA", "DETACHED"], "STOREYS", k=30.0, parent_by=["ERA", "DETACHED"])]
    if df_b is not None:
        sb += [dict(r, field="foundation") for r in shares(df_b, ["DIV", "ERA", "DETACHED"], "FOUNDATION", k=30.0, parent_by=["ERA", "DETACHED"])]
    lh = lot_house(df[df["DETACHED"] == "detached"])
    prior = combine(walls, materials, nsi_rows, counties_div)
    write_csv(os.path.join(out, "walls_soc.csv"), walls)
    write_csv(os.path.join(out, "walls_secondary.csv"), sec)
    write_csv(os.path.join(out, "storeys_base.csv"), sb)
    write_csv(os.path.join(out, "lot_house.csv"), lh)
    write_csv(os.path.join(out, "nsi_aggregate.csv"), nsi_rows or [])
    write_csv(os.path.join(out, "walls_prior.csv"), prior)
    with open(os.path.join(out, "run_log.json"), "w", encoding="utf-8") as f:
        json.dump({"tool": "learn_walls.py", "graph_version": "world-graph-v0.1", "soc_files": files,
                   "houses_used": int(len(df)), "rows": {"walls": len(walls), "secondary": len(sec), "storeys_base": len(sb),
                                                          "lot_house": len(lh), "nsi": len(nsi_rows or []), "prior": len(prior)},
                   "credits": ["U.S. Census Bureau, Survey of Construction microdata (public domain)"] +
                              (["U.S. Army Corps of Engineers National Structure Inventory (USACE NSI); public fields only; aggregates only"] if nsi_rows else []),
                   "time_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}, f, indent=1)
    return {"walls": walls, "secondary": sec, "storeys_base": sb, "lot_house": lh, "prior": prior, "df": df}


# ----------------------------------------------------------------------------------------------
# Self-test
# ----------------------------------------------------------------------------------------------

def synthetic_soc(seed=5, n=40000):
    import pandas as pd
    rng = np.random.default_rng(seed)
    years = rng.integers(1999, 2026, n)
    div = rng.integers(1, 10, n)
    # truth: division 2 = 70 % vinyl (5), 20 % brick (2), 10 % fiber cement (8); division 9 = 60 % stucco (4),
    # 30 % fiber cement, 10 % wood (1); other divisions 25 % each of vinyl, stucco, brick, fiber cement
    wal = np.empty(n, dtype=int)
    for i in range(n):
        if div[i] == 2:
            wal[i] = rng.choice([5, 2, 8], p=[0.7, 0.2, 0.1])
        elif div[i] == 9:
            wal[i] = rng.choice([4, 8, 1], p=[0.6, 0.3, 0.1])
        else:
            wal[i] = rng.choice([5, 4, 2, 8])
    wal2 = np.where((wal == 2) & (rng.random(n) < 0.6), 5, 0)   # 60 % of brick houses have vinyl sides
    wal2 = np.where(years < 2009, np.where(wal2 == 0, 10, wal2), wal2)
    weight = rng.uniform(5, 50, n)
    df = pd.DataFrame({"DIV": div, "STRT": years * 100 + rng.integers(1, 13, n), "WAL1": wal, "WAL2": wal2,
                       "STOR": rng.choice([1, 2, 3], n, p=[0.45, 0.5, 0.05]), "BASE": rng.choice([1, 2, 3], n),
                       "DET": rng.choice([1, 2], n, p=[0.9, 0.1]), "GAR": rng.choice([1, 2, 3, 4], n),
                       "PRCH": rng.choice([1, 2], n), "DECK": rng.choice([1, 2], n), "PATI": rng.choice([1, 2], n),
                       "MFGS": rng.choice([1, 3], n, p=[0.03, 0.97]), "AREA": rng.lognormal(9.0, 0.5, n),
                       "SQFS": rng.lognormal(7.7, 0.3, n), "WEIGHT": weight, "FILE_YEAR": years})
    # a duplicate case from the next year's file (must be dropped by the start-year rule)
    dup = df.iloc[:100].copy()
    dup["FILE_YEAR"] = dup["FILE_YEAR"] + 1
    return pd.concat([df, dup], ignore_index=True)


def selftest():
    materials = json.load(open(os.path.join(HERE, "materials.json"), encoding="utf-8"))
    df = synthetic_soc()
    out = os.path.join(HERE, "selftest_out_walls")
    nsi_rows = aggregate_nsi_records("99001", (
        {"occtype": "RES1-1SNB", "bldgtype": "M" if i % 2 else "W", "found_type": "C", "num_story": 1, "med_yr_blt": 1955,
         "p_extwall": "SECRET-MUST-BE-DROPPED"} for i in range(1000)))
    res = run(df, ["synthetic"], materials, nsi_rows, {"99001": 4}, out)
    ok = True

    def check(name, cond, detail):
        nonlocal ok
        print(("PASS " if cond else "FAIL ") + name + ": " + detail)
        ok = ok and cond

    w = [r for r in res["walls"] if r["DIV"] == 2 and r["DETACHED"] == "detached" and r["wall"] == "vinyl"]
    check("div 2 vinyl ~0.70 in every era", len(w) == 5 and all(abs(r["share"] - 0.7) < 0.05 for r in w), str([r["share"] for r in w]))
    w = [r for r in res["walls"] if r["DIV"] == 9 and r["DETACHED"] == "detached" and r["wall"] == "stucco"]
    check("div 9 stucco ~0.60", len(w) == 5 and all(abs(r["share"] - 0.6) < 0.05 for r in w), str([r["share"] for r in w]))
    s = [r for r in res["secondary"] if r["WALL"] == "brick" and r["wall2"] == "vinyl"]
    check("brick front with vinyl sides ~0.60", bool(s) and all(abs(r["share"] - 0.6) < 0.08 for r in s), "%d rows, mean %.3f" % (len(s), np.mean([r["share"] for r in s]) if s else -1))
    check("duplicate cases dropped", len(res["df"]) == 40000, "houses used %d" % len(res["df"]))
    nsi_ok = all("p_extwall" not in k for r in nsi_rows for k in r) and abs(nsi_rows[0].get("bldg_M_share", 0) - 0.5) < 1e-6
    check("NSI aggregate keeps public fields only", nsi_ok, str(nsi_rows[0]))
    p = [r for r in res["prior"] if r["div"] == 4 and r["era"] == "1946-1964"]
    mas = sum(r["share"] for r in p if r["material"] in materials["masonry_materials"])
    check("NSI masonry share applied to the expert era row", abs(mas - 0.5) < 0.02 and "nsi" in p[0]["source"], "masonry %.3f, %s" % (mas, p[0]["source"]))
    tot_ok = True
    for key in set((r["div"], r["era"]) for r in res["prior"]):
        t = sum(r["share"] for r in res["prior"] if (r["div"], r["era"]) == key)
        tot_ok = tot_ok and abs(t - 1) < 0.01
    check("every prior row set sums to 1", tot_ok, "")
    print("SELFTEST", "OK" if ok else "FAILED")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--out", default=os.path.join(HERE, "out"))
    ap.add_argument("--cache", default=os.path.join(HERE, "cache", "soc"))
    ap.add_argument("--materials", default=os.path.join(HERE, "materials.json"))
    ap.add_argument("--counties", default=os.path.join(HERE, "counties.json"))
    ap.add_argument("--nsi", default="", help="comma list of county FIPS to aggregate from the NSI API (run time only)")
    ap.add_argument("--offline", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    materials = json.load(open(a.materials, encoding="utf-8"))
    cj = json.load(open(a.counties, encoding="utf-8"))
    counties_div = {row[0]: row[3] for row in cj["counties"]}
    df, files = load_soc(a.cache, a.offline)
    nsi_rows = []
    for f in [x for x in a.nsi.split(",") if x]:
        try:
            nsi_rows += nsi_aggregate_county(f, a.offline)
        except Exception as e:
            log("  NSI %s skipped: %s" % (f, e))
    run(df, files, materials, nsi_rows, counties_div, a.out)
    log("done; outputs in", a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
