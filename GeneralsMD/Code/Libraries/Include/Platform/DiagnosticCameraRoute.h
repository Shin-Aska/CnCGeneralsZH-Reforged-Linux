#pragma once

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

// Capture diagnostics only. Times are absolute logic frames; fractional samples are
// allowed for -recordfps pictures. XY and angle are offsets from the saved view,
// zoom is a multiplier, and Z/pitch remain those of that view.
namespace DiagnosticCameraRoute {
// Exact float round trips, rather than rounding a wall-clock-settled zoom. These
// are the inputs to the ordinary camera transform and its projection plane.
struct Anchor {
    enum Field {
        PosX, PosY, PosZ, Zoom, Angle, Pitch, Height, Ground, FXPitch,
            OffsetX, OffsetY, OffsetZ, Fov, PlaneMinX, PlaneMinY, PlaneMaxX, PlaneMaxY, Count
    };
    float values[Count] = {};
    int realZoom = 0;
    static const char *Name(int index)
    {
        static const char *names[] = {"x", "y",
            "z", "zoom", "angle",
            "pitch", "height", "ground",
            "fx_pitch",
            "offset_x", "offset_y", "offset_z",
            "fov", "plane_min_x", "plane_min_y",
            "plane_max_x", "plane_max_y"};
        return names[index];
    }
    bool Valid() const
    {
        for (float value : values)
            if (!std::isfinite(value) || std::fabs(value) > 1000000)
                return false;
        return values[Zoom] >= 0.01f && values[Zoom] <= 100
            && std::fabs(values[Angle]) <= 3.141593f && std::fabs(values[Pitch]) < 1.5707963f
            && values[Height] > 0 && values[FXPitch] > 0 && values[FXPitch] <= 100
            && values[OffsetZ] > 0 && values[Fov] > 0 && values[Fov] < 3.141593f
            && values[PlaneMinX] < values[PlaneMaxX] && values[PlaneMinY] < values[PlaneMaxY]
            && (realZoom == 0 || realZoom == 1);
    }
    bool Read(FILE *file)
    {
        Anchor parsed;
        char label[64];
        if (!file || std::fscanf(file, "%63s", label) != 1 || std::strcmp(label,
            "ZH_CAMERA_ROUTE_STATE_V1"))
            return false;
        for (int i = 0; i < Count; ++i)
            if (std::fscanf(file, "%63s%f", label,
                &parsed.values[i]) != 2 || std::strcmp(label, Name(i)))
                return false;
        char boolean[8];
        if (std::fscanf(file, "%63s%7s", label, boolean) != 2 || std::strcmp(label,
            "real_zoom"))
            return false;
        if (std::strcmp(boolean, "0") && std::strcmp(boolean,
            "1"))
            return false;
        parsed.realZoom = boolean[0] == '1';
        char extra;
        if (std::fscanf(file, " %c",
            &extra) != EOF || std::ferror(file) || !parsed.Valid())
            return false;
        *this = parsed;
        return true;
    }
    bool Write(FILE *file) const
    {
        if (!file || !Valid() || std::fprintf(file, "ZH_CAMERA_ROUTE_STATE_V1\n") < 0)
            return false;
        for (int i = 0; i < Count; ++i)
            if (std::fprintf(file, "%s %.*g\n", Name(i),
                std::numeric_limits<float>::max_digits10, values[i]) < 0)
                return false;
        return std::fprintf(file, "real_zoom %d\n", realZoom) >= 0 && !std::ferror(file);
    }
};
struct Knot {
    double frame, x, y, zoom, angle;
};
class Route {
    std::vector<Knot> knots;
public:
    bool Read(FILE *file)
    {
        knots.clear();
        char line[512];
        if (!file || !std::fgets(line, sizeof(line), file))
            return false;
        line[std::strcspn(line, "\r\n")] = 0;
        if (std::strcmp(line, "logic_frame,offset_x,offset_y,zoom_scale,angle_offset"))
            return false;
        while (std::fgets(line, sizeof(line), file)) {
            // A truncated line must never become a valid route prefix.
            if (!std::strchr(line, '\n') && !std::feof(file)) {
                knots.clear();
                return false;
            }
            if (line[0] == '#' || line[0] == '\n'
                || line[0] == '\r')
                continue;
            Knot k;
            char extra;
            if (std::sscanf(line, "%lf,%lf,%lf,%lf,%lf %c", &k.frame, &k.x, &k.y, &k.zoom, &k.angle, &extra) != 5
                || !std::isfinite(k.frame) || !std::isfinite(k.x) || !std::isfinite(k.y)
                || !std::isfinite(k.zoom) || !std::isfinite(k.angle)
                || k.frame < 0 || k.frame > 2147483647.0 || std::floor(k.frame) != k.frame
                || std::fabs(k.x) > 1000000 || std::fabs(k.y) > 1000000
                || k.zoom < 0.01 || k.zoom > 100 || std::fabs(k.angle) > 6.283185307179586
                || (!knots.empty() && k.frame <= knots.back().frame) || knots.size() >= 4096) {
                knots.clear();
                return false;
            }
            knots.push_back(k);
        }
        if (std::ferror(file) || knots.size() < 2 || knots.front().x != 0 || knots.front().y != 0
            || knots.front().zoom != 1 || knots.front().angle != 0) {
            knots.clear();
            return false;
        }
        return true;
    }
    bool Empty() const {
        return knots.empty();
    }
    double FirstFrame() const {
        return knots.front().frame;
    }
    Knot Sample(double frame) const
    {
        if (frame <= knots.front().frame)
            return knots.front();
        if (frame >= knots.back().frame)
            return knots.back();
        size_t low = 0, high = knots.size() - 1;
        while (high - low > 1) {
            const size_t middle = (low + high) / 2;
            if (knots[middle].frame <= frame)
                low = middle;
            else
                high = middle;
        }
        const Knot &a = knots[low], &b = knots[high];
        const double t = (frame - a.frame) / (b.frame - a.frame);
        return {frame, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
            a.zoom + (b.zoom - a.zoom) * t, a.angle + (b.angle - a.angle) * t};
    }
};
}
