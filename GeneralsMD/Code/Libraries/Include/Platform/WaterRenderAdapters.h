#pragma once

#include <cstdint>

#include "Platform/WaterRenderBridge.h"

namespace WaterRenderAdapters {

template<class Matrix>
inline void DeviceView(const Matrix &engine,float output[16])
{
    for(unsigned i=0;i<16;++i)output[i]=0;
    for(unsigned row=0;row<3;++row)for(unsigned column=0;column<4;++column)
        output[column*4+row]=engine[row][column];
    output[15]=1;
}

template<class Matrix>
inline void DeviceProjection(const Matrix &engine,float output[16])
{
    for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column)
        output[column*4+row]=engine[row][column];
}

struct GridMetadata { float u2,v2; };
inline GridMetadata ModernGridMetadata(float worldHeight) { return {0,worldHeight}; }

inline std::uint64_t NextCaptureSerial(std::uint64_t &sequence)
{
    if(++sequence==0) ++sequence;
    return sequence;
}

class CaptureScope
{
public:
    std::uint64_t Begin() { return active=NextCaptureSerial(sequence); }
    void End() { active=0; }
    bool Stamp(WaterRenderBridge::PrepareRequest &request) const
    {
        if(!active) return false;
        request.captureSerial=active;
        return true;
    }

private:
    std::uint64_t sequence=0,active=0;
};

}
