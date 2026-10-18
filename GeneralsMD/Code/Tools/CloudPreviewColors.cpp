// Editable palettes and explicit persistence for the isolated cloud preview.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "CloudPreviewColors.h"

#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <vector>

#include <unistd.h>

namespace CloudPreviewColors {
namespace {

// User-approved palette, preserved in cloud_preview/presets/preferred.ini.
constexpr std::array<Palette, 4> defaults = {{
    {{0.769999981f, 0.939999998f, 0.550000012f}, {0.125490203f, 0.31764707f, 0.0549019612f}, 0.699999988f, 0.670000017f},
    {{0.749019623f, 0.670588255f, 0.469999999f}, {0.298039228f, 0.184313729f, 0.0705882385f}, 1.29999995f, 0.f},
    {{0.660000026f, 0.75999999f, 1.0f}, {0.0649999976f, 0.100000001f, 0.209999993f}, 1.52999997f, 0.769999981f},
    {{0.839999974f, 0.629999995f, 1.0f}, {0.192156866f, 0.0470588244f, 0.235294119f}, 1.32000005f, 0.74000001f},
}};
constexpr const char *names[] = {"toxin", "nuclear", "toxin_beta", "toxin_gamma"};
constexpr unsigned displayOrder[] = {0, 2, 3, 1};
std::array<Palette, 4> palettes = defaults;
std::string path;
std::string status = "Default cloud colors";
unsigned currentRevision = 0;

bool equal(const Color &a, const Color &b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

bool equal(const Palette &a, const Palette &b)
{
    return equal(a.main, b.main) && equal(a.shadow, b.shadow) && a.brightness == b.brightness
        && a.transparency == b.transparency;
}

bool bounded(float value, float maximum)
{
    return std::isfinite(value) && value >= 0 && value <= maximum;
}

bool valid(const Color &c)
{
    return bounded(c.r, 1) && bounded(c.g, 1) && bounded(c.b, 1);
}

bool valid(const Palette &p)
{
    return valid(p.main) && valid(p.shadow) && bounded(p.brightness, 2) && bounded(p.transparency, 1);
}

std::string trim(const std::string &text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

bool number(const std::string &text, float &value)
{
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    if (!(in >> value))
        return false;
    in >> std::ws;
    return in.eof() && std::isfinite(value);
}

bool color(const std::string &text, Color &result)
{
    std::string input = text;
    for (char &c : input)
        if (c == ',')
            c = ' ';
    std::istringstream in(input);
    in.imbue(std::locale::classic());
    if (!(in >> result.r >> result.g >> result.b))
        return false;
    in >> std::ws;
    return in.eof() && valid(result);
}

std::string hex(const Color &c)
{
    char result[8];
    std::snprintf(result, sizeof(result), "#%02X%02X%02X",
                  unsigned(std::lround(c.r * 255)), unsigned(std::lround(c.g * 255)),
                  unsigned(std::lround(c.b * 255)));
    return result;
}

bool failLoad(const std::string &reason, unsigned line = 0)
{
    status = "Colors not loaded: " + reason;
    if (line)
        status += " (line " + std::to_string(line) + ")";
    return false;
}

} // namespace

const Palette &get(unsigned index)
{
    return palettes[index < palettes.size() ? index : 0];
}

void set(unsigned index, const Palette &palette)
{
    if (index >= palettes.size() || !valid(palette)) {
        status = "Edit rejected: RGB/transparency 0-1, brightness 0-2";
        return;
    }
    if (!equal(palettes[index], palette)) {
        palettes[index] = palette;
        ++currentRevision;
    }
    status = std::string("Edited ") + names[index] + " (not saved)";
}

void reset(unsigned index)
{
    if (index >= palettes.size()) {
        status = "Color reset rejected: unknown palette";
        return;
    }
    set(index, defaults[index]);
    status = std::string("Reset ") + names[index] + " (not saved)";
}

unsigned revision() { return currentRevision; }
std::string lastStatus() { return status; }
std::string savePath() { return path; }

bool initialize(const std::string &userDir)
{
    palettes = defaults;
    ++currentRevision;
    path.clear();
    if (userDir.empty()) {
        status = "Color file unavailable: empty preview directory";
        return false;
    }
    path = (std::filesystem::path(userDir) / "cloud-colors.ini").string();
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error)
        return failLoad(error.message());
    if (!exists) {
        status = "Default cloud colors (no saved file yet)";
        return true;
    }
    return load();
}

std::string exportText()
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "; Isolated cloud preview colors. RGB values are linear shader values.\n"
           "; Main and shadow RGB: 0-1. Brightness: 0-2. Hex comments are rounded.\n"
           "; Transparency: 0-1, higher is clearer; 0 keeps full smoke density.\n"
           "version = 2\n";
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    for (unsigned index : displayOrder) {
        const Palette &p = palettes[index];
        out << "\n[" << names[index] << "]\n";
        out << "main = " << p.main.r << ", " << p.main.g << ", " << p.main.b
            << " ; " << hex(p.main) << "\n";
        out << "shadow = " << p.shadow.r << ", " << p.shadow.g << ", " << p.shadow.b
            << " ; " << hex(p.shadow) << "\n";
        out << "brightness = " << p.brightness << "\n";
        out << "transparency = " << p.transparency << "\n";
    }
    return out.str();
}

bool save()
{
    if (path.empty()) {
        status = "Colors not saved: no preview directory";
        return false;
    }
    // The temporary file shares its destination directory, so rename replaces
    // the previous complete palette atomically. mkstemp cannot follow a stale
    // temporary-file symlink and is safe for two running preview processes.
    std::string temporary = path + ".tmp-XXXXXX";
    std::vector<char> temporaryName(temporary.begin(), temporary.end());
    temporaryName.push_back('\0');
    const int file = mkstemp(temporaryName.data());
    if (file < 0) {
        status = std::string("Colors not saved: ") + std::strerror(errno);
        return false;
    }
    const std::string text = exportText();
    size_t written = 0;
    int failure = 0;
    while (written < text.size()) {
        const ssize_t count = write(file, text.data() + written, text.size() - written);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            failure = count < 0 ? errno : EIO;
            break;
        }
        written += static_cast<size_t>(count);
    }
    if (!failure && fsync(file) != 0)
        failure = errno;
    if (close(file) != 0 && !failure)
        failure = errno;
    if (!failure && std::rename(temporaryName.data(), path.c_str()) != 0)
        failure = errno;
    if (failure) {
        std::remove(temporaryName.data());
        status = std::string("Colors not saved: ") + std::strerror(failure);
        return false;
    }
    status = "Saved all four cloud palettes";
    return true;
}

bool load()
{
    if (path.empty())
        return failLoad("no preview directory");
    std::ifstream file(path);
    file.imbue(std::locale::classic());
    if (!file)
        return failLoad("cannot open cloud-colors.ini");

    // Parse into an independent snapshot; malformed input never applies only
    // part of a palette, nor silently resets the user's other cloud colors.
    auto parsed = defaults;
    // Version 1 predates transparency and must retain its original appearance,
    // independently of later preferred defaults. Version 2 requires the field.
    for (auto &palette : parsed) palette.transparency = 0.f;
    unsigned fields[4] = {};
    unsigned sections = 0;
    int section = -1;
    unsigned version = 0;
    unsigned lineNumber = 0;
    std::string line;
    while (std::getline(file, line)) {
        ++lineNumber;
        if (lineNumber > 256 || line.size() > 1024)
            return failLoad("file exceeds palette format limits", lineNumber);
        line = trim(line.substr(0, line.find_first_of(";#")));
        if (line.empty())
            continue;
        if (line.front() == '[') {
            if (line.back() != ']')
                return failLoad("malformed section", lineNumber);
            const std::string name = trim(line.substr(1, line.size() - 2));
            section = -1;
            for (unsigned i = 0; i < 4; ++i)
                if (name == names[i])
                    section = static_cast<int>(i);
            if (section < 0)
                return failLoad("unknown palette " + name, lineNumber);
            if (sections & (1u << section))
                return failLoad("duplicate palette " + name, lineNumber);
            sections |= 1u << section;
            continue;
        }
        const auto assignment = line.find('=');
        if (assignment == std::string::npos)
            return failLoad("expected key = value", lineNumber);
        const std::string key = trim(line.substr(0, assignment));
        const std::string value = trim(line.substr(assignment + 1));
        if (section < 0) {
            if (key != "version" || (value != "1" && value != "2") || version)
                return failLoad("expected one version = 1 or 2 header", lineNumber);
            version = value == "2" ? 2 : 1;
            continue;
        }
        unsigned flag = 0;
        bool accepted = false;
        if (key == "main") {
            flag = 1;
            accepted = color(value, parsed[section].main);
        } else if (key == "shadow") {
            flag = 2;
            accepted = color(value, parsed[section].shadow);
        } else if (key == "brightness") {
            flag = 4;
            accepted = number(value, parsed[section].brightness)
                && bounded(parsed[section].brightness, 2);
        } else if (key == "transparency") {
            if (version != 2)
                return failLoad("transparency requires version 2", lineNumber);
            flag = 8;
            accepted = number(value, parsed[section].transparency)
                && bounded(parsed[section].transparency, 1);
        } else {
            return failLoad("unknown key " + key, lineNumber);
        }
        if (fields[section] & flag)
            return failLoad("duplicate key " + key, lineNumber);
        if (!accepted)
            return failLoad("invalid " + key + " value", lineNumber);
        fields[section] |= flag;
    }
    if (file.bad())
        return failLoad("file read failed");
    if (!version || sections != 15)
        return failLoad("version 1 or 2 and all four palettes are required");
    for (unsigned i = 0; i < 4; ++i)
        if (fields[i] != (version == 2 ? 15u : 7u))
            return failLoad(std::string("missing values in ") + names[i]);
    bool changed = false;
    for (unsigned i = 0; i < 4; ++i)
        changed |= !equal(palettes[i], parsed[i]);
    palettes = parsed;
    if (changed)
        ++currentRevision;
    status = "Loaded all four cloud palettes";
    return true;
}

} // namespace CloudPreviewColors
