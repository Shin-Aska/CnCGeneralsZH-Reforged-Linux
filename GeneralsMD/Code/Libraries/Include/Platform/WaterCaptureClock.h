#pragma once
#include <cmath>
#include <cstring>

// A capture-only scenery clock. Pictures are measured in (possibly fractional) logic
// frames, not output video frames. It never advances gameplay or reads wall time.
namespace WaterCaptureClock {
inline bool Enabled(const char *mode, int controlPort, int videoStart, int videoEnd,
                    bool liveGame, bool multiplayer, bool replay)
{
    return mode && std::strcmp(mode, "picture") == 0 && controlPort > 0 && controlPort <= 65535
        && videoStart >= 0 && videoEnd > 0 && videoEnd >= videoStart
        && liveGame && !multiplayer && !replay;
}
inline float PresentationFraction(bool eligible,int pictures,float ordinary)
{
    return eligible && pictures==1 ? 1.0f : ordinary;
}
struct Sample { double picture = 0, seconds = 0, elapsed = 0; bool restarted = false; };
class Clock {
    const double logicFramesPerSecond;
    bool initialized = false;
    double lastPicture = 0, frozenPictures = 0, phasePicture = 0;
public:
    explicit Clock(double rate) : logicFramesPerSecond(rate) {}
    void Reset() { if (initialized) { initialized=false; lastPicture=frozenPictures=phasePicture=0; } }
    Sample Update(double picture, bool frozen)
    {
        Sample result;
        if (!std::isfinite(logicFramesPerSecond) || logicFramesPerSecond <= 0) return result;
        result.picture=phasePicture;result.seconds=phasePicture/logicFramesPerSecond;
        if (!std::isfinite(picture) || picture < 0) return result;
        if (!initialized || picture < lastPicture) {
            initialized=true;lastPicture=phasePicture=picture;frozenPictures=0;
            result.picture=phasePicture;result.seconds=phasePicture/logicFramesPerSecond;result.restarted=true;return result;
        }
        const double interval=picture-lastPicture;
        lastPicture=picture;
        if (frozen) frozenPictures+=interval;
        const double next=picture-frozenPictures;
        result.elapsed=next>phasePicture ? (next-phasePicture)/logicFramesPerSecond : 0;
        phasePicture=next;result.picture=phasePicture;result.seconds=phasePicture/logicFramesPerSecond;return result;
    }
};
}
