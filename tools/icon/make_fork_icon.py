"""Build the fork's platform icons and previews from its square camera artwork."""
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
RESOURCES = ROOT / "FLXFilmLab" / "Resources"
ARTWORK = RESOURCES / "AppIcon.icon" / "Assets" / "artwork.png"
SIZE = (1024, 1024)
MAC_BACKGROUND = "#56565a"  # Match the fill in AppIcon.icon/icon.json.


def main() -> None:
    with Image.open(ARTWORK) as source:
        art = source.convert("RGBA")
    if art.size != SIZE:
        raise ValueError("The app icon artwork must be 1024 × 1024 pixels")

    flat_icon = Image.new("RGBA", SIZE, MAC_BACKGROUND)
    flat_icon.alpha_composite(art)
    flat_icon.convert("RGB").save(RESOURCES / "AppIcon.icns")

    art.resize((256, 256), Image.Resampling.LANCZOS).save(ROOT / "docs/images/icon.png")
    social = Image.new("RGBA", (1280, 640), MAC_BACKGROUND)
    social.alpha_composite(art.resize((640, 640), Image.Resampling.LANCZOS), (320, 0))
    social.convert("RGB").save(ROOT / "tools/icon/social-preview.png")

    subprocess.run([sys.executable, str(ROOT / "windows/tools/pc/make_icon.py")], check=True)


if __name__ == "__main__":
    main()
