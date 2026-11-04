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
// Render-only proof that a triangle cannot sample local waves. No engine state or simulation input.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace WaterOpenSeaSplit {
class Mask {
    static constexpr int DilationCells = 2;
    int width = 0, height = 0; // corner dimensions; the table covers (width-1)*(height-1) cells
    unsigned revision = 0;
    std::vector<size_t> sums;

    static unsigned NextRevision() {
        static unsigned next = 0;
        return ++next;
    }
    static size_t Sum(const std::vector<size_t> &table, int stride, int x0, int y0, int x1, int y1) {
        return table[size_t(y1) * stride + x1] - table[size_t(y0) * stride + x1]
            - table[size_t(y1) * stride + x0] + table[size_t(y0) * stride + x0];
    }
    static int Cell(float p, int size) {
        // Match floor(clamp(p,0,size-1)), capped at size-2, in fftLocalFields.
        return (std::min)(int((std::max)(0.0f, (std::min)(float(size - 1), p))), size - 2);
    }
public:
    Mask() = default;
    Mask(const std::vector<uint32_t> &metadata, int w, int h) {
        if (w < 2 || h < 2 || size_t(w) * h != metadata.size())
            return;
        width = w;
        height = h;
        revision = NextRevision();
        std::vector<size_t> corners(size_t(w + 1) * (h + 1), 0);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                corners[size_t(y + 1) * (w + 1) + x + 1] = (metadata[size_t(y) * w + x] & 255 ? 1 : 0)
                    + corners[size_t(y) * (w + 1) + x + 1] + corners[size_t(y + 1) * (w + 1) + x]
                    - corners[size_t(y) * (w + 1) + x];
        sums.assign(size_t(w) * h, 0);
        for (int y = 0; y < h - 1; ++y)
            for (int x = 0; x < w - 1; ++x) {
                // Any corner in this cell or its two-cell Chebyshev dilation marks it needed.
                const bool needed = Sum(corners, w + 1, (std::max)(0, x - DilationCells), (std::max)(0, y - DilationCells),
                    (std::min)(w, x + (DilationCells + 2)), (std::min)(h, y + (DilationCells + 2))) != 0;
                sums[size_t(y + 1) * w + x + 1] = size_t(needed) + sums[size_t(y) * w + x + 1]
                    + sums[size_t(y + 1) * w + x] - sums[size_t(y) * w + x];
            }
    }
    bool Valid() const {
        return !sums.empty();
    }
    unsigned Revision() const {
        return revision;
    }
    bool Any() const {
        return !Valid() || sums.back() != 0;
    }
    bool Touches(float minGx, float minGy, float maxGx, float maxGy) const {
        if (!Valid() || !std::isfinite(minGx) || !std::isfinite(minGy)
            || !std::isfinite(maxGx) || !std::isfinite(maxGy) || minGx > maxGx || minGy > maxGy)
            return true;
        return Sum(sums, width, Cell(minGx, width), Cell(minGy, height),
            Cell(maxGx, width) + 1, Cell(maxGy, height) + 1) != 0;
    }
};
struct Point {
    float x, y;
};
// A hull's reflected samples: pixels within radius may read up to reach from themselves.
struct Disc {
    float x, y, radius, reach = std::numeric_limits<float>::infinity();
};

namespace Detail {
// Classification is conservative for invalid inputs. Keep the mask proof and
// moving reflection footprints together; partitioning only chooses an output.
template<class Vertex>
inline bool NeedsLocalWaves(const Vertex &a, const Vertex &b, const Vertex &c, const Mask &mask,
    float spacing, float border, const std::vector<Disc> &discs, float margin)
{
    const float minX = (std::min)({a.x, b.x, c.x}) - margin, minY = (std::min)({a.y, b.y, c.y}) - margin;
    const float maxX = (std::max)({a.x, b.x, c.x}) + margin, maxY = (std::max)({a.y, b.y, c.y}) + margin;
    bool needed = !std::isfinite(spacing) || spacing <= 0 || !std::isfinite(border)
        || !std::isfinite(margin) || margin < 0
        || !std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x)
        || !std::isfinite(b.y) || !std::isfinite(c.x) || !std::isfinite(c.y)
        || mask.Touches(minX / spacing + border, minY / spacing + border,
            maxX / spacing + border, maxY / spacing + border);
    if (!needed)
        for (const Disc &disc : discs) {
            const double dx = double(disc.x) - (std::max)(minX, (std::min)(maxX, disc.x));
            const double dy = double(disc.y) - (std::max)(minY, (std::min)(maxY, disc.y));
            if (!std::isfinite(disc.x) || !std::isfinite(disc.y) || !std::isfinite(disc.radius)
                || disc.radius < 0) {
                needed = true;
                break;
            }
            if (dx * dx + dy * dy <= double(disc.radius) * disc.radius
                && mask.Touches((minX - disc.reach) / spacing + border, (minY - disc.reach) / spacing + border,
                    (maxX + disc.reach) / spacing + border, (maxY + disc.reach) / spacing + border)) {
                needed = true;
                break;
            }
        }
    return needed;
}
} // namespace Detail

// Append in source order. The caller retains subset/disc capacity across patches and frames.
// Positions are the final uploaded pre-displacement XY, which WrapVertex carries in TexCoord2.
template<class Position>
inline void Partition(const std::vector<uint16_t> &indices, Position position, const Mask &mask,
    float spacing, float border, const std::vector<Disc> &discs, std::vector<uint16_t> &open,
    std::vector<uint16_t> &local, float margin = 0)
{
    for (size_t first = 0; first + 2 < indices.size(); first += 3) {
        const auto a = position(indices[first]), b = position(indices[first + 1]), c = position(indices[first + 2]);
        const bool needed = Detail::NeedsLocalWaves(a, b, c, mask, spacing, border, discs, margin);
        auto &out = needed ? local : open;
        out.insert(out.end(), indices.begin() + first, indices.begin() + first + 3);
    }
}
// Most frames resubmit the same mesh. Reuse requires the same mask revision,
// sampling parameters and exact index/position bytes. Hulls move, so discs
// apply afresh each call without changing that cached base split.
class Cache {
    static constexpr size_t Capacity = 8;
    struct Entry {
        unsigned revision = 0, age = 0;
        float spacing = 0, border = 0, margin = 0;
        std::vector<uint16_t> indices, open, local;
        std::vector<Point> positions;

        bool Matches(const std::vector<uint16_t> &sourceIndices, const std::vector<Point> &sourcePositions,
            unsigned maskRevision, float sampleSpacing, float sampleBorder, float sampleMargin) const
        {
            // NaN sampling parameters deliberately miss and classify conservatively again.
            return revision == maskRevision && revision
                && spacing == sampleSpacing && border == sampleBorder && margin == sampleMargin
                && indices.size() == sourceIndices.size() && positions.size() == sourcePositions.size()
                && (sourceIndices.empty() || !std::memcmp(indices.data(), sourceIndices.data(), sourceIndices.size() * sizeof(uint16_t)))
                && (sourcePositions.empty() || !std::memcmp(positions.data(), sourcePositions.data(), sourcePositions.size() * sizeof(Point)));
        }
        template<class Position>
        void Rebuild(const std::vector<uint16_t> &sourceIndices, const std::vector<Point> &sourcePositions,
            Position position, const Mask &mask, float sampleSpacing, float sampleBorder, float sampleMargin)
        {
            revision = mask.Revision();
            spacing = sampleSpacing;
            border = sampleBorder;
            margin = sampleMargin;
            indices = sourceIndices;
            positions = sourcePositions;
            open.clear();
            local.clear();
            Partition(sourceIndices, position, mask, sampleSpacing, sampleBorder, std::vector<Disc>(), open, local, sampleMargin);
        }
    };
    Entry entries[Capacity];
    unsigned clock = 0;
    std::vector<uint16_t> discOpen, discLocal;

    template<class Position>
    Entry &BasePartition(const std::vector<uint16_t> &indices, const std::vector<Point> &positions,
        Position position, const Mask &mask, float spacing, float border, float margin)
    {
        Entry *found = NULL, *oldest = entries;
        for (Entry &entry : entries) {
            if (entry.Matches(indices, positions, mask.Revision(), spacing, border, margin)) {
                found = &entry;
                break;
            }
            if (entry.age < oldest->age)
                oldest = &entry;
        }
        if (!found) {
            found = oldest;
            found->Rebuild(indices, positions, position, mask, spacing, border, margin);
        }
        found->age = ++clock;
        return *found;
    }
public:
    // Both vectors belong to this cache and may change on a later call.
    struct Split {
        const std::vector<uint16_t> *open, *local;
    };
    Split Partitioned(const std::vector<uint16_t> &indices, const std::vector<Point> &positions, const Mask &mask,
        float spacing, float border, const std::vector<Disc> &discs, float margin)
    {
        auto position = [&](uint16_t i) {
            return i < positions.size() ? positions[i] : Point{std::numeric_limits<float>::quiet_NaN(), 0};
        };
        Entry &base = BasePartition(indices, positions, position, mask, spacing, border, margin);
        if (discs.empty())
            return {&base.open, &base.local};
        // Hulls move each call. Apply them to the cached open subset without
        // changing the base split. Scratch vectors retain capacity across calls.
        discOpen.clear();
        discLocal = base.local;
        Partition(base.open, position, mask, spacing, border, discs, discOpen, discLocal, margin);
        return {&discOpen, &discLocal};
    }
};
} // namespace WaterOpenSeaSplit
