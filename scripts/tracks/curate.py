# Builds data/tracks/tracks.json, the race tracks of the pause menu.
#
#   uv run --no-project --with shapely python scripts/tracks/curate.py <cache dir>
#
# The curated part is the list below: each track's name, place, kind, published length
# and published banking (the track's Wikipedia infobox, checked November 2026). Everything
# about where a track is comes from OpenStreetMap (highway=raceway ways, ODbL), fetched once
# into the cache directory through Overpass:
#   - the loop: the outer boundary of every raceway line of the track (an oval's infield road
#     course, pit lanes, kart and dirt tracks left out by name), simplified to 1.5 m;
#   - the start: the point of the loop nearest the middle of the pit lane (the start/finish
#     straight of almost every US circuit); an oval with no pit lane mapped starts mid-way
#     along the straight that bows most (a D-oval's frontstretch);
#   - the bearing: ovals run counter-clockwise; road courses the way their mapped oneway runs.
# The mapped loop length is written beside the published one so a stale map shows.
import json, math, os, re, subprocess, sys, time
from shapely.geometry import LineString, Polygon
from shapely.ops import polygonize, unary_union

WIKI = 'https://en.wikipedia.org/wiki/'
# id, name, place, kind, published miles, banking deg (turns, frontstretch, backstretch), Wikipedia title, note, (lat, lon) to search
TRACKS = [
    ('daytona', 'Daytona International Speedway', 'Daytona Beach, Florida', 'oval', 2.5, (31, 18, 3), 'Daytona_International_Speedway', 'Turns 31°, tri-oval 18°, back straightaway 3°', (29.1852, -81.0705)),
    ('talladega', 'Talladega Superspeedway', 'Lincoln, Alabama', 'oval', 2.66, (33, 16.5, 2), 'Talladega_Superspeedway', 'Turns 33°, tri-oval 16.5°, straights 2°', (33.5670, -86.0650)),
    ('richmond', 'Richmond Raceway', 'Richmond, Virginia', 'oval', 0.75, (14, 8, 2), 'Richmond_Raceway', 'Turns 14°, frontstretch 8°, backstretch 2°', (37.5920, -77.4196)),
    ('bristol', 'Bristol Motor Speedway', 'Bristol, Tennessee', 'oval', 0.533, (26, 6, 6), 'Bristol_Motor_Speedway', 'Turns 24-28° (26° used), straights 4-8° (6° used)', (36.5157, -82.2570)),
    ('martinsville', 'Martinsville Speedway', 'Ridgeway, Virginia', 'oval', 0.526, (12, 0, 0), 'Martinsville_Speedway', 'Turns 12°, straights 0°', (36.6340, -79.8510)),
    ('charlotte', 'Charlotte Motor Speedway', 'Concord, North Carolina', 'oval', 1.5, (24, 5, 5), 'Charlotte_Motor_Speedway', 'Turns 24°, straights 5°', (35.3520, -80.6830)),
    ('darlington', 'Darlington Raceway', 'Darlington, South Carolina', 'oval', 1.366, (24, 6, 6), 'Darlington_Raceway', 'Turns 1-2 25°, turns 3-4 23° (24° used), straights 6°', (34.2955, -79.9055)),
    ('indianapolis', 'Indianapolis Motor Speedway', 'Speedway, Indiana', 'oval', 2.5, (9.2, 0, 0), 'Indianapolis_Motor_Speedway', 'Turns 9.2°, straights 0°', (39.7950, -86.2348)),
    ('phoenix', 'Phoenix Raceway', 'Avondale, Arizona', 'oval', 1.022, (10, 9, 3), 'Phoenix_Raceway', 'Turns 1-2 9°, turns 3-4 11° (10° used), frontstretch 9°, backstretch 3°', (33.3747, -112.3115)),
    ('lasvegas', 'Las Vegas Motor Speedway', 'Las Vegas, Nevada', 'oval', 1.5, (20, 9, 9), 'Las_Vegas_Motor_Speedway', 'Turns 20°, straights 9°', (36.2720, -115.0110)),
    ('texas', 'Texas Motor Speedway', 'Fort Worth, Texas', 'oval', 1.5, (22, 5, 5), 'Texas_Motor_Speedway', 'Turns 1-2 20°, turns 3-4 24° (22° used), straights 5°', (33.0372, -97.2810)),
    ('kansas', 'Kansas Speedway', 'Kansas City, Kansas', 'oval', 1.5, (18.5, 10, 5), 'Kansas_Speedway', 'Turns 17-20° (18.5° used), frontstretch 10°, backstretch 5°', (39.1157, -94.8310)),
    ('michigan', 'Michigan International Speedway', 'Brooklyn, Michigan', 'oval', 2.0, (18, 12, 5), 'Michigan_International_Speedway', 'Turns 18°, frontstretch 12°, backstretch 5°', (42.0660, -84.2410)),
    ('pocono', 'Pocono Raceway', 'Long Pond, Pennsylvania', 'oval', 2.5, (9, 0, 0), 'Pocono_Raceway', 'Turn 1 14°, turn 2 8°, turn 3 6° (9° used for all three)', (41.0550, -75.5110)),
    ('dover', 'Dover Motor Speedway', 'Dover, Delaware', 'oval', 1.0, (24, 9, 9), 'Dover_Motor_Speedway', 'Turns 24°, straights 9°', (39.1900, -75.5300)),
    ('nashville', 'Nashville Superspeedway', 'Lebanon, Tennessee', 'oval', 1.33, (14, 9, 6), 'Nashville_Superspeedway', 'Turns 14°, frontstretch 9°, backstretch 6°', (36.0450, -86.4120)),
    ('atlanta', 'Atlanta Motor Speedway', 'Hampton, Georgia', 'oval', 1.54, (28, 5, 5), 'Atlanta_Motor_Speedway', 'Turns 28°, straights 5° (since the 2022 rebuild)', (33.3840, -84.3170)),
    ('homestead', 'Homestead-Miami Speedway', 'Homestead, Florida', 'oval', 1.5, (19, 4, 4), 'Homestead%E2%80%93Miami_Speedway', 'Turns 18-20° (19° used), straights 4°', (25.4520, -80.4090)),
    ('chicagoland', 'Chicagoland Speedway', 'Joliet, Illinois', 'oval', 1.52, (18, 11, 5), 'Chicagoland_Speedway', 'Turns 18°, frontstretch 11°, backstretch 5°', (41.4750, -88.0570)),
    ('iowa', 'Iowa Speedway', 'Newton, Iowa', 'oval', 0.875, (13, 10, 4), 'Iowa_Speedway', 'Turns 12-14° (13° used), frontstretch 10°, backstretch 4°', (41.6780, -93.0140)),
    ('gateway', 'Gateway Motorsports Park', 'Madison, Illinois', 'oval', 1.25, (10, 3, 3), 'Gateway_Motorsports_Park', 'Turns 1-2 11°, turns 3-4 9° (10° used); straights not published (3° assumed)', (38.6505, -90.1360)),
    ('newhampshire', 'New Hampshire Motor Speedway', 'Loudon, New Hampshire', 'oval', 1.058, (4.5, 1, 1), 'New_Hampshire_Motor_Speedway', 'Turns 2-7° progressive (4.5° used), straights 1°', (43.3625, -71.4605)),
    ('watkinsglen', 'Watkins Glen International', 'Watkins Glen, New York', 'road', 3.45, None, 'Watkins_Glen_International', 'Full course with the Boot', (42.3369, -76.9272)),
    ('sonoma', 'Sonoma Raceway', 'Sonoma, California', 'road', 2.52, None, 'Sonoma_Raceway', 'The road course as mapped', (38.1610, -122.4550)),
    ('cota', 'Circuit of the Americas', 'Austin, Texas', 'road', 3.426, None, 'Circuit_of_the_Americas', 'Grand Prix circuit, counter-clockwise', (30.1340, -97.6360)),
    ('roadamerica', 'Road America', 'Elkhart Lake, Wisconsin', 'road', 4.048, None, 'Road_America', '', (43.7980, -87.9960)),
    ('charlotteroval', 'Charlotte Motor Speedway Roval', 'Concord, North Carolina', 'road', 2.32, (24, 5, 5), 'Charlotte_Motor_Speedway', 'Oval turns 24°, oval straights 5°, infield flat; starts on the oval frontstretch', (35.3520, -80.6830)),
    ('lagunaseca', 'Laguna Seca Raceway', 'Monterey, California', 'road', 2.238, None, 'Laguna_Seca', 'Counter-clockwise, the Corkscrew', (36.5840, -121.7530)),
    ('roadatlanta', 'Road Atlanta', 'Braselton, Georgia', 'road', 2.54, None, 'Road_Atlanta', '', (34.1470, -83.8150)),
    ('sebring', 'Sebring International Raceway', 'Sebring, Florida', 'road', 3.741, None, 'Sebring_International_Raceway', 'Old airfield concrete in places', (27.4560, -81.3480)),
    ('limerock', 'Lime Rock Park', 'Lakeville, Connecticut', 'road', 1.53, None, 'Lime_Rock_Park', '', (41.9280, -73.3820)),
    ('vir', 'Virginia International Raceway', 'Alton, Virginia', 'road', 3.27, None, 'Virginia_International_Raceway', 'Full course', (36.5680, -79.2060)),
    ('midohio', 'Mid-Ohio Sports Car Course', 'Lexington, Ohio', 'road', 2.258, None, 'Mid-Ohio_Sports_Car_Course', '', (40.6890, -82.6360)),
    ('barber', 'Barber Motorsports Park', 'Birmingham, Alabama', 'road', 2.38, None, 'Barber_Motorsports_Park', '', (33.5320, -86.6190)),
]
# A street circuit: public streets, so no loop and no banking; the start is a point on its front straight.
STREETS = [
    dict(id='longbeach', name='Long Beach street circuit', place='Long Beach, California', kind='street', length_mi=1.968,
         start=[33.76255, -118.19305], bearing=None, centre=[33.7634, -118.1935], width_m=0, street='East Shoreline Drive',
         sources=['OpenStreetMap streets (ODbL)', WIKI + 'Grand_Prix_of_Long_Beach'], note='Public streets as they are on a normal day: no barriers'),
]
# Raceway lines that are not an oval's own racing surface (matched per ';'-separated name part).
NOT_OVAL = re.compile(r"road course|roval|chicane|pit|flat track|dirt|kart|small oval|1/4|1/3|1/5|half mile|bullring|legends|lil'|drag|horseshoe|le mans|route 66|thunder valley|grand prix|warmup|escape|corner|allison|exotics|the strip|access|ten tenths", re.I)
OVERPASS = 'https://maps.mail.ru/osm/tools/overpass/api/interpreter'


def fetch(cache, key, lat, lon):
    path = os.path.join(cache, key + '.json')
    for _ in range(6):
        try:
            if '"elements"' in open(path, encoding='utf-8').read():
                return json.load(open(path, encoding='utf-8'))['elements']
        except OSError:
            pass
        q = f'[out:json][timeout:100];way(around:2500,{lat},{lon})[highway=raceway];out tags geom;'
        subprocess.run(['curl', '-s', '-m', '110', '-o', path, '--data-urlencode', 'data=' + q, OVERPASS])
        time.sleep(3)
    raise SystemExit(f'{key}: Overpass gave no answer')


def width_m(kind, miles, tid):
    # OpenStreetMap rarely maps a raceway's width and the vector tiles drop it: typical widths
    # by kind (superspeedways about 18 m, intermediate ovals 16 m, short ovals 15 m, road courses 13 m).
    if kind == 'oval':
        return 18.0 if miles >= 2.4 and tid not in ('indianapolis', 'pocono') else 16.0 if miles >= 1.0 else 15.0
    return 13.0


def analyse(elements, lat0, lon0, oval):
    kx, ky = 111320 * math.cos(math.radians(lat0)), 110540
    ll = lambda x, y: [round(lat0 + y / ky, 6), round(lon0 + x / kx, 6)]
    ways = []
    for w in elements:
        if 'geometry' not in w:
            continue
        t = w.get('tags', {})
        ways.append(dict(name=t.get('name', ''), ow=t.get('oneway', ''),
                         g=LineString([((p['lon'] - lon0) * kx, (p['lat'] - lat0) * ky) for p in w['geometry']])))
    if oval:
        use = [w for w in ways if not (w['name'] and all(NOT_OVAL.search(p) for p in w['name'].split(';')))]
    else:
        use = [w for w in ways if 'pit' not in w['name'].lower()]
    u = unary_union(list(polygonize(unary_union([w['g'] for w in use]))))
    big = max([u] if u.geom_type == 'Polygon' else list(u.geoms), key=lambda p: p.area)
    ring = Polygon(big.exterior).simplify(1.5)
    if not ring.exterior.is_ccw:
        ring = Polygon(list(ring.exterior.coords)[::-1])
    ext = ring.exterior
    pits = [w for w in ways if re.search(r'pit (lane|road)', w['name'], re.I) and not re.search(r'exit|entry|warmup|road course|roval|formula|1/4', w['name'], re.I)]
    pits = pits or [w for w in ways if 'pit' in w['name'].lower()]
    if pits:
        s = ext.project(max(pits, key=lambda w: w['g'].length)['g'].interpolate(0.5, normalized=True))
    else:
        n, L = 400, ext.length
        pts = [ext.interpolate(L * i / n) for i in range(n)]
        hd = [math.atan2(pts[(i + 1) % n].y - pts[i].y, pts[(i + 1) % n].x - pts[i].x) for i in range(n)]
        straight = [abs((hd[(i + 5) % n] - hd[i - 5] + math.pi) % (2 * math.pi) - math.pi) < 0.12 for i in range(n)]
        first = straight.index(False)
        runs, cur = [], []
        for j in [(first + k) % n for k in range(n)]:
            if straight[j]:
                cur.append(j)
            elif cur:
                runs.append(cur); cur = []
        if cur:
            runs.append(cur)
        bow = lambda r: max(LineString([pts[r[0]], pts[r[-1]]]).distance(pts[j]) for j in r)
        r = max(sorted(runs, key=len, reverse=True)[:2], key=bow)
        s = L * r[len(r) // 2] / n
    q = ext.interpolate(s)
    a, b = ext.interpolate((s - 15) % ext.length), ext.interpolate((s + 15) % ext.length)
    dx, dy = b.x - a.x, b.y - a.y  # counter-clockwise along the loop
    if not oval:
        near = min([w for w in ways if 'pit' not in w['name'].lower()], key=lambda w: w['g'].distance(q))
        t = near['g'].project(q)
        g1, g2 = near['g'].interpolate(max(0, t - 10)), near['g'].interpolate(min(near['g'].length, t + 10))
        wdx, wdy = g2.x - g1.x, g2.y - g1.y
        if near['ow'] == '-1':
            wdx, wdy = -wdx, -wdy
        if near['ow'] in ('yes', '1', '-1') and wdx * dx + wdy * dy < 0:
            dx, dy = -dx, -dy
    c = big.centroid
    return dict(start=ll(q.x, q.y), bearing=round((math.degrees(math.atan2(dx, dy)) + 360) % 360, 1), centre=ll(c.x, c.y),
                mapped_mi=round(ext.length / 1609.344, 3), loop=[ll(x, y) for x, y in ext.coords])


def main():
    cache = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.environ.get('TEMP', '/tmp'), 'osds-tracks')
    os.makedirs(cache, exist_ok=True)
    out = []
    for tid, name, place, kind, miles, bank, wiki, note, (lat, lon) in TRACKS:
        key = 'charlotte' if tid == 'charlotteroval' else tid
        # The Roval's start and banked loop are the oval's: it uses the oval's frontstretch and turns.
        a = analyse(fetch(cache, key, lat, lon), lat, lon, oval=kind == 'oval' or tid == 'charlotteroval')
        e = dict(id=tid, name=name, place=place, kind=kind, length_mi=miles, mapped_mi=a['mapped_mi'], start=a['start'], bearing=a['bearing'],
                 centre=a['centre'], width_m=width_m(kind, miles, tid),
                 sources=['OpenStreetMap highway=raceway ways (ODbL), fetched ' + time.strftime('%Y-%m-%d'), WIKI + wiki], note=note)
        if bank:
            e.update(bank_turn_deg=bank[0], bank_front_deg=bank[1], bank_back_deg=bank[2], loop=a['loop'])
        out.append(e)
        print(f"{tid:15} {a['mapped_mi']:6.3f} mi mapped / {miles} published, start {a['start']} bearing {a['bearing']}")
    out += STREETS
    doc = dict(version=1, note='Race tracks of the pause menu (SimCruiseTracks), made by scripts/tracks/curate.py. Start points, bearings, centres and oval loops come from OpenStreetMap highway=raceway ways (ODbL); lengths and banking are the published figures of each track (Wikipedia infobox, November 2026). OSDS is not affiliated with or endorsed by any track or racing series.', tracks=out)
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    path = os.path.join(root, 'data', 'tracks', 'tracks.json')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(doc, f, indent=1, ensure_ascii=False)
        f.write('\n')
    print(path, len(out), 'tracks')


if __name__ == '__main__':
    main()
