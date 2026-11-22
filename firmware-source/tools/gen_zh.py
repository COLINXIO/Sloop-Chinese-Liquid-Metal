# SPDX-License-Identifier: GPL-3.0-only
"""Compile the screen glossary and bitmap font from an Unifont HEX source."""
import argparse
from bisect import bisect_left
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'firmware/src'
SUBSET = ROOT / 'assets/fonts/sloop-zh-wqy12.hex'
DOTS = 12

def fnv(text):
    h = 2166136261
    for b in text.encode('utf-8'):
        if ord('a') <= b <= ord('z'):
            b -= 32
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h

def glossary():
    rows = {}
    for line in (SRC / 'zh_terms.txt').read_text(encoding='utf-8-sig').splitlines():
        if not line or line.startswith('#'):
            continue
        keys, value = line.rsplit('=', 1)
        for key in keys.split('|'):
            identity = key.strip()
            h = fnv(identity)
            val = (identity.upper(), value.strip())
            if h in rows and rows[h] != val:
                raise ValueError(f'Glossary conflict: {rows[h]} and {val}')
            rows[h] = val
    return rows

def emit_list(kind, name, values, unit, columns):
    return [
        f'static const {kind} {name}[] = {{',
        *('    ' + ', '.join(unit(v) for v in values[i:i + columns]) + ','
          for i in range(0, len(values), columns)),
        '};',
    ]

def parse_hex(path):
    with path.open(encoding='ascii') as data:
        return {int(code, 16): bytes.fromhex(bits)
                for code, bits in (line.strip().split(':') for line in data)}

def font_bits(path, charcodes):
    from PIL import Image, ImageDraw, ImageFont
    font = ImageFont.truetype(str(path), 15)
    bitmaps = {}
    for code in charcodes:
        letter = chr(code)
        left, top, right, bottom = font.getbbox(letter)
        if right - left > DOTS or bottom > DOTS + 1:
            raise ValueError(f'12-pixel font bounds for {code:04X}: {font.getbbox(letter)}')
        image = Image.new('1', (DOTS, DOTS))
        ImageDraw.Draw(image).text((-left, -1), letter, fill=1, font=font)
        bits = 0
        for pixel in image.get_flattened_data():
            bits = (bits << 1) | bool(pixel)
        bitmaps[code] = bits.to_bytes(DOTS * DOTS // 8, 'big')
    return bitmaps

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font', type=Path, help='WenQuanYi Bitmap Song 12px TrueType source')
    args = parser.parse_args()
    rows = glossary()
    texts = sorted({v for _, v in rows.values()})
    charset = set()
    for content in (*texts, *(p.read_text(encoding='utf-8') for p in SRC.glob('*.c'))):
        charset.update(ord(c) for c in content if '\u3000' <= c <= '\u9fff' or '\uff00' <= c <= '\uffef')
    charcodes = sorted(charset)
    bitmaps = font_bits(args.font, charcodes) if args.font else parse_hex(SUBSET)
    subset = [f'{code:04X}:{bitmaps[code].hex().upper()}' for code in charcodes]
    SUBSET.write_text('\n'.join(subset) + '\n', encoding='ascii')
    lines = [
        '/* SPDX-License-Identifier: GPL-2.0-or-later WITH Font-exception-2.0 */',
        '/* WenQuanYi Bitmap Song subset. See assets/fonts/WenQuanYi-LICENSE.txt. */',
        f'#define ZH_DOTS {DOTS}u',
        f'#define ZH_BITMAP_BYTES {DOTS * DOTS // 8}u',
        'static const struct { uint16_t code; uint8_t pixels[ZH_BITMAP_BYTES]; } FONT_ZH[] = {',
    ]
    for code in charcodes:
        bitmap = bitmaps[code]
        if len(bitmap) != DOTS * DOTS // 8:
            raise ValueError(f'Wrong bitmap size for {code:04x}')
        octets = ', '.join(f'0x{octet:02x}' for octet in bitmap)
        lines.append(f'    {{0x{code:04x}, {{{octets}}}}},')
    (SRC / 'font_zh.h').write_text('\n'.join([*lines, '};', '']), encoding='ascii')
    hashes = {h & 0xffffff: value for h, value in rows.items()}
    if len(hashes) != len(rows):
        raise ValueError("Display glossary has a 24-bit hash collision")
    codes = sorted(hashes)
    common = [code for code, _ in Counter(ord(c) for value in texts for c in value if ord(c) >= 128).most_common(96)]
    chars = {code: i for i, code in enumerate(charcodes)}
    short = {code: i + 0x80 for i, code in enumerate(common)}
    def encode(char):
        code = ord(char)
        if code < 128:
            return bytes((code,))
        if code in short:
            return bytes((short[code],))
        index = chars[code]
        return bytes((0xe0 + index // 128, 0x80 + index % 128))
    offsets, blob = {}, bytearray()
    for value in sorted(texts, key=lambda value: (-len(value), value)):
        if value not in offsets:
            for i, char in enumerate(value):
                offsets.setdefault(value[i:], len(blob))
                blob.extend(encode(char))
            blob.append(0)
    assert len(blob) < 8192
    offset_bits = sum(offsets[hashes[h][1]] << (13 * i) for i, h in enumerate(codes))
    packed_offsets = offset_bits.to_bytes((13 * len(codes) + 7) // 8 + 2, 'little')
    locale = ['/* Generated by tools/gen_zh.py. */']
    buckets = [bisect_left(codes, bucket << 20) for bucket in range(17)]
    hash_low = sum((code & 0xfffff) << (20 * i) for i, code in enumerate(codes))
    packed = hash_low.to_bytes((20 * len(codes) + 7) // 8 + 2, 'little')
    locale += emit_list('uint16_t', 'ZH_BUCKETS', buckets, str, 17)
    locale += emit_list('uint8_t', 'ZH_HASHES', packed, str, 18)
    locale += emit_list('uint8_t', 'ZH_OFFSETS', packed_offsets, str, 18)
    locale += emit_list('uint16_t', 'ZH_COMMON', common, lambda x: f'0x{x:04x}', 10)
    locale += emit_list('unsigned char', 'ZH_TEXT', blob, lambda x: f'0x{x:02x}', 16)
    (SRC / 'zh_catalog.h').write_text('\n'.join([*locale, '']), encoding='ascii')
    print(f'Glossary: {len(codes)} keys, {len(texts)} translations, {len(blob) + len(packed) + len(packed_offsets) + 2*(len(common) + len(buckets))} bytes')
    print(f'Font: {len(charcodes)} glyphs, {len(charcodes)*(2 + DOTS*DOTS//8)} bytes')

if __name__ == '__main__':
    main()
