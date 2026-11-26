"""Build the fork's platform icons and previews from its square camera artwork."""
from pathlib import Path
import subprocess
import sys

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
RESOURCES = ROOT / "FLXFilmLab" / "Resources"
ARTWORK = RESOURCES / "AppIcon.icon" / "Assets" / "artwork.png"


def main() -> None:
    with Image.open(ARTWORK) as source:
        art = source.convert("RGB")
    if art.size != (1024, 1024):
        raise ValueError("The app icon artwork must be 1024 × 1024 pixels")

    art.save(RESOURCES / "AppIcon.icns")
    art.resize((256, 256), Image.Resampling.LANCZOS).save(ROOT / "docs/images/icon.png")

    social = Image.new("RGB", (1280, 640), "#222121")
    social.paste(art.resize((640, 640), Image.Resampling.LANCZOS), (320, 0))
    social.save(ROOT / "tools/icon/social-preview.png")

    subprocess.run([sys.executable, str(ROOT / "windows/tools/pc/make_icon.py")], check=True)


if __name__ == "__main__":
    main()
