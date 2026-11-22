import os
import re
import subprocess
import sys
from pathlib import Path

root = Path.cwd()
sys.path.insert(0, str(root / 'tools'))
import build

bin_dir = root.parent / 'pi32v2-llvm-23.1.6-1-windows-x64' / 'bin'
tools = {
    'cc': ('clang.exe', '--target=pi32v2', '-mcpu=r3'),
    'pi32v2/bin/ld': ('ld.lld.exe',),
    'common/bin/objcopy': ('llvm-objcopy.exe',),
    'common/bin/objdump': ('llvm-objdump.exe',),
}
def run(tool, *args):
    exe, *flags = tools[tool]
    command = [str(bin_dir / exe), *flags, *map(str, args)]
    if tool == 'pi32v2/bin/ld':
        command.extend(str(path) for path in bin_dir.parent.rglob('libclang_rt.builtins*.a'))
    result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', errors='replace')
    if result.returncode:
        build.LAST_ERR = result.stdout + result.stderr
        print(build.LAST_ERR, file=sys.stderr)
        raise SystemExit(f'{tool} failed')
    if result.stderr:
        print(result.stderr, file=sys.stderr)
    return result.stdout

def generated_check():
    required = ('font', 'icons', 'tables', 'samples', 'drumkits', 'fm6')
    if not all((root / 'build/gen' / f'felucca_{name}.h').is_file() for name in required):
        raise SystemExit('Generated headers are missing; run the full build first.')

def normalize_sample_initializer():
    path = root / 'build/gen/felucca_samples.h'
    text = path.read_text(encoding='ascii')
    match = re.search(r'(static const uint8_t SMP_DATA\[(\d+)\] = )\{([^}]+)\};', text)
    if not match:
        return
    payload = bytes(map(int, re.findall(r'\d+', match[3])))
    if len(payload) != int(match[2]):
        raise SystemExit('Unexpected generated sample length')
    parts = ['    "' + ''.join(f'\\x{b:02x}' for b in payload[i:i + 32]) + '"'
             for i in range(0, len(payload), 32)]
    path.write_text(text[:match.start()] + match[1] + '\n' + '\n'.join(parts) +
                    ';' + text[match.end():], encoding='ascii')

upstream_generate = build.generate
def prepare_headers():
    if os.environ.get('SLOOP_REUSE_GENERATED') == '1':
        generated_check()
    else:
        upstream_generate()
    normalize_sample_initializer()

build.generate = prepare_headers
build.tc = run
os.environ['FELUCCA_ICONS'] = '0'
build.VERSION = 'SLOOP 2.8.5 ZGB'
sys.argv = ['build.py', '--sdk', str(root.parent / 'sdk')]
sys.exit(build.main())





