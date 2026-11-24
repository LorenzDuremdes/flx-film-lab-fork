#pragma once

#include "RawUnpack.h"

// LibRaw supplies the camera metadata. Decode the full-resolution raw IFD's
// tiles and apply its linearization table before black/white normalization.
void unpackJxlDngPixels(const std::string &path, RawMosaic &m, float3 black, float3 scale);
