// Source-row layout proof for retaining native water static packets.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace WaterPathTracing {
struct StaticSpan {
    const void *Identity=nullptr;
    size_t First=0,Count=0;
    uint64_t Fingerprint=0;
};
inline bool SameStaticSpanLayout(const std::vector<StaticSpan> &before,const std::vector<StaticSpan> &current)
{
    if(before.size()!=current.size())return false;
    for(size_t i=0;i<current.size();++i)
        if(before[i].Identity!=current[i].Identity || before[i].First!=current[i].First
            || before[i].Count!=current[i].Count)return false;
    return true;
}
}
