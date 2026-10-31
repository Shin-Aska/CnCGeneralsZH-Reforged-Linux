#pragma once
#include "Platform/WaterLocalWaveField.h"
#include "WaterBodyAnalysisTest.h"
namespace WaterLocalWaveFieldTest {
using WaterBodyAnalysisTest::Grid;
using WaterLocalWaveField::Field;
inline void Identity(const Grid&g,const Field&f){
    WaterWaveHeading::Field legacy;CHECK(WaterWaveHeading::Build(g.wet.data(),g.wet.data(),g.w,g.h,0,10,legacy));
    CHECK(f.uStep==legacy.uStep && f.prevailing.x==legacy.prevailing.x && f.prevailing.y==legacy.prevailing.y);
    for(size_t i=0;i<g.wet.size();++i){
        CHECK(f.texels[i*5]==legacy.texels[i*2]);
        CHECK((f.texels[i*5+1]&0xffff0000u)==(legacy.texels[i*2+1]&0xffff0000u));
        if(!(f.texels[i*5+4]&255))CHECK(f.texels[i*5+1]==legacy.texels[i*2+1]);
        else{const auto s=WaterLocalWaveField::DecodeBankVector(f.texels[i*5+1]);CHECK(s.x*s.x+s.y*s.y<=1.0000001f);}
    }
}
inline void Check(){
    const std::vector<uint32_t> envelope={0x20000000u,0x60000055u,0x900000aau,0xe00000ffu};
    const float sampleX=.37f,sampleY=.63f,epsilon=.001f;
    const auto gain=WaterLocalWaveField::SampleLegacyGain(envelope,2,2,sampleX,sampleY,10);
    const float finiteX=(WaterLocalWaveField::SampleLegacyGain(envelope,2,2,sampleX+epsilon,sampleY,10).value-WaterLocalWaveField::SampleLegacyGain(envelope,2,2,sampleX-epsilon,sampleY,10).value)/(20*epsilon);
    const float finiteY=(WaterLocalWaveField::SampleLegacyGain(envelope,2,2,sampleX,sampleY+epsilon,10).value-WaterLocalWaveField::SampleLegacyGain(envelope,2,2,sampleX,sampleY-epsilon,10).value)/(20*epsilon);
    CHECK(std::abs(gain.dx-finiteX)<1e-5f && std::abs(gain.dy-finiteY)<1e-5f);
    CHECK(gain.value>0 && gain.value<1);
    for(float x:{-1.0f,0.0f,1.0f,2.0f}){const auto edge=WaterLocalWaveField::SampleLegacyGain(envelope,2,2,x,sampleY,10);CHECK(edge.dx==0);}
    std::vector<uint32_t> identity(4,0x12000000u);
    const auto seaGain=WaterLocalWaveField::SampleLegacyGain(identity,2,2,.37f,.63f,10);
    CHECK(seaGain.value==1 && seaGain.dx==0 && seaGain.dy==0);
    CHECK(WaterLocalWaveField::PackBankVector({0,0})==0x00008080u);
    for(unsigned angle=0;angle<360;++angle){const float radians=float(angle)*WaterFftSpectrum::Pi/180;
        const auto s=WaterLocalWaveField::DecodeBankVector(WaterLocalWaveField::PackBankVector({std::cos(radians),std::sin(radians)}));
        CHECK(s.x*s.x+s.y*s.y<=1.0000001f && std::hypot(s.x,s.y)>.988f);
    }
    auto build=[](Grid&g,Field&f,const std::vector<WaterBodyAnalysis::VisualConnection>&c=std::vector<WaterBodyAnalysis::VisualConnection>{}){
        return WaterLocalWaveField::Build(g.wet.data(),g.wet.data(),g.links.data(),g.w,g.h,0,10,f,c);
    };
    Field f,again;Grid sea(81,81);sea.Rect(0,0,80,80);sea.Link();CHECK(build(sea,f));Identity(sea,f);
    // Diagonal chart transport requires every edge of its grid square. A wet
    // corner alone cannot shorten a path through a blocked authored segment.
    const uint8_t squareWet[4]={255,255,255,255},squareLinks[4]={3,0,1,0};
    const double inf=std::numeric_limits<double>::infinity();
    std::vector<double> squareSlow(4,1),squareSeed={0,inf,inf,inf},squarePhase;
    CHECK(WaterWaveCharts::SolveChart(squareWet,2,2,squareSlow,squareSeed,0,0,0,squarePhase,squareLinks));
    CHECK(std::abs(squarePhase[3]-2)<1e-8);
    for(size_t i=0;i<sea.wet.size();++i)CHECK((f.texels[i*5+4]&255)==0);
    Grid paddedSea(97,97);paddedSea.Rect(0,0,96,96);paddedSea.Link();
    CHECK(WaterLocalWaveField::Build(paddedSea.wet.data(),paddedSea.wet.data(),paddedSea.links.data(),97,97,8,10,again));
    WaterWaveHeading::Field paddedLegacy;
    CHECK(WaterWaveHeading::Build(paddedSea.wet.data(),paddedSea.wet.data(),97,97,8,10,paddedLegacy));
    for(size_t i=0;i<paddedSea.wet.size();++i){CHECK((again.texels[i*5+4]&255)==0);
        CHECK(again.texels[i*5]==paddedLegacy.texels[i*2] && again.texels[i*5+1]==paddedLegacy.texels[i*2+1]);}
    CHECK(again.analysis.component[0]==-1 && again.analysis.sea[0]==0); // Padding did not join the classifier.
    CHECK(again.texels[2]!=0 && again.texels[3]!=0); // Finite affine phase continuation.
    CHECK(!WaterLocalWaveField::Build(paddedSea.wet.data(),paddedSea.wet.data(),paddedSea.links.data(),97,97,8,10,again,{{-1,100,.65f}}));
    CHECK(again.texels.empty());
    CHECK(WaterLocalWaveField::Build(sea.wet.data(),sea.wet.data(),sea.links.data(),sea.w,sea.h,0,10,again,{},false));
    Identity(sea,again);CHECK(again.analysis.sourceCells.empty());
    CHECK(float(1+again.localStep)>1);
    for(size_t i=0;i<sea.wet.size();++i)CHECK((again.texels[i*5+4]&0x00ffff00u)==0 && (again.texels[i*5+4]&255)==255);
    Grid lake(61,61);lake.Rect(8,8,52,52);lake.Link();CHECK(build(lake,f));Identity(lake,f);
    CHECK(f.localConverged && f.analysis.sourceCells.empty());
    CHECK(float(1+f.localStep)>1);
    for(size_t i=0;i<lake.wet.size();++i)if(lake.wet[i]){
        CHECK((f.texels[i*5+4]&0x00ffff00u)==0 && (f.texels[i*5+4]&255)==255);
        CHECK((f.texels[i*5+4]>>24)>=15 && (f.texels[i*5+4]>>24)<=16);
    }
    // The two persistent island routes remain separate beyond their meeting,
    // with opposite transverse tangents and nonzero independent amplitude.
    Grid island(97,97);island.Rect(4,0,92,96);
    for(int y=0;y<97;++y)for(int x=0;x<97;++x)if(std::hypot(double(x-48),double(y-48))<11)island.wet[y*97+x]=0;
    island.Link();CHECK(build(island,f));CHECK(f.localConverged);Identity(island,f);
    // The upstream island face needs an outward water normal even though both
    // arrival charts point directly into it. Sides retain their own independent
    // normals rather than borrowing the source heading or arrival direction.
    for(const auto&point:std::vector<std::pair<int,int>>{{48,59},{37,48},{59,48},{48,37}}){
        const int i=point.second*97+point.first;const auto s=WaterLocalWaveField::DecodeBankVector(f.texels[i*5+1]);
        const float rx=float(point.first-48)/11,ry=float(point.second-48)/11;
        CHECK(s.x*rx+s.y*ry>.99f);
        if(point.second==59)for(unsigned k=0;k<2;++k){const auto v=WaterWaveHeading::DecodeGradient(f.texels[i*5+2+k]);
            const float inward=v.x*s.x+v.y*s.y;CHECK(inward<-.5f);
            const float correctedX=v.x-s.x*(std::min)(inward,0.0f),correctedY=v.y-s.y*(std::min)(inward,0.0f);
            CHECK(correctedX*s.x+correctedY*s.y>-.001f);
        }
    }
    // Near a curved stair-step bank the smoothed clearance normal agrees with
    // the physical radial normal; the 50-world cutoff packs exact neutral.
    unsigned nearBank=0;
    for(int y=32;y<=64;++y)for(int x=32;x<=64;++x){const int i=y*97+x;if(!island.wet[i])continue;
        const auto s=WaterLocalWaveField::DecodeBankVector(f.texels[i*5+1]);
        const float radius=std::hypot(float(x-48),float(y-48));
        if(f.analysis.clearance[i]<=10){CHECK((s.x*(x-48)+s.y*(y-48))/radius>.97f);++nearBank;}
        if(f.analysis.clearance[i]>=50)CHECK((f.texels[i*5+1]&65535)==0x8080u);
    }
    CHECK(nearBank>40);
    unsigned crossing=0;
    for(int y=15;y<37;++y)for(int x=43;x<=53;++x){const int i=y*97+x;
        const auto g0=WaterWaveHeading::DecodeGradient(f.texels[i*5+2]),g1=WaterWaveHeading::DecodeGradient(f.texels[i*5+3]);
        const uint32_t m=f.texels[i*5+4];
        if(g0.x*g1.x<-.02f && ((m>>16)&255) && ((m>>8)&255))++crossing;
    }
    CHECK(crossing>0);
    for(int y=1;y<96;++y)for(int x=5;x<92;++x){const int i=y*97+x;
        if(!island.wet[i] || !island.wet[i-1] || !island.wet[i+1] || !island.wet[i-97] || !island.wet[i+97])continue;
        for(unsigned k=0;k<2;++k){const auto g=WaterWaveHeading::DecodeGradient(f.texels[i*5+2+k]);
            const float dx=(WaterWaveHeading::DecodeU(f.texels[(i+1)*5+2+k],f.localStep)-WaterWaveHeading::DecodeU(f.texels[(i-1)*5+2+k],f.localStep))/20;
            const float dy=(WaterWaveHeading::DecodeU(f.texels[(i+97)*5+2+k],f.localStep)-WaterWaveHeading::DecodeU(f.texels[(i-97)*5+2+k],f.localStep))/20;
            if(std::abs(g.x)<WaterWaveHeading::GradientRange-.1f)CHECK(std::abs(g.x-dx)<WaterWaveHeading::GradientRange/254+f.localStep/10+.001f);
            if(std::abs(g.y)<WaterWaveHeading::GradientRange-.1f)CHECK(std::abs(g.y-dy)<WaterWaveHeading::GradientRange/254+f.localStep/10+.001f);
        }
    }
    for(size_t i=0;i<island.wet.size();++i)if(island.wet[i]){
        const uint32_t m=f.texels[i*5+4];CHECK(((m>>16)&255)+((m>>8)&255)<=unsigned(std::lround(f.analysis.energy[i]*255)));
    }
    CHECK(build(island,again));CHECK(f.texels==again.texels && f.localStep==again.localStep);
    // Mixed source sea and a narrow continuation share the same atlas. Sea
    // bypasses must still preserve every original byte alongside local banks.
    Grid seaStream(181,121);seaStream.Rect(0,0,100,120);seaStream.Rect(100,50,180,70);seaStream.Link();
    CHECK(build(seaStream,f));CHECK(f.analysis.sourceKind==WaterBodyAnalysis::SourceKind::BroadSea);Identity(seaStream,f);
    unsigned seaSamples=0,localSamples=0;
    for(size_t i=0;i<seaStream.wet.size();++i)if(seaStream.wet[i]){
        if(f.texels[i*5+4]&255)++localSamples;else ++seaSamples;
    }
    CHECK(seaSamples>100 && localSamples>100);
    Grid paddedStream(73,49);paddedStream.Rect(0,16,72,32);paddedStream.Link();
    CHECK(WaterLocalWaveField::Build(paddedStream.wet.data(),paddedStream.wet.data(),paddedStream.links.data(),73,49,8,10,f));
    const int apronWet=16*73,playableWet=apronWet+8,apronGhost=15*73;
    CHECK((f.texels[apronWet*5+1]&65535)==(f.texels[playableWet*5+1]&65535));
    CHECK((f.texels[apronGhost*5+1]&65535)==(f.texels[apronWet*5+1]&65535));
    CHECK(WaterLocalWaveField::DecodeBankVector(f.texels[apronWet*5+1]).y>.99f);
    // One uncovered edge partitions adjacent wet pixels. The unsourced half
    // stays calm and receives neither local chart's amplitude.
    Grid gap(61,31);gap.Rect(0,8,60,22);gap.Link();for(int y=8;y<=22;++y)gap.links[y*61+29]&=~WaterBodyAnalysis::PositiveX;
    CHECK(build(gap,f));const int dryArrival=15*61+10;CHECK(!std::isfinite(f.analysis.distance[dryArrival]));
    CHECK((f.texels[dryArrival*5+4]&0x00ffff00u)==0);
    CHECK(WaterLocalWaveField::DecodeBankVector(f.texels[(15*61+29)*5+1]).x<-.99f);
    CHECK(WaterLocalWaveField::DecodeBankVector(f.texels[(15*61+30)*5+1]).x>.99f);
    // Phase traverses an explicit underpass by its geometric span. Changing
    // transmission changes amplitude/arrival energy, never the phase delay.
    Grid bridge(121,31);bridge.Rect(0,8,50,22);bridge.Rect(56,8,110,22);bridge.Link();
    const int a=15*121+50,b=15*121+56;
    CHECK(build(bridge,f,{{a,b,.65f}}));CHECK(f.localConverged);
    CHECK(build(bridge,again,{{a,b,.35f}}));CHECK(again.localConverged);
    for(unsigned k=0;k<2;++k){
        const float ua=WaterWaveHeading::DecodeU(f.texels[a*5+2+k],f.localStep),ub=WaterWaveHeading::DecodeU(f.texels[b*5+2+k],f.localStep);
        CHECK(std::abs(ub-ua-60)<=f.localStep*1.1f+.001f);
        const float other=WaterWaveHeading::DecodeU(again.texels[b*5+2+k],again.localStep);
        CHECK(std::abs(ub-other)<=f.localStep+again.localStep+.001f);
    }
    CHECK(f.analysis.energy[b]>again.analysis.energy[b]);
    // A dry ghost extrapolates the adjacent patch's own scalar/tangent and has
    // zero local weights; its blend envelope remains continuous at the bank.
    const int wet=15*121+56,ghost=wet-1;
    CHECK((f.texels[ghost*5+4]&0x00ffff00u)==0);
    CHECK((f.texels[ghost*5+4]&0xff0000ffu)==(f.texels[wet*5+4]&0xff0000ffu));
    CHECK((f.texels[ghost*5+1]&65535)==(f.texels[wet*5+1]&65535));
    for(unsigned k=0;k<2;++k){const auto g=WaterWaveHeading::DecodeGradient(f.texels[wet*5+2+k]);
        const float expected=WaterWaveHeading::DecodeU(f.texels[wet*5+2+k],f.localStep)-g.x*10;
        const float actual=WaterWaveHeading::DecodeU(f.texels[ghost*5+2+k],f.localStep);
        CHECK(std::abs(expected-actual)<=f.localStep*1.1f+10*WaterWaveHeading::GradientRange/254+.001f);
    }
    // B is reached before C at its west port, but its far east port receives a
    // shorter geometric route through C. A single component-order pass skips
    // that late dependency and incorrectly reports a much larger port delay.
    Grid lateBridge(121,61);lateBridge.Rect(0,50,120,60);
    lateBridge.Rect(5,35,115,40);lateBridge.Rect(5,20,115,30);lateBridge.Link();
    const int sourceWest=50*121+5,bodyBWest=40*121+5;
    const int sourceEast=50*121+115,bodyCEast=30*121+115,bodyBEast=35*121+115;
    CHECK(build(lateBridge,f,{{sourceWest,bodyBWest,.65f},{sourceEast,bodyCEast,.65f},{bodyCEast,bodyBEast,.65f}}));
    CHECK(f.localConverged && f.failedComponents==0);Identity(lateBridge,f);
    CHECK(f.analysis.component[bodyBWest]==f.analysis.component[bodyBEast]);
    CHECK(f.analysis.component[bodyBEast]!=f.analysis.component[bodyCEast]);
    CHECK(f.analysis.distance[bodyBWest]<f.analysis.distance[bodyCEast]);
    CHECK(f.analysis.distance[bodyCEast]<f.analysis.distance[bodyBEast]);
    for(unsigned k=0;k<2;++k){
        const float from=WaterWaveHeading::DecodeU(f.texels[bodyCEast*5+2+k],f.localStep);
        const float to=WaterWaveHeading::DecodeU(f.texels[bodyBEast*5+2+k],f.localStep);
        CHECK(std::abs(to-from-50)<=f.localStep*1.1f+.001f);
    }
}
}
