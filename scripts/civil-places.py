"""Civil structures for the final test: one real US example per kind, picked at random (seeded) from
OpenStreetMap, plus the world lane's example, each with approach points on both sides.

Usage: python scripts/civil-places.py <seed> [out.json]

For each kind, a seeded random US area (metro and rural boxes over the lower 48, Alaska and Hawaii) is
queried on the public Overpass API, one request at a time, at least 6 s apart (the usage policy asks for
a low rate), with a generic user agent and no personal data. A kind not found in an area tries the next
seeded area (up to 12). The chosen way's middle, its bearing there, and two approach points 250 m before
each end of the structure are written (drive from each approach: both directions). Data (c)
OpenStreetMap contributors, ODbL 1.0.
"""
import json, math, random, sys, time, urllib.parse, urllib.request

ENDPOINT = 'https://overpass-api.de/api/interpreter'
UA = 'road-structure-test-picker/1.0 (game test data; low rate)'

# (name, south, north, west, east): metros and rural belts; AK and HI included.
AREAS = [
    ('Seattle', 47.45, 47.75, -122.45, -122.15), ('Portland', 45.4, 45.6, -122.8, -122.5), ('San Francisco Bay', 37.4, 37.9, -122.5, -121.9),
    ('Los Angeles', 33.85, 34.15, -118.45, -118.05), ('San Diego', 32.65, 32.9, -117.25, -116.95), ('Phoenix', 33.35, 33.6, -112.2, -111.8),
    ('Las Vegas', 36.05, 36.25, -115.3, -115.05), ('Salt Lake City', 40.6, 40.8, -112.0, -111.8), ('Denver', 39.6, 39.85, -105.1, -104.85),
    ('Albuquerque', 35.0, 35.2, -106.75, -106.5), ('Dallas', 32.7, 32.95, -96.95, -96.65), ('Houston', 29.65, 29.9, -95.55, -95.25),
    ('San Antonio', 29.35, 29.55, -98.6, -98.4), ('Oklahoma City', 35.4, 35.55, -97.65, -97.4), ('Kansas City', 39.0, 39.15, -94.7, -94.5),
    ('Minneapolis', 44.9, 45.05, -93.35, -93.15), ('Chicago', 41.8, 42.0, -87.8, -87.6), ('Milwaukee', 42.95, 43.1, -88.0, -87.85),
    ('Detroit', 42.3, 42.45, -83.2, -83.0), ('Indianapolis', 39.7, 39.85, -86.25, -86.05), ('Columbus', 39.9, 40.05, -83.1, -82.9),
    ('Pittsburgh', 40.4, 40.5, -80.05, -79.9), ('St. Louis', 38.55, 38.7, -90.35, -90.15), ('Nashville', 36.1, 36.25, -86.85, -86.7),
    ('Atlanta', 33.7, 33.9, -84.5, -84.3), ('Charlotte', 35.15, 35.3, -80.95, -80.75), ('Miami', 25.7, 25.85, -80.35, -80.15),
    ('Tampa', 27.9, 28.05, -82.55, -82.4), ('New Orleans', 29.9, 30.0, -90.15, -89.95), ('Washington DC', 38.85, 38.95, -77.1, -76.95),
    ('Baltimore', 39.25, 39.35, -76.7, -76.55), ('Philadelphia', 39.9, 40.05, -75.25, -75.1), ('New York', 40.65, 40.85, -74.05, -73.85),
    ('Boston', 42.3, 42.4, -71.15, -71.0), ('Hartford', 41.7, 41.8, -72.75, -72.6), ('Buffalo', 42.85, 42.95, -78.9, -78.8),
    ('Appalachia WV', 37.3, 38.4, -81.9, -80.5), ('Ozarks', 35.9, 36.6, -93.5, -92.3), ('Great Plains NE', 40.5, 41.5, -98.5, -96.5),
    ('Central Valley CA', 36.3, 37.3, -120.3, -119.3), ('Interstate WY', 41.1, 41.9, -107.5, -105.5), ('Florida Keys', 24.55, 25.2, -81.8, -80.4),
    ('Lake Pontchartrain', 30.0, 30.4, -90.3, -89.8), ('Chesapeake', 36.8, 37.2, -76.4, -75.9),
    ('Anchorage AK', 61.1, 61.3, -150.0, -149.7), ('Fairbanks AK', 64.8, 64.9, -147.9, -147.6), ('Juneau AK', 58.28, 58.42, -134.65, -134.35),
    ('Honolulu HI', 21.27, 21.42, -157.95, -157.75), ('Maui HI', 20.75, 20.95, -156.55, -156.3), ('Hilo HI', 19.65, 19.78, -155.15, -155.0),
]

ROAD = 'highway~"^(motorway|trunk|primary|secondary|tertiary|unclassified|residential|motorway_link|trunk_link|primary_link)$"'
KINDS = {
    'bridge': f'way[{ROAD}][bridge=yes]({{b}});',
    'long-water-bridge': f'way[{ROAD}][bridge=yes]({{b}})(if:length()>1000);',
    'road-under-bridge': f'way[{ROAD}][layer~"^-[1-9]"][tunnel!~"."]({{b}});',
    'elevated-road': f'way[{ROAD}][layer~"^[1-9]"][!bridge]({{b}});',
    'ramp': 'way[highway=motorway_link]({b});',
    'stack-interchange': 'way[highway=motorway_link][layer~"^[2-9]"]({b});',
    'road-tunnel': f'way[{ROAD}][tunnel=yes]({{b}});',
    'roundabout': 'way[junction~"^(roundabout|circular)$"][highway]({b});',
    'footbridge': 'way[highway~"^(footway|path|cycleway|pedestrian)$"][bridge=yes]({b});',
    'skyway': 'way[building=bridge]({b});',
    'rail-level-crossing': 'node[railway=level_crossing]({b});',
    'rail-bridge': 'way[railway=rail][bridge=yes]({b});',
    'multi-storey-parking': 'way[amenity=parking][parking=multi-storey]({b});',
    'retaining-wall': 'way[barrier=retaining_wall]({b});',
    'cutting': f'way[{ROAD}][cutting=yes]({{b}});',
    'embankment': f'way[{ROAD}][embankment=yes]({{b}});',
    'toll-booth': 'node[barrier=toll_booth]({b});',
    'ferry': 'way[route=ferry]({b});',
    'dam-road': f'way[waterway=dam]({{b}})->.d; way(around.d:15)[{ROAD}];',
}
# The world lane's example of each kind (D:\sc\detail-staging\civil-structures.md, 2026-11-14).
WORLD = {
    'bridge': (42.3342, -83.0457, 'Detroit, Woodward over I-75'), 'road-under-bridge': (19.7357, -155.0940, 'Hilo'),
    'elevated-road': (19.7150, -155.0720, 'Hilo'), 'ramp': (19.7357, -155.0940, 'Hilo'), 'road-tunnel': (47.6284, -122.3547, 'Seattle'),
    'footbridge': (19.7357, -155.0940, 'Hilo'), 'skyway': (47.6580, -122.3547, 'Seattle'), 'rail-bridge': (33.3489, -111.7859, 'Gilbert'),
    'multi-storey-parking': (42.3667, -83.0457, 'Detroit'), 'roundabout': (39.9784, -86.1180, 'Carmel, IN'),
    'toll-booth': (61.2227, -149.8865, 'Anchorage (booth POI)'), 'ferry': (25.7900, -80.1672, 'Miami'), 'dam-road': (36.0160, -114.7377, 'Hoover Dam'),
}

def query(q):
    data = urllib.parse.urlencode({'data': f'[out:json][timeout:25];({q});out geom 20;'}).encode()
    req = urllib.request.Request(ENDPOINT, data=data, headers={'User-Agent': UA})
    return json.load(urllib.request.urlopen(req, timeout=60))

def dist(a, b):
    k = math.radians((a[0] + b[0]) / 2)
    return math.hypot((b[0] - a[0]) * 111320, (b[1] - a[1]) * 111320 * math.cos(k))

def bearing(a, b):
    dy = (b[0] - a[0]); dx = (b[1] - a[1]) * math.cos(math.radians(a[0]))
    return math.degrees(math.atan2(dx, dy)) % 360

def walk(line, start, metres, sign):
    """A point metres beyond the end at index start, continuing the end segment's direction."""
    a, b = (line[start], line[start + sign]) if 0 <= start + sign < len(line) else (line[start], line[start])
    h = math.radians(bearing(b, a))
    return (round(a[0] + metres * math.cos(h) / 111320, 6), round(a[1] + metres * math.sin(h) / (111320 * math.cos(math.radians(a[0]))), 6))

def describe(el):
    if el['type'] == 'node':
        p = (el['lat'], el['lon'])
        return {'lat': round(p[0], 6), 'lon': round(p[1], 6), 'heading_deg': None, 'approach_a': p, 'approach_b': p, 'length_m': 0}
    line = [(g['lat'], g['lon']) for g in el.get('geometry', [])]
    if len(line) < 2: return None
    length = sum(dist(line[i], line[i + 1]) for i in range(len(line) - 1))
    m = len(line) // 2
    h = bearing(line[m - 1], line[m]) if m > 0 else bearing(line[0], line[1])
    return {'lat': round(line[m][0], 6), 'lon': round(line[m][1], 6), 'heading_deg': round(h, 1), 'heading_back_deg': round((h + 180) % 360, 1),
            'approach_a': walk(line, 0, 250, 1), 'approach_b': walk(line, len(line) - 1, 250, -1), 'length_m': round(length)}

def main():
    seed = int(sys.argv[1]); out = sys.argv[2] if len(sys.argv) > 2 else 'data/civil-places.json'
    rng = random.Random(seed)
    places = []
    for kind, q in KINDS.items():
        areas = AREAS[:]; rng.shuffle(areas)
        found = None
        for name, s, n, w, e in areas[:12]:
            # A 0.1-degree box at a seeded spot inside the area.
            la = rng.uniform(s, max(s, n - 0.1)); lo = rng.uniform(w, max(w, e - 0.1))
            b = f'{la:.4f},{lo:.4f},{la + 0.1:.4f},{lo + 0.1:.4f}'
            try:
                els = query(q.replace('{b}', b)).get('elements', [])
            except Exception as ex:
                print(f'{kind}: {name}: {ex}'); time.sleep(20); continue
            finally:
                time.sleep(6)
            els = [x for x in els if x['type'] in ('way', 'node')]
            if els:
                el = rng.choice(els)
                d = describe(el)
                if d:
                    found = dict(kind=kind, source='random', area=name, osm=f"{el['type']}/{el['id']}", name=el.get('tags', {}).get('name', ''), **d)
                    break
        if found:
            places.append(found); print(f"{kind}: {found['area']} {found['osm']} {found['lat']},{found['lon']} {found['name']}")
        else:
            places.append(dict(kind=kind, source='random', area=None, osm=None, note='not found in 12 seeded areas')); print(f'{kind}: not found')
        if kind in WORLD:
            la, lo, label = WORLD[kind]
            places.append(dict(kind=kind, source='world-lane', area=label, lat=la, lon=lo, osm=None, note='tile centre or known place from the world lane inventory; approaches from the start pick'))
    doc = {'seed': seed, 'made_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
           'source': 'OpenStreetMap via the Overpass API (overpass-api.de), low rate', 'licence': 'ODbL 1.0, (c) OpenStreetMap contributors',
           'drive': 'from approach_a and from approach_b (both directions), bridge and deck rules, frame beside the aerial', 'places': places}
    json.dump(doc, open(out, 'w', encoding='utf-8'), indent=1)
    print(f'{len(places)} entries -> {out}')

if __name__ == '__main__':
    main()
