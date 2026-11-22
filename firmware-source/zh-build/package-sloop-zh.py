import binascii
import hashlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path.cwd() / 'tools'))
import fm1_install
import fm1pkg_make

fm1_install._crc16 = lambda data, initial=0: binascii.crc_hqx(data, initial)
fm1pkg_make.crc16 = lambda data, initial=0: binascii.crc_hqx(data, initial)

RELEASE_SHA256 = 'b0446effb840740a3aa5c386ce004e79334e4f833d48d82a9eb5edbfde92ae67'

def files(package):
    identity, image = fm1_install.load_package(package, force=False)
    count = int.from_bytes(fm1_install._dec(image[:64])[8:10], 'little')
    entries = {}
    for start in range(64, 64 + count * 80, 80):
        entry = fm1_install._dec(image[start:start + 80])
        name = entry[64:80].split(b'\0', maxsplit=1)[0].decode('ascii')
        offset = int.from_bytes(entry[8:12], 'little')
        size = int.from_bytes(entry[12:16], 'little')
        entries[name] = image[offset:offset + size]
    return identity, entries

def main():
    original, generated, output = map(Path, sys.argv[1:])
    if hashlib.sha256(original.read_bytes()).hexdigest() != RELEASE_SHA256:
        raise SystemExit('Unexpected upstream release package')
    original_id, original_files = files(original)
    generated_id, generated_files = files(generated)
    if original_id != generated_id:
        raise SystemExit('Product identity changed')
    flash = generated_files['flash.bin']
    old_flash = original_files['flash.bin']
    if len(flash) != len(old_flash) or flash[:0x4000] != old_flash[:0x4000]:
        raise SystemExit('Boot region or flash image size changed')
    release_loader = original_files['ota.bin']
    output.write_bytes(fm1pkg_make.ufw(flash, release_loader, original_id))
    if files(output) != (original_id, {'flash.bin': flash, 'ota.bin': release_loader}):
        raise SystemExit('Packaged data differs from the verified input')
    print('Package CRCs checked; FM-1 identity and boot region match upstream; upstream OTA loader retained.')
    print(f'{output}: {hashlib.sha256(output.read_bytes()).hexdigest()}')

if __name__ == '__main__':
    main()

