"""Write data/carla/props.json: the CARLA models OSDS places by rule, by kind and climate band.

Scans a CARLA content checkout (default D:/sc/carla-content) for named static meshes; levels of detail
(_Lod) and trunk-only parts (Base) are left out. Bands: north, south, wet, dry, tropic
(OsdsCarlaCatalog::BandAt). Run: python scripts/carla-catalog.py [checkout]
"""
import json, os, re, sys

root = sys.argv[1] if len(sys.argv) > 1 else 'D:/sc/carla-content'
static = os.path.join(root, 'Static')
ALL = ['north', 'south', 'wet', 'dry', 'tropic']
RULES = [
    # (regex on the asset name, kind, bands, cull m, shadow, collide, min scale, max scale)
    (r'^SM_GrassCluster\d+_[A-Z]$', 'grass', ['north', 'south', 'wet', 'tropic'], 60, False, False, 0.8, 1.2),
    (r'^SM_Grass0[1-5]_[A-Z]$', 'grass', ['north', 'south', 'wet', 'tropic'], 60, False, False, 0.8, 1.2),
    (r'^SM_SmallGrassv\d$', 'grass', ALL, 50, False, False, 0.8, 1.2),
    (r'^SM_Rushes$', 'grass', ['wet', 'south'], 60, False, False, 0.8, 1.1),
    (r'^SM_Bush0[1-6](_[A-Z])?$', 'bush', ['north', 'south', 'wet'], 150, True, False, 0.8, 1.2),
    (r'^SM_Bush03_[LM]_[A-Z]$', 'bush', ['north', 'south', 'wet'], 150, True, False, 0.8, 1.2),
    (r'^SM_BushOleander_\d+$', 'bush', ['south', 'tropic', 'dry'], 150, True, False, 0.8, 1.2),
    (r'^SM_Phormium_\d+$', 'bush', ['tropic', 'wet'], 120, True, False, 0.8, 1.2),
    (r'^SM_PineBush01$', 'bush', ['dry', 'north'], 150, True, False, 0.8, 1.2),
    (r'^SM_WillowBush01$', 'bush', ['wet', 'north'], 150, True, False, 0.8, 1.2),
    (r'^SM_CypressBush01$', 'bush', ['south'], 150, True, False, 0.8, 1.2),
]
# Only models that load with all their materials (OSDS.World.CarlaAssets writes Saved/CarlaAssets.txt).
complete = None
if os.path.exists('Saved/CarlaAssets.txt'):
    complete = {l.split()[1] for l in open('Saved/CarlaAssets.txt', encoding='utf-8', errors='replace') if l.startswith('OK')}
entries = []
for dirpath, _, files in os.walk(os.path.join(static, 'Vegetation')):
    for f in sorted(files):
        if not f.endswith('.uasset'):
            continue
        name = f[:-7]
        if '_Lod' in name or 'LOD' in name:
            continue
        for pat, kind, bands, cull, shadow, collide, lo, hi in RULES:
            if re.match(pat, name):
                rel = os.path.relpath(os.path.join(dirpath, name), static).replace(os.sep, '/')
                if complete is not None and f'/Game/Carla/Static/{rel}.{name}' not in complete:
                    break
                entries.append({'key': name, 'path': f'/Game/Carla/Static/{rel}.{name}', 'kind': kind, 'bands': bands,
                                'cull_m': cull, 'shadow': shadow, 'collide': collide, 'min_scale': lo, 'max_scale': hi})
                break
out = {'source': 'CARLA simulator content (bitbucket.org/carla-simulator/carla-content, ue5-dev), CC-BY 4.0, CARLA Team; fetched by scripts/fetch-carla.ps1',
       'made_by': 'scripts/carla-catalog.py', 'entries': entries}
os.makedirs('data/carla', exist_ok=True)
json.dump(out, open('data/carla/props.json', 'w', encoding='utf-8'), indent=1)
kinds = {}
for e in entries:
    kinds[e['kind']] = kinds.get(e['kind'], 0) + 1
print('entries', len(entries), kinds)
