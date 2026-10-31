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

// Separate from LogicSequence: the first post-load sample is deliberately the
// transition from the previous loading/menu Present, not a steady interval.
struct ColdSequence {
    unsigned target=0,accepted=0,rejected=0;
    bool loadBegun=false,armed=false,started=false,failed=false;
    std::uint32_t first=0,previous=0;
    const char *failure="";
    bool Configure(const char *text) {
        if(!text||!*text)return false;
        std::uint64_t number=0;
        for(;*text;++text) {
            if(*text<'0'||*text>'9')return false;
            number=number*10+unsigned(*text-'0');
            if(number>LogicWindow::MaximumSamples)return false;
        }
        if(number==0)return false;
        target=unsigned(number);return true;
    }
    void Fail(const char *why) {failed=true;++rejected;failure=why;}
    bool BeginLoad() {
        if(started) {Fail("load-interrupted-collection");return false;}
        loadBegun=true;armed=false;failed=false;accepted=rejected=0;failure="";
        return true;
    }
    bool Arm() {
        if(!loadBegun||armed||started) {Fail("invalid-load-ready-boundary");return false;}
        armed=true;return true;
    }
    bool Observe(bool eligible,std::uint32_t frame,bool hasPreviousPresent) {
        if(!armed||failed||accepted>=target)return false;
        if(!started) {
            if(!eligible)return false;
            started=true;first=previous=frame;
            if(!hasPreviousPresent) {Fail("first-transition-has-no-preceding-present");return false;}
        } else if(!eligible||std::uint64_t(previous)+1!=frame) {
            Fail(!eligible?"ineligible-present-after-first":"duplicate-or-gap-after-first");return false;
        }
        previous=frame;++accepted;return true;
    }
    bool Complete() const {return target>0&&accepted==target&&!failed&&rejected==0;}
};
}

#if !defined(_WIN32)
// Called at the render sync point; consumed once by the next device Present.
void PosixGpuTimingStampLogicFrame(std::uint32_t frame,bool eligible);
// Diagnostic-only lifecycle markers use the same SDL epoch as Present CSVs.
// load-ready arms cold collection only after an explicit load-begin marker.
void PosixGpuTimingLoadMarker(const char *name,std::uint32_t frame);
#endif
