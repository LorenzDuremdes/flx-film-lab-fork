#!/usr/bin/env python
"""Build the Windows app's icon from the fork's transparent camera artwork."""

from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
# the Mac app's tree: mac/ in the port's own repository, the parent beside
# windows/ in the public one
MAC = ROOT / "mac" if (ROOT / "mac" / "build.sh").exists() else ROOT.parent
ARTWORK = MAC / "FLXFilmLab" / "Resources" / "AppIcon.icon" / "Assets" / "artwork.png"
OUT = ROOT / "app" / "flx.ico"
SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)


def main() -> None:
    with Image.open(ARTWORK) as source:
        art = source.convert("RGBA")
    images = [art.resize((s, s), Image.Resampling.LANCZOS) for s in SIZES]
    images[-1].save(OUT, format="ICO", sizes=[(s, s) for s in SIZES], append_images=images[:-1])
    print(f"wrote {OUT.relative_to(ROOT)}: {', '.join(str(s) for s in SIZES)} px")


if __name__ == "__main__":
    main()
