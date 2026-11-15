#include "W3DDevice/GameClient/WeatherParticleMath.h"
#include "GameClient/WeatherBreezeMath.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <initializer_list>

static int failures=0;
static void expect(bool condition,const char *message)
{
    if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message); ++failures; }
}
int main(int argc,char **argv)
{
    using namespace WeatherParticleMath;
    // A cached random speed factor and phase must survive every point of a rain fade.
    const float cachedStep=0.71f,initialPhase=19.25f;
    expect(WeatherBreezeMath::period(150,90,0)==150 && WeatherBreezeMath::period(150,90,1)==90,
        "fully dry and wet rain use the script and storm periods respectively");
    expect(WeatherBreezeMath::period(150,90,.5f)==120 && WeatherBreezeMath::period(60,90,.5f)==60,
        "period blends through a fade without slowing a stronger scripted breeze");
    expect(WeatherBreezeMath::step(cachedStep,150,150)==cachedStep,
        "ordinary off/dry period retains the exact legacy step bits");
    const float dryStep=WeatherBreezeMath::step(cachedStep,90,150);
    const float wetStep=WeatherBreezeMath::step(cachedStep,90,90);
    expect(std::fabs(wetStep/dryStep-150.0f/90.0f)<0.00001f,
        "dry cached storm steps restore the default 150-frame script speed rather than 90");
    float previous=0;
    for (int i=0;i<=100;++i) {
        const int desired=WeatherBreezeMath::period(150,90,float(i)/100);
        const float delta=WeatherBreezeMath::step(cachedStep,90,desired);
        expect(delta>=previous && delta<=cachedStep,"fade advances phase monotonically within original random speed bounds");
        previous=delta;
    }
    float phase=initialPhase;int lastFrame=-1;
    for (int render=0;render<240;++render) {
        const int frame=render/4;const bool paused=frame>=20 && frame<30;
        if (!paused && frame!=lastFrame) {
            lastFrame=frame;phase+=WeatherBreezeMath::step(cachedStep,90,150);
        }
    }
    expect(std::fabs(phase-(initialPhase+50*dryStep))<0.0002f,
        "paused and repeated render frames never advance tree phase; resumed dry phase keeps its original origin");
    expect(cachedStep==0.71f,"fading never rewrites the cached random step");
    if (argc==4 && (std::strcmp(argv[1],"--rain-window")==0 || std::strcmp(argv[1],"--rain-opening")==0)) {
        const bool opening=std::strcmp(argv[1],"--rain-opening")==0;
        const double seconds=opening ? 0.0 : std::strtod(argv[2],nullptr);
        const std::uint32_t seed=opening ? rainSeedForMatch(std::uint32_t(std::strtoul(argv[2],nullptr,0)),
            std::uint32_t(std::strtoul(argv[3],nullptr,0))) : std::uint32_t(std::strtoul(argv[3],nullptr,0));
        const RainSpell spell=rainSpell(seconds,seed);
        std::printf("{\"weatherSeed\":%u,\"envelope\":%.9g,\"slotStart\":%.9g,\"wetStart\":%.9g,\"plateauStart\":%.9g,"
            "\"plateauEnd\":%.9g,\"wetEnd\":%.9g,\"slotEnd\":%.9g}\n",seed,spell.envelope,
            spell.slotStart,spell.wetStart,spell.plateauStart,spell.plateauEnd,spell.wetEnd,spell.slotEnd);
        return 0;
    }
    Motion motion; motion.reset();
    for (int i=0;i<100;++i) motion.advance(0.01f,1.0f,-0.5f,130.0f,25.0f,1.0f);
    expect(std::fabs(motion.x-130.0)<0.001 && std::fabs(motion.y+65.0)<0.001,
        "integrated wind agrees with one second of travel");
    expect(std::fabs(motion.fall-25.0)<0.001,"vertical travel uses the same elapsed time");
    const double before=motion.x;
    motion.advance(0.01f,1.0f,-0.5f,130.0f,25.0f,1.5f);
    expect(std::fabs(motion.x-before-1.95)<0.001,"gust changes velocity without reprojecting elapsed position");
    motion.advance(0.0f,1,1,130,25,1);
    motion.advance(-1.0f,1,1,130,25,1);
    motion.advance(std::numeric_limits<float>::quiet_NaN(),1,1,130,25,1);
    expect(std::fabs(motion.x-before-1.95)<0.001,"paused/invalid steps do not advance position");
    const double stalled=motion.x;
    motion.advance(10.0f,1,0,130,25,1);
    expect(std::fabs(motion.x-stalled-13.0)<0.001,"a stalled frame has a bounded presentation step");
    const Presentation left=sample(3599.999),right=sample(3600.001);
    expect(std::fabs(left.gust-right.gust)<0.001 && std::fabs(left.intensity-right.intensity)<0.001,
        "gust and density remain continuous across the old one-hour clock boundary");
    motion.reset();
    double seconds=0;
    for (int i=0;i<1000000;++i) {
        seconds+=0.016;
        const Presentation p=sample(seconds);
        const double previous=motion.x;
        motion.advance(0.016f,0.6f,-0.8f,145,19,p.gust);
        if (!std::isfinite(motion.x) || !std::isfinite(motion.y) || !std::isfinite(motion.fall)
            || p.gust<0.65f || p.gust>1.35f || p.intensity<0.75f || p.intensity>1.01f
            || std::fabs(motion.x-previous)>1.9) {
            expect(false,"long-running weather stays finite, bounded and continuous"); break;
        }
    }
    motion.reset();
    expect(motion.x==0 && motion.y==0 && motion.fall==0,"reset discards travel from the previous map/kind");
    expect(!surface(20,false,100).water,"missing polygon water cannot create a ripple");
    expect(!surface(20,true,19).water,"terrain above water receives a ground splash");
    expect(!surface(20,true,20.05f).water,"shore tolerance avoids coplanar water z-fighting");
    const Surface wet=surface(20,true,25);
    expect(wet.water && wet.height==25,"water above terrain receives a waterline ripple");
    expect(!surface(20,true,std::numeric_limits<float>::quiet_NaN()).water,"invalid water height cannot create a ripple");
    expect(crossesSurface(30,24,25,0.1,1,2),"downward trajectory crossing the waterline emits one contact");
    expect(!crossesSurface(24,30,25,0.1,1,2),"fall wrap is not a contact");
    expect(!crossesSurface(30,24,25,0.4,1,2),"stale trajectories do not emit contacts");
    expect(!crossesSurface(30,24,25,0.1,100,2),"camera/tile teleport does not emit a contact");
    expect(!crossesSurface(24,20,25,0.1,1,2),"already submerged particle does not emit again");
    const int rates[]={15,20,30,60};
    for (int fps:rates) for (float depth:{384.0f,1536.0f}) {
        const float dt=1.0f/fps;
        const int tracks=contactTrackCount(dt,depth,1100.0f);
        expect(tracks>=48 && tracks<=256,"contact work stays within the bounded tracked set");
        double sampled[256]; float previousZ[256];
        for (int i=0;i<256;++i) { sampled[i]=-1; previousZ[i]=0; }
        int cursor=0,contacts=0,continuous=0;
        for (int frame=0;frame<240;++frame) {
            const double time=frame*double(dt);
            for (int i=0;i<48;++i) {
                const int index=cursor; cursor=(cursor+1)%tracks;
                const float phase=float(std::fmod(time*1100/depth+double(index)/tracks,1.0));
                const float z=100.0f-phase*depth;
                if (sampled[index]>=0) {
                    const double elapsed=time-sampled[index];
                    expect(elapsed<0.3 && elapsed<depth/1100.0,
                        "15/20/30/60 FPS revisit remains timely and cannot skip a full fall cycle");
                    ++continuous;
                    if (crossesSurface(previousZ[index],z,0,elapsed,0,0)) ++contacts;
                }
                sampled[index]=time; previousZ[index]=z;
            }
        }
        expect(continuous>0 && contacts>0,"rain contacts remain active at 15/20/30/60 FPS");
    }
    const std::uint32_t rainSeed=rainSeedForMatch(12345,7);
    int wetOpenings=0,dryOpenings=0;
    for (std::uint32_t match=0;match<1024;++match) {
        const std::uint32_t seed=rainSeedForMatch(12345,match);
        const RainSpell opening=rainSpell(0,seed);
        expect(opening.slotStart<=0 && opening.slotEnd>0,
            "a randomized opening lies within a valid weather slot");
        if (opening.envelope>0) ++wetOpenings; else ++dryOpenings;
    }
    expect(wetOpenings>0 && dryOpenings>wetOpenings,
        "new match seeds produce both rainy and clear openings, with clear starts more common");
    expect(rainSeedForMatch(12345,7)!=rainSeedForMatch(12345,8)
        && rainSeedForMatch(12345,7)!=rainSeedForMatch(54321,7),
        "map and immutable match seed both influence weather without a random-stream draw");
    // The slot containing time zero may have begun before the map loaded. Check full future slots.
    for (int cycle=1;cycle<=20;++cycle) {
        const RainSpell spell=rainSpell(cycle*210.0+20.0,rainSeed);
        expect(spell.wetEnd-spell.wetStart>=48 && spell.wetEnd-spell.wetStart<=56,
            "rain wet episodes have bounded minute-scale lengths");
        expect(spell.plateauEnd-spell.plateauStart>=32,
            "each wet spell retains a full thirty-second active benchmark window");
        expect(rainSpell(spell.wetStart,rainSeed).envelope==0
            && rainSpell(spell.wetEnd,rainSeed).envelope==0,
            "wet episode boundaries are dry without a hard opacity pop");
        expect(rainSpell((spell.plateauStart+spell.plateauEnd)*0.5,rainSeed).envelope==1,
            "active plateau has full rain occupancy");
        expect(rainSpell(spell.wetEnd+20,rainSeed).envelope==0,
            "long dry break suppresses rain");
        const RainSpell next=rainSpell((cycle+1)*210.0+20,rainSeed);
        expect(next.wetStart-spell.wetEnd>=138 && next.wetStart-spell.wetEnd<=178,
            "dry intervals are longer than wet episodes");
        for (double edge:{spell.wetStart,spell.plateauStart,spell.plateauEnd,spell.wetEnd,spell.slotEnd}) {
            const double h=0.001;
            const double l=rainSpell(edge-h,rainSeed).envelope,m=rainSpell(edge,rainSeed).envelope;
            const double r=rainSpell(edge+h,rainSeed).envelope;
            expect(std::fabs(l-r)<0.001 && std::fabs((r-m)-(m-l))/h<0.001,
                "rain fade value and first derivative are continuous at every timing boundary");
        }
    }
    const RainSpell first=rainSpell(0,rainSeed),reset=rainSpell(0,rainSeed);
    expect(first.wetStart==reset.wetStart && first.plateauEnd==reset.plateauEnd,
        "reset replays the same local cosmetic rain schedule");
    expect(first.envelope==reset.envelope && first.slotStart==reset.slotStart,
        "the same map and match seed reproduce the opening for replay/capture");
    expect(rainSpell(230,1).wetStart!=rainSpell(230,2).wetStart,
        "independent local seeds choose independent later episode timing");
    expect(rainSpell(std::numeric_limits<double>::infinity(),rainSeed).envelope==0
        && rainSpell(-10,rainSeed).envelope==0,"invalid weather time is dry and finite");
    for (double time:{1.0e12,1.0e100,1.0e300}) {
        const RainSpell spell=rainSpell(time,rainSeed);
        expect(std::isfinite(spell.envelope) && spell.envelope>=0 && spell.envelope<=1
            && std::isfinite(spell.slotStart) && std::isfinite(spell.wetEnd),
            "very large finite time cannot overflow rain scheduling or hashes");
    }
    const RainSpell future=rainSpell(first.slotEnd+0.001,rainSeed);
    const double nextPlateau=(future.plateauStart+future.plateauEnd)*0.5;
    double dryClock=0;
    while (dryClock<nextPlateau) dryClock+=1.0/60.0;
    expect(rainSpell(dryClock,rainSeed).envelope>0,
        "continuing the selected rain clock traverses the dry break and reaches the next wet spell");
    const Presentation beforeSchedule=sample(260);
    rainSpell(260,rainSeed);
    const Presentation afterSchedule=sample(260);
    expect(beforeSchedule.gust==afterSchedule.gust && beforeSchedule.intensity==afterSchedule.intensity,
        "rain scheduling cannot alter the sand or snow presentation sample");
    const Point shoreA={0,0,10},shoreB={100,0,10},shoreC={0,100,10};
    expect(triangleOverlapsBoxAbove(shoreA,shoreB,shoreC,5,5,15,15,0),
        "dust footprint above water is rejected");
    expect(triangleOverlapsBoxAbove(shoreA,shoreB,shoreC,-5,5,5,15,0),
        "a footprint crossing the shoreline is rejected even when its center is on land");
    expect(!triangleOverlapsBoxAbove(shoreA,shoreB,shoreC,80,80,90,90,0),
        "water triangle bounding-box overlap cannot reject dry ground outside its triangle");
    expect(!triangleOverlapsBoxAbove(shoreA,shoreB,shoreC,5,5,15,15,20),
        "broad water polygons hidden below dry terrain do not suppress land dust");
    expect(footprintShoreFade(-20,0,-10,10,0,0,96,96,32)>0
        && footprintShoreFade(-20,0,-10,10,0,0,96,96,32)<1,
        "dust fades on the dry side before its footprint reaches water");
    expect(footprintShoreFade(-10,0,1,10,0,0,96,96,32)==0,
        "any water overlap suppresses the complete dust footprint");
    expect(footprintShoreFade(-60,0,-40,10,0,0,96,96,32)==1,
        "dust well inside verified dry land keeps its normal opacity");
    expect(dustArrival(0,true)==0 && dustArrival(0.3,true)>0 && dustArrival(0.3,true)<1
        && dustArrival(0.6,true)==1,"newly verified dry cells ease from zero through a ramp to full dust");
    expect(dustArrival(10,false)==0,"unknown and wet cells never admit dust");
    const double resolution=20.0,revisionResolution=25.0,resetResolution=0.0;
    expect(dustArrival(25-resolution,true)==1 && dustArrival(25-revisionResolution,true)==0
        && dustArrival(0-resetResolution,true)==0,
        "revision and map reset resolution timestamps restart the dust arrival ramp");
    const Point a={0,0,10},b={10,0,20},c={0,10,30}; float height=0;
    expect(triangleHeight(a,b,c,2,3,&height) && std::fabs(height-18)<0.001,
        "render water triangle returns its sloping surface height");
    expect(triangleHeight(c,b,a,2,3,&height) && std::fabs(height-18)<0.001,
        "water triangle winding does not change the height");
    Point normal,reverse;
    expect(triangleHeight(a,b,c,2,3,&height,&normal),"sloped river provides an orientation");
    expect(normal.z>0 && std::fabs(normal.x*normal.x+normal.y*normal.y+normal.z*normal.z-1)<0.000001,
        "sloped river normal points upward and is normalized");
    expect(std::fabs(normal.x*10+normal.z*10)<0.000001
        && std::fabs(normal.y*10+normal.z*20)<0.000001,
        "ring orientation is perpendicular to both sloping water edges");
    expect(std::fabs(normal.x)>0.1 && std::fabs(normal.y)>0.1,
        "sloped river ring cannot retain a horizontal plane");
    expect(triangleHeight(c,b,a,2,3,&height,&reverse)
        && std::fabs(normal.x-reverse.x)+std::fabs(normal.y-reverse.y)+std::fabs(normal.z-reverse.z)<0.000001,
        "water ring orientation is independent of triangle winding");
    const Point flatA={0,0,12},flatB={10,0,12},flatC={0,10,12};
    expect(triangleHeight(flatA,flatB,flatC,2,3,&height,&normal)
        && normal.x==0 && normal.y==0 && normal.z==1,
        "pond ring retains the horizontal water plane");
    expect(!triangleHeight(a,b,c,9,9,&height),"point outside water triangle is rejected");
    expect(!triangleHeight(a,a,c,0,0,&height),"degenerate water geometry is rejected");
    if (!failures) std::puts("weather particle math: all checks passed");
    return failures ? 1 : 0;
}
