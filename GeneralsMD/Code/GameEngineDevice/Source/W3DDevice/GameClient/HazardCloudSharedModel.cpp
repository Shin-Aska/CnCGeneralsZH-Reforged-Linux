#include "Platform/HazardCloudSharedModel.h"
#include "Platform/HazardCloudPolicy.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace HazardCloudShared {
namespace {
constexpr float Displacement = 18;
constexpr float TravelSpeed = 22;

float rampDuration(int palette, float radius, float lifetime)
{
    return (std::min)(palette == 1 && radius >= 100 ? 6.0f : 3.0f, lifetime * .25f);
}

// Exact integral of release=1-.65*smoothstep(age/ramp), followed by zero.
// Evaluating from birth makes seeking, replay, and different update rates agree.
float travelAt(float age, float ramp)
{
    const float t = (std::min)((std::max)(age, 0.0f), ramp);
    const float u = t / ramp;
    return TravelSpeed * (t - .65f * ramp * (u * u * u - .5f * u * u * u * u));
}

float peakDensityAt(float age, float rise, float lifetime, float endDuration)
{
    float peakTime = rise;
    if (lifetime - endDuration < rise) {
        // Very short hazards can fade before their density ramp ends. Solve
        // d(smoothstep(t/rise)*(lifetime-t))/dt=0 for the historical maximum.
        const double a = 6.0 * lifetime + 9.0 * rise;
        const double discriminant = a * a - 192.0 * rise * lifetime;
        const float u = float(12.0 * lifetime / (a + std::sqrt((std::max)(0.0, discriminant))));
        peakTime = std::clamp(u * rise, (std::max)(0.0f, lifetime - endDuration), rise);
    }
    const float t = (std::min)(age, peakTime);
    return HazardCloudPolicy::smoothProgress(t, rise)
        * std::clamp((lifetime - t) / endDuration, 0.0f, 1.0f);
}
}

bool Model::configure(const Config &config)
{
    const float finite[] = {config.centreX, config.centreY, config.groundZ, config.radius,
        config.fringe, config.height, config.spacing, config.lifetime, config.stagger};
    for (float v : finite) if (!std::isfinite(v)) return false;
    if (!config.count || config.palette < -1 || config.palette > 3 || config.radius <= 0
        || config.fringe < 0 || config.height <= 0 || config.spacing < 0
        || config.lifetime * .25f <= 0 || config.stagger < 0
        || (config.layout != Layout::Grid && config.layout != Layout::Overlap)) return false;
    const unsigned columns = config.columns ? config.columns
        : static_cast<unsigned>(std::ceil(std::sqrt(double(config.count))));
    const unsigned rows = (config.count - 1) / columns + 1;
    const float width = float((std::min)(columns, config.count) - 1) * config.spacing;
    const float depth = float(rows - 1) * config.spacing;
    const float reach = config.radius + config.fringe + Displacement;
    Bounds nextDomain;
    nextDomain.minX = config.centreX - (config.layout == Layout::Grid ? width * .5f : 0) - reach;
    nextDomain.maxX = config.centreX + (config.layout == Layout::Grid ? width * .5f : 0) + reach;
    nextDomain.minY = config.centreY - (config.layout == Layout::Grid ? depth * .5f : 0) - reach;
    nextDomain.maxY = config.centreY + (config.layout == Layout::Grid ? depth * .5f : 0) + reach;
    nextDomain.minZ = config.groundZ;
    nextDomain.maxZ = config.groundZ + config.height * 2.6f;
    const float derived[] = {nextDomain.minX, nextDomain.minY, nextDomain.maxX,
        nextDomain.maxY, nextDomain.maxZ, float(config.count - 1) * config.stagger + config.lifetime};
    for (float v : derived) if (!std::isfinite(v)) return false;
    if (nextDomain.maxX <= nextDomain.minX || nextDomain.maxY <= nextDomain.minY
        || nextDomain.maxZ <= nextDomain.minZ || config.groundZ + config.height <= config.groundZ) return false;
    if (!HazardCloudPolicy::validField(config.centreX, config.centreY, config.groundZ,
            config.radius, config.height, 0, 0, config.fringe, 0)) return false;

    std::vector<Source> next(config.count);
    static const int mixed[] = {0, 2, 3, 1};
    for (unsigned i = 0; i < config.count; ++i) {
        Source &source = next[i];
        source.id = i;
        source.field.ground.x = config.centreX;
        source.field.ground.y = config.centreY;
        source.field.ground.z = config.groundZ;
        if (config.layout == Layout::Grid) {
            source.field.ground.x += float(i % columns) * config.spacing - width * .5f;
            source.field.ground.y += float(i / columns) * config.spacing - depth * .5f;
        }
        source.field.radius = config.radius;
        source.field.fringe = config.fringe;
        source.field.height = config.height;
        source.field.palette = float(config.palette < 0 ? mixed[i % 4] : config.palette);
        source.field.formation = source.field.palette == 1 ? 1 : 0;
        // Match the approved procedural pattern for each independent source.
        source.seed = 0;
        source.born = float(i) * config.stagger;
        source.expiry = source.born + config.lifetime;
    }
    settings = config;
    previewConfigured = true;
    values.swap(next);
    domain = nextDomain;
    launched = false;
    launchedAt = 0;
    return true;
}

bool Model::setSources(const std::vector<Source> &sources, const Bounds &bounds)
{
    const float coordinates[] = {bounds.minX, bounds.minY, bounds.minZ,
        bounds.maxX, bounds.maxY, bounds.maxZ};
    for (float value : coordinates) if (!std::isfinite(value)) return false;
    if (!(bounds.maxX > bounds.minX && bounds.maxY > bounds.minY && bounds.maxZ > bounds.minZ)
        || !std::isfinite(bounds.maxX - bounds.minX) || !std::isfinite(bounds.maxY - bounds.minY)
        || !std::isfinite(bounds.maxZ - bounds.minZ)
        || sources.size() > (std::numeric_limits<std::uint32_t>::max)()) return false;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const auto &source = sources[i];
        const auto &field = source.field;
        const float finite[] = {field.palette, field.release, source.sourceAge, source.sourceTravel,
            source.dissipation, source.seed, source.born, source.expiry};
        for (float value : finite) if (!std::isfinite(value)) return false;
        if (source.id != i || field.palette < 0 || field.palette > 3
            || field.palette != std::floor(field.palette)
            || !HazardCloudPolicy::validField(field.ground.x, field.ground.y, field.ground.z,
                field.radius, field.height, field.density, field.formation, field.fringe, int(field.palette))
            || field.release < 0 || field.release > 1 || source.sourceAge < 0 || source.sourceTravel < 0
            || source.dissipation < 0 || source.dissipation > 1 || source.expiry < source.born)
            return false;
    }
    values = sources;
    previewConfigured = false;
    domain = bounds;
    launched = false;
    return true;
}

void Model::launch(float absoluteSeconds)
{
    if (!previewConfigured || !std::isfinite(absoluteSeconds)) return;
    const double finalExpiry = double(absoluteSeconds) + double(values.empty() ? 0 : values.size() - 1)
        * settings.stagger + settings.lifetime;
    if (std::abs(finalExpiry) > (std::numeric_limits<float>::max)()) return;
    launchedAt = absoluteSeconds;
    launched = true;
    for (Source &source : values) {
        source.born = launchedAt + float(source.id) * settings.stagger;
        source.expiry = source.born + settings.lifetime;
    }
    update(absoluteSeconds);
}

void Model::update(float absoluteSeconds)
{
    if (!launched || !std::isfinite(absoluteSeconds)) return;
    for (Source &source : values) {
        const float age = (std::max)(0.0f, absoluteSeconds - source.born);
        const int palette = int(source.field.palette);
        const auto buildup = HazardCloudPolicy::visualBuildup(palette, settings.radius >= 100,
            age, settings.lifetime);
        const float left = (std::max)(0.0f, settings.lifetime - age);
        const float endDuration = (std::max)(.1f, (std::min)(3.0f, settings.lifetime * .25f));
        source.field.formation = buildup.formation;
        source.field.release = buildup.release;
        source.field.density = buildup.density * std::clamp(left / endDuration, 0.0f, 1.0f);
        source.sourceAge = age;
        source.sourceTravel = travelAt(age, rampDuration(palette, settings.radius, settings.lifetime));
        const float riseDuration = palette == 1 ? rampDuration(palette, settings.radius, settings.lifetime)
            : (std::min)(.6f, rampDuration(palette, settings.radius, settings.lifetime));
        const float peak = peakDensityAt(age, riseDuration, settings.lifetime, endDuration);
        source.dissipation = peak > .0001f ? (std::max)(0.0f, 1 - source.field.density / peak) : 0;
        if (absoluteSeconds < source.born) {
            source.field.density = source.field.release = 0;
            source.sourceAge = source.sourceTravel = source.dissipation = 0;
        }
        source.active = absoluteSeconds >= source.born && absoluteSeconds < source.expiry
            && source.field.density > .00001f;
    }
}

Grid Model::buildBins(float cellSize) const
{
    Grid grid;
    if (!std::isfinite(cellSize) || cellSize <= 0
        || domain.maxX <= domain.minX || domain.maxY <= domain.minY) return grid;
    grid.minX = float(std::floor(double(domain.minX) / cellSize) * cellSize);
    grid.minY = float(std::floor(double(domain.minY) / cellSize) * cellSize);
    const double columns = std::floor((double(domain.maxX) - grid.minX) / cellSize) + 1;
    const double rows = std::floor((double(domain.maxY) - grid.minY) / cellSize) + 1;
    const auto maxIndex = (std::numeric_limits<std::uint32_t>::max)();
    if (!std::isfinite(grid.minX) || !std::isfinite(grid.minY) || columns < 1 || rows < 1
        || columns * rows >= maxIndex) return {};
    grid.cellSize = cellSize;
    grid.width = static_cast<unsigned>(columns);
    grid.height = static_cast<unsigned>(rows);
    const std::size_t cells = std::size_t(grid.width) * grid.height;
    grid.offsets.assign(cells + 1, 0);
    const auto visit = [&](const Source &source, auto emit) {
        const float reach = source.field.radius + source.field.fringe + Displacement;
        const auto cell = [cellSize](double point, float origin, unsigned count) {
            return static_cast<unsigned>(std::clamp(std::floor((point - origin) / cellSize), 0.0, double(count - 1)));
        };
        const unsigned loX = cell(double(source.field.ground.x) - reach, grid.minX, grid.width);
        const unsigned hiX = cell(double(source.field.ground.x) + reach, grid.minX, grid.width);
        const unsigned loY = cell(double(source.field.ground.y) - reach, grid.minY, grid.height);
        const unsigned hiY = cell(double(source.field.ground.y) + reach, grid.minY, grid.height);
        for (unsigned y = loY; y <= hiY; ++y)
            for (unsigned x = loX; x <= hiX; ++x) emit(std::size_t(y) * grid.width + x);
    };
    std::uint64_t total = 0;
    for (const Source &source : values) if (source.active) {
        visit(source, [&](std::size_t cell) { ++grid.offsets[cell + 1]; ++total; });
        if (total > maxIndex) return {};
    }
    for (std::size_t cell = 1; cell <= cells; ++cell) grid.offsets[cell] += grid.offsets[cell - 1];
    grid.indices.resize(static_cast<std::size_t>(total));
    std::vector<std::uint32_t> cursor(grid.offsets.begin(), grid.offsets.end() - 1);
    for (const Source &source : values) if (source.active)
        visit(source, [&](std::size_t cell) { grid.indices[cursor[cell]++] = source.id; });
    return grid;
}

} // namespace HazardCloudShared
