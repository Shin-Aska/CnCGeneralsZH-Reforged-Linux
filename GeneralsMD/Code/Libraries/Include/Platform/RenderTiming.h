// Optional host frame timing diagnostics. No simulation state or renderer dependency.
#pragma once
#include <cstdint>

namespace RenderTiming {
struct LogicWindow {
    std::uint32_t first=0,last=0;
    static constexpr unsigned MaximumSamples=100000;
    bool Parse(const char *text) {
        if(!text)return false;
        auto number=[&](std::uint32_t &value) {
            if(*text<'0'||*text>'9')return false;
            std::uint64_t result=0;
            do {
                result=result*10+unsigned(*text++-'0');
                if(result>UINT32_MAX)return false;
            } while(*text>='0'&&*text<='9');
            value=std::uint32_t(result);return true;
        };
        std::uint32_t a,b;
        if(!number(a)||*text++!=','||!number(b)||*text||a==0||b<a
            ||std::uint64_t(b)-a+1>MaximumSamples)return false;
        first=a;last=b;return true;
    }
    unsigned Count() const {return last-first+1;}
};
struct LogicSequence {
    bool previousValid=false;
    std::uint32_t previous=0;
    unsigned accepted=0,rejected=0;
    bool Observe(const LogicWindow &window,bool valid,std::uint32_t frame) {
        bool selected=false;
        if(valid&&frame>=window.first&&frame<=window.last) {
            selected=previousValid&&std::uint64_t(previous)+1==frame
                &&std::uint64_t(window.first)+accepted==frame;
            if(selected)++accepted;else ++rejected;
        }
        previousValid=valid;previous=frame;return selected;
    }
    bool Complete(const LogicWindow &window) const {return accepted==window.Count()&&rejected==0;}
};
}

#if !defined(_WIN32)
// Called at the render sync point; consumed once by the next device Present.
void PosixGpuTimingStampLogicFrame(std::uint32_t frame,bool eligible);
#endif
