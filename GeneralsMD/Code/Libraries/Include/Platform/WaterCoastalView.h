/* Bounded view-to-physical-coast interpolation. No state copying or GPU access. */
#pragma once
#include "Platform/WaterCoastalDomain.h"
#include <limits>

namespace WaterCoastalView {
using Mapping=WaterShoreHistory::Mapping;
using Lookup=WaterCoastalSurf::Lookup;
constexpr unsigned MaxSide=WaterCoastalSurf::MaxViewSide;
constexpr size_t MaxPixels=size_t(MaxSide)*MaxSide;
// Helper-call allowance, not a CPU-instruction budget: one texel setup/At unit
// plus at most four bounded Topology::Find and four Support::Allows calls. At
// has a bounded binary search; Find/Allows bound probes or clipped vertices.
constexpr size_t WorkPerPixel=9,MaxBuildWork=WorkPerPixel*MaxPixels;
// Separate bounded coverage work: tile searches/corner checks/polygon edge visits.
constexpr size_t MaxFootprintTiles=49,MaxFootprintBuildWork=64*MaxPixels;
inline bool FitsFloat(double value){
    const double limit=(std::numeric_limits<float>::max)();
    return std::isfinite(value)&&value>=-limit&&value<=limit;
}
inline bool ExactFloat(double value){return FitsFloat(value)&&double(float(value))==value;}
inline bool ValidLevel(double level){return ExactFloat(level);}
inline bool ValidMapping(const Mapping&m){
    return m.widthPixels>0&&m.heightPixels>0&&m.widthPixels<=MaxSide&&m.heightPixels<=MaxSide
        &&ExactFloat(m.originX)&&ExactFloat(m.originY)&&ExactFloat(m.widthWorld)&&ExactFloat(m.heightWorld)
        &&m.widthWorld>0&&m.heightWorld>0&&FitsFloat(m.originX+m.widthWorld)
        &&FitsFloat(m.originY+m.heightWorld)&&std::isfinite(1.f/float(m.widthWorld))
        &&std::isfinite(1.f/float(m.heightWorld));
}
inline bool SameMapping(const Mapping&a,const Mapping&b){
    return a.originX==b.originX&&a.originY==b.originY&&a.widthWorld==b.widthWorld
        &&a.heightWorld==b.heightWorld&&a.widthPixels==b.widthPixels&&a.heightPixels==b.heightPixels;
}
inline WaterShoreSupport::AreaResult FilterFootprint(const WaterCoastalDomain::Domain&domain,
    const Mapping&m,double level,unsigned x,unsigned y,size_t &remaining){
    using Result=WaterShoreSupport::AreaResult;const auto &g=domain.Grid();
    const double dx=m.widthWorld/m.widthPixels,dy=m.heightWorld/m.heightPixels;
    // The exact bilinear tent is centre +/- one texel spacing. Its enclosing 3x3
    // pixel cells add half a texel of coordinate/filter margin. Clip to the chart:
    // the presentation rejects UV outside it, and clamp only extends boundary cells.
    const double x0=(m.originX+(x?double(x-1):0)*dx-g.originX)/g.spacing;
    const double y0=(m.originY+(y?double(y-1):0)*dy-g.originY)/g.spacing;
    const double x1=(m.originX+(std::min)(double(x)+2,double(m.widthPixels))*dx-g.originX)/g.spacing;
    const double y1=(m.originY+(std::min)(double(y)+2,double(m.heightPixels))*dy-g.originY)/g.spacing;
    if(!std::isfinite(x0)||!std::isfinite(y0)||!std::isfinite(x1)||!std::isfinite(y1)
        ||x0<(std::numeric_limits<int32_t>::min)()||y0<(std::numeric_limits<int32_t>::min)()
        ||x1>=(std::numeric_limits<int32_t>::max)()||y1>=(std::numeric_limits<int32_t>::max)()
        ||x1<=x0||y1<=y0)return Result::Unsupported;
    const int32_t loX=int32_t(std::floor(x0)),loY=int32_t(std::floor(y0));
    const int32_t hiX=int32_t(std::ceil(x1)-1),hiY=int32_t(std::ceil(y1)-1);
    const uint64_t nx=uint64_t(int64_t(hiX)-loX+1),ny=uint64_t(int64_t(hiY)-loY+1);
    if(nx>MaxFootprintTiles||ny>MaxFootprintTiles||nx*ny>MaxFootprintTiles)return Result::OverBudget;
    for(int32_t iy=loY;iy<=hiY;++iy)for(int32_t ix=loX;ix<=hiX;++ix){
        if(!remaining)return Result::OverBudget;--remaining;
        const auto tile=domain.Support().At(ix,iy,level,0,0);
        const Result result=tile.CoversRectangle((std::max)(0.,x0-ix),(std::max)(0.,y0-iy),
            (std::min)(1.,x1-ix),(std::min)(1.,y1-iy),remaining);
        if(result!=Result::Covered)return result;
    }
    return Result::Covered;
}
// Texel centres use world=origin+(pixel+.5)*extent/count. The later shader uses
// UV=(world-origin)/extent, with NO extra half-texel offset or flipped Y axis.
// Float-exact metadata prevents a second rounded origin/extent in those uniforms.
inline bool Build(const WaterCoastalDomain::Domain&domain,const Mapping&mapping,double level,
                  std::vector<Lookup>&output,size_t workAllowance=MaxBuildWork,
                  size_t footprintAllowance=MaxFootprintBuildWork){
    if(!ValidMapping(mapping)||!ValidLevel(level))return false;
    const size_t count=size_t(mapping.widthPixels)*mapping.heightPixels;
    if(count*WorkPerPixel>(std::min)(workAllowance,MaxBuildWork))return false;
    const auto &topology=domain.Topology();const auto &grid=domain.Grid();
    const size_t nodes=domain.Nodes().size();
    if(nodes>WaterCoastalSurf::MaxNodes||topology.Cells().size()!=nodes||!WaterShoreHistory::ValidGrid(grid))return false;
    size_t remainingFootprint=(std::min)(footprintAllowance,MaxFootprintBuildWork);
    std::vector<Lookup> next(count);
    for(unsigned y=0;y<mapping.heightPixels;++y)for(unsigned x=0;x<mapping.widthPixels;++x){
        const double wx=mapping.originX+(double(x)+.5)*mapping.widthWorld/mapping.widthPixels;
        const double wy=mapping.originY+(double(y)+.5)*mapping.heightWorld/mapping.heightPixels;
        const double px=(wx-grid.originX)/grid.spacing,py=(wy-grid.originY)/grid.spacing;
        // An entirely off-domain view remains a valid empty lookup. Clamp neither
        // coordinates nor indices: either would smear an outer row into distant water.
        if(!std::isfinite(px)||!std::isfinite(py)||px<(std::numeric_limits<int32_t>::min)()
            ||py<(std::numeric_limits<int32_t>::min)()||px>=(std::numeric_limits<int32_t>::max)()
            ||py>=(std::numeric_limits<int32_t>::max)())continue;
        const int32_t ix=int32_t(std::floor(px)),iy=int32_t(std::floor(py));
        const double fx=px-ix,fy=py-iy;
        const auto support=domain.Support().At(ix,iy,level,fx,fy);
        Lookup &lookup=next[size_t(y)*mapping.widthPixels+x];
        for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx){
            const double weight=(dx?fx:1-fx)*(dy?fy:1-fy);
            if(weight<=0)continue;
            const int index=topology.Find(ix+int32_t(dx),iy+int32_t(dy),level);
            if(index<0)continue;
            if(size_t(index)>=nodes)return false;
            if(!support.Allows(dx,dy))continue;
            const float stored=float(weight);if(stored<=0)continue;
            const unsigned corner=dy*2+dx;
            lookup.node[corner]=uint32_t(index)+1;lookup.weight[corner]=stored;
        }
        // Missing/dry/wrong-level/gap contributions stay exactly zero. There is no
        // normalization of retained weights and no nearest-node ownership shortcut.
        if(!WaterCoastalSurf::ValidLookup(lookup,unsigned(nodes)))return false;
        bool nonzero=false;for(unsigned c=0;c<4;++c)nonzero=nonzero||lookup.weight[c]>0;
        if(nonzero){
            const auto area=FilterFootprint(domain,mapping,level,x,y,remainingFootprint);
            if(area==WaterShoreSupport::AreaResult::OverBudget)return false;
            if(area!=WaterShoreSupport::AreaResult::Covered)lookup={};
        }
    }
    output.swap(next);return true;
}
} // namespace WaterCoastalView
