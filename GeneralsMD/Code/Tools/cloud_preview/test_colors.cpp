// Standalone CPU checks for the isolated preview's editable color palettes.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "CloudPreviewColors.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include <unistd.h>

namespace C = CloudPreviewColors;
namespace fs = std::filesystem;

namespace {

struct TemporaryDirectory {
    fs::path path;
    TemporaryDirectory()
    {
        std::string pattern = (fs::temp_directory_path() / "cloud-colors-test-XXXXXX").string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        const char *result = mkdtemp(buffer.data());
        assert(result);
        path = result;
    }
    ~TemporaryDirectory() { fs::remove_all(path); }
};

std::string read(const fs::path &path)
{
    std::ifstream file(path);
    assert(file);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void write(const fs::path &path, const std::string &text)
{
    std::ofstream file(path);
    file << text;
    assert(file.good());
}

bool equal(const C::Color &a, const C::Color &b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

bool equal(const C::Palette &a, const C::Palette &b)
{
    return equal(a.main, b.main) && equal(a.shadow, b.shadow) && a.brightness == b.brightness
        && a.transparency == b.transparency;
}

std::array<C::Palette, 4> snapshot()
{
    return {{C::get(0), C::get(1), C::get(2), C::get(3)}};
}

void expectSnapshot(const std::array<C::Palette, 4> &expected)
{
    for (unsigned i = 0; i < 4; ++i)
        assert(equal(C::get(i), expected[i]));
}

std::string replace(std::string text, const std::string &from, const std::string &to)
{
    const auto found = text.find(from);
    assert(found != std::string::npos);
    text.replace(found, from.size(), to);
    return text;
}

std::string versionOne(std::string text)
{
    text = replace(text, "version = 2", "version = 1");
    for (auto at = text.find("transparency = "); at != std::string::npos; at = text.find("transparency = "))
        text.erase(at, text.find('\n', at) - at + 1);
    return text;
}

} // namespace

int main()
{
    TemporaryDirectory temporary;
    const fs::path directory = temporary.path / "preview colors";
    fs::create_directory(directory);
    assert(!C::save());
    assert(!C::load());
    assert(C::initialize(directory.string()));
    assert(C::savePath() == (directory / "cloud-colors.ini").string());
    assert(!fs::exists(C::savePath()));
    const std::array<C::Palette, 4> expectedDefaults = {{
        {{0.769999981f, 0.939999998f, 0.550000012f}, {0.125490203f, 0.31764707f, 0.0549019612f}, 0.699999988f, 0.670000017f},
        {{0.749019623f, 0.670588255f, 0.469999999f}, {0.298039228f, 0.184313729f, 0.0705882385f}, 1.29999995f, 0.f},
        {{0.660000026f, 0.75999999f, 1.0f}, {0.0649999976f, 0.100000001f, 0.209999993f}, 1.52999997f, 0.769999981f},
        {{0.839999974f, 0.629999995f, 1.0f}, {0.192156866f, 0.0470588244f, 0.235294119f}, 1.32000005f, 0.74000001f},
    }};
    expectSnapshot(expectedDefaults);
    assert(equal(C::get(500), C::get(0)));
    assert(!C::load());
    expectSnapshot(expectedDefaults);
    std::puts("PASS exact approved palettes; missing file creates nothing and changes nothing");

    C::Palette edit = {{0, 1, std::nextafter(0.4f, 1.0f)}, {0.123456791f, 0.75f, 0.5f}, 2, 0.375f};
    C::set(0, edit);
    const unsigned editedRevision = C::revision();
    C::set(0, edit);
    assert(C::revision() == editedRevision);
    edit.transparency = std::nextafter(0.4f, 1.0f);
    C::set(0, edit);
    assert(C::revision() == editedRevision + 1); // Transparency-only changes reach the renderer.
    edit.main.r = 0.25f;
    edit.brightness = 0;
    edit.transparency = 1;
    C::set(1, edit);
    edit.main.g = 0.333333343f;
    edit.brightness = 0.765432119f;
    edit.transparency = 0.654321134f;
    C::set(2, edit);
    edit.shadow.b = std::nextafter(1.0f, 0.0f);
    edit.transparency = 0;
    C::set(3, edit);
    const auto edited = snapshot();
    const std::string exported = C::exportText();
    assert(exported.find("version = 2\n") != std::string::npos);
    assert(exported.find("transparency = 1\n") != std::string::npos);
    assert(exported.find("transparency = 0\n") != std::string::npos);
    assert(exported.find("[toxin]") < exported.find("[toxin_beta]"));
    assert(exported.find("[toxin_beta]") < exported.find("[toxin_gamma]"));
    assert(exported.find("[toxin_gamma]") < exported.find("[nuclear]"));
    assert(exported.find("; #") != std::string::npos);
    assert(!fs::exists(C::savePath()));
    assert(C::save());
    assert(read(C::savePath()) == exported);
    for (unsigned i = 0; i < 4; ++i)
        C::reset(i);
    expectSnapshot(expectedDefaults);
    assert(read(C::savePath()) == exported); // Reset is not an implicit save.
    assert(C::load());
    expectSnapshot(edited);
    const unsigned loadedRevision = C::revision();
    assert(C::load());
    assert(C::revision() == loadedRevision);
    assert(C::initialize(directory.string()));
    expectSnapshot(edited);
    assert(read(C::savePath()) == exported);
    std::puts("PASS explicit save/reload and startup load preserve every float exactly; reset never writes");

    C::reset(2);
    auto resetSelected = edited;
    resetSelected[2] = expectedDefaults[2];
    expectSnapshot(resetSelected);
    assert(C::get(2).transparency == 0.769999981f);
    C::set(2, edited[2]);
    expectSnapshot(edited);
    const std::string legacy = versionOne(exported);
    write(C::savePath(), legacy);
    assert(C::load());
    auto upgraded = edited;
    for (auto &palette : upgraded) palette.transparency = 0;
    expectSnapshot(upgraded);
    assert(read(C::savePath()) == legacy); // Loading old palettes never rewrites them.
    assert(C::initialize(directory.string()));
    expectSnapshot(upgraded);
    assert(C::save());
    assert(read(C::savePath()).find("version = 2\n") != std::string::npos);
    assert(C::load());
    expectSnapshot(upgraded);
    write(C::savePath(), exported);
    assert(C::load());
    expectSnapshot(edited);
    std::puts("PASS version 1 palettes load with zero transparency; selected reset and explicit version 2 upgrade preserve other colors");

    const unsigned revision = C::revision();
    for (float invalid : {-0.001f, 1.001f, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()}) {
        auto bad = edited[0];
        bad.main.g = invalid;
        C::set(0, bad);
        bad = edited[0];
        bad.shadow.b = invalid;
        C::set(0, bad);
        bad = edited[0];
        bad.transparency = invalid;
        C::set(0, bad);
        expectSnapshot(edited);
    }
    for (float invalid : {-0.01f, 2.001f, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()}) {
        auto bad = edited[0];
        bad.brightness = invalid;
        C::set(0, bad);
        expectSnapshot(edited);
    }
    C::set(500, edit);
    C::reset(500);
    expectSnapshot(edited);
    assert(C::revision() == revision);
    assert(read(C::savePath()) == exported);
    std::puts("PASS invalid edits, NaN, infinities and invalid palette IDs cannot affect live colors");

    // Make an early valid edit, then damage the last palette. Rejection must
    // retain every live palette, including the early section that parsed OK.
    const std::string altered = replace(exported, "main = 0, 1,", "main = 0.5, 1,");
    const std::string brightnessLine = "brightness = 0\n";
    const std::string transparencyLine = "transparency = 1\n";
    const std::vector<std::string> badFiles = {
        replace(altered, "version = 2", "version = 3"),
        replace(altered, "version = 2", ""),
        replace(altered, "version = 2", "version = 2\nversion = 2"),
        replace(altered, "version = 2", "version = 1"), // New fields require the new version.
        replace(altered, "[nuclear]", "[unknown]"),
        replace(altered, "[nuclear]", "[toxin]"),
        replace(altered, brightnessLine, ""),
        replace(altered, brightnessLine, "brightness = nan\n"),
        replace(altered, brightnessLine, "brightness = inf\n"),
        replace(altered, brightnessLine, "brightness = -0.01\n"),
        replace(altered, brightnessLine, "brightness = 2.01\n"),
        replace(altered, brightnessLine, "brightness = 1 trailing\n"),
        replace(altered, brightnessLine, "brightness = 1\nbrightness = 1\n"),
        replace(altered, brightnessLine, "glow = 1\n"),
        replace(altered, transparencyLine, ""),
        replace(altered, transparencyLine, "transparency = nan\n"),
        replace(altered, transparencyLine, "transparency = inf\n"),
        replace(altered, transparencyLine, "transparency = -0.01\n"),
        replace(altered, transparencyLine, "transparency = 1.01\n"),
        replace(altered, transparencyLine, "transparency = 0.5 trailing\n"),
        replace(altered, transparencyLine, "transparency = 0.5\ntransparency = 0.5\n"),
        replace(altered, "main = 0.25, 1,", "main = nan, 1,"),
        replace(altered, "main = 0.25, 1,", "main = 0.25, 1.01,"),
        replace(altered, "main = 0.25, 1,", "main = 0.25, -0.01,"),
        replace(altered, "main = 0.25, 1,", "main = 0.25, 1e300,"),
        altered + "\nmalformed\n",
    };
    for (const auto &badFile : badFiles) {
        write(C::savePath(), badFile);
        assert(!C::load());
        expectSnapshot(edited);
        assert(C::revision() == revision);
        assert(read(C::savePath()) == badFile);
        assert(C::lastStatus().find("not loaded") != std::string::npos);
    }
    assert(!C::initialize(directory.string()));
    expectSnapshot(expectedDefaults);
    assert(read(C::savePath()) == badFiles.back());
    std::puts("PASS malformed/partial/out-of-range files are rejected transactionally and never overwritten");

    assert(C::save());
    assert(read(C::savePath()) == C::exportText());
    unsigned fileCount = 0;
    for (const auto &entry : fs::directory_iterator(directory)) {
        assert(entry.path().filename() == "cloud-colors.ini");
        ++fileCount;
    }
    assert(fileCount == 1); // Successful save leaves no temporary files.

    fs::remove(C::savePath());
    fs::create_directory(C::savePath()); // A rename failure must clean up.
    write(fs::path(C::savePath()) / "keep.txt", "preserve this");
    assert(!C::save());
    assert(read(fs::path(C::savePath()) / "keep.txt") == "preserve this");
    fileCount = 0;
    for (const auto &entry : fs::directory_iterator(directory)) {
        assert(entry.path().filename() == "cloud-colors.ini");
        ++fileCount;
    }
    assert(fileCount == 1);
    assert(C::initialize((temporary.path / "missing parent").string()));
    assert(!C::save());
    assert(!fs::exists(temporary.path / "missing parent"));
    assert(!C::initialize(""));
    assert(!C::save());
    std::puts("PASS atomic saves replace complete files and failures preserve data without temporary residue");
    std::puts("Cloud preview color checks passed.");
}
