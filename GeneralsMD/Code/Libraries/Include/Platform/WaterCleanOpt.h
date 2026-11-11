/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace WaterCleanOpt {
struct Flags {
    bool stream=false,fused=false,loop=false,finelite=false,half=false;
    float collapse=1.5f;
};
// An explicit list replaces the defaults. Empty/none is the pass-2 baseline.
inline Flags Parse(const char *raw) {
    Flags f;
    if(!raw) {f.stream=true;return f;}  // stream alone timed fastest (2026-11-11)
    const std::string s(raw);
    for(size_t p=0;p<=s.size();) {
        const size_t e=s.find(',',p);std::string t=s.substr(p,e-p);
        const size_t first=t.find_first_not_of(" \t\r\n");
        t=first==std::string::npos?"":t.substr(first,t.find_last_not_of(" \t\r\n")-first+1);
        if(t=="stream")f.stream=true;else if(t=="fused")f.fused=true;
        else if(t=="loop")f.loop=true;else if(t=="finelite")f.finelite=true;else if(t=="half")f.half=true;
        else if(t.compare(0,9,"collapse=")==0) {
            char *end=nullptr;const float v=std::strtof(t.c_str()+9,&end);
            if(end!=t.c_str()+9 && !*end && std::isfinite(v) && v>=0 && v<=180)f.collapse=v;
        }
        if(e==std::string::npos)break;p=e+1;
    }
    return f;
}
inline std::string Describe(const Flags &f) {
    std::string s;
    for(const auto &v:{std::pair<bool,const char*>{f.stream,"stream"},{f.fused,"fused"},
        {f.loop,"loop"},{f.finelite,"finelite"},{f.half,"half"}})
        if(v.first) {if(!s.empty())s+=",";s+=v.second;}
    char number[48];std::snprintf(number,sizeof(number),"collapse=%.9g",double(f.collapse));
    if(!s.empty())s+=",";return s+number;
}
inline const Flags &Get() {
    static const Flags flags=[] {
        const auto f=Parse(std::getenv("ZH_WATER_CLEAN_OPT"));
        std::fprintf(stderr,"ZH_WATER_CLEAN_OPT=%s\n",Describe(f).c_str());return f;
    }();
    return flags;
}
inline void Replace(std::string &s,const std::string &from,const std::string &to) {
    for(size_t p=s.find(from);p!=std::string::npos;p=s.find(from,p+to.size()))s.replace(p,from.size(),to);
}
}
