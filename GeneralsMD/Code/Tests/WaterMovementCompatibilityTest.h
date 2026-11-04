/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once
#include "Platform/WaterMovementBehavior.h"
#include "Platform/WaterMovementCompatibility.h"
#include "WaterMovement351Reference.h"
#include <limits>
namespace WaterMovementCompatibilityTest {
inline void Check() {
    // Exact original producer contract includes full terrain padding, edge
    // differences, prevailing cancellation, blur and degenerate dry headings.
    for (int scenario = 0; scenario < 5; ++scenario) {
        const int w = 97, h = 73;
        std::vector<uint8_t> wet(w * h), exposure(w * h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const int i = y * w + x;
                bool water = scenario == 0 || (scenario == 1 ? x < 63
                    : scenario == 2 ? y > 17
                    : scenario == 3 ? std::hypot(x - 48.0, y - 36.0) > 11
                    : (x < 31 || x > 64));
                wet[i] = water ? 255 : 0;
                exposure[i] = uint8_t((x * 17 + y * 31) % 256);
            }
        for (float spacing : {5.0f, 10.0f, 37.0f, 1000.0f,
            std::numeric_limits<float>::min(), std::numeric_limits<float>::max()}) {
            WaterMovementCompatibility::Field actual, repeat;
            WaterMovement351Reference::Field expected;
            CHECK(WaterMovementCompatibility::Build(wet.data(), exposure.data(), w, h, spacing, actual));
            CHECK(WaterMovement351Reference::Build(wet.data(), exposure.data(), w, h, spacing, expected));
            CHECK(actual.prevailing.x == expected.prevailing.x && actual.prevailing.y == expected.prevailing.y);
            CHECK(actual.texels == expected.texels && actual.texels.size() == wet.size());
            CHECK(WaterMovementCompatibility::Rotation(actual.prevailing)
                == WaterMovement351Reference::Rotation(expected.prevailing));
            CHECK(WaterMovementCompatibility::Build(wet.data(), exposure.data(), w, h, spacing, repeat));
            CHECK(actual.texels == repeat.texels);
        }
    }
    using WaterMovementBehavior::HeadingParameter;
    CHECK(HeadingParameter(false, false, .125f) == 0 && HeadingParameter(false, true, .125f) == 0);
    CHECK(HeadingParameter(true, false, .125f) == 1.125f && HeadingParameter(true, true, .125f) == -1);
}
}
