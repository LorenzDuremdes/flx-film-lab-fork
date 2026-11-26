#include "RawUnpack.h"
#include "LinearDngJxl.h"

#include "Paths.h"

#include <libraw/libraw.h>

#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

[[noreturn]] void fail(const std::string &path, const std::string &why)
{
    throw std::runtime_error(path + ": " + why);
}

// dcraw's flip to EXIF's orientation
int exifOrientation(int flip)
{
    switch (flip) {
    case 3: return 3;               // 180
    case 5: return 8;               // 90 counter-clockwise
    case 6: return 6;               // 90 clockwise
    default: return 1;
    }
}

void readMetadata(const libraw_data_t &d, RawMosaic &m)
{
    m.orientation = exifOrientation(d.sizes.flip);
    m.exposure.iso = d.other.iso_speed;
    if (std::string(d.idata.make).rfind("Fuji", 0) == 0) {
        const auto &f = d.makernotes.fuji;
        if (std::abs(f.ExpoMidPointShift) < 100)
            m.exposure.fujiRawExposureShift = f.ExpoMidPointShift;
        if (f.DevelopmentDynamicRange != 0xffff)
            m.exposure.fujiDynamicRange = f.DevelopmentDynamicRange;
    }
    if (std::string(d.idata.make).rfind("Canon", 0) == 0)
        m.exposure.canonHighlightTonePriority = d.makernotes.canon.HighlightTonePriority;
    if (d.color.dng_levels.baseline_exposure > -999)
        m.exposure.dngBaselineExposure = d.color.dng_levels.baseline_exposure;
    auto trimmed = [](std::string s) {
        while (!s.empty() && s.back() == ' ')
            s.pop_back();
        return s;
    };
    m.lensMaker = trimmed(d.lens.LensMake);
    m.lensModel = trimmed(d.lens.Lens);
    m.focalLength = d.other.focal_len;
    if (m.focalLength > 0 && d.lens.FocalLengthIn35mmFormat > 0)
        m.cropFactor = float(d.lens.FocalLengthIn35mmFormat) / m.focalLength;
    m.photo.shutter = d.other.shutter;
    m.photo.aperture = d.other.aperture;
    m.photo.focalLength35 = d.lens.FocalLengthIn35mmFormat;
    m.photo.minFocal = d.lens.MinFocal;
    m.photo.maxFocal = d.lens.MaxFocal;
    m.photo.maxApertureAtMin = d.lens.MaxAp4MinFocal;
    m.photo.maxApertureAtMax = d.lens.MaxAp4MaxFocal;
    m.photo.taken = static_cast<long long>(d.other.timestamp);
    m.photo.artist = trimmed(d.other.artist);
    m.photo.description = trimmed(d.other.desc);
    const libraw_gps_info_t &g = d.other.parsed_gps;
    if (g.gpsparsed) {
        m.photo.gps = true;
        for (int i = 0; i < 3; ++i) {
            m.photo.latitude[i] = g.latitude[i];
            m.photo.longitude[i] = g.longitude[i];
        }
        m.photo.altitude = g.altitude;
        m.photo.latitudeRef = g.latref;
        m.photo.longitudeRef = g.longref;
        m.photo.altitudeRef = g.altref;
        m.photo.gpsStatus = g.gpsstatus;
    }
}

// The DNG's colour space can differ from the original camera's. Use its
// embedded daylight matrix, including camera calibration and AnalogBalance.
Mat3 linearDngMatrix(const libraw_colordata_t &colour)
{
    for (int light : {int(LIBRAW_WBI_D65), -1}) {
        for (const auto &profile : colour.dng_color) {
            if (light >= 0 && profile.illuminant != light)
                continue;
            Mat3 matrix{}, calibration{};
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) {
                    matrix.at(i, j) = profile.colormatrix[i][j];
                    calibration.at(i, j) = profile.calibration[i][j];
                }
            if (calibration.determinant() != 0)
                matrix = calibration * matrix;
            const float *a = colour.dng_levels.analogbalance;
            matrix = Mat3::diagonal({a[0], a[1], a[2]}) * matrix;
            if (std::abs(matrix.determinant()) > 1e-8f)
                return matrix;
        }
    }
    throw std::runtime_error("The linear DNG has no usable embedded colour matrix.");
}

// PureRAW 6 X-H2 exports with this daylight calibration retain the RAF's
// black-subtracted, white-normalized channels. The native FLX transform
// already characterizes those channels. Other linear DNGs can store RGB
// in a different basis, even when they name the same camera.
bool usesNativeXH2RGB(const libraw_data_t &d, const Mat3 &daylight)
{
    const std::string software = d.idata.software;
    if (d.idata.maker_index != LIBRAW_CAMERAMAKER_Fujifilm || std::string(d.idata.model) != "X-H2"
        || (software != "DxO PureRAW 6" && software.rfind("DxO PureRAW 6.", 0) != 0))
        return false;
    const Mat3 camera = Mat3::rows({1.1809f, -.5358f, -.1141f}, {-.4248f, 1.2164f, .2343f},
                                   {-.0514f, .1097f, .5848f});
    const Mat3 calibrated = Mat3::diagonal({.9375f, 1, 1.0122f}) * camera;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            if (std::abs(daylight.at(r, c) - calibrated.at(r, c)) > .0001f)
                return false;
    return true;
}

RawMosaic unpackLinearDng(const std::string &path, const libraw_data_t &d, bool jpegXL)
{
    RawMosaic m;
    m.decoder = "libraw";
    m.make = d.idata.make;
    m.model = d.idata.model;
    m.width = d.sizes.raw_width;
    m.height = d.sizes.raw_height;
    m.cropLeft = d.sizes.left_margin;
    m.cropTop = d.sizes.top_margin;
    m.cropWidth = d.sizes.width;
    m.cropHeight = d.sizes.height;
    const auto &crop = d.sizes.raw_inset_crops[0];
    if (crop.cwidth && crop.cheight) {
        m.cropLeft = crop.cleft;
        m.cropTop = crop.ctop;
        m.cropWidth = crop.cwidth;
        m.cropHeight = crop.cheight;
    }
    m.cfaSize = 0;
    m.xyzToCamera = linearDngMatrix(d.color);
    m.nativeCameraRGB = usesNativeXH2RGB(d, m.xyzToCamera);
    const float *wb = d.color.cam_mul;
    if (wb[0] > 0 && wb[1] > 0 && wb[2] > 0)
        m.asShotNeutral = {wb[1] / wb[0], 1, wb[1] / wb[2]};
    readMetadata(d, m);

    float3 black, scale;
    const auto &levels = d.color.dng_levels;
    for (int c = 0; c < 3; ++c) {
        black[c] = float(d.color.black) + float(d.color.cblack[c]);
        const float white = float(levels.dng_whitelevel[c] ? levels.dng_whitelevel[c] : d.color.maximum);
        if (white <= black[c])
            throw std::runtime_error("Invalid linear DNG black or white level.");
        scale[c] = 1 / (white - black[c]);
    }
    if (jpegXL) {
        unpackJxlDngPixels(path, m, black, scale);
        return m;
    }
    const auto &raw = d.rawdata;
    const int channels = raw.color4_image ? 4 : 3;
    const uint16_t *pixels = raw.color4_image ? raw.color4_image[0] : raw.color3_image[0];
    const size_t stride = d.sizes.raw_pitch / sizeof(uint16_t);
    if (stride < size_t(m.width) * channels)
        throw std::runtime_error("Invalid linear DNG row size.");
    m.linearRGB.resize(size_t(m.width) * m.height);
    for (int y = 0; y < m.height; ++y) {
        const uint16_t *p = pixels + size_t(y) * stride;
        for (int x = 0; x < m.width; ++x, p += channels)
            m.linearRGB[size_t(y) * m.width + x] = (float3{float(p[0]), float(p[1]), float(p[2])} - black) * scale;
    }
    return m;
}

} // namespace

void readMetadataWithLibRaw(const std::string &path, RawMosaic &m)
{
    auto raw = std::make_unique<LibRaw>();
    if (raw->open_file(fsPath(path).c_str()) == LIBRAW_SUCCESS)
        readMetadata(raw->imgdata, m);
}

long long captureTimeWithLibRaw(const std::string &path)
{
    auto raw = std::make_unique<LibRaw>();
    if (raw->open_file(fsPath(path).c_str()) != LIBRAW_SUCCESS)
        return 0;
    return static_cast<long long>(raw->imgdata.other.timestamp);
}

// Formats LibRaw identifies that none of the decoders here read, refused
// before unpacking with what to do instead: Nikon's High Efficiency RAW (HE,
// HE*), coded in JPEG XS, which no open decoder reads yet (its compression,
// 13 or 14, rather than LibRaw's list of models, so newer cameras count
// too); and Sony's lossless compressed RAW at M and S size, which the camera
// stores as YCbCr colour, not as the sensor's mosaic.
void refuseUnreadable(LibRaw &raw)
{
    libraw_decoder_info_t info{};
    raw.get_decoder_info(&info);
    const std::string decoder = info.decoder_name ? info.decoder_name : "";
    const unsigned nef = raw.imgdata.makernotes.nikon.NEFCompression;
    if (decoder == "nikon_he_load_raw()" || nef == 13 || nef == 14)
        throw UnreadableFormat(
            "The Windows app cannot read Nikon's High Efficiency RAW (HE or HE*) yet: none of its decoders "
            "reads that format. Shoot Lossless compressed RAW, or convert the file to DNG with Adobe DNG Converter "
            "(free), which the app opens.");
    if (decoder == "sony_ycbcr_load_raw()")
        throw UnreadableFormat(
            "The Windows app cannot read Sony's lossless compressed RAW at M or S size: the camera stores those as "
            "colour pixels, not as the sensor's mosaic. The L size opens, as do uncompressed and compressed RAW.");
}

RawMosaic unpackWithLibRaw(const std::string &path)
{
    auto raw = std::make_unique<LibRaw>();
    if (const int e = raw->open_file(fsPath(path).c_str()); e != LIBRAW_SUCCESS)
        fail(path, libraw_strerror(e));
    const bool linearDng = raw->imgdata.idata.dng_version && !raw->imgdata.idata.filters && raw->imgdata.idata.colors == 3;
    if (!linearDng)
        refuseUnreadable(*raw);
    else if (raw->get_internal_data_pointer()->unpacker_data.tiff_compress == 52546)
        return unpackLinearDng(path, raw->imgdata, true);
    if (const int e = raw->unpack(); e != LIBRAW_SUCCESS)
        fail(path, libraw_strerror(e));
    const libraw_data_t &d = raw->imgdata;
    if (linearDng && (d.rawdata.color3_image || d.rawdata.color4_image))
        return unpackLinearDng(path, d, false);
    if (!d.rawdata.raw_image)
        fail(path, "not a single-plane mosaic (LibRaw reads it, the port does not yet)");
    if (d.idata.filters == 0 || d.idata.colors != 3)
        fail(path, "not a Bayer or X-Trans mosaic (not handled yet)");

    RawMosaic m;
    m.decoder = "libraw";
    m.make = d.idata.make;
    m.model = d.idata.model;
    m.width = d.sizes.raw_width;
    m.height = d.sizes.raw_height;
    m.data.resize(size_t(m.width) * m.height);
    const int pitch = d.sizes.raw_pitch / 2;
    for (int y = 0; y < m.height; ++y)
        for (int x = 0; x < m.width; ++x)
            m.data[size_t(y) * m.width + x] = d.rawdata.raw_image[size_t(y) * pitch + x];

    m.cropLeft = d.sizes.left_margin;
    m.cropTop = d.sizes.top_margin;
    m.cropWidth = d.sizes.width;
    m.cropHeight = d.sizes.height;
    // LibRaw's COLOR() is relative to the image area; the pattern here is
    // the mosaic's, from its origin. Its second green (3) is green. X-Trans
    // (filters 9) has its own, from the origin already.
    if (d.idata.filters == 9) {
        m.cfaSize = 6;
        for (int y = 0; y < 6; ++y)
            for (int x = 0; x < 6; ++x)
                m.cfa[y][x] = d.idata.xtrans_abs[y][x];
    } else {
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x) {
                const int c = raw->COLOR(y - m.cropTop, x - m.cropLeft);
                m.cfa[(y + 2) % 2][(x + 2) % 2] = c == 3 ? 1 : c;
            }
    }
    // black: the overall level, per channel, and per position of a pattern
    const auto &c = d.color;
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
            // X-Trans: one level for all, as Fujifilm's are
            const int ch = m.cfaSize == 6 ? 1 : raw->COLOR(y - m.cropTop, x - m.cropLeft);
            float b = float(c.black) + float(c.cblack[ch]);
            if (c.cblack[4] && c.cblack[5])
                b += float(c.cblack[6 + ((y % c.cblack[4]) * c.cblack[5]) + (x % c.cblack[5])]);
            m.black[y][x] = b;
        }
    m.white = float(c.maximum);
    // LibRaw opens some cameras it has no data for: no colour matrix, and a
    // pattern and levels that are guesses
    float matrix = 0;
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k) {
            m.xyzToCamera.at(r, k) = c.cam_xyz[r][k];
            matrix += std::abs(c.cam_xyz[r][k]);
        }
    if (matrix == 0)
        fail(path, "LibRaw does not know the camera (" + m.make + " " + m.model + ")");
    m.asShotNeutral = {c.cam_mul[1] / std::max(c.cam_mul[0], 1e-6f), 1.0f, c.cam_mul[1] / std::max(c.cam_mul[2], 1e-6f)};
    readMetadata(raw->imgdata, m);
    return m;
}
