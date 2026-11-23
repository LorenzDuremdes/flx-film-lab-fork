"""Draw the DNG fork's exposure-step icon and the platform assets."""
from pathlib import Path
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[2]
art = Image.new("RGB", (1024, 1024), "#15242c")
pen = ImageDraw.Draw(art)
for i, colour in enumerate(("#3b6066", "#5d8c8b", "#98bbae", "#dfe8c9")):
    left = 166 + 174 * i
    pen.rectangle((left, 608 - 88 * i, left + 145, 753), fill=colour)
art.save(root / "FLXFilmLab/Resources/AppIcon.icns")
art.save(root / "FLXFilmLab/Resources/AppIcon.icon/Assets/artwork.png")
art.resize((256, 256), Image.Resampling.LANCZOS).save(root / "docs/images/icon.png")
art.save(root / "windows/app/flx.ico", sizes=[(n, n) for n in (16, 24, 32, 48, 64, 128, 256)])
social = Image.new("RGB", (1280, 640), "#15242c")
social.paste(art.resize((640, 640), Image.Resampling.LANCZOS), (320, 0))
social.save(root / "tools/icon/social-preview.png")
