#include "LinearDngJxl.h"

#include "io/FileReader.h"
#include "parsers/TiffParser.h"
#include "tiff/TiffEntry.h"
#include "tiff/TiffTag.h"
#include <jxl/decode.h>

#include <algorithm>
#include <memory>

namespace {
using namespace rawspeed;
using Tag = TiffTag;

const TiffIFD &rawDirectory(const TiffRootIFD &root, const RawMosaic &m)
{
    for (const TiffIFD *ifd : root.getIFDsWithTag(Tag::PHOTOMETRICINTERPRETATION)) {
        if (ifd->getEntry(Tag::PHOTOMETRICINTERPRETATION)->getU32() == 34892
            && ifd->getEntry(Tag::COMPRESSION)->getU32() == 52546
            && ifd->getEntry(Tag::IMAGEWIDTH)->getU32() == unsigned(m.width)
            && ifd->getEntry(Tag::IMAGELENGTH)->getU32() == unsigned(m.height)
            && (!ifd->hasEntry(Tag::NEWSUBFILETYPE) || ifd->getEntry(Tag::NEWSUBFILETYPE)->getU32() == 0))
            return *ifd;
    }
    throw std::runtime_error("No full-resolution JPEG XL linear DNG image.");
}

void check(JxlDecoderStatus status)
{
    if (status != JXL_DEC_SUCCESS)
        throw std::runtime_error("Invalid JPEG XL tile in linear DNG.");
}

struct Tile {
    unsigned width, height;
    std::vector<uint16_t> rgb;
};

Tile decodeTile(Buffer compressed, unsigned width, unsigned height, unsigned usedWidth, unsigned usedHeight)
{
    std::unique_ptr<JxlDecoder, decltype(&JxlDecoderDestroy)> dec(JxlDecoderCreate(nullptr), JxlDecoderDestroy);
    if (!dec)
        throw std::bad_alloc();
    check(JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE));
    check(JxlDecoderSetKeepOrientation(dec.get(), JXL_TRUE));
    check(JxlDecoderSetInput(dec.get(), compressed.begin(), compressed.getSize()));
    JxlDecoderCloseInput(dec.get());
    if (JxlDecoderProcessInput(dec.get()) != JXL_DEC_BASIC_INFO)
        throw std::runtime_error("Missing JPEG XL tile header.");
    JxlBasicInfo info{};
    check(JxlDecoderGetBasicInfo(dec.get(), &info));
    if ((info.xsize != width && info.xsize != usedWidth) || (info.ysize != height && info.ysize != usedHeight)
        || info.num_color_channels != 3 || info.num_extra_channels || info.have_animation
        || info.exponent_bits_per_sample || info.bits_per_sample > 16)
        throw std::runtime_error("Unsupported JPEG XL DNG tile dimensions or sample format.");
    if (JxlDecoderProcessInput(dec.get()) != JXL_DEC_NEED_IMAGE_OUT_BUFFER)
        throw std::runtime_error("Missing JPEG XL tile pixels.");
    Tile tile{info.xsize, info.ysize, std::vector<uint16_t>(size_t(info.xsize) * info.ysize * 3)};
    const JxlPixelFormat format{3, JXL_TYPE_UINT16, JXL_NATIVE_ENDIAN, 0};
    check(JxlDecoderSetImageOutBuffer(dec.get(), &format, tile.rgb.data(), tile.rgb.size() * sizeof(uint16_t)));
    const JxlBitDepth depth{JXL_BIT_DEPTH_FROM_CODESTREAM, 0, 0};
    check(JxlDecoderSetImageOutBitDepth(dec.get(), &depth));
    if (JxlDecoderProcessInput(dec.get()) != JXL_DEC_FULL_IMAGE)
        throw std::runtime_error("Incomplete JPEG XL DNG tile.");
    return tile;
}
} // namespace

void unpackJxlDngPixels(const std::string &path, RawMosaic &m, float3 black, float3 scale)
{
    using namespace rawspeed;
    const auto file = FileReader(path.c_str()).readFile();
    const auto root = TiffParser::parse(nullptr, file.second);
    const auto &ifd = rawDirectory(*root, m);
    const bool tiled = ifd.hasEntry(Tag::TILEOFFSETS);
    const auto *offsets = ifd.getEntry(tiled ? Tag::TILEOFFSETS : Tag::STRIPOFFSETS);
    const auto *counts = ifd.getEntry(tiled ? Tag::TILEBYTECOUNTS : Tag::STRIPBYTECOUNTS);
    const unsigned w = m.width, h = m.height;
    const unsigned tw = tiled ? ifd.getEntry(Tag::TILEWIDTH)->getU32() : w;
    const unsigned th = tiled ? ifd.getEntry(Tag::TILELENGTH)->getU32() :
                        ifd.hasEntry(Tag::ROWSPERSTRIP) ? std::min(h, ifd.getEntry(Tag::ROWSPERSTRIP)->getU32()) : h;
    if (!tw || !th || tw > 65535 || th > 65535 || uint64_t(tw) * th > (1u << 28)
        || (ifd.hasEntry(Tag::PLANARCONFIGURATION) && ifd.getEntry(Tag::PLANARCONFIGURATION)->getU32() != 1))
        throw std::runtime_error("Unsupported JPEG XL DNG tile layout.");
    const unsigned cols = 1 + (w - 1) / tw, rows = 1 + (h - 1) / th;
    if (uint64_t(cols) * rows != offsets->count || counts->count != offsets->count)
        throw std::runtime_error("Incomplete JPEG XL DNG tile directory.");
    std::vector<uint16_t> curve;
    if (ifd.hasEntry(Tag::LINEARIZATIONTABLE)) {
        const auto *table = ifd.getEntry(Tag::LINEARIZATIONTABLE);
        if (!table->count || table->count > 65536)
            throw std::runtime_error("Invalid DNG linearization table.");
        curve = table->getU16Array(table->count);
    }
    m.linearRGB.resize(size_t(w) * h);
    for (unsigned i = 0; i < offsets->count; ++i) {
        const unsigned x0 = (i % cols) * tw, y0 = (i / cols) * th;
        const unsigned nx = std::min(tw, w - x0), ny = std::min(th, h - y0);
        const auto tile = decodeTile(file.second.getSubView(offsets->getU32(i), counts->getU32(i)), tw, th, nx, ny);
        for (unsigned y = 0; y < ny; ++y) {
            const uint16_t *src = tile.rgb.data() + size_t(y) * tile.width * 3;
            for (unsigned x = 0; x < nx; ++x, src += 3) {
                float3 pixel{float(src[0]), float(src[1]), float(src[2])};
                if (!curve.empty())
                    for (int c = 0; c < 3; ++c)
                        pixel[c] = curve[std::min<size_t>(src[c], curve.size() - 1)];
                m.linearRGB[size_t(y0 + y) * w + x0 + x] = (pixel - black) * scale;
            }
        }
    }
}
