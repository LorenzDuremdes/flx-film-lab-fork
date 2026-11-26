"""Run with python linear_dng.py path/to/flxport-decode (numpy, tifffile, imagecodecs)."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
from tempfile import TemporaryDirectory

import numpy as np
import tifffile


def render(decoder, source, output, *options, profiled=False, wb="as-shot"):
    subprocess.run([decoder, source, output, "--white-balance", wb, *options], check=True)
    metadata = json.loads(output.with_suffix(".json").read_text())
    magic, version, w, h, channels, dtype, _, _ = struct.unpack("<4s7I", output.read_bytes()[:32])
    assert (magic, version, channels, dtype) == (b"FLXD", 1, 4, 0)
    assert metadata["linearDng"] and metadata["profiled"] == profiled
    pixels = np.fromfile(output, "<f4", offset=32).reshape(h, w, 4)
    assert np.all(pixels[:, :, 3] == 1)
    return pixels[:, :, :3]


def check(decoder, folder, app):
    # Rec.2020 primaries make the expected scene-linear RGB known independently.
    matrix = (1.716651, -.355671, -.253366, -.666684, 1.616481, .015769, .017640, -.042771, .942103)
    tags = [
        (50706, "B", 4, (1, 7, 0, 0)), (50707, "B", 4, (1, 1, 0, 0)),
        (50708, "s", 0, "FLX test camera"), (50778, "H", 1, 21),
        (50721, "2i", 9, tuple(k for v in matrix for k in (round(v * 1000000), 1000000))),
        (50728, "2I", 3, (1, 1, 1, 1, 1, 1)), (50730, "2i", 1, (1, 1)),
        (50714, "H", 3, (400, 600, 900)), (50717, "I", 3, (20400, 20600, 20900)),
        (274, "H", 1, 6), (50719, "H", 2, (2, 3)), (50720, "H", 2, (88, 56)),
    ]
    y, x = np.mgrid[:64, :96]
    codes = np.stack((40 + x, 60 + y, 90 + (x + y) % 29), axis=-1).astype("uint16")
    curve = np.arange(1024, dtype="uint16") * 10
    linear = curve[codes]
    expected = np.rot90((linear[3:59, 2:90] - np.array([400, 600, 900])) / 10000, k=-1)
    for mode in ("none", "jpeg", "jpegxl_dng"):
        source = folder / (mode + ".dng")
        jxl = mode == "jpegxl_dng"
        extra = [(50712, "H", len(curve), curve)] if jxl else []
        args = {"lossless": True, "bitspersample": 10} if jxl else {}
        if mode == "jpeg":
            args = dict(lossless=True, bitspersample=16, colorspace="RGB", outcolorspace="RGB")
        tifffile.imwrite(source, codes if jxl else linear, photometric=34892, planarconfig="contig",
                         compression=mode, compressionargs=args, tile=(32, 32),
                         metadata=None, extratags=tags + extra)
        output = source.with_suffix(".f32")
        np.testing.assert_allclose(render(decoder, source, output), expected, atol=0.0002, rtol=0)
        preview = render(decoder, source, output, "--size", "44")
        assert preview.shape == (44, 28, 3) and np.isfinite(preview).all()

        if app:
            album = folder / "Étude"
            album.mkdir(exist_ok=True)
            app_source = source.rename(album / source.name)
            print_path = app_source.with_suffix(".tif")
            subprocess.run([app, app_source, "--library", folder / "library", "--export", print_path],
                           check=True, timeout=180)
            with tifffile.TiffFile(print_path) as result:
                page = result.pages[0]
                assert (page.imagewidth, page.imagelength) == (56, 88)
                assert page.bitspersample == 16 and page.samplesperpixel >= 3
                assert page.tags["Orientation"].value == 1
                assert "Edited with FLX Film Lab." in page.description
                assert np.ptp(page.asarray()[:, :, :3]) > 0


def check_xh2(decoder, folder):
    # The same normalized camera RGB should enter the film at the same
    # exposure and D55 transform as the RAF, even with a +2 EV DNG baseline.
    matrix = (11809, -5358, -1141, -4248, 12164, 2343, -514, 1097, 5848)
    calibration = (9375, 0, 0, 0, 10000, 0, 0, 0, 10122)
    def rationals(values):
        return tuple(k for v in values for k in (v, 10000))

    tags = [
        (271, "s", 0, "FUJIFILM"), (272, "s", 0, "X-H2"),
        (50706, "B", 4, (1, 4, 0, 0)), (50708, "s", 0, "Fujifilm X-H2"),
        (50778, "H", 1, 21), (50723, "2i", 9, rationals(calibration)),
        (50730, "2i", 1, (202, 100)), (50717, "I", 3, (65535,) * 3),
    ]
    codes = np.full((64, 96, 3), (5300, 8700, 6200), dtype=np.uint16)
    profiles = json.loads((decoder.parent / "apple-colour.json").read_text(encoding="utf8"))
    d55 = np.array(profiles["cameras"]["Fujifilm|X-H2"]["fixed"]["D55"]["transform"])
    for software, colour, native in [
        ("DxO PureRAW 6", matrix, True),
        ("Another converter", matrix, False),
        ("DxO PureRAW 6", (10000, 0, 0, 0, 10000, 0, 0, 0, 10000), False),
    ]:
        source = folder / "xh2.dng"
        tifffile.imwrite(source, codes, photometric=34892, planarconfig="contig", metadata=None,
                         software=software, extratags=tags + [(50721, "2i", 9, rationals(colour))])
        pixels = render(decoder, source, source.with_suffix(".f32"), profiled=native, wb="film")
        assert pixels.shape == codes.shape
        if native:
            np.testing.assert_allclose(pixels, (codes / 65535) @ d55.T, atol=1e-6, rtol=0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("decoder", type=Path)
    parser.add_argument("--app", type=Path, help="also export each fixture through FLX Stills")
    args = parser.parse_args()
    with TemporaryDirectory() as tmp:
        check(args.decoder.resolve(), Path(tmp), args.app.resolve() if args.app else None)
        check_xh2(args.decoder.resolve(), Path(tmp))
