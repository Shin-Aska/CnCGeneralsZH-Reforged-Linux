/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
namespace WaterCleanProfile {
struct Flags {bool nolong=false,nomips=false,nodepth=false,nofoam=false,nosss=false,noshore=false,flatvertex=false;};
inline const Flags &Environment() {
    static const Flags flags=[] {Flags f;const char *raw=std::getenv("ZH_WATER_CLEAN_PROFILE");
        if(!raw || !*raw)return f;
        std::string s(raw);size_t p=0;std::string active;
        while(p<=s.size()) {size_t e=s.find(',',p);std::string t=s.substr(p,e-p);const size_t first=t.find_first_not_of(" \t\r\n");t=first==std::string::npos?"":t.substr(first,t.find_last_not_of(" \t\r\n")-first+1);bool known=true;
            if(t=="nolong")f.nolong=true;else if(t=="nomips")f.nomips=true;else if(t=="nodepth")f.nodepth=true;
            else if(t=="nofoam")f.nofoam=true;else if(t=="nosss")f.nosss=true;else if(t=="noshore")f.noshore=true;
            else if(t=="flatvertex")f.flatvertex=true;else known=false;
            if(known && (","+active+",").find(","+t+",")==std::string::npos) {if(!active.empty())active+=",";active+=t;}if(e==std::string::npos)break;p=e+1;
        }
        if(!active.empty())std::fprintf(stderr,"water-clean-profile: %s\n",active.c_str());return f;
    }();return flags;
}
// The renderer publishes this snapshot at a water render/map-load boundary.
// Shader generation and CPU field builders never consult mutable game options.
inline unsigned Key(const Flags &f) {
    return unsigned(f.nolong) | unsigned(f.nomips)<<1 | unsigned(f.nodepth)<<2 |
        unsigned(f.nofoam)<<3 | unsigned(f.nosss)<<4 | unsigned(f.noshore)<<5 | unsigned(f.flatvertex)<<6;
}
inline Flags Effective(const Flags &environment, bool shoreWaveTurning, bool waveFiltering, bool modern, bool clean) {
    Flags flags=environment;
#if defined(__linux__)
    if(modern && clean) {
        flags.nomips |= !waveFiltering;
        flags.noshore |= !shoreWaveTurning;
    }
#endif
    return flags;
}
inline Flags &Applied() {static Flags flags=Environment();return flags;}
inline const Flags &Get() {return Applied();}
inline bool Apply(bool shoreWaveTurning, bool waveFiltering, bool modern, bool clean) {
    const Flags next=Effective(Environment(),shoreWaveTurning,waveFiltering,modern,clean);
    const bool changed=Key(next)!=Key(Applied());Applied()=next;return changed;
}
}
