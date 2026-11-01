// CPU checks for source lifecycles and uncapped shared-volume spatial lists.
#include "CloudStressModel.h"
#include "Platform/HazardCloudPolicy.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

namespace S = CloudStress;
namespace {
bool near(float a, float b) { return std::abs(a - b) < .0001f; }

void sameSources(const std::vector<S::Source> &a, const std::vector<S::Source> &b)
{
    assert(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        assert(a[i].id == b[i].id && a[i].active == b[i].active);
        assert(a[i].field.density == b[i].field.density);
        assert(a[i].field.formation == b[i].field.formation);
        assert(a[i].field.release == b[i].field.release);
        assert(a[i].sourceAge == b[i].sourceAge && a[i].sourceTravel == b[i].sourceTravel);
        assert(a[i].dissipation == b[i].dissipation && a[i].seed == b[i].seed);
    }
}

void layoutAndPalettes()
{
    S::Model model;
    S::Config config;
    config.centreX = 123; config.centreY = -217; config.groundZ = 7;
    assert(model.configure(config));
    assert(model.sources().size() == 128);
    std::array<unsigned, 4> palettes{};
    for (const auto &source : model.sources()) {
        ++palettes[unsigned(source.field.palette)];
        assert(!source.active && source.field.density == 0 && source.field.ground.z == 7);
        assert(source.field.radius == 30 && source.field.fringe == 14);
    }
    for (unsigned count : palettes) assert(count == 32);
    assert(near(model.sources().front().field.ground.x, 123 - 480));
    assert(near(model.sources().back().field.ground.x, 123 + 480));
    assert(near(model.sources().front().field.ground.y, -217 - 224));
    assert(near(model.sources().back().field.ground.y, -217 + 224));
    const auto domain = model.bounds();
    model.launch(10); model.update(17); model.update(40);
    assert(model.bounds().minX == domain.minX && model.bounds().maxY == domain.maxY);
    for (int palette = 0; palette < 4; ++palette) {
        config.palette = palette;
        assert(model.configure(config));
        for (const auto &source : model.sources()) assert(source.field.palette == palette);
    }
}

void lifecycleAndReplay()
{
    S::Model model;
    S::Config config;
    config.count = 4; config.lifetime = 12;
    assert(model.configure(config));
    model.launch(100);
    for (const auto &source : model.sources()) {
        assert(source.born == 100 && source.expiry == 112);
        assert(!source.active && source.sourceAge == 0 && source.sourceTravel == 0);
    }
    model.update(100.3f);
    for (const auto &source : model.sources()) {
        const auto expected = HazardCloudPolicy::visualBuildup(int(source.field.palette), false,
            source.sourceAge, config.lifetime);
        assert(source.active && source.field.density == expected.density);
        assert(source.field.formation == expected.formation && source.field.release == expected.release);
        assert(source.dissipation == 0 && source.sourceTravel > 0);
        if (source.field.palette == 1) assert(source.field.formation == 1);
        else assert(source.field.formation > 0 && source.field.formation < 1);
    }
    model.update(103);
    for (const auto &source : model.sources()) {
        assert(source.field.density == 1 && source.field.formation == 1);
        assert(source.field.release == 0 && near(source.sourceTravel, 44.55f));
    }
    model.update(110.5f);
    for (const auto &source : model.sources()) {
        assert(near(source.field.density, .5f) && near(source.dissipation, .5f));
        assert(near(source.sourceTravel, 44.55f));
    }
    const auto snapshot = model.sources();
    model.update(112);
    for (const auto &source : model.sources()) assert(!source.active && source.field.density == 0);
    model.update(110.5f); // Seeking does not retain stale peak/travel state.
    sameSources(snapshot, model.sources());
    model.launch(200);
    model.update(210.5f);
    sameSources(snapshot, model.sources());
    assert(model.sources()[0].born == 200 && model.sources()[0].expiry == 212);
    model.launch(0);
    for (unsigned step = 1; step <= 300; ++step) model.update(float(step) / 100);
    const auto smallSteps = model.sources();
    model.launch(0); model.update(3);
    sameSources(smallSteps, model.sources());
}

void staggerAndLargeNuclear()
{
    S::Config config;
    config.count = 4; config.stagger = 2; config.lifetime = 40;
    config.palette = 1; config.radius = 140;
    S::Model model;
    assert(model.configure(config));
    model.launch(10); model.update(13);
    assert(model.sources()[0].born == 10 && model.sources()[3].expiry == 56);
    assert(model.sources()[0].active && model.sources()[1].active);
    assert(!model.sources()[2].active && !model.sources()[3].active);
    assert(model.sources()[0].field.formation == 1 && near(model.sources()[0].field.density, .5f));
    assert(model.sources()[2].sourceAge == 0 && model.sources()[2].sourceTravel == 0);
    model.update(16);
    assert(near(model.sources()[0].sourceTravel, 89.1f));
    assert(!model.sources()[3].active); // Zero density at exact birth.
    model.update(50);
    assert(!model.sources()[0].active && model.sources()[3].active);
    for (unsigned i = 0; i < model.sources().size(); ++i) assert(model.sources()[i].id == i);
}

void shortHazards()
{
    S::Config config;
    config.count = 4; config.lifetime = .08f;
    S::Model model;
    assert(model.configure(config)); model.launch();
    std::array<float, 4> peak{};
    for (unsigned step = 1; step <= 1000; ++step) {
        model.update(config.lifetime * float(step) / 1000);
        for (unsigned i = 0; i < 4; ++i) {
            const auto &source = model.sources()[i];
            peak[i] = (std::max)(peak[i], source.field.density);
            assert(std::isfinite(source.dissipation) && source.dissipation >= 0 && source.dissipation <= 1);
            if (peak[i] > .0001f)
                assert(std::abs(source.dissipation - (1 - source.field.density / peak[i])) < .00002f);
        }
    }
    for (const auto &source : model.sources()) assert(!source.active && source.field.density == 0);
}

void uncappedBins()
{
    S::Config config;
    config.count = 257; config.layout = S::Layout::Overlap; config.radius = 140;
    config.centreX = -37; config.centreY = 123;
    S::Model model;
    assert(model.configure(config)); model.launch(); model.update(8);
    const auto grid = model.buildBins(64);
    assert(grid.valid());
    assert(grid.minX == std::floor(model.bounds().minX / 64) * 64);
    assert(grid.minY == std::floor(model.bounds().minY / 64) * 64);
    const unsigned x = unsigned(std::floor((config.centreX - grid.minX) / grid.cellSize));
    const unsigned y = unsigned(std::floor((config.centreY - grid.minY) / grid.cellSize));
    const std::size_t cell = std::size_t(y) * grid.width + x;
    assert(grid.offsets[cell + 1] - grid.offsets[cell] == 257);
    for (unsigned i = 0; i < 257; ++i) assert(grid.indices[grid.offsets[cell] + i] == i);
    assert(grid.offsets.back() == grid.indices.size());
    for (std::size_t c = 0; c + 1 < grid.offsets.size(); ++c)
        assert(grid.offsets[c + 1] - grid.offsets[c] == 257);
    model.update(24);
    const auto empty = model.buildBins(64);
    assert(empty.valid() && empty.indices.empty() && empty.offsets.back() == 0);
}

void binCoverageMatchesSupport()
{
    S::Model model;
    S::Config config;
    config.count = 129; config.columns = 11; config.stagger = .02f;
    config.centreX = -123; config.centreY = -58;
    assert(model.configure(config)); model.launch(); model.update(2);
    const auto grid = model.buildBins(48);
    assert(grid.valid());
    for (unsigned y = 0; y < grid.height; ++y) for (unsigned x = 0; x < grid.width; ++x) {
        const std::size_t cell = std::size_t(y) * grid.width + x;
        const float lowX = grid.minX + x * grid.cellSize, lowY = grid.minY + y * grid.cellSize;
        const auto first = grid.indices.begin() + grid.offsets[cell];
        const auto last = grid.indices.begin() + grid.offsets[cell + 1];
        assert(std::adjacent_find(first, last) == last);
        for (const auto &source : model.sources()) {
            const float reach = source.field.radius + source.field.fringe + 18;
            const bool overlaps = source.active && source.field.ground.x + reach >= lowX
                && source.field.ground.x - reach < lowX + grid.cellSize
                && source.field.ground.y + reach >= lowY
                && source.field.ground.y - reach < lowY + grid.cellSize;
            assert((std::find(first, last, source.id) != last) == overlaps);
        }
    }
}

void staggeredBinsAndReplay()
{
    S::Model model;
    S::Config config;
    config.count = 128; config.layout = S::Layout::Overlap;
    config.stagger = .125f; config.lifetime = 4;
    assert(model.configure(config));
    const auto beforeLaunch = model.buildBins(64);
    assert(beforeLaunch.valid() && beforeLaunch.indices.empty());
    model.launch(10);
    const auto zeroAge = model.buildBins(64);
    assert(zeroAge.valid() && zeroAge.indices.empty());
    for (float time : {10.5f, 14.5f, 20.0f, 30.0f}) {
        model.update(time);
        const auto grid = model.buildBins(64);
        assert(grid.valid());
        std::vector<unsigned> expected;
        for (const auto &source : model.sources()) if (source.active) expected.push_back(source.id);
        for (std::size_t cell = 0; cell + 1 < grid.offsets.size(); ++cell) {
            const auto first = grid.indices.begin() + grid.offsets[cell];
            const auto last = grid.indices.begin() + grid.offsets[cell + 1];
            assert(std::vector<unsigned>(first, last) == expected);
        }
    }
    model.launch(10); model.update(14.5f);
    const auto firstReplay = model.buildBins(64);
    model.launch(100); model.update(104.5f);
    const auto nextReplay = model.buildBins(64);
    assert(firstReplay.offsets == nextReplay.offsets && firstReplay.indices == nextReplay.indices);
    assert(!firstReplay.indices.empty());
}

void productionSnapshotsAreTransactional()
{
    S::Model preview;
    S::Config config;
    config.count = 129; config.layout = S::Layout::Overlap;
    assert(preview.configure(config)); preview.launch(); preview.update(2);
    auto sources = preview.sources();
    auto bounds = preview.bounds();
    S::Model model;
    assert(model.setSources(sources, bounds));
    const auto captured = model.sources();
    model.update(100); model.launch(50); model.update(101);
    // Imported simulation data is owned by the production caller, so preview
    // updates must not age, move, or resurrect any imported source.
    sameSources(captured, model.sources());
    const auto full = model.buildBins(64);
    assert(full.valid() && !full.indices.empty());
    sources[0].active = false;
    sources[1].terrain = false;
    sources[1].field.ground.x += 400;
    sources[1].field.ground.z = 80;
    bounds.maxX += 400; bounds.maxZ += 100;
    assert(model.setSources(sources,bounds));
    assert(!model.sources()[0].active && !model.sources()[1].terrain);
    assert(model.sources()[1].field.ground.z == 80);
    assert(model.sources()[1].field.ground.x != captured[1].field.ground.x);
    const auto moved = model.buildBins(64);
    assert(moved.valid() && moved.width > full.width);
    assert(std::find(moved.indices.begin(),moved.indices.end(),0) == moved.indices.end());
    const auto stable = model.sources();
    for (unsigned kind = 0; kind < 12; ++kind) {
        auto bad = sources; auto badBounds = bounds;
        switch (kind) {
        case 0: bad[1].id = 0; break;
        case 1: bad[1].field.ground.x = (std::numeric_limits<float>::quiet_NaN)(); break;
        case 2: bad[1].sourceTravel = (std::numeric_limits<float>::infinity)(); break;
        case 3: bad[1].field.palette = 1.5f; break;
        case 4: bad[1].field.release = 2; break;
        case 5: bad[1].dissipation = -1; break;
        case 6: bad[1].expiry = bad[1].born - 1; break;
        case 7: badBounds.maxX = badBounds.minX; break;
        case 8: badBounds.minY = (std::numeric_limits<float>::quiet_NaN)(); break;
        case 9: bad[1].field.radius = -1; break;
        case 10: bad[1].seed = (std::numeric_limits<float>::infinity)(); break;
        case 11: bad[1].field.density = 2; break;
        }
        assert(!model.setSources(bad,badBounds));
        sameSources(stable,model.sources());
        assert(model.bounds().maxX == bounds.maxX);
    }
    assert(model.setSources({},bounds));
    const auto empty = model.buildBins(64);
    assert(empty.valid() && empty.indices.empty());
}

void rejectInvalidWithoutChangingModel()
{
    S::Model model;
    S::Config good;
    good.count = 1;
    assert(model.configure(good)); model.launch(); model.update(2);
    const auto before = model.sources();
    for (unsigned kind = 0; kind < 10; ++kind) {
        auto bad = good;
        switch (kind) {
        case 0: bad.count = 0; break;
        case 1: bad.palette = 4; break;
        case 2: bad.radius = -1; break;
        case 3: bad.lifetime = 0; break;
        case 4: bad.stagger = -1; break;
        case 5: bad.spacing = (std::numeric_limits<float>::infinity)(); break;
        case 6: bad.centreX = (std::numeric_limits<float>::quiet_NaN)(); break;
        case 7: bad.centreX = 1e30f; break; // Float precision collapses XY support.
        case 8: bad.groundZ = 1e30f; break; // Float precision collapses height.
        case 9: bad.radius = 1e30f; break; // Squared shader support would overflow.
        }
        assert(!model.configure(bad));
        sameSources(before, model.sources());
    }
    assert(!model.buildBins(0).valid());
    assert(!model.buildBins((std::numeric_limits<float>::quiet_NaN)()).valid());
    model.update((std::numeric_limits<float>::quiet_NaN)());
    sameSources(before, model.sources());
}
}

int main()
{
    layoutAndPalettes();
    lifecycleAndReplay();
    staggerAndLargeNuclear();
    shortHazards();
    uncappedBins();
    binCoverageMatchesSupport();
    staggeredBinsAndReplay();
    productionSnapshotsAreTransactional();
    rejectInvalidWithoutChangingModel();
    std::puts("cloud stress model: lifecycle, replay, mixed palettes and uncapped bins passed");
}
