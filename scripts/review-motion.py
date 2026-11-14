"""Motion review of one run: event clips, a real-speed strip, and an automatic scan of the video for
faults that still frames do not show (pop-in, flicker, shimmer, judder, a stale or black mirror).

Usage: python scripts/review-motion.py <run dir> [<run dir> ...]

Reads <run>/video.mp4 (the robot's frames at 10 fps), review.json (the judge's events with their drive
times), frames.csv (the drive time of each video frame; older runs: the "(frame N)" numbers in game.log,
else a straight line over the drive) and preflight.json. Writes into <run>/motion/:
  clips/<t>s-<event>.mp4 and .png   every judge event, 1 s before to 1 s after (video + a strip)
  strip-NN.png                      the whole drive, one frame each 0.5 s, 5 x 4 frames a sheet
  scan/<t>s-<kind>.png              each scan finding: previous | this | next frame, the cell boxed, 3x crop
  motion.md                         the table of findings with times, for the reviewer to watch
The scan finds candidates, not verdicts: watch each clip and write the verdict in motion.md.
"""
import csv, glob, json, math, os, re, shutil, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw

FPS = 10.0
GRID_X, GRID_Y = 12, 6            # cells over the view
# The windscreen between the A-pillars, above the dash (1920x1080, the Miata and the Corvette at the
# driver's eye): one camera shift fits it; the side windows move differently and gave false pops.
VIEW = (660, 230, 1380, 590)
MIRROR = (1380, 285, 1790, 385)   # the inside mirror glass (Miata and Corvette at the driver's eye)

def ffmpeg():
    for c in ('ffmpeg', r'C:\ffmpeg\bin\ffmpeg.exe'):
        p = shutil.which(c)
        if p: return p
    raise SystemExit('ffmpeg not found')

def frame_times(run, n_frames, drive_s):
    """Drive time of each video frame."""
    p = os.path.join(run, 'frames.csv')
    if os.path.exists(p):
        rows = list(csv.DictReader(open(p)))
        t = [float(r['drive_s']) for r in rows]
        if len(t) >= n_frames: return t[:n_frames], 'frames.csv'
    # Older runs have no frames.csv: a straight line over the drive (the frames are not evenly spaced,
    # so an old run's clips can be a second or two off; new runs write frames.csv).
    return [i * drive_s / max(n_frames - 1, 1) for i in range(n_frames)], 'linear over the drive (no frames.csv; times can be 1-2 s off)'

def extract(run, ff):
    d = os.path.join(run, 'motion', 'all')
    if not glob.glob(os.path.join(d, '*.jpg')):
        os.makedirs(d, exist_ok=True)
        subprocess.run([ff, '-hide_banner', '-loglevel', 'error', '-y', '-i', os.path.join(run, 'video.mp4'), '-q:v', '3', os.path.join(d, '%05d.jpg')], check=True)
    return sorted(glob.glob(os.path.join(d, '*.jpg')))

def lum(path, box, size):
    return np.asarray(Image.open(path).convert('L').crop(box).resize(size, Image.BILINEAR), dtype=np.float32)

def events(run):
    p = os.path.join(run, 'review.json')
    if not os.path.exists(p): return []
    out = []
    for e in json.load(open(p)).get('events', []):
        m = re.match(r'([\d.]+) s (.+)', e)
        if m: out.append((float(m.group(1)), m.group(2)))
    return out

def nearest(times, t):
    return min(range(len(times)), key=lambda i: abs(times[i] - t))

def sheet(paths, out, cols=5, w=384):
    if not paths: return
    ims = [Image.open(p).convert('RGB') for p in paths]
    h = int(w * ims[0].height / ims[0].width)
    rows = (len(ims) + cols - 1) // cols
    s = Image.new('RGB', (cols * w, rows * (h + 16)), 'black')
    d = ImageDraw.Draw(s)
    for k, (im, p) in enumerate(zip(ims, paths)):
        x, y = (k % cols) * w, (k // cols) * (h + 16)
        s.paste(im.resize((w, h)), (x, y + 16))
        d.text((x + 4, y + 2), os.path.basename(p), fill=(255, 255, 0))
    s.save(out)

def finding_png(files, i, cell, out, label):
    """previous | this | next, the cell boxed, and the cell enlarged 3x below."""
    x0, y0, x1, y1 = cell
    trio = [Image.open(files[j]).convert('RGB') for j in (max(i - 1, 0), i, min(i + 1, len(files) - 1))]
    W = 640; H = int(W * trio[0].height / trio[0].width); sx = W / trio[0].width
    crops = [t.crop((max(x0 - 40, 0), max(y0 - 40, 0), x1 + 40, y1 + 40)) for t in trio]
    cw = min(640, crops[0].width * 3); ch = int(cw * crops[0].height / crops[0].width)
    s = Image.new('RGB', (3 * W, H + ch + 24), 'black')
    d = ImageDraw.Draw(s)
    for k, (t, c) in enumerate(zip(trio, crops)):
        s.paste(t.resize((W, H)), (k * W, 20))
        d.rectangle([k * W + x0 * sx, 20 + y0 * sx, k * W + x1 * sx, 20 + y1 * sx], outline=(255, 0, 0), width=2)
        s.paste(c.resize((cw, ch)), (k * W, H + 24))
    d.text((4, 2), label + '   (previous | this | next)', fill=(255, 255, 0))
    s.save(out)

def scan(files, times, out_dir):
    """Candidates: pop (a cell jumps once while the rest moves smoothly), flicker (a cell toggles),
    shimmer (a cell keeps changing at high frequency), mirror (black, or frozen while the view moves),
    judder (a gap in the frame times)."""
    os.makedirs(out_dir, exist_ok=True)
    cw, ch = (VIEW[2] - VIEW[0]) // GRID_X, (VIEW[3] - VIEW[1]) // GRID_Y
    L = np.stack([lum(f, VIEW, (GRID_X * 8, GRID_Y * 8)) for f in files])            # frames x H x W
    # Camera motion first: the whole view's shift between two frames (phase correlation), so a turn,
    # a rocking car or a passing near object is not read as a pop (Doraville, 2026-11-14).
    def shift(a, b):
        A, B = np.fft.fft2(a - a.mean()), np.fft.fft2(b - b.mean())
        R = A * np.conj(B); R /= np.abs(R) + 1e-6
        r = np.fft.ifft2(R).real
        dy, dx = np.unravel_index(np.argmax(r), r.shape)
        if dy > r.shape[0] // 2: dy -= r.shape[0]
        if dx > r.shape[1] // 2: dx -= r.shape[1]
        return int(dy), int(dx)
    D = np.zeros((len(files) - 1, GRID_Y, GRID_X), dtype=np.float32)
    for i in range(len(files) - 1):
        dy, dx = shift(L[i + 1], L[i])
        prev = np.roll(L[i], (dy, dx), axis=(0, 1))
        diff = np.abs(L[i + 1] - prev)
        # The rows and columns the roll wrapped round are not compared.
        if dy > 0: diff[:dy, :] = 0
        if dy < 0: diff[dy:, :] = 0
        if dx > 0: diff[:, :dx] = 0
        if dx < 0: diff[:, dx:] = 0
        D[i] = diff.reshape(GRID_Y, 8, GRID_X, 8).mean(axis=(1, 3))
    C = L.reshape(len(files), GRID_Y, 8, GRID_X, 8).mean(axis=(2, 4))                 # cell means
    G = np.median(D.reshape(len(D), -1), axis=1)                                      # the view's usual change
    M = np.stack([lum(f, MIRROR, (64, 16)) for f in files])
    mirror_mean = M.mean(axis=(1, 2))
    mirror_change = np.abs(np.diff(M, axis=0)).mean(axis=(1, 2))
    found = []
    # The inside mirror is checked on its own below; its cells are left out of the world scan.
    def in_mirror(x, y):
        x0, y0 = VIEW[0] + x * cw, VIEW[1] + y * ch
        return x0 + cw > 1380 and x0 < 1920 and y0 + ch > 260 and y0 < 410
    for i in range(2, len(D) - 2):
        for y in range(GRID_Y):
            for x in range(GRID_X):
                if in_mirror(x, y): continue
                d0 = D[i, y, x]
                around = np.median([D[i - 2, y, x], D[i - 1, y, x], D[i + 1, y, x], D[i + 2, y, x]])
                if d0 < 18 or d0 < 4 * max(around, G[i], 2.0):
                    continue
                # Parallax, not a pop: a near object's edge moving across the view changes the
                # neighbouring cells in the frames just before or after (Doraville 12.2 s, 2026-11-14:
                # a near building passing on the right read as a pop in three frames).
                nb = [D[j, y, xx] for j in (i - 1, i + 1) for xx in (x - 1, x + 1) if 0 <= xx < GRID_X]
                if nb and max(nb) > 0.5 * d0:
                    continue
                back = abs(C[i + 2, y, x] - C[i, y, x])          # did it come back? (index shift: D[i] = C[i+1]-C[i])
                kind = 'flicker' if back > 0.7 * d0 and D[i + 1, y, x] > 0.5 * d0 else 'pop'
                found.append((float(d0), i + 1, x, y, kind))
    # Shimmer: a cell whose change stays far above the view's for a whole second.
    for i in range(0, len(D) - 10, 5):
        for y in range(GRID_Y):
            for x in range(GRID_X):
                if in_mirror(x, y): continue
                seg = D[i:i + 10, y, x]
                if seg.min() > 10 and seg.mean() > 3 * max(np.median(G[i:i + 10]), 2.0):
                    found.append((float(seg.mean()), i + 5, x, y, 'shimmer'))
    # Keep the strongest per kind and second.
    found.sort(reverse=True)
    kept, seen = [], set()
    for f in found:
        key = (f[4], int(times[min(f[1], len(times) - 1)]))
        if key in seen: continue
        seen.add(key); kept.append(f)
        if len(kept) >= 30: break
    rows = []
    for score, i, x, y, kind in sorted(kept, key=lambda f: f[1]):
        cell = (VIEW[0] + x * cw, VIEW[1] + y * ch, VIEW[0] + (x + 1) * cw, VIEW[1] + (y + 1) * ch)
        t = times[min(i, len(times) - 1)]
        p = os.path.join(out_dir, f'{t:05.1f}s-{kind}-{x}-{y}.png')
        finding_png(files, i, cell, p, f'{kind} at {t:.1f} s, cell {x},{y}, change {score:.0f} (view {G[min(i, len(G) - 1)]:.1f})')
        rows.append((t, kind, f'cell {x},{y} {cell}', f'{score:.0f} vs view {G[min(i, len(G) - 1)]:.1f}', os.path.relpath(p, os.path.dirname(out_dir))))
    # The mirror: black frames, and frozen while the view moves.
    dark = [i for i in range(len(files)) if mirror_mean[i] < 25]
    if dark:
        rows.append((times[dark[0]], 'mirror black', f'{len(dark)} frames, {times[dark[0]]:.1f}-{times[dark[-1]]:.1f} s', f'mean {mirror_mean[dark[0]]:.0f}', ''))
        finding_png(files, dark[0], MIRROR, os.path.join(out_dir, f'{times[dark[0]]:05.1f}s-mirror-black.png'), 'mirror black')
    frozen = [i for i in range(1, len(mirror_change)) if mirror_change[i] < 0.3 and G[i] > 4]
    if len(frozen) > 5:
        rows.append((times[frozen[0]], 'mirror frozen while the view moves', f'{len(frozen)} frames', '', ''))
    # Judder: gaps in the frame times (frames.csv): a frame interval over 3x the usual one.
    if len(times) > 3:
        dt = np.diff(times); usual = np.median(dt)
        for i in np.where(dt > 3 * max(usual, 0.05))[0][:10]:
            rows.append((times[i], 'judder (frame gap)', f'{dt[i]:.2f} s between frames {i} and {i + 1}', f'usual {usual:.2f} s', ''))
    return sorted(rows)

def run_one(run, ff):
    pre = json.load(open(os.path.join(run, 'preflight.json'))) if os.path.exists(os.path.join(run, 'preflight.json')) else {}
    files = extract(run, ff)
    if not files:
        print(f'{run}: no video'); return
    times, how = frame_times(run, len(files), float(pre.get('drive_seconds', len(files) / FPS)))
    mdir = os.path.join(run, 'motion')
    # 1) Event clips, 1 s before to 1 s after.
    cdir = os.path.join(mdir, 'clips'); os.makedirs(cdir, exist_ok=True)
    clips = []
    for t, name in events(run):
        i = nearest(times, t)
        a, b = max(i - int(FPS), 0), min(i + int(FPS), len(files) - 1)
        stem = f'{t:05.1f}s-' + re.sub(r'[^a-z0-9]+', '-', name.lower()).strip('-')[:40]
        subprocess.run([ff, '-hide_banner', '-loglevel', 'error', '-y', '-framerate', str(FPS), '-start_number', str(a + 1),
                        '-i', os.path.join(mdir, 'all', '%05d.jpg'), '-frames:v', str(b - a + 1), '-pix_fmt', 'yuv420p',
                        os.path.join(cdir, stem + '.mp4')], check=False)
        sheet(files[a:b + 1:2], os.path.join(cdir, stem + '.png'))
        clips.append((t, name, f'clips/{stem}.mp4'))
    # 2) The whole drive at one frame per 0.5 s.
    step = max(1, int(round(0.5 * FPS)))
    picks = files[::step]
    for k in range(0, len(picks), 20):
        sheet(picks[k:k + 20], os.path.join(mdir, f'strip-{k // 20:02d}.png'))
    # 3) The scan.
    rows = scan(files, times, os.path.join(mdir, 'scan'))
    md = [f'# Motion review: {os.path.basename(run)}', '',
          f'{len(files)} video frames; frame times: {how}. Result of the judge: {pre.get("result", "?")} {pre.get("reasons", "")}', '',
          'Watch every clip and every scan finding at real speed (the .mp4), then fill Verdict (real fault / not a fault) and Fault.',
          'A scan candidate is not a fault until at least 8 video frames round it were watched (a near object passing, a turn or a rocking car can look like a pop in 3 frames).', '',
          '## Judge events (clips, 1 s before to 1 s after)', '', '| Time (s) | Event | Clip | Verdict | Fault |', '|---|---|---|---|---|']
    md += [f'| {t:.1f} | {n} | {c} | | |' for t, n, c in clips]
    md += ['', '## Video scan (candidates)', '', '| Time (s) | Kind | Where | Measure | Picture | Verdict | Fault |', '|---|---|---|---|---|---|---|']
    md += [f'| {t:.1f} | {k} | {w} | {m} | {p} | | |' for t, k, w, m, p in rows]
    md += ['', f'## Real-speed strips', '', 'motion/strip-NN.png: one frame each 0.5 s. Look through them all, as a player would see the drive.',
           '', 'Not automated yet: wheel and gauge motion against the car data (the sidecars have no needle reading); watch the cluster in the clips.']
    open(os.path.join(mdir, 'motion.md'), 'w', encoding='utf-8').write('\n'.join(md) + '\n')
    print(f'{run}: {len(clips)} event clips, {len(picks)} strip frames, {len(rows)} scan findings -> {os.path.join(mdir, "motion.md")}')

if __name__ == '__main__':
    ff = ffmpeg()
    for r in sys.argv[1:]:
        run_one(r, ff)
