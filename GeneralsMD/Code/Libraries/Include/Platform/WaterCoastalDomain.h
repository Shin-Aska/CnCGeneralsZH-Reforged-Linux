/* Immutable physical coastal domain. No camera, simulation or map mutation. */
#pragma once
#include "Platform/WaterCoastalSurf.h"
#include "Platform/WaterShoreSource.h"

namespace WaterCoastalDomain {
using Result=WaterShoreHistory::BuildResult;
using Level=WaterShoreSource::Level;
constexpr size_t MaxWetCells=4*WaterOpticalContext::MaxCells;
// Conservative scan/sort, lattice and flood work units, shared across all levels.
// A caller may supply a smaller allowance; no caller can exceed this fixed cap.
constexpr size_t MaxBuildWork=128u*1024u*1024u;
class Domain {
    WaterShoreHistory::Grid grid;
    WaterShoreHistory::Topology topology;
    WaterShoreSupport::Domain support;
    std::vector<WaterShoreHistory::Cell> cells;
    std::vector<WaterCoastalSurf::Node> nodes;
public:
    Result Build(const uint8_t *heights,int width,int height,int border,double spacing,double heightScale,
                 const std::vector<WaterOpticalContext::Polygon>&polygons,const std::vector<Level>&inputLevels,
                 size_t workAllowance=MaxBuildWork) {
        using namespace WaterShoreHistory;
        if(!heights||width<2||height<2||width>WaterOpticalContext::MaxDimension||height>WaterOpticalContext::MaxDimension
            ||size_t(width)*height>WaterOpticalContext::MaxCells||border<0||border>(width-2)/2||border>(height-2)/2
            ||!std::isfinite(spacing)||spacing<8||spacing>128||!std::isfinite(heightScale)||heightScale<=0
            ||polygons.size()>WaterOpticalContext::MaxPoints/3)
            return Result::Invalid;
        if(inputLevels.size()>WaterCoastalSurf::MaxLevels)return Result::TooManyLevels;
        size_t points=0;
        for(const auto &p:polygons) {
            if(p.points.size()>WaterOpticalContext::MaxPoints-points)return Result::Invalid;
            points+=p.points.size();
            for(const auto &v:p.points)if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))return Result::Invalid;
        }
        std::vector<Level> levels=inputLevels;
        const size_t terrainCount=size_t(width)*height;
        for(const auto &l:levels) {
            if(!l.field||!std::isfinite(l.level)||double(float(l.level))!=l.level)return Result::Invalid;
            const auto &f=*l.field;
            if(f.ocean.size()!=terrainCount||f.shore.size()!=terrainCount||f.coast.size()!=terrainCount
                ||f.exposure.size()!=terrainCount||f.physicalShore.size()!=terrainCount)return Result::Invalid;
        }
        std::sort(levels.begin(),levels.end(),[](const Level&a,const Level&b){return a.level<b.level;});
        for(size_t i=1;i<levels.size();++i)if(levels[i-1].level==levels[i].level)return Result::Invalid;
        size_t remaining=(std::min)(workAllowance,MaxBuildWork);
        auto spend=[&](size_t amount){if(amount>remaining)return false;remaining-=amount;return true;};
        // Intervals has at most three spans per input vertex. Charge its bounded sorting
        // as well as point visits before performing a scanline, including empty polygons.
        size_t sortPasses=0;for(size_t n=3*points+1;n>1;n=(n+1)/2)++sortPasses;
        const size_t lineWork=1+polygons.size()+points*(4+3*sortPasses);
        const size_t coveredWork=sortPasses+1; // upper_bound on at most three spans per vertex
        // Build at most two bounded candidates from the same completed fine flood.
        // Stride2 is exactly the original terrain grid, so support rectangles remain
        // bilinear. Larger strides would cross terrain-cell boundaries and need a
        // different support proof; they are deliberately not attempted here.
        Domain fine,coarse;fine.grid={-border*spacing,-border*spacing,spacing*.5};
        coarse.grid={fine.grid.originX,fine.grid.originY,spacing};
        // Keep the existing GPU graph spacing contract; no stride2 candidate is
        // possible when its spacing would exceed64 world units.
        bool fineOverflow=false,coarseOverflow=spacing>64;
        auto append=[&](Domain &candidate,bool &overflow,int x,int y,double level,
                       const WaterCoastalSurf::Node &node,uint8_t connections){
            if(overflow)return;
            if(candidate.cells.size()>=WaterCoastalSurf::MaxNodes){
                overflow=true;candidate.cells.clear();candidate.nodes.clear();return;
            }
            candidate.cells.push_back({x,y,level,uint32_t(candidate.nodes.size()),connections});
            candidate.nodes.push_back(node);
        };
        // Unlike the legacy width-epsilon sampler, exact final-row vertices remain exact.
        // Cardinal half-cell segments stay within one bilinear terrain cell, so their
        // height is linear: wet endpoints prove the entire cardinal segment is wet.
        auto terrain=[&](double x,double y){
            x=Clamp(x,0,width-1);y=Clamp(y,0,height-1);
            const int ix=(std::min)(int(x),width-2),iy=(std::min)(int(y),height-2);
            const double fx=x-ix,fy=y-iy;const size_t n=size_t(iy)*width+ix;
            return float(((heights[n]*(1-fx)+heights[n+1]*fx)*(1-fy)
                +(heights[n+width]*(1-fx)+heights[n+width+1]*fx)*fy)*heightScale);
        };
        const int nx=2*width-1,ny=2*height-1;
        const size_t wetCount=size_t(nx)*ny;
        if(wetCount>MaxWetCells)return Result::Invalid;
        enum : uint8_t {Wet=1,PositiveX=2,PositiveY=4,Connected=8};
        for(const Level &entry:levels) {
            const auto &f=*entry.field;
            // Full wet connectivity precedes shallow/distance pruning: an island coast
            // remains exposed when its only path to the playable boundary is deep ocean.
            // Memory is reused one level at a time; no optical exposure bit is trusted.
            std::vector<uint8_t> wet(wetCount,0),columnWet(size_t(nx),0);
            for(int y=0;y<ny;++y) {
                if(!spend(lineWork+size_t(nx)*(1+2*coveredWork)))return Result::Invalid;
                const double wy=fine.grid.originY+y*fine.grid.spacing;
                const auto row=WaterShoreSource::Intervals(polygons,entry.level,wy,true);
                if(row.empty())continue;
                for(int x=0;x<nx;++x) {
                    const double wx=fine.grid.originX+x*fine.grid.spacing;
                    if(!WaterShoreSource::Covered(row,wx,wx)||!(terrain(x*.5,y*.5)<entry.level))continue;
                    uint8_t flags=Wet;
                    if(x+1<nx&&WaterShoreSource::Covered(row,wx,wx+fine.grid.spacing))flags|=PositiveX;
                    wet[size_t(y)*nx+x]=flags;columnWet[size_t(x)]=1;
                }
            }
            for(int x=0;x<nx;++x)if(columnWet[size_t(x)]) {
                if(!spend(lineWork+size_t(ny)*(1+coveredWork)))return Result::Invalid;
                const auto column=WaterShoreSource::Intervals(polygons,entry.level,
                    fine.grid.originX+x*fine.grid.spacing,false);
                for(int y=0;y+1<ny;++y) {
                    auto &flags=wet[size_t(y)*nx+x];if(!(flags&Wet))continue;
                    const double wy=fine.grid.originY+y*fine.grid.spacing;
                    if(WaterShoreSource::Covered(column,wy,wy+fine.grid.spacing))flags|=PositiveY;
                }
            }
            std::vector<uint32_t> queue;queue.reserve(wetCount);
            auto seed=[&](size_t index){
                auto &flags=wet[index];if((flags&(Wet|Connected))!=Wet)return;
                flags|=Connected;queue.push_back(uint32_t(index));
            };
            const int left=2*border,right=2*(width-border-1),top=2*border,bottom=2*(height-border-1);
            for(int x=left;x<=right;++x){seed(size_t(top)*nx+x);seed(size_t(bottom)*nx+x);}
            for(int y=top;y<=bottom;++y){seed(size_t(y)*nx+left);seed(size_t(y)*nx+right);}
            for(size_t q=0;q<queue.size();++q) {
                if(!spend(4))return Result::Invalid;
                const size_t i=queue[q];const int x=int(i%nx),y=int(i/nx);
                if(x+1<nx&&(wet[i]&PositiveX))seed(i+1);
                if(x>0&&(wet[i-1]&PositiveX))seed(i-1);
                if(y+1<ny&&(wet[i]&PositiveY))seed(i+nx);
                if(y>0&&(wet[i-nx]&PositiveY))seed(i-nx);
            }
            if(!spend(wetCount))return Result::Invalid;
            // Final-row/column source knots remain excluded to retain the existing
            // support-tile contract. They participated in the full wet connectivity.
            for(int y=0;y+1<ny;++y)for(int x=0;x+1<nx;++x) {
                const uint8_t flags=wet[size_t(y)*nx+x];if(!(flags&Connected))continue;
                const float gx=x*.5f,gy=y*.5f;
                const float depth=float(entry.level)-terrain(gx,gy);
                if(!(depth>.15f)||depth>=12)continue;
                const auto distance=WaterSurfaceModel::SampleOffshore(f.physicalShore.data(),width,height,gx,gy,float(spacing));
                // The saturated code means at least160, not exactly160. It cannot
                // prove membership in a collar when spacing makes that collar wider.
                if(distance.height>=1)continue;
                const float physical=distance.height*WaterOpticalContext::PhysicalShoreRange;
                if(physical>WaterCoastalSurf::PhysicalCoastEnd+spacing+WaterOpticalContext::PhysicalShoreRange/510)continue;
                const auto ocean=WaterSurfaceModel::SampleOffshore(f.ocean.data(),width,height,gx,gy,float(spacing));
                WaterCoastalSurf::Node node;
                node.world[0]=float(fine.grid.originX+x*fine.grid.spacing);
                node.world[1]=float(fine.grid.originY+y*fine.grid.spacing);node.world[2]=float(entry.level);
                auto &c=node.context;c.depth=depth;c.distance=physical;
                c.outwardX=distance.dx*WaterOpticalContext::PhysicalShoreRange;
                c.outwardY=distance.dy*WaterOpticalContext::PhysicalShoreRange;
                c.exposure=1;c.offshore=ocean.height;
                c.analyticDamping=WaterSurfaceModel::TerrainDamping(depth,ocean.height);
                c.spectralDamping=WaterFftGeometry::SafeDamping(c.analyticDamping,depth);
                c.wet=c.admitted=1;
                const uint8_t connections=uint8_t((flags&PositiveX?1:0)|(flags&PositiveY?2:0));
                append(fine,fineOverflow,x,y,entry.level,node,connections);
                if((x&1)==0&&(y&1)==0&&!coarseOverflow){
                    if(!spend(4))return Result::Invalid;
                    auto linked=[&](bool vertical){
                        const int ex=x+(vertical?0:2),ey=y+(vertical?2:0);
                        if(ex>=nx||ey>=ny)return false;
                        const size_t step=vertical?size_t(nx):1,start=size_t(y)*nx+x;
                        const uint8_t direction=vertical?PositiveY:PositiveX;
                        for(unsigned k=0;k<2;++k){
                            const uint8_t a=wet[start+k*step],b=wet[start+(k+1)*step];
                            if((a&(Wet|Connected|direction))!=(Wet|Connected|direction)
                                ||(b&(Wet|Connected))!=(Wet|Connected))return false;
                        }
                        return true;
                    };
                    const uint8_t links=uint8_t((linked(false)?1:0)|(linked(true)?2:0));
                    append(coarse,coarseOverflow,x/2,y/2,entry.level,node,links);
                }
                if(fineOverflow&&coarseOverflow)return Result::TooManyCells;
            }
        }
        const unsigned selectedStride=fineOverflow?2u:1u;
        Domain next=std::move(fineOverflow?coarse:fine);
        const auto built=next.topology.Build(next.grid,next.cells);if(built!=Result::Ready)return built;
        // Sorted level/y/x insertion preserves the immutable node IDs after Topology.Build.
        if(next.topology.Cells().size()!=next.nodes.size())return Result::Invalid;
        for(const auto &edge:next.topology.Edges()) {
            const auto &a=next.cells[edge.a],&b=next.cells[edge.b];
            const unsigned side=a.y==b.y?1:3,opposite=side-1;
            next.nodes[edge.a].neighbour[side]=edge.b+1;next.nodes[edge.b].neighbour[opposite]=edge.a+1;
        }
        if(!WaterCoastalSurf::ValidGraph(next.nodes,next.grid.spacing))return Result::Invalid;
        auto supportTerrain=[&](double x,double y){return terrain(x*selectedStride,y*selectedStride);};
        if(!next.support.Build(next.grid,next.cells,width,height,polygons,supportTerrain))return Result::Invalid;
        *this=std::move(next);return Result::Ready;
    }
    const WaterShoreHistory::Grid& Grid()const{return grid;}
    const WaterShoreHistory::Topology& Topology()const{return topology;}
    const std::vector<WaterShoreHistory::Cell>& Cells()const{return cells;}
    const std::vector<WaterCoastalSurf::Node>& Nodes()const{return nodes;}
    const WaterShoreSupport::Domain& Support()const{return support;}
};
}
