"""Static dependency check of a packaged game: which DLLs each shipped executable and DLL needs, and
whether a clean Windows has them. No admin, no install: it reads the PE import tables.

Usage: python scripts/dep-check.py <package dir (the folder with SimCruise.exe)> [<out.md>]

Each needed DLL is one of:
  shipped        a file of that name is in the package
  windows        part of Windows itself (in System32 and not a redistributable below; api-ms-win-* sets)
  VC++ runtime   msvcp140*, vcruntime140*, concrt140, vccorlib140: NOT part of Windows; the installer must
                 install the Visual C++ 2015-2022 x64 runtime (or ship the DLLs app-local)
  DirectX extra  d3dcompiler_43, d3dx*, xinput1_3, xaudio2_7, x3daudio1_7: the old DirectX June 2010
                 redistributable, NOT part of a clean Windows
  NOT FOUND      neither shipped nor on this PC
A clean Windows here means Windows 10/11 with no runtimes installed: this PC has them, so the check
names them instead of trusting System32.
"""
import os, struct, sys

VC = ('msvcp140', 'vcruntime140', 'concrt140', 'vccorlib140', 'msvcp140_1', 'msvcp140_2', 'vcruntime140_1')
DX_EXTRA = ('d3dcompiler_43', 'd3dx9', 'd3dx10', 'd3dx11', 'xinput1_3', 'xaudio2_7', 'x3daudio1_7', 'xapofx1_5')

def rva_to_off(sections, rva):
    for va, vsize, raw, rsize in sections:
        if va <= rva < va + max(vsize, rsize):
            return rva - va + raw
    return None

def cstr(b, off):
    end = b.index(b'\0', off)
    return b[off:end].decode('ascii', 'replace')

def imports(path):
    """The DLL names in the import and delay-import tables."""
    b = open(path, 'rb').read()
    if b[:2] != b'MZ': return []
    pe = struct.unpack_from('<I', b, 0x3C)[0]
    if b[pe:pe + 4] != b'PE\0\0': return []
    nsec = struct.unpack_from('<H', b, pe + 6)[0]
    optsize = struct.unpack_from('<H', b, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from('<H', b, opt)[0]
    dd = opt + (112 if magic == 0x20B else 96)
    sec = opt + optsize
    sections = []
    for i in range(nsec):
        s = sec + 40 * i
        vsize, va, rsize, raw = struct.unpack_from('<IIII', b, s + 8)
        sections.append((va, vsize, raw, rsize))
    names = []
    for index, stride, name_at in ((1, 20, 12), (13, 32, 4)):     # import table, delay-import table
        rva, size = struct.unpack_from('<II', b, dd + 8 * index)
        off = rva_to_off(sections, rva) if rva else None
        while off is not None and off + stride <= len(b):
            name_rva = struct.unpack_from('<I', b, off + name_at)[0]
            if name_rva == 0: break
            noff = rva_to_off(sections, name_rva)
            if noff is None: break
            names.append((cstr(b, noff), 'delay' if index == 13 else 'load'))
            off += stride
    return names

def classify(dll, shipped):
    low = dll.lower()
    stem = low[:-4] if low.endswith('.dll') else low
    if low in shipped: return 'shipped'
    if stem.startswith(VC): return 'VC++ runtime'
    if stem.startswith(DX_EXTRA): return 'DirectX extra'
    if low.startswith(('api-ms-win-', 'ext-ms-')): return 'windows'
    if os.path.exists(os.path.join(os.environ.get('SystemRoot', r'C:\Windows'), 'System32', dll)): return 'windows'
    return 'NOT FOUND'

def vc_runtime_installed(root, iss):
    """The VC++ runtime counts as covered only when the package carries Microsoft's vc_redist.x64.exe
    (Engine\\Extras\\Redist\\en-us, from package.ps1 -prereqs) AND the installer script runs it silently at
    install (vc_redist.x64.exe with /install and /quiet, in [Run] or in its code). Returns (ok, why)."""
    redist = os.path.join(root, 'Engine', 'Extras', 'Redist', 'en-us', 'vc_redist.x64.exe')
    if not os.path.exists(redist):
        return False, f'no {os.path.relpath(redist, root)} in the package'
    if not iss or not os.path.exists(iss):
        return False, 'no installer script given (--iss=<simcruise.iss>)'
    text = open(iss, encoding='utf-8', errors='replace').read()
    for line in text.splitlines():
        l = line.lower()
        if 'vc_redist.x64.exe' in l and '/install' in l and '/quiet' in l and not l.lstrip().startswith((';', '//')):
            return True, f'{os.path.relpath(redist, root)} shipped; the installer runs it: {line.strip()[:120]}'
    return False, 'the installer script does not run vc_redist.x64.exe /install /quiet'

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--iss=')]
    iss = next((a.split('=', 1)[1] for a in sys.argv[1:] if a.startswith('--iss=')), '')
    root = args[0]
    out = args[1] if len(args) > 1 else os.path.join(root, 'dependencies.md')
    vc_ok, vc_why = vc_runtime_installed(root, iss)
    bins, shipped = [], set()
    for d, _, files in os.walk(root):
        for f in files:
            if f.lower().endswith(('.exe', '.dll')):
                bins.append(os.path.join(d, f)); shipped.add(f.lower())
    need = {}
    for p in bins:
        try:
            for dll, how in imports(p):
                need.setdefault(dll.lower(), (dll, set(), how))[1].add(os.path.relpath(p, root))
        except Exception as e:
            print(f'{p}: not read ({e})')
    rows = sorted(((classify(dll, shipped), dll, how, users) for dll, users, how in need.values()), key=lambda r: (r[0] != 'NOT FOUND', r[0] != 'VC++ runtime', r[0] != 'DirectX extra', r[0], r[1].lower()))
    if vc_ok:
        rows = [('installed by the installer' if r[0] == 'VC++ runtime' else r[0], r[1], r[2], r[3]) for r in rows]
    bad = [r for r in rows if r[0] in ('VC++ runtime', 'DirectX extra', 'NOT FOUND')]
    md = [f'# Dependencies of {root}', '', f'{len(bins)} executables and DLLs read; {len(rows)} DLLs needed. A clean Windows has the "windows" ones; the installer must bring every other one that is not "shipped".', '',
          f'VC++ runtime: {"COVERED" if vc_ok else "NOT COVERED"} ({vc_why}).', '',
          f'**Not on a clean Windows and not shipped: {len(bad)}** ' + (', '.join(sorted({r[1] for r in bad})) if bad else '(none)'), '',
          '| Kind | DLL | Load | Needed by |', '|---|---|---|---|']
    md += [f'| {k} | {dll} | {how} | {", ".join(sorted(u)[:4])}{" ..." if len(u) > 4 else ""} |' for k, dll, how, u in rows if k != 'windows']
    md += ['', f'Windows system DLLs ({sum(1 for r in rows if r[0] == "windows")}): ' + ', '.join(sorted(r[1] for r in rows if r[0] == 'windows'))]
    open(out, 'w', encoding='utf-8').write('\n'.join(md) + '\n')
    print(f'VC++ runtime: {"COVERED" if vc_ok else "NOT COVERED"} ({vc_why})')
    print(f'{len(bins)} binaries, {len(rows)} DLLs, {len(bad)} not shipped and not part of Windows -> {out}')
    for k, dll, how, u in bad: print(f'  {k}: {dll} ({how}) needed by {sorted(u)[0]}{" and more" if len(u) > 1 else ""}')

if __name__ == '__main__':
    main()
