/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#pragma pop_macro("max")
#pragma pop_macro("min")

// Render/client thread only. Merely including this header performs no file I/O.
namespace WaterTuning {
enum Key { WindSpeed, Fetch, Swell, TravelAngle, HeightLong, HeightMedium, HeightFine,
    Split, ChopLong, ChopShort, WaveSpeed, DepthLong, DepthMedium, DepthFine,
    CrestColor, FoamThreshold, Glint, Sea2Share, Sea2Angle, Sea2Wavelength, Sea2Spread, Spread,
    ShoreTurn, ShoreReach, ShoreTile, ShoreSharp, ShoreBands, ShoreMaxTurn, TravelMode, HeadingFollow, Count };
struct Definition { const char *name; double low, high, initial; };
inline constexpr Definition Keys[Count]={
    {"wind_speed",1,30,8.7}, {"fetch",1000,200000,20000}, {"swell",0,2,.7}, {"travel_angle",-180,180,-135},
    {"height_long",0,3,1.5}, {"height_medium",0,3,.75}, {"height_fine",0,3,.18}, {"split",0,1,1},
    {"chop_long",0,2.5,1.83}, {"chop_short",0,2.5,1.92}, {"wave_speed",.1,1.5,.4},
    {"depth_long",.5,40,24}, {"depth_medium",.5,40,12}, {"depth_fine",.5,40,4},
    {"crest_color",0,1,.11}, {"foam_threshold",0,1,.73}, {"glint",0,4,1},
    {"sea2_share",0,.9,.33}, {"sea2_angle",-180,180,135}, {"sea2_wavelength",100,1500,600},
    {"sea2_spread",1,40,12}, {"spread",0,1,0}, {"shore_turn",0,1,1},
    {"shore_reach",100,3000,1500}, {"shore_tile",100,3000,300}, {"shore_sharp",1,8,3}, {"shore_bands",0,2,2},
    {"shore_max_turn",0,180,60}, {"travel_mode",0,1,0}, {"heading_follow",0,1,1}
};
struct Values {
    double value[Count];
    Values() { for(int i=0;i<Count;++i)value[i]=Keys[i].initial; }
    double operator[](Key key) const {return value[key];}
};
inline double NormalizeAngle(double degrees) {
    double angle=std::fmod(degrees+180,360);
    if(angle<0)angle+=360;
    return angle-180;
}
// The FFT's positive phase travels opposite its spectral heading. Keep that
// internal convention (and exactly 45 degrees at the default travel of 225).
struct AutoDirection {double angle=0,strength=0;bool dominant=false;unsigned revision=0;};
inline AutoDirection &MapDirection() {static AutoDirection direction;return direction;}
inline double EffectiveTravelAngle(const Values &values) {
    return values[TravelMode]==0 && MapDirection().dominant?MapDirection().angle:values[TravelAngle];
}
inline double WindAngle(const Values &values) {return NormalizeAngle(EffectiveTravelAngle(values)-180);}
inline double LegacyTravelAngle(double wind) {
    return NormalizeAngle((std::max)(-180.,(std::min)(180.,wind))+180);
}
inline bool Enabled() {
#if defined(__linux__)
    static const bool enabled=[] {
        const char *clean=std::getenv("ZH_WATER_CLEAN"), *tune=std::getenv("ZH_WATER_TUNING");
        return !(clean && std::strcmp(clean,"0")==0) && tune && std::strcmp(tune,"1")==0;
    }();
    return enabled;
#else
    return false;
#endif
}
inline double Clamp(int key,double v) {return (std::max)(Keys[key].low,(std::min)(Keys[key].high,v));}
inline bool Number(const std::string &s,double &v) {
    char *end=nullptr;errno=0;v=std::strtod(s.c_str(),&end);
    return end!=s.c_str() && *end=='\0' && errno!=ERANGE && std::isfinite(v);
}
inline std::string Trim(const std::string &s) {
    const auto a=s.find_first_not_of(" \t\r"), b=s.find_last_not_of(" \t\r");
    return a==std::string::npos?std::string():s.substr(a,b-a+1);
}
// Match the existing tuple validators before clamping to the narrower tuner ranges.
inline Values StartValues() {
    Values result;double a,b,c;char tail;
    const char *s=std::getenv("ZH_WATER_CLEAN_SPECTRUM");
    if(s && std::sscanf(s,"%lf,%lf,%lf %c",&a,&b,&c,&tail)==3
        && std::isfinite(a) && a>=1 && a<=50 && std::isfinite(b) && b>=100 && b<=1000000
        && std::isfinite(c) && c>=0 && c<=2) {
        result.value[WindSpeed]=Clamp(WindSpeed,a);result.value[Fetch]=Clamp(Fetch,b);result.value[Swell]=c;
    }
    s=std::getenv("ZH_WATER_CLEAN_LOOK");
    if(s && std::sscanf(s,"%lf,%lf,%lf %c",&a,&b,&c,&tail)==3
        && std::isfinite(a) && a>=0 && a<=1 && std::isfinite(b) && b>=0 && b<=1
        && std::isfinite(c) && c>=0 && c<=4) {
        result.value[CrestColor]=a;result.value[FoamThreshold]=b;result.value[Glint]=c;
    }
    s=std::getenv("ZH_WATER_CLEAN_SPLIT");
    if(s && std::strcmp(s,"spectrum")==0)result.value[Split]=1;
    else if(s && std::strcmp(s,"fixed")==0)result.value[Split]=0;
    s=std::getenv("ZH_WATER_FFT_TIMESCALE");
    const double speed=s?std::atof(s):Keys[WaveSpeed].initial;
    result.value[WaveSpeed]=Clamp(WaveSpeed,speed>0 && speed<=4?speed:Keys[WaveSpeed].initial);
    const auto tuple=[&](const char *env,Key first,int count) {
        const char *raw=std::getenv(env);if(!raw)return;
        const std::string text(raw);double parsed[4];size_t at=0;
        for(int i=0;i<count;++i) {
            const auto end=text.find(',',at);
            if((end==std::string::npos)!=(i==count-1) || !Number(Trim(text.substr(at,end-at)),parsed[i]))return;
            at=end+1;
        }
        for(int i=0;i<count;++i)result.value[first+i]=Clamp(first+i,parsed[i]);
    };
    tuple("ZH_WATER_CLEAN_HEIGHTS",HeightLong,3);tuple("ZH_WATER_CLEAN_CHOP",ChopLong,2);
    tuple("ZH_WATER_CLEAN_DEPTHS",DepthLong,3);
    s=std::getenv("ZH_WATER_CLEAN_WIND_ANGLE");double legacyWind;
    if(s && Number(Trim(s),legacyWind))result.value[TravelAngle]=LegacyTravelAngle(legacyWind);
    // Explicit travel direction takes precedence over the legacy environment alias.
    tuple("ZH_WATER_CLEAN_TRAVEL_ANGLE",TravelAngle,1);
    s=std::getenv("ZH_WATER_CLEAN_TRAVEL_MODE");
    if(s && std::strcmp(s,"manual")==0)result.value[TravelMode]=1;
    tuple("ZH_WATER_CLEAN_SEA2",Sea2Share,4);tuple("ZH_WATER_CLEAN_SPREAD",Spread,1);
    tuple("ZH_WATER_CLEAN_SHORE",ShoreTurn,4);
    tuple("ZH_WATER_CLEAN_SHORE_MAX_TURN",ShoreMaxTurn,1);
    tuple("ZH_WATER_CLEAN_HEADING_FOLLOW",HeadingFollow,1);
    s=std::getenv("ZH_WATER_CLEAN_SHORE_BANDS");
    if(s && std::strcmp(s,"medium")==0)result.value[ShoreBands]=0;
    else if(s && std::strcmp(s,"long_medium")==0)result.value[ShoreBands]=1;
    return result;
}
inline Values &Current() {static Values values=StartValues();return values;}
inline const Values &Configured() {static const Values startup=StartValues();return Enabled()?Current():startup;}
inline unsigned &SpectrumRevision() {static unsigned revision=0;return revision;}
inline unsigned &Revision() {static unsigned revision=0;return revision;}
inline unsigned Apply(const Values &next) {
    unsigned changed=0;bool spectrum=EffectiveTravelAngle(Current())!=EffectiveTravelAngle(next);
    for(int i=0;i<Count;++i)if(Current().value[i]!=next.value[i]) {
        ++changed;spectrum=spectrum || (i<=Split && i!=TravelAngle) || (i>=Sea2Share && i<=Spread);
    }
    Current()=next;if(changed)++Revision();if(spectrum)++SpectrumRevision();return changed;
}
// Called only by clean-water map preparation. Uses the same revision/rebuild
// mechanism as live direction edits, including nonlive clean rendering.
inline void SetMapDirection(double angle,double strength,bool dominant) {
    const double before=EffectiveTravelAngle(Configured());
    auto &map=MapDirection();map.angle=NormalizeAngle(angle);map.strength=strength;map.dominant=dominant;++map.revision;
    if(before!=EffectiveTravelAngle(Configured())) {++Revision();++SpectrumRevision();}
}
template<class Log> inline Values Parse(const std::string &text,const Values &previous,Log log) {
    Values result=previous;
    if(text.size()>65536 || text.find('\0')!=std::string::npos) {
        log(0,"binary or oversized file ignored");return result;
    }
    size_t at=0;unsigned lineNumber=0;bool loggedLegacyShore=false,sawTravelMode=false,sawHeadingFollow=false;
    while(at<text.size()) {
        ++lineNumber;const size_t end=text.find('\n',at);
        // Never accept a truncated numeric tail (e.g. 0. instead of 0.72).
        if(end==std::string::npos) {log(lineNumber,"unterminated line ignored");break;}
        const std::string line=Trim(text.substr(at,end-at));at=end+1;
        if(line.empty() || line[0]=='#' || line[0]==';')continue;
        const auto equal=line.find('=');
        const std::string name=Trim(line.substr(0,equal));int key=0;
        if(equal!=std::string::npos && name=="shore_depth") {
            if(!loggedLegacyShore)log(lineNumber,"shore_depth ignored; use shore_reach in game units");
            loggedLegacyShore=true;continue;
        }
        const bool legacyWind=equal!=std::string::npos && name=="wind_angle";
        if(legacyWind)key=TravelAngle;
        else for(;key<Count && name!=Keys[key].name;++key){}
        if(equal==std::string::npos || key==Count) {log(lineNumber,"unknown key or missing '='");continue;}
        const std::string value=Trim(line.substr(equal+1));double number;
        if(key==HeadingFollow)sawHeadingFollow=true;
        if(key==TravelMode) {
            sawTravelMode=true;
            if(value!="auto" && value!="manual") {log(lineNumber,"travel_mode must be auto or manual");continue;}
            number=value=="manual";
        } else if(key==Split) {
            if(value!="fixed" && value!="spectrum") {log(lineNumber,"split must be fixed or spectrum");continue;}
            number=value=="spectrum";
        } else if(key==ShoreBands) {
            if(value!="medium" && value!="long_medium" && value!="all") {log(lineNumber,"shore_bands must be medium, long_medium or all");continue;}
            number=value=="all"?2:value=="long_medium"?1:0;
        } else if(!Number(value,number)) {log(lineNumber,"expected a finite number");continue;}
        result.value[key]=legacyWind?LegacyTravelAngle(number):Clamp(key,number);
    }
    if(!sawTravelMode)result.value[TravelMode]=0; // old files opt into per-map auto
    if(!sawHeadingFollow)result.value[HeadingFollow]=1; // old files follow the map's wave paths
    return result;
}
inline std::string Serialize(const Values &values) {
    std::ostringstream out;out.precision(12);
    for(int i=0;i<Count;++i) {
        out<<Keys[i].name<<'=';
        if(i==TravelMode)out<<(values.value[i]?"manual":"auto");
        else if(i==Split)out<<(values.value[i]?"spectrum":"fixed");
        else if(i==ShoreBands)out<<(values.value[i]>=2?"all":values.value[i]>=1?"long_medium":"medium");else out<<values.value[i];
        out<<'\n';
    }
    return out.str();
}
class Watcher {
    std::string path;
    std::chrono::steady_clock::time_point next{};
    struct stat seen{};bool haveSeen=false,started=false,reportedCreate=false;
    unsigned publishedMap=~0u;
public:
    void Poll(const char *userData,std::chrono::steady_clock::time_point now=std::chrono::steady_clock::now()) {
#if defined(__linux__)
        if(now<next)return;
        next=now+std::chrono::milliseconds(500);
        if(!started) {
            started=true;const char *overridePath=std::getenv("ZH_WATER_TUNING_FILE");
            if(overridePath && *overridePath)path=overridePath;
            else {
                // The engine's user-data path ends in a Windows separator; use POSIX separators.
                std::string base=userData?userData:"";
                for(char &c:base)if(c=='\\')c='/';
                while(base.size()>1 && base.back()=='/')base.pop_back();
                path=base+"/water-tuning.ini";
            }
        }
        // Separate runtime metadata preserves the configured manual/fallback angle
        // and avoids racing the tuner's atomic INI edits. Publish once per map result.
        if(publishedMap!=MapDirection().revision) {
            const std::string temporary=path+".auto.tmp",destination=path+".auto";
            if(FILE *file=std::fopen(temporary.c_str(),"w")) {
                const auto &map=MapDirection();
                const bool written=std::fprintf(file,"{\"travel_angle\":%.12g,\"strength\":%.12g,\"dominant\":%s}\n",
                    map.angle,map.strength,map.dominant?"true":"false")>0;
                const bool closed=std::fclose(file)==0;
                if(written && closed && std::rename(temporary.c_str(),destination.c_str())==0)publishedMap=map.revision;
                else std::remove(temporary.c_str());
            }
        }
        struct stat stamp{};
        if(::stat(path.c_str(),&stamp)!=0) {
            if(errno==ENOENT && !haveSeen) {
                // Exclusive creation avoids overwriting a page/editor racing startup.
                FILE *file=std::fopen(path.c_str(),"wx");
                if(file) {
                    const auto text=Serialize(Current());
                    const bool ok=std::fwrite(text.data(),1,text.size(),file)==text.size();
                    const bool closed=std::fclose(file)==0;
                    if(!ok || !closed)std::fprintf(stderr,"water tuning: could not write %s\n",path.c_str());
                } else if(errno!=EEXIST && !reportedCreate) {
                    reportedCreate=true;
                    std::fprintf(stderr,"water tuning: could not create %s: %s\n",path.c_str(),std::strerror(errno));
                }
            }
            return;
        }
        if(haveSeen && stamp.st_mtim.tv_sec==seen.st_mtim.tv_sec && stamp.st_mtim.tv_nsec==seen.st_mtim.tv_nsec
            && stamp.st_size==seen.st_size && stamp.st_ino==seen.st_ino)return;
        if(stamp.st_size<0 || stamp.st_size>65536 || !S_ISREG(stamp.st_mode)) {
            seen=stamp;haveSeen=true;std::fprintf(stderr,"water tuning: nonregular or oversized file ignored\n");return;
        }
        std::ifstream input(path,std::ios::binary);if(!input)return;
        std::string content(size_t(stamp.st_size),'\0');input.read(content.data(),content.size());
        struct stat after{};
        if(input.gcount()!=std::streamsize(content.size()) || ::stat(path.c_str(),&after)!=0
            || after.st_size!=stamp.st_size || after.st_ino!=stamp.st_ino
            || after.st_mtim.tv_sec!=stamp.st_mtim.tv_sec || after.st_mtim.tv_nsec!=stamp.st_mtim.tv_nsec)return;
        seen=stamp;haveSeen=true;
        const Values values=Parse(content,Current(),[](unsigned line,const char *reason) {
            std::fprintf(stderr,"water tuning: line %u: %s\n",line,reason);
        });
        const unsigned changes=Apply(values);
        if(changes)std::fprintf(stderr,"water tuning: applied %u values\n",changes);
#else
        (void)userData;(void)now;
#endif
    }
};
inline void Poll(const char *userData) {if(Enabled()){static Watcher watcher;watcher.Poll(userData);}}
}
