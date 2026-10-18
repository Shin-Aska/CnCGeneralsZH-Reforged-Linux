// Editable palettes and explicit persistence for the isolated cloud preview.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#include <string>

namespace CloudPreviewColors {

// Palette IDs match the volume shader: toxin, nuclear, beta, gamma.
struct Color { float r, g, b; };
struct Palette { Color main, shadow; float brightness; float transparency = 0.f; };

// RGB channels are linear shader values in [0, 1]; brightness is in [0, 2].
// Transparency is in [0, 1]: 0 keeps full smoke density, 1 clears it.
// Invalid edits are rejected without changing the palette. Invalid get IDs
// return toxin. set/reset never write a file or modify any game setting.
const Palette &get(unsigned index);
void set(unsigned index, const Palette &palette);
void reset(unsigned index);
unsigned revision();

// Start with the user-approved preview palettes, then load an existing file.
// An absent file is normal and is not created. A malformed file leaves all
// defaults intact and returns false. The caller owns the user directory.
bool initialize(const std::string &userDir);
bool save(); // Explicit, atomic replacement of cloud-colors.ini.
bool load(); // Transactional reload; version 1 defaults transparency to zero.
std::string exportText(); // Complete reproducible INI suitable for a clipboard.
std::string lastStatus();
std::string savePath();

} // namespace CloudPreviewColors
