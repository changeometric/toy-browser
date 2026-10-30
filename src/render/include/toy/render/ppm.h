#pragma once

#include <string>

#include "toy/render/surface.h"

namespace toy::render {

// Writes the surface as a binary PPM (P6, maxval 255), dropping the alpha channel.
// Returns true on success and false when the file cannot be written.
bool WritePpm(const Surface& surface, const std::string& path);

}  // namespace toy::render
