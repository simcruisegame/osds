"""Side-by-side sheets for a frame review of the world's fine detail: each review frame next to the
aerial photograph of the same spot (USGS Imagery Only, NAIP-based, public domain), turned so the
car's direction of travel is up, with the car and its view wedge drawn on it.

Usage: python scripts/detail-sheets.py <run dir> [<run dir> ...]
Writes <run dir>/aerial/<frame>.jpg (the photo, cached), <run dir>/sheets/<frame>.png and
<run dir>/sheets/all.png (up to 8 frames). The travel direction comes from the frame positions
(lat/lon in review/*.json), not the game's yaw (that is in the world frame, not a compass bearing).
"""
import glob, io, json, math, os, sys, urllib.request
from PIL import Image, ImageDraw

HALF_M = 150.0          # the photo shows 300 m x 300 m around the point 100 m ahead of the car
AHEAD_M = 100.0
FOV_DEG = 75.0
URL = ('https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer/export'
       '?bbox={},{},{},{}&bboxSR=3857&imageSR=3857&size=1024,1024&format=jpg&f=image')

def merc(lat, lon):
    x = math.radians(lon) * 6378137.0
    y = math.log(math.tan(math.pi / 4 + math.radians(lat) / 2)) * 6378137.0
    return x, y

def bearing(a, b):
    """Compass bearing (deg, 0 north, clockwise) from frame a to frame b."""
    dy = (b['lat'] - a['lat']) * 111320.0
    dx = (b['lon'] - a['lon']) * 111320.0 * math.cos(math.radians(a['lat']))
    return math.degrees(math.atan2(dx, dy)) % 360.0, math.hypot(dx, dy)

def photo(run, name, lat, lon, brg):
    os.makedirs(os.path.join(run, 'aerial'), exist_ok=True)
    path = os.path.join(run, 'aerial', name + '.jpg')
    # The centre 100 m ahead along the travel direction; Web Mercator metres grow with 1/cos(lat).
    k = 1.0 / math.cos(math.radians(lat))
    cx, cy = merc(lat, lon)
    cx += math.sin(math.radians(brg)) * AHEAD_M * k
    cy += math.cos(math.radians(brg)) * AHEAD_M * k
    h = HALF_M * k
    if not os.path.exists(path):
        req = urllib.request.Request(URL.format(cx - h, cy - h, cx + h, cy + h), headers={'User-Agent': 'frame-review-aerial'})
        data = urllib.request.urlopen(req, timeout=60).read()
        if data[:2] != b'\xff\xd8':
            raise RuntimeError('not a JPEG from the imagery service')
        open(path, 'wb').write(data)
    img = Image.open(path).convert('RGB')
    # Turn so the travel direction is up; the car sits AHEAD_M below the centre.
    img = img.rotate(brg, resample=Image.BICUBIC, expand=False)
    d = ImageDraw.Draw(img)
    s = img.width / (2 * HALF_M)
    car = (img.width / 2, img.height / 2 + AHEAD_M * s)
    reach = 140 * s
    for side in (-1, 1):
        a = math.radians(side * FOV_DEG / 2)
        d.line([car, (car[0] + math.sin(a) * reach, car[1] - math.cos(a) * reach)], fill=(255, 255, 0), width=3)
    d.ellipse([car[0] - 8, car[1] - 8, car[0] + 8, car[1] + 8], outline=(255, 0, 0), width=4)
    for m in (25, 50, 100):
        d.arc([car[0] - m * s, car[1] - m * s, car[0] + m * s, car[1] + m * s], 270 - FOV_DEG / 2, 270 + FOV_DEG / 2, fill=(255, 255, 255), width=1)
    d.text((8, 8), 'aerial (USGS Imagery Only / NAIP), travel up; rings 25/50/100 m', fill=(255, 255, 0))
    return img

def sheet(run):
    frames = []
    for j in sorted(glob.glob(os.path.join(run, 'review', '*.json'))):
        f = json.load(open(j))
        if 'lat' in f and os.path.exists(j[:-5] + '.png'):
            frames.append(f)
    if not frames:
        print(f'{run}: no frames with positions'); return
    os.makedirs(os.path.join(run, 'sheets'), exist_ok=True)
    out = []
    for i, f in enumerate(frames):
        # The travel direction: to the next frame that moved more than 3 m, else from the last one before.
        brg = None
        for g in frames[i + 1:]:
            b, dist = bearing(f, g)
            if dist > 3: brg = b; break
        if brg is None:
            for g in reversed(frames[:i]):
                b, dist = bearing(g, f)
                if dist > 3: brg = b; break
        if brg is None:
            continue
        try:
            air = photo(run, f['name'], f['lat'], f['lon'], brg).resize((720, 720))
        except Exception as e:
            print(f"{run} {f['name']}: no aerial ({e})"); continue
        game = Image.open(os.path.join(run, 'review', f['name'] + '.png')).convert('RGB').resize((1280, 720))
        s = Image.new('RGB', (2000, 750), 'black')
        s.paste(game, (0, 30)); s.paste(air, (1280, 30))
        ImageDraw.Draw(s).text((6, 8), f"{os.path.basename(run)}  {f['name']}  t {f['t']:.1f} s  {f['lat']:.5f}, {f['lon']:.5f}  travel {brg:.0f} deg", fill=(255, 255, 0))
        p = os.path.join(run, 'sheets', f['name'] + '.png')
        s.save(p); out.append(p)
    pick = out[::max(1, len(out) // 8)][:8]
    if pick:
        tiles = [Image.open(p).resize((1000, 375)) for p in pick]
        allimg = Image.new('RGB', (2000, 375 * ((len(tiles) + 1) // 2)), 'black')
        for k, t in enumerate(tiles):
            allimg.paste(t, ((k % 2) * 1000, (k // 2) * 375))
        allimg.save(os.path.join(run, 'sheets', 'all.png'))
    print(f'{run}: {len(out)} sheets')

if __name__ == '__main__':
    for r in sys.argv[1:]:
        sheet(r)
