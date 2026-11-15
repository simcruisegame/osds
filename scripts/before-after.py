"""Before and after at the same spot: two runs of the same place (same start, car and seed), the frame
nearest each drive second (or path metre) side by side.

Usage: python scripts/before-after.py <before run> <after run> <out.png> <t1> [<t2> ...] [--path]
  t: drive seconds (default) or, with --path, metres along the path (review/*.json path_m).
Each row: the before frame left, the after frame right, both 960x540, with their file names and times.
"""
import glob, json, os, sys
from PIL import Image, ImageDraw

def frames(run):
    out = []
    for j in glob.glob(os.path.join(run, 'review', '*.json')):
        f = json.load(open(j))
        png = j[:-5] + '.png'
        if os.path.exists(png): out.append((f.get('t', 0.0), f.get('path_m', 0.0), png))
    return out

def nearest(fr, x, by_path):
    k = 1 if by_path else 0
    return min(fr, key=lambda r: abs(r[k] - x)) if fr else None

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    by_path = '--path' in sys.argv
    before, after, out = args[0], args[1], args[2]
    ts = [float(t) for t in args[3:]]
    fb, fa = frames(before), frames(after)
    sheet = Image.new('RGB', (1920, len(ts) * 570), 'black')
    d = ImageDraw.Draw(sheet)
    for k, t in enumerate(ts):
        for col, (fr, label) in enumerate(((fb, 'before'), (fa, 'after'))):
            r = nearest(fr, t, by_path)
            if not r: continue
            sheet.paste(Image.open(r[2]).convert('RGB').resize((960, 540)), (col * 960, k * 570 + 30))
            d.text((col * 960 + 6, k * 570 + 8), f'{label}: {os.path.basename(os.path.dirname(os.path.dirname(r[2])))}/{os.path.basename(r[2])}  t {r[0]:.1f} s  path {r[1]:.0f} m', fill=(255, 255, 0))
    sheet.save(out)
    print(f'{len(ts)} pairs -> {out}')

if __name__ == '__main__':
    main()
