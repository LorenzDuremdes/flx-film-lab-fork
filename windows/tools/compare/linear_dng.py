"""Run with python linear_dng.py path/to/flxport-decode (numpy, tifffile, imagecodecs)."""
import json
from pathlib import Path
import struct
import subprocess
import sys
from tempfile import TemporaryDirectory

import numpy as np
import tifffile


def render(decoder, source, output, *options):
    subprocess.run([decoder, source, output, "--white-balance", "as-shot", *options], check=True)
    metadata = json.loads(output.with_suffix(".json").read_text())
    magic, version, w, h, channels, dtype, _, _ = struct.unpack("<4s7I", output.read_bytes()[:32])
    assert (magic, version, channels, dtype) == (b"FLXD", 1, 4, 0)
    assert metadata["linearDng"] and not metadata["profiled"]
    pixels = np.fromfile(output, "<f4", offset=32).reshape(h, w, 4)
    assert np.all(pixels[:, :, 3] == 1)
    return pixels[:, :, :3]


def check(decoder, folder):
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


if __name__ == "__main__":
    with TemporaryDirectory() as tmp:
        check(Path(sys.argv[1]).resolve(), Path(tmp))
