/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "Platform/WaterTuning.h"
#include <unistd.h>
namespace WaterTuningTest {
inline void Check() {
    using namespace WaterTuning;
    Values previous;previous.value[Fetch]=12000;unsigned errors=0;
    auto log=[&](unsigned,const char*){++errors;};
    auto valid=Parse("# water\nwind_speed=12.5\nfetch=45000\nswell=1.2\nwind_angle=-135\n"
        "height_long=2\nheight_medium=1\nheight_fine=.3\nsplit=spectrum\nchop_long=2\nchop_short=.9\n"
        "wave_speed=.7\ndepth_long=30\ndepth_medium=9\ndepth_fine=2\ncrest_color=.4\nfoam_threshold=0\nglint=2\n"
        "sea2_share=.4\nsea2_angle=-25\nsea2_wavelength=950\nsea2_spread=18\nspread=.2\n"
        "shore_turn=.6\nshore_reach=1800\nshore_tile=1200\nshore_sharp=4\nshore_bands=long_medium\n",previous,log);
    CHECK(errors==0);CHECK(valid[WindSpeed]==12.5 && valid[Fetch]==45000 && valid[Split]==1);
    CHECK(valid[TravelAngle]==45 && WindAngle(valid)==-135 && valid[HeightFine]==.3 && valid[DepthMedium]==9);
    CHECK(valid[Sea2Share]==.4 && valid[Sea2Angle]==-25 && valid[Sea2Wavelength]==950);
    CHECK(valid[Sea2Spread]==18 && valid[Spread]==.2 && valid[ShoreTurn]==.6);
    CHECK(valid[ShoreReach]==1800 && valid[ShoreTile]==1200 && valid[ShoreSharp]==4 && valid[ShoreBands]==1);
    CHECK(valid[CrestColor]==.4 && valid[FoamThreshold]==0 && valid[Glint]==2);
    auto same=Parse(Serialize(valid),Values{},log);
    for(int i=0;i<Count;++i)CHECK(same.value[i]==valid.value[i]);
    auto partial=Parse("wind_speed=10\n",previous,log);
    CHECK(partial[WindSpeed]==10 && partial[Fetch]==12000 && partial[HeightMedium]==.75);
    std::string clamp;
    for(int i=0;i<Count;++i)if(i!=Split && i!=ShoreBands && i!=TravelMode)clamp+=std::string(Keys[i].name)+"=-999999\n";
    auto low=Parse(clamp,previous,log);
    for(int i=0;i<Count;++i)if(i!=Split && i!=ShoreBands && i!=TravelMode)CHECK(low.value[i]==Keys[i].low);
    clamp.clear();for(int i=0;i<Count;++i)if(i!=Split && i!=ShoreBands && i!=TravelMode)clamp+=std::string(Keys[i].name)+"=999999\n";
    auto high=Parse(clamp,previous,log);
    for(int i=0;i<Count;++i)if(i!=Split && i!=ShoreBands && i!=TravelMode)CHECK(high.value[i]==Keys[i].high);
    errors=0;
    auto bad=Parse("garbage\nunknown=4\nfetch=nan\nswell=inf\nchop_long=1junk\nwave_speed=\n"
        "split=broken\nshore_bands=bad\nheight_fine=1e9999\ndepth_long=3=4\nwind_speed=1.",previous,log);
    CHECK(errors==11);
    for(int i=0;i<Count;++i)CHECK(bad.value[i]==previous.value[i]);
    errors=0;
    auto binary=Parse(std::string("wind_speed=9\n\0fetch=3\n",22),previous,log);
    CHECK(errors==1 && binary[WindSpeed]==previous[WindSpeed]);
    errors=0;auto large=Parse(std::string(65537,'x'),previous,log);
    CHECK(errors==1 && large[Fetch]==12000);
    errors=0;auto prefix=Parse("wind_speed=11\nswell=0.",previous,log);
    CHECK(errors==1 && prefix[WindSpeed]==11 && prefix[Swell]==previous[Swell]);
    auto duplicates=Parse("fetch=30000\nfetch=no\nfetch=35000\n",previous,log);
    CHECK(duplicates[Fetch]==35000);
    CHECK(Parse(";comment\n \twind_speed \t= 9 \r\n\n",previous,log)[WindSpeed]==9);
    CHECK(partial[Sea2Share]==.33 && partial[Spread]==0 && partial[ShoreTurn]==1 && partial[ShoreBands]==2);
    CHECK(Values{}[ShoreBands]==2);
    CHECK(Values{}[HeadingFollow]==1 && Values{}[ShoreTurn]==1);
    auto headingPrevious=previous;headingPrevious.value[HeadingFollow]=.25;
    CHECK(Parse("wind_speed=10\n",headingPrevious,log)[HeadingFollow]==1); // old INIs use the new default
    CHECK(Parse("heading_follow=.4\n",previous,log)[HeadingFollow]==.4);
    CHECK(Parse("heading_follow=nan\n",headingPrevious,[](unsigned,const char*){})[HeadingFollow]==.25);
    CHECK(Parse("heading_follow=0\n",previous,log)[HeadingFollow]==0);
    CHECK(Serialize(valid).find("heading_follow=1\n")!=std::string::npos);
    CHECK(Values{}[TravelAngle]==-135 && WindAngle(Values{})==45);
    CHECK(Values{}[ShoreMaxTurn]==60 && Values{}[ShoreReach]==1500);
    CHECK(Serialize(valid).find("wind_angle=")==std::string::npos);
    CHECK(Serialize(Values{}).find("travel_angle=-135\n")!=std::string::npos);
    CHECK(Serialize(Values{}).find("shore_max_turn=60\n")!=std::string::npos);
    for(double wind:{-180.,-135.,0.,45.,135.,180.}) {
        const auto old=Parse("wind_angle="+std::to_string(wind)+"\n",previous,log);
        CHECK(old[TravelAngle]==NormalizeAngle(wind+180));
        CHECK(NormalizeAngle(WindAngle(old)-wind)==0);
        CHECK(Parse(Serialize(old),Values{},log)[TravelAngle]==old[TravelAngle]);
    }
    auto direction=Parse("travel_angle=70\nshore_max_turn=35\n",previous,log);
    CHECK(direction[TravelAngle]==70 && WindAngle(direction)==-110 && direction[ShoreMaxTurn]==35);
    CHECK(Parse("wind_angle=45\ntravel_angle=90\n",previous,log)[TravelAngle]==90);
    CHECK(Parse("travel_angle=90\nwind_angle=45\n",previous,log)[TravelAngle]==-135);
    CHECK(Values{}[TravelMode]==0);
    auto manualPrevious=previous;manualPrevious.value[TravelMode]=1;
    CHECK(Parse("travel_angle=50\n",manualPrevious,log)[TravelMode]==0);
    CHECK(Parse("travel_mode=invalid\n",manualPrevious,[](unsigned,const char*){})[TravelMode]==1);
    CHECK(Serialize(Values{}).find("travel_mode=auto\n")!=std::string::npos);
    for(const char *mode:{"auto","manual"}) {
        auto parsed=Parse(std::string("travel_mode=")+mode+"\n",previous,log);
        CHECK(parsed[TravelMode]==(std::string(mode)=="manual"));
        CHECK(Parse(Serialize(parsed),Values{},log)[TravelMode]==parsed[TravelMode]);
    }
    errors=0;CHECK(Parse("travel_mode=bad\n",previous,log)[TravelMode]==previous[TravelMode]);CHECK(errors==1);
    const auto savedMap=MapDirection();const auto savedValues=Current();
    Values autoValues;Apply(autoValues);auto before=SpectrumRevision();
    SetMapDirection(90,.8,true);
    CHECK(EffectiveTravelAngle(autoValues)==90 && WindAngle(autoValues)==-90);
    CHECK(SpectrumRevision()==before+1);
    before=SpectrumRevision();autoValues.value[TravelAngle]=30;Apply(autoValues);
    CHECK(SpectrumRevision()==before); // changing the saved manual angle cannot change auto travel
    autoValues.value[TravelMode]=1;Apply(autoValues);
    CHECK(EffectiveTravelAngle(autoValues)==30 && SpectrumRevision()==before+1);
    before=SpectrumRevision();SetMapDirection(-45,.9,true);
    if(Enabled())CHECK(SpectrumRevision()==before); // manual live mode ignores new map direction
    autoValues.value[TravelMode]=0;Apply(autoValues);CHECK(EffectiveTravelAngle(autoValues)==-45);
    SetMapDirection(90,.01,false);CHECK(EffectiveTravelAngle(autoValues)==30); // symmetric fallback
    SetMapDirection(0,0,false);CHECK(EffectiveTravelAngle(autoValues)==30); // next / dry map clears previous result
    // Runtime auto publication is atomic and separate from the manual INI.
    // Exercise the actual watcher used by the tuner, including map invalidation.
    char directory[]="/tmp/water-auto-watch-XXXXXX";CHECK(mkdtemp(directory)!=nullptr);
    const std::string ini=std::string(directory)+"/water-tuning.ini";
    const char *oldFile=std::getenv("ZH_WATER_TUNING_FILE");const bool hadFile=oldFile!=nullptr;
    const std::string savedFile=oldFile?oldFile:"";setenv("ZH_WATER_TUNING_FILE",ini.c_str(),1);
    {
        Watcher watcher;const auto now=std::chrono::steady_clock::now();
        SetMapDirection(90,.8,true);watcher.Poll(directory,now);
        std::ifstream file(ini+".auto");std::string metadata((std::istreambuf_iterator<char>(file)),{});
        CHECK(metadata=="{\"travel_angle\":90,\"strength\":0.8,\"dominant\":true}\n");
        std::ifstream config(ini);std::string manual((std::istreambuf_iterator<char>(config)),{});
        CHECK(manual.find("travel_angle=30\n")!=std::string::npos);
        SetMapDirection(0,0,false);watcher.Poll(directory,now+std::chrono::seconds(1));
        std::ifstream cleared(ini+".auto");std::string invalid((std::istreambuf_iterator<char>(cleared)),{});
        CHECK(invalid=="{\"travel_angle\":0,\"strength\":0,\"dominant\":false}\n");
    }
    if(hadFile)setenv("ZH_WATER_TUNING_FILE",savedFile.c_str(),1);else unsetenv("ZH_WATER_TUNING_FILE");
    std::remove(ini.c_str());std::remove((ini+".auto").c_str());rmdir(directory);
    MapDirection()=savedMap;Apply(savedValues);
    const char *bands[]={"medium","long_medium","all"};
    for(int i=0;i<3;++i) {
        const auto parsed=Parse(std::string("shore_bands=")+bands[i]+"\n",valid,log);
        CHECK(parsed[ShoreBands]==i);
        CHECK(Serialize(parsed).find(std::string("shore_bands=")+bands[i]+"\n")!=std::string::npos);
        CHECK(Parse(Serialize(parsed),Values{},log)[ShoreBands]==i);
    }
    errors=0;
    auto legacy=Parse("shore_depth=18\nshore_depth=nonsense\nshore_reach=950\n",previous,[&](unsigned,const char *message) {
        ++errors;CHECK(std::string(message)=="shore_depth ignored; use shore_reach in game units");
    });
    CHECK(errors==1 && legacy[ShoreReach]==950);
    CHECK(Parse("shore_depth=18\n",previous,log)[ShoreReach]==previous[ShoreReach]);
    CHECK(Serialize(valid).find("shore_depth")==std::string::npos);
    const auto saved=Current();Values applied;Apply(applied);const unsigned revision=SpectrumRevision();
    applied.value[Sea2Share]=.4;
    CHECK(Apply(applied)==1 && SpectrumRevision()==revision+1);
    for(Key key:{TravelAngle,Sea2Angle,Sea2Wavelength,Sea2Spread,Spread}) {
        const auto before=SpectrumRevision();applied.value[key]+=1;
        CHECK(Apply(applied)==1 && SpectrumRevision()==before+1);
    }
    for(Key key:{ShoreTurn,ShoreReach,ShoreTile,ShoreSharp,ShoreBands,ShoreMaxTurn}) {
        const auto before=SpectrumRevision();applied.value[key]+=key==ShoreBands?-1:1;
        CHECK(Apply(applied)==1 && SpectrumRevision()==before);
    }
    Apply(saved);
    { // Heading following is a constant-only live edit, never a spectrum edit.
        const auto before=SpectrumRevision();auto values=Current();values.value[HeadingFollow]=.3;
        CHECK(Apply(values)==1 && SpectrumRevision()==before);Apply(saved);
    }
    // All aliases are seeded even for nonlive clean settings; complete tuples are atomic.
    const char *aliases[]={"ZH_WATER_CLEAN_SEA2","ZH_WATER_CLEAN_SPREAD","ZH_WATER_CLEAN_SHORE","ZH_WATER_CLEAN_SHORE_BANDS",
        "ZH_WATER_CLEAN_WIND_ANGLE","ZH_WATER_CLEAN_TRAVEL_ANGLE","ZH_WATER_CLEAN_SHORE_MAX_TURN","ZH_WATER_CLEAN_TRAVEL_MODE","ZH_WATER_CLEAN_HEADING_FOLLOW"};
    std::string savedAliases[9];bool hadAliases[9];
    for(unsigned i=0;i<9;++i){const char *value=std::getenv(aliases[i]);hadAliases[i]=value!=nullptr;if(value)savedAliases[i]=value;unsetenv(aliases[i]);}
    CHECK(StartValues()[HeadingFollow]==1);
    const char *oldSplit=std::getenv("ZH_WATER_CLEAN_SPLIT");const bool hadSplit=oldSplit!=nullptr;
    const std::string savedSplit=oldSplit?oldSplit:"";
    unsetenv("ZH_WATER_CLEAN_SPLIT");CHECK(StartValues()[Split]==1);
    setenv("ZH_WATER_CLEAN_SPLIT","fixed",1);CHECK(StartValues()[Split]==0);
    setenv("ZH_WATER_CLEAN_SPLIT","spectrum",1);CHECK(StartValues()[Split]==1);
    setenv("ZH_WATER_CLEAN_SPLIT","invalid",1);CHECK(StartValues()[Split]==1);
    if(hadSplit)setenv("ZH_WATER_CLEAN_SPLIT",savedSplit.c_str(),1);else unsetenv("ZH_WATER_CLEAN_SPLIT");
    setenv("ZH_WATER_CLEAN_HEADING_FOLLOW",".4",1);CHECK(StartValues()[HeadingFollow]==.4);
    setenv("ZH_WATER_CLEAN_HEADING_FOLLOW","2",1);CHECK(StartValues()[HeadingFollow]==1);
    setenv("ZH_WATER_CLEAN_HEADING_FOLLOW","nan",1);CHECK(StartValues()[HeadingFollow]==1);
    CHECK(StartValues()[TravelAngle]==-135 && StartValues()[ShoreMaxTurn]==60);
    CHECK(StartValues()[TravelMode]==0);
    setenv("ZH_WATER_CLEAN_TRAVEL_MODE","manual",1);CHECK(StartValues()[TravelMode]==1);
    setenv("ZH_WATER_CLEAN_TRAVEL_MODE","bad",1);CHECK(StartValues()[TravelMode]==0);
    unsetenv("ZH_WATER_CLEAN_TRAVEL_MODE");
    setenv("ZH_WATER_CLEAN_WIND_ANGLE","45",1);CHECK(StartValues()[TravelAngle]==-135);
    setenv("ZH_WATER_CLEAN_TRAVEL_ANGLE","90",1);CHECK(StartValues()[TravelAngle]==90);
    setenv("ZH_WATER_CLEAN_TRAVEL_ANGLE","nan",1);CHECK(StartValues()[TravelAngle]==-135);
    setenv("ZH_WATER_CLEAN_SHORE_MAX_TURN","35",1);CHECK(StartValues()[ShoreMaxTurn]==35);
    setenv("ZH_WATER_CLEAN_SHORE_MAX_TURN","999",1);CHECK(StartValues()[ShoreMaxTurn]==180);
    setenv("ZH_WATER_CLEAN_SHORE_MAX_TURN","nan",1);CHECK(StartValues()[ShoreMaxTurn]==60);
    setenv("ZH_WATER_CLEAN_SEA2",".6,-170,700,22",1);
    setenv("ZH_WATER_CLEAN_SPREAD",".3",1);
    setenv("ZH_WATER_CLEAN_SHORE",".7,2300,1300,5",1);
    setenv("ZH_WATER_CLEAN_SHORE_BANDS","long_medium",1);
    auto seeded=StartValues();
    CHECK(seeded[Sea2Share]==.6 && seeded[Sea2Angle]==-170 && seeded[Sea2Wavelength]==700 && seeded[Sea2Spread]==22);
    CHECK(seeded[Spread]==.3 && seeded[ShoreTurn]==.7 && seeded[ShoreReach]==2300);
    CHECK(seeded[ShoreTile]==1300 && seeded[ShoreSharp]==5 && seeded[ShoreBands]==1);
    for(int i=0;i<3;++i) {
        setenv("ZH_WATER_CLEAN_SHORE_BANDS",bands[i],1);
        CHECK(StartValues()[ShoreBands]==i);
    }
    setenv("ZH_WATER_CLEAN_SHORE_BANDS","invalid",1);CHECK(StartValues()[ShoreBands]==2);
    unsetenv("ZH_WATER_CLEAN_SHORE_BANDS");CHECK(StartValues()[ShoreBands]==2);
    setenv("ZH_WATER_CLEAN_SEA2",".6,garbage,700,22",1);
    CHECK(StartValues()[Sea2Share]==.33);
    for(unsigned i=0;i<9;++i){if(hadAliases[i])setenv(aliases[i],savedAliases[i].c_str(),1);else unsetenv(aliases[i]);}

}
}
