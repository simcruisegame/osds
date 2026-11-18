"""Renames the code name SimCruise to OSDS across a checkout (text, file and folder names).

The same rules on every branch, so a branch made before the rename can run this script and then
merge with main without conflicts:
  python scripts/rename-to-osds.py [--dry-run]

Rules (in this order):
  - kept as history: lines that say the game was called SimCruise ("called SimCruise",
    "SimCruise up to", "was SimCruise", "formerly SimCruise"), and the GitHub account simcruisegame
    (only the repository is renamed: simcruisegame/simcruise -> simcruisegame/osds);
  - the Unreal module and project: /Script/SimCruise, Source/SimCruise, SimCruise.uproject,
    "SimCruise" as a module or target name -> OSDS;
  - inside identifiers and file names (ASimCruiseVehicle, SimCruiseGraphics.cpp, -SimCruiseStart):
    SimCruise -> Osds;
  - the product name on its own: SimCruise -> OSDS; SIMCRUISE -> OSDS; simcruise -> osds.
Generated Unreal assets under Content (the materials the SimCruiseBuildWorldMaterials commandlet
saves) are deleted, not renamed: the commandlet writes them again under their new names.
"""
import os
import re
import subprocess
import sys

DRY = '--dry-run' in sys.argv
ROOT = subprocess.check_output(['git', 'rev-parse', '--show-toplevel'], text=True).strip()
TEXT_EXT = {'.cpp', '.h', '.cs', '.ini', '.md', '.txt', '.json', '.ps1', '.py', '.mjs', '.js', '.ts', '.iss', '.uproject', '.html',
            '.xml', '.yml', '.yaml', '.toml', '.bat', '.sh', '.css', '.svg', '.gitignore', '.gitattributes', '.csv', '.cfg', '.nsi', ''}
KEEP_LINE = re.compile(r'called SimCruise|SimCruise up to|was SimCruise|formerly SimCruise|SimCruise \(the old|code name,? SimCruise', re.I)

MODULE_FIXED = [
    ('/Script/SimCruise.', '/Script/OSDS.'),
    ('Source/SimCruise/', 'Source/OSDS/'), ('Source\\SimCruise\\', 'Source\\OSDS\\'),
    ('Source/SimCruise', 'Source/OSDS'), ('Source\\SimCruise', 'Source\\OSDS'),
    ('SimCruise.uproject', 'OSDS.uproject'),
    ('SimCruiseEditor.Target', 'OSDSEditor.Target'), ('SimCruise.Target', 'OSDS.Target'), ('SimCruise.Build', 'OSDS.Build'),
    ('SimCruiseEditor', 'OSDSEditor'),
    ('UnrealEditor-SimCruise', 'UnrealEditor-OSDS'), ('SimCruise-Win64-', 'OSDS-Win64-'),
    ('"SimCruise"', '"OSDS"'), ("'SimCruise'", "'OSDS'"),
    ('SimCruiseTarget', 'OSDSTarget'), ('SimCruiseEditorTarget', 'OSDSEditorTarget'),
    ('class SimCruise :', 'class OSDS :'), ('public SimCruise(', 'public OSDS('),
    ('IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, SimCruise,', 'IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, OSDS,'),
    ('Windows\\SimCruise\\', 'Windows\\OSDS\\'), ('Windows/SimCruise/', 'Windows/OSDS/'),
    ('ProjectName=SimCruise', 'ProjectName=OSDS'),
]


def rename_text(s):
    out = []
    for line in s.split('\n'):
        if KEEP_LINE.search(line):
            out.append(line)
            continue
        line = line.replace('simcruisegame/simcruise', 'simcruisegame/osds').replace('simcruisegame.github.io/simcruise', 'simcruisegame.github.io/osds')
        line = line.replace('simcruisegame', '\x00ACCOUNT\x00')
        for a, b in MODULE_FIXED:
            line = line.replace(a, b)
        line = re.sub(r'SimCruise(?=[A-Za-z0-9_])', 'Osds', line)
        line = re.sub(r'(?<![A-Za-z0-9_])Simcruise', 'OSDS', line)
        line = line.replace('SimCruise', 'OSDS').replace('SIMCRUISE', 'OSDS')
        line = re.sub(r'simcruise(?=[A-Za-z0-9_])', 'osds', line)
        line = line.replace('simcruise', 'osds')
        line = line.replace('\x00ACCOUNT\x00', 'simcruisegame')
        out.append(line)
    return '\n'.join(out)


def rename_path_part(p):
    if p == 'SimCruise':
        return 'OSDS'
    for a, b in [('SimCruiseEditor.Target', 'OSDSEditor.Target'), ('SimCruise.Target', 'OSDS.Target'), ('SimCruise.Build', 'OSDS.Build'),
                 ('SimCruise.uproject', 'OSDS.uproject')]:
        if p == a + p[len(a):] and p.startswith(a):
            return b + p[len(a):]
    p = re.sub(r'SimCruise(?=[A-Za-z0-9_])', 'Osds', p)
    p = p.replace('SimCruise', 'OSDS')
    p = re.sub(r'simcruise', 'osds', p)
    return p


def main():
    os.chdir(ROOT)
    files = subprocess.check_output(['git', 'ls-files'], text=True).split('\n')
    changed = 0
    for f in files:
        if not f or f.startswith('Content/Carla'):
            continue
        if f.startswith('Content/') and f.endswith('.uasset') and re.search(r'/(M|LF|T|DA)_SimCruise', f):
            if not DRY:
                subprocess.check_call(['git', 'rm', '-q', f])
            changed += 1
            continue
        ext = os.path.splitext(f)[1].lower()
        if ext in TEXT_EXT and os.path.isfile(f):
            try:
                s = open(f, encoding='utf-8').read()
            except UnicodeDecodeError:
                continue
            t = rename_text(s)
            if t != s:
                changed += 1
                if not DRY:
                    open(f, 'w', encoding='utf-8', newline='').write(t)
    # Paths: deepest first, so a folder is renamed after what is inside it.
    files = [f for f in subprocess.check_output(['git', 'ls-files'], text=True).split('\n') if f and not f.startswith('Content/Carla')]
    moves = {}
    for f in files:
        parts = f.split('/')
        new = '/'.join(rename_path_part(x) for x in parts)
        if new != f:
            moves[f] = new
    for old, new in sorted(moves.items()):
        if DRY:
            continue
        os.makedirs(os.path.dirname(new) or '.', exist_ok=True)
        subprocess.check_call(['git', 'mv', '-k', old, new])
    # Remove emptied folders.
    if not DRY:
        for dirpath, dirnames, filenames in sorted(os.walk(ROOT), key=lambda x: -len(x[0])):
            if '.git' in dirpath or 'Content' + os.sep + 'Carla' in dirpath:
                continue
            if not dirnames and not filenames and 'SimCruise' in dirpath.replace(ROOT, ''):
                os.rmdir(dirpath)
    print(f'{changed} files rewritten or removed, {len(moves)} paths renamed{" (dry run)" if DRY else ""}')


if __name__ == '__main__':
    main()
