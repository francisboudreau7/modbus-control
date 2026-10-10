"""Packs PNG images into a Windows .ico file (PNG-compressed entries, Vista and later).

Usage: python pack_ico.py <out.ico> <in1.png> [<in2.png> ...]
Each PNG must be square, at most 256 px. Used by tools/make_icons.ps1.
"""
import struct
import sys


def png_size(data):
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('not a PNG')
    width, height = struct.unpack('>II', data[16:24])
    return width, height


def main():
    out, inputs = sys.argv[1], sys.argv[2:]
    images = []
    for path in inputs:
        with open(path, 'rb') as f:
            data = f.read()
        w, h = png_size(data)
        if w != h or w > 256:
            raise ValueError(f'{path}: {w}x{h} is not a square of at most 256 px')
        images.append((w, data))
    images.sort(key=lambda item: item[0])

    header = struct.pack('<HHH', 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries, blobs = b'', b''
    for size, data in images:
        dim = 0 if size == 256 else size  # 0 means 256 in the ICO directory
        entries += struct.pack('<BBBBHHII', dim, dim, 0, 0, 1, 32, len(data), offset)
        blobs += data
        offset += len(data)
    with open(out, 'wb') as f:
        f.write(header + entries + blobs)


if __name__ == '__main__':
    main()
