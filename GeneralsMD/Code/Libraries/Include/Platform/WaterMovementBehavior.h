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
namespace WaterMovementBehavior {
// The requested option becomes active only at the map-load callback. Cache,
// style and device invalidations never own or reset this state.
class MapSelection {
    bool current = true;
public:
    void Load(bool requested) {
        current = requested;
    }
    bool Current() const {
        return current;
    }
};
// c191.w is copied into each accepted draw, never read from mutable options by
// the backend. Negative FftParameters.z selects v3.5.1 headed sampling; zero
// retains the ordinary unheaded fallback, positive values select modern charts.
inline bool Compatibility(float requested) {
    return requested > .5f;
}
inline float HeadingParameter(bool heading, bool compatibility, float localStep) {
    return !heading ? 0.0f : compatibility ? -1.0f : 1.0f + localStep;
}
}
