#pragma once

// Render-side Linux water preferences. Updated at the water render boundary, never
// during an in-flight draw. Shared shader callers retain the original defaults.
namespace WaterPerformanceSettings {
enum Refraction { Full = 0, Simple = 1, Off = 2 };
struct Configuration {
    bool sceneReflections = true;
    unsigned requestedFftResolution = 256;
    unsigned activeFftResolution = 256; // the successfully allocated ocean, including fallback
    Refraction refraction = Full;
    bool sceneColourRequired = true; // resolved from refraction and the effective reflection mode
    int reflectionDetail = 1; // planar target detail; 0 Low, 1 original High
};
inline Configuration &Applied() {
    static Configuration configuration;
    return configuration;
}
inline void Apply(bool reflections, int waveDetail, int refraction, int reflectionDetail = 1) {
    Configuration &configuration = Applied();
    configuration.sceneReflections = reflections;
    configuration.requestedFftResolution = waveDetail == 0 ? 128 : 256;
    configuration.refraction = refraction == Simple ? Simple : refraction == Off ? Off : Full;
    configuration.sceneColourRequired = configuration.refraction != Off || reflections;
    configuration.reflectionDetail = reflectionDetail == 0 ? 0 : 1;
}
}
