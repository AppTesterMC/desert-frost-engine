#!/usr/bin/env python3
"""Compare the floppy prologue's eight complete RGB caption strips with original DOS captures."""
import argparse
from pathlib import Path

from PIL import Image, ImageChops

from check_floppy_prologue import PAGES


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path, help="original DOS checkpoints p00.png through p07.png")
    parser.add_argument("engine", type=Path, help="engine checkpoints prologue-stars.png through prologue-night.png")
    args = parser.parse_args()
    failed = False
    for number, page in enumerate(PAGES[:-1]):  # The ninth card (palace dawn) has no caption.
        with Image.open(args.original / f"p{number:02}.png") as original, \
                Image.open(args.engine / f"prologue-{page}.png") as engine:
            if original.size != (320, 200) or engine.size != original.size:
                parser.error(f"{page}: supply native 320x200 frames")
            strip = (0, 152, 320, 200)
            reference = original.convert("RGB").crop(strip)
            if len(reference.getcolors(320 * 48)) < 2:
                parser.error(f"{page}: the original frame has a blank caption strip")
            difference = ImageChops.difference(reference, engine.convert("RGB").crop(strip))
            pixels = sum(count for count, color in difference.getcolors(320 * 48) if color != (0, 0, 0))
            failed |= pixels != 0
            print(f"{page}: {pixels} differing RGB pixels")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
