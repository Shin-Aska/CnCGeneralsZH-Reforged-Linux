// Conservative primary capture bounds for the optional water path tracer.
#pragma once
#include "Platform/WaterPathTracing.h"

namespace WaterPathTracing {

class CaptureScope {
public:
    explicit CaptureScope(const View &view) : origin(view.origin), waterLevel(view.waterLevel)
    {
        if (!Finite(origin) || !Finite(view.rayTopLeft) || !Finite(view.rayDx) || !Finite(view.rayDy)
            || !std::isfinite(waterLevel) || !(view.maxDistance>0)
            || !(view.secondaryDistance>=0) || !(view.shadowDistance>=0))
            return;
        const Vec3 corners[4]={view.rayTopLeft,Add(view.rayTopLeft,view.rayDx),
            Add(Add(view.rayTopLeft,view.rayDx),view.rayDy),Add(view.rayTopLeft,view.rayDy)};
        const Vec3 centre=Add(view.rayTopLeft,Scale(Add(view.rayDx,view.rayDy),0.5f));
        const Vec3 sun=Normalized(view.sunDirection);
        for (unsigned i=0;i<4;++i) {
            Vec3 normal=Normalized(Cross(corners[i],corners[(i+1)%4]));
            if (!(Dot(normal,normal)>0) || !Finite(normal)) return;
            if (Dot(normal,centre)<0) normal=Scale(normal,-1);
            sides[i]=normal;
            // Include secondary hits and sunlight occluders outside the primary
            // frustum. The latter extend towards the sun, not in every direction.
            margins[i]=view.secondaryDistance+1.0f
                +std::max(0.0f,-Dot(normal,sun)*view.shadowDistance);
        }
        active=true;
        // For a crop whose corners all cross the water in front of the camera,
        // its water footprint is a convex quadrilateral bounded by those corners.
        // Sweeping its AABB by the ray budgets is conservative for every pixel.
        finiteFootprint=true;
        for (unsigned i=0;i<4;++i) {
            if (!(corners[i].z>1e-6f)) { finiteFootprint=false;break; }
            const double t=(double(waterLevel)-origin.z)/corners[i].z;
            if (!(t>=0) || !std::isfinite(t)) {
                finiteFootprint=false;
                break;
            }
            const Vec3 at={float(double(origin.x)+corners[i].x*t),
                float(double(origin.y)+corners[i].y*t),waterLevel};
            if (!Finite(at)) { finiteFootprint=false;break; }
            if (!i) low=high=at;
            else { low=Minimum(low,at);high=Maximum(high,at); }
        }
        if (finiteFootprint) {
            const float radius=view.maxDistance+view.secondaryDistance+1.0f;
            const Vec3 sweep=Scale(sun,view.shadowDistance);
            low=Sub(Add(low,Minimum(sweep,{0,0,0})),{radius,radius,radius});
            high=Add(Add(high,Maximum(sweep,{0,0,0})),{radius,radius,radius});
        }
    }

    bool Matches(Vec3 minimum,Vec3 maximum) const
    {
        if (!active || !Finite(minimum) || !Finite(maximum)) return true;
        // Primary rays begin above the interface. Omitting submerged secondary
        // contributors is an intentional approximation in this quality setting.
        if (maximum.z<waterLevel-0.05f) return false;
        if (finiteFootprint && (maximum.x<low.x || minimum.x>high.x
            || maximum.y<low.y || minimum.y>high.y || maximum.z<low.z || minimum.z>high.z))
            return false;
        const Vec3 centre=Scale(Add(minimum,maximum),0.5f);
        const Vec3 extent=Scale(Sub(maximum,minimum),0.5f);
        const Vec3 relative=Sub(centre,origin);
        for (unsigned i=0;i<4;++i) {
            const Vec3 n=sides[i];
            const float radius=std::abs(n.x)*extent.x+std::abs(n.y)*extent.y+std::abs(n.z)*extent.z;
            if (Dot(n,relative)+radius < -margins[i]) return false;
        }
        return true;
    }

private:
    Vec3 origin{},sides[4]{},low{},high{};
    float waterLevel=0,margins[4]{};
    bool active=false,finiteFootprint=false;
};

} // namespace WaterPathTracing
