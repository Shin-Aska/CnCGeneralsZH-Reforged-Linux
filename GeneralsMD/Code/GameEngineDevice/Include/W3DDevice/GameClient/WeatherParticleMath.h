// Render-only weather arithmetic, independent of game state and random streams.
#pragma once
#include <cmath>
#include <cstdint>

namespace WeatherParticleMath {
struct Presentation {
    float gust, intensity;
};
inline Presentation sample(double seconds)
{
    Presentation result;
    result.gust=1.0f+0.23f*float(std::sin(seconds*0.37))+0.12f*float(std::sin(seconds*0.91+0.7));
    result.intensity=0.88f+0.10f*float(std::sin(seconds*0.13+0.8))+0.02f*float(std::sin(seconds*0.43));
    return result;
}
inline std::uint32_t hash(std::uint32_t value)
{
    value^=value>>16; value*=0x7feb352dU; value^=value>>15; value*=0x846ca68bU; value^=value>>16;
    return value;
}
inline float unitSeed(std::uint32_t value) { return float(hash(value)&0x00ffffffU)/16777216.0f; }
inline std::uint32_t rainSeedForMatch(std::uint32_t mapSeed,std::uint32_t matchSeed)
{
    // Read the match's immutable seed without drawing from any shared random stream.
    return hash(mapSeed^hash(matchSeed+0x9e3779b9U));
}
struct RainSpell {
    float envelope;
    double slotStart, wetStart, plateauStart, plateauEnd, wetEnd, slotEnd;
};
inline float eased(float x)
{
    if (x<=0) return 0;
    if (x>=1) return 1;
    return x*x*(3.0f-2.0f*x);
}
inline RainSpell rainSpell(double seconds,std::uint32_t seed)
{
    RainSpell result={0,0,0,0,0,0,0};
    if (!std::isfinite(seconds) || seconds<0.0) return result;
    const double slotLength=210.0;
    // Start at a seeded point in the weather cycle, including its longer dry interval.
    // The opening can already be wet or clear; it is not forced to begin a storm.
    const double openingPhase=slotLength*unitSeed(seed^0x85ebca6bU);
    const double phasedSeconds=seconds+openingPhase;
    const double slotNumber=std::floor(phasedSeconds/slotLength);
    const std::uint32_t slot=std::uint32_t(std::fmod(slotNumber,4294967296.0));
    const std::uint32_t key=hash(seed^hash(slot+0x9e3779b9U));
    const double lead=16.0*unitSeed(key+1U);
    const double length=48.0+8.0*unitSeed(key+2U);
    const double fade=6.0+2.0*unitSeed(key+3U);
    const double local=std::fmod(phasedSeconds,slotLength);
    result.slotStart=seconds-local; result.slotEnd=result.slotStart+slotLength;
    result.wetStart=result.slotStart+lead; result.wetEnd=result.wetStart+length;
    result.plateauStart=result.wetStart+fade; result.plateauEnd=result.wetEnd-fade;
    if (local<lead || local>=lead+length) return result;
    const float enter=eased(float((local-lead)/fade));
    const float leave=eased(float((lead+length-local)/fade));
    result.envelope=enter<leave ? enter : leave;
    return result;
}
struct Motion {
    double x, y, fall;
    void reset() { x=y=fall=0.0; }
    void advance(float dt, float windX, float windY, float along, float down, float gust)
    {
        if (!std::isfinite(dt) || !std::isfinite(windX) || !std::isfinite(windY)
            || !std::isfinite(gust) || dt<=0.0f) return;
        if (dt>0.1f) dt=0.1f; // a stalled presentation never catches up in one visible jump
        x+=double(windX)*along*gust*dt;
        y+=double(windY)*along*gust*dt;
        fall+=double(down)*(0.92f+0.08f*gust)*dt;
    }
};
inline int contactTrackCount(float dt, float depth, float down)
{
    // 48 samples per frame remain enough at low frame rates by tracking fewer drops.
    // Revisit before a quarter second and half a fall cycle, rather than accepting stale hits.
    if (!std::isfinite(dt) || dt<=0.0f || depth<=0.0f || down<=0.0f) return 48;
    const float halfCycle=depth/(2.0f*down);
    const float interval=halfCycle<0.24f ? halfCycle : 0.24f;
    if (interval/dt>=6.0f) return 256;
    int frames=int(std::floor(interval/dt));
    if (frames<1) frames=1;
    return frames*48;
}
struct Surface {
    float height;
    bool water;
};
inline Surface surface(float terrainHeight, bool hasWater, float waterHeight)
{
    Surface result;
    result.water=hasWater && std::isfinite(waterHeight) && waterHeight>terrainHeight+0.1f;
    result.height=result.water ? waterHeight : terrainHeight;
    return result;
}
inline bool crossesSurface(float previousZ, float currentZ, float height,
    double elapsed, float deltaX, float deltaY)
{
    // Wrapping, a camera teleport/zoom, and an old sample are not a physical ground contact.
    return elapsed>0.0 && elapsed<0.3 && std::fabs(deltaX)<80.0f && std::fabs(deltaY)<80.0f
        && currentZ<previousZ && currentZ<=height && previousZ>height;
}
struct Point { double x, y, z; };
inline bool triangleHeight(const Point &a, const Point &b, const Point &c,
    double x, double y, float *height, Point *normal=nullptr)
{
    const double det=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);
    if (std::fabs(det)<0.0001) return false;
    const double u=((b.y-c.y)*(x-c.x)+(c.x-b.x)*(y-c.y))/det;
    const double v=((c.y-a.y)*(x-c.x)+(a.x-c.x)*(y-c.y))/det;
    if (u<0.0 || v<0.0 || u+v>1.0) return false;
    *height=float(u*a.z+v*b.z+(1.0-u-v)*c.z);
    if (!std::isfinite(*height)) return false;
    if (normal) {
        const double abX=b.x-a.x, abY=b.y-a.y, abZ=b.z-a.z;
        const double acX=c.x-a.x, acY=c.y-a.y, acZ=c.z-a.z;
        normal->x=abY*acZ-abZ*acY;
        normal->y=abZ*acX-abX*acZ;
        normal->z=abX*acY-abY*acX;
        const double length=std::sqrt(normal->x*normal->x+normal->y*normal->y+normal->z*normal->z);
        const double scale=(normal->z<0.0 ? -1.0 : 1.0)/length;
        normal->x*=scale; normal->y*=scale; normal->z*=scale;
    }
    return true;
}
inline bool triangleOverlapsBoxAbove(const Point &a,const Point &b,const Point &c,
    double minX,double minY,double maxX,double maxY,double terrainMinimum)
{
    Point input[12]={a,b,c}, output[12]; int count=3;
    for (int edge=0;edge<4 && count;++edge) {
        int nextCount=0;
        const double bound=edge==0 ? minX : edge==1 ? maxX : edge==2 ? minY : maxY;
        for (int i=0;i<count;++i) {
            const Point &p=input[i], &q=input[(i+1)%count];
            const double pv=edge<2 ? p.x : p.y, qv=edge<2 ? q.x : q.y;
            const bool pin=(edge==0 || edge==2) ? pv>=bound : pv<=bound;
            const bool qin=(edge==0 || edge==2) ? qv>=bound : qv<=bound;
            if (pin) output[nextCount++]=p;
            if (pin!=qin) {
                const double t=(bound-pv)/(qv-pv);
                output[nextCount++]={p.x+t*(q.x-p.x),p.y+t*(q.y-p.y),p.z+t*(q.z-p.z)};
            }
        }
        count=nextCount;
        for (int i=0;i<count;++i) input[i]=output[i];
    }
    for (int i=0;i<count;++i) if (input[i].z>terrainMinimum+0.1) return true;
    return false;
}
inline float dustArrival(double elapsed,bool verifiedDry)
{
    return verifiedDry && std::isfinite(elapsed) ? eased(float(elapsed/0.6)) : 0.0f;
}
inline float footprintShoreFade(double minX,double minY,double maxX,double maxY,
    double wetMinX,double wetMinY,double wetMaxX,double wetMaxY,double fadeDistance)
{
    const double dx=minX>wetMaxX ? minX-wetMaxX : wetMinX>maxX ? wetMinX-maxX : 0.0;
    const double dy=minY>wetMaxY ? minY-wetMaxY : wetMinY>maxY ? wetMinY-maxY : 0.0;
    return eased(float(std::sqrt(dx*dx+dy*dy)/fadeDistance));
}

}
