# Rewrites the track tables at the end of docs/tracks.md from data/tracks/tracks.json:
#   python scripts/tracks/docs.py
import json, os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MARK = '<!-- The tables below are written by scripts/tracks/docs.py from data/tracks/tracks.json. -->'


def row(t):
    bank = (f"{t['bank_turn_deg']:g}° turns, {t['bank_front_deg']:g}° front, {t['bank_back_deg']:g}° back"
            if 'bank_turn_deg' in t else 'flat (as mapped)')
    facing = f"{t['bearing']:.0f}°" if t.get('bearing') is not None else '-'
    sources = ' · '.join(f"[{'Wikipedia' if 'wikipedia' in s else 'link'}]({s})" if s.startswith('http') else s for s in t['sources'])
    mapped = f"{t['mapped_mi']:.3f}" if t.get('mapped_mi') else '-'
    width = f"{t['width_m']:g}" if t.get('width_m') else 'street'
    return (f"| {t['name']} | `{t['id']}` | {t['place']} | {t['start'][0]:.5f}, {t['start'][1]:.5f} | {facing} | "
            f"{t['length_mi']:g} | {mapped} | {width} | {bank} | {t['note']} | {sources} |")


def main():
    tracks = json.load(open(os.path.join(ROOT, 'data', 'tracks', 'tracks.json'), encoding='utf-8'))['tracks']
    head = ('| Track | Id | Place | Start (lat, lon) | Facing | Length mi (published) | Loop mi (map) | Width m | Banking used | Notes | Sources |\n'
            '|---|---|---|---|---|---|---|---|---|---|---|')
    ovals = [t for t in tracks if t['kind'] == 'oval']
    roads = [t for t in tracks if t['kind'] != 'oval']
    tables = (f"{MARK}\n\n## Oval tracks ({len(ovals)})\n\n{head}\n" + '\n'.join(map(row, ovals)) +
              f"\n\n## Road courses ({len(roads)})\n\nA road course on an oval (the Roval, Indianapolis's) starts on the oval's frontstretch and is banked where it runs on the oval; its loop length is the oval's.\n\n{head}\n" + '\n'.join(map(row, roads)) + '\n')
    path = os.path.join(ROOT, 'docs', 'tracks.md')
    text = open(path, encoding='utf-8').read()
    cut = text.find(MARK)
    if cut < 0:
        cut = text.find('## Oval tracks')
    open(path, 'w', encoding='utf-8', newline='\n').write(text[:cut] + tables)
    print(path, len(ovals), 'ovals,', len(roads), 'road courses')


if __name__ == '__main__':
    main()
