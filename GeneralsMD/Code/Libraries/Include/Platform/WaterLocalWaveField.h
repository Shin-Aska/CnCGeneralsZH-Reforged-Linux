/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "Platform/WaterWaveHeading.h"
#include "Platform/WaterBodyAnalysis.h"
#include "Platform/WaterWaveCharts.h"
namespace WaterLocalWaveField {
using Vector=WaterWaveHeading::Vector;
constexpr size_t TexelsPerCell=5;
constexpr float GradientRange=WaterWaveHeading::GradientRange;
constexpr double ConfidenceStartSlowness=1.05,ConfidenceEndSlowness=1.25,ConfidencePackingMargin=.85;
struct Field {
    Vector prevailing=WaterWaveHeading::BaseHeading;
    float uStep=1,localStep=1;
    std::vector<uint32_t> texels;
    WaterBodyAnalysis::Field analysis;
    bool localConverged=true;
    unsigned failedComponents=0;
};
struct GainSample {float value=1,dx=0,dy=0;};
// Elevated authored rivers cannot inherit the lower sea's wave energy. Keep
// the easing input bounded: the engine's global clamp uses (lo,value,hi),
// unlike HLSL/std::clamp, and extrapolating this cubic amplifies high rivers.
inline float ElevatedRiverGain(float level,float sourceLevel,float transitionHeight){
    constexpr float calm=.06f;
    if(!std::isfinite(level) || !std::isfinite(sourceLevel)
        || !std::isfinite(transitionHeight) || transitionHeight<=0)return calm;
    const float t=(std::max)(0.0f,(std::min)(1.0f,(level-sourceLevel)/transitionHeight));
    const float remaining=(1.0f-t)*(1.0f-t)*(1.0f+2.0f*t);
    return calm+(1.0f-calm)*remaining;
}
// metadata is one packed fifth texel per logical cell, not the five-word atlas.
// Match fftLocalFields' separate quintic envelopes and product derivative.
inline GainSample SampleLegacyGain(const std::vector<uint32_t>&metadata,int width,int height,
                                  float gx,float gy,float spacing){
    if(width<2 || height<2 || size_t(width)*height!=metadata.size() || !std::isfinite(gx)
        || !std::isfinite(gy) || !std::isfinite(spacing) || spacing<=0)return {};
    const bool insideX=gx>=0 && gx<=width-1,insideY=gy>=0 && gy<=height-1;
    gx=(std::max)(0.0f,(std::min)(float(width-1),gx));gy=(std::max)(0.0f,(std::min)(float(height-1),gy));
    const int x=(std::min)(int(gx),width-2),y=(std::min)(int(gy),height-2);
    const float fx=gx-x,fy=gy-y;
    auto basis=[](float t){return t*t*t*(t*(6*t-15)+10);};
    auto derivative=[](float t){return 30*t*t*(t-1)*(t-1);};
    const float ux=basis(fx),uy=basis(fy),dx=insideX?derivative(fx)/spacing:0,dy=insideY?derivative(fy)/spacing:0;
    auto sample=[&](unsigned shift){
        const float a=float((metadata[size_t(y)*width+x]>>shift)&255)/255;
        const float b=float((metadata[size_t(y)*width+x+1]>>shift)&255)/255;
        const float c=float((metadata[size_t(y+1)*width+x]>>shift)&255)/255;
        const float d=float((metadata[size_t(y+1)*width+x+1]>>shift)&255)/255;
        return GainSample{(a+(b-a)*ux)*(1-uy)+(c+(d-c)*ux)*uy,
            ((b-a)*(1-uy)+(d-c)*uy)*dx,((c-a)*(1-ux)+(d-b)*ux)*dy};
    };
    const GainSample influence=sample(0);if(influence.value==0)return {};
    const GainSample residual=sample(24);
    return {1-influence.value+influence.value*residual.value,
        influence.dx*(residual.value-1)+influence.value*residual.dx,
        influence.dy*(residual.value-1)+influence.value*residual.dy};
}
inline std::vector<double> ChartConfidence(const uint8_t* wet,int width,int height,
    const std::vector<double>& slow,const std::vector<double>& phase,const std::vector<Vector>& gradient,
    std::vector<Vector>* metrics=nullptr) {
    const size_t count=size_t(width)*height;
    std::vector<double> confidence(count,0),packing(count,0);
    if(metrics)metrics->assign(count,{0,0});
    auto physicalAt=[&](int x,int y) {
        const int i=y*width+x;double physical=slow[i];
        if(!wet[i])for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            const int nx=x+dx,ny=y+dy;
            if(nx>=0 && nx<width && ny>=0 && ny<height && wet[ny*width+nx])
                physical=(std::max)(physical,slow[ny*width+nx]);
        }
        return physical;
    };
    auto fade=[](double value,double start,double end) {
        const double t=(std::max)(0.0,(std::min)(1.0,(value-start)/(end-start)));
        return 1-t*t*(3-2*t);
    };
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        const int i=y*width+x;if(!std::isfinite(phase[i]))continue;
        const double physical=physicalAt(x,y);
        double stencilPhysical=physical;
        double packingBound=(std::max)(std::abs(double(gradient[i].x)),std::abs(double(gradient[i].y)));
        double directionalBound=0;
        auto bound=[&](int nx,int ny) {
            if(nx>=0 && nx<width && ny>=0 && ny<height && std::isfinite(phase[ny*width+nx])) {
                const double adjacentPhysical=physicalAt(nx,ny);
                stencilPhysical=(std::max)(stencilPhysical,adjacentPhysical);
                const double difference=std::abs(phase[ny*width+nx]-phase[i]);
                packingBound=(std::max)(packingBound,difference);
                // A directional derivative has magnitude at most the slowness.
                // These independent bounds must not be combined as vector components:
                // their maxima can occur at different points on a curved wavefront.
                directionalBound=(std::max)(directionalBound,difference/(std::max)(physical,adjacentPhysical));
            }
        };
        bound(x-1,y);bound(x+1,y);bound(x,y-1);bound(x,y+1);
        const double physicalBound=(std::max)(directionalBound,
            std::hypot(double(gradient[i].x),double(gradient[i].y))/stencilPhysical);
        confidence[i]=fade(physicalBound,ConfidenceStartSlowness,ConfidenceEndSlowness);
        // Both packed tangent components have independent signed byte ranges.
        packing[i]=fade(packingBound,double(GradientRange)*.75,double(GradientRange)*ConfidencePackingMargin);
        if(metrics)(*metrics)[i]={float(physicalBound),float(packingBound)};
    }
    // A nonzero wet amplitude affects four adjacent patches. Protect all of their
    // payload corners against component clipping, including zero-amplitude ghosts.
    // Physical confidence is local: propagating its minimum would unnecessarily
    // suppress a well-resolved shore arrival beside a physically slower neighbour.
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        const int i=y*width+x;
        if(!wet[i]){confidence[i]=0;continue;}
        double value=1;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            const int nx=x+dx,ny=y+dy;
            if(nx>=0 && nx<width && ny>=0 && ny<height)value=(std::min)(value,packing[ny*width+nx]);
        }
        confidence[i]*=value;
    }
    return confidence;
}
inline uint32_t PackAmplitude(double amplitude,double secondaryWeight,double c0,double c1) {
    // r0=c0*((1-b1)+b1*(1-c1)), r1=c1*(b1+(1-b1)*(1-c0)).
    // Their sum is this smooth combined confidence, exactly one if either is safe.
    const double budget=1-(1-c0)*(1-c1);
    const double r1=c1*(secondaryWeight+(1-secondaryWeight)*(1-c0));
    const uint32_t total=uint32_t(std::round(amplitude*budget*255));
    const uint32_t secondary=budget>0?(std::min)(total,uint32_t(std::round(total*r1/budget))):0;
    return 0xff000000u|((total-secondary)<<16)|(secondary<<8);
}
// Texel 1 retains legacy amplitude R and validity A. Its otherwise unused G/B
// bytes carry the bank vector's X/Y; zero is exactly 128 in each signed byte.
// Round within the integer unit disk so a projected velocity cannot reverse its
// bank-normal component through a quantized vector longer than one.
inline uint32_t PackBankVector(Vector s) {
    int x=int(std::lround(127*(std::max)(-1.0f,(std::min)(1.0f,s.x))));
    int y=int(std::lround(127*(std::max)(-1.0f,(std::min)(1.0f,s.y))));
    while(x*x+y*y>127*127){if(std::abs(x)>=std::abs(y))x+=x>0?-1:1;else y+=y>0?-1:1;}
    return (uint32_t(x+128)<<8)|uint32_t(y+128);
}
inline Vector DecodeBankVector(uint32_t texel) {
    return {(float((texel>>8)&255)-128)/127,(float(texel&255)-128)/127};
}
inline std::vector<Vector> BankVectors(const uint8_t*wet,const uint8_t*links,int width,int height,
                                      int border,float spacing,const std::vector<float>&clearance) {
    const int count=width*height;std::vector<Vector> normal(count,{0,0}),scratch;
    auto inside=[&](int x,int y){return x>=border && y>=border && x<width-border && y<height-border;};
    auto connected=[&](int i,int j){
        if(!wet[j])return false;
        if(j==i-1)return (links[j]&1)!=0;if(j==i+1)return (links[i]&1)!=0;
        if(j==i-width)return (links[j]&2)!=0;return (links[i]&2)!=0;
    };
    for(int y=border;y<height-border;++y)for(int x=border;x<width-border;++x){
        const int i=y*width+x;if(!wet[i])continue;
        auto derivative=[&](bool axis){const int d=axis?width:1;
            const bool lo=inside(x-(axis?0:1),y-(axis?1:0)),hi=inside(x+(axis?0:1),y+(axis?1:0));
            // A blocked exact segment is a real bank even if both endpoints
            // are wet. Unknown crop edges contribute no artificial bank.
            const float a=lo?(connected(i,i-d)?clearance[i-d]:-.5f*spacing):clearance[i];
            const float b=hi?(connected(i,i+d)?clearance[i+d]:-.5f*spacing):clearance[i];
            return (b-a)/(lo && hi?2.0f:1.0f);
        };
        normal[i]={derivative(false),derivative(true)};
    }
    // Smooth physical clearance gradients across covered water only. This
    // removes staircase flips on diagonal banks without mixing opposite sides
    // of an island, an uncovered segment, or an authored visual underpass.
    for(unsigned pass=0;pass<2;++pass){scratch=normal;
        for(int y=border;y<height-border;++y)for(int x=border;x<width-border;++x){
            const int i=y*width+x;if(!wet[i])continue;Vector sum={normal[i].x*4,normal[i].y*4};float weight=4;
            auto add=[&](int nx,int ny){if(!inside(nx,ny))return;const int j=ny*width+nx;
                if(connected(i,j)){sum.x+=normal[j].x;sum.y+=normal[j].y;++weight;}};
            add(x-1,y);add(x+1,y);add(x,y-1);add(x,y+1);
            scratch[i]={sum.x/weight,sum.y/weight};
        }normal.swap(scratch);
    }
    for(int i=0;i<count;++i){const float length=std::hypot(normal[i].x,normal[i].y);
        const float t=WaterBodyAnalysis::Clamp01((clearance[i]-10)/40);
        const float scale=length>1e-6f?std::sqrt((std::max)(0.0f,1-t*t*(3-2*t)))/length:0;
        normal[i]={normal[i].x*scale,normal[i].y*scale};
    }
    return normal;
}
inline bool Build(const uint8_t*wet,const uint8_t*exposure,const uint8_t*links,
                  int width,int height,int border,float spacing,Field&result,
                  const std::vector<WaterBodyAnalysis::VisualConnection>&connections={},bool allowSources=true) {
    result=Field{};WaterWaveHeading::Field legacy;
    if(!WaterWaveHeading::Build(wet,exposure,width,height,border,spacing,legacy)
        || !WaterBodyAnalysis::Build(wet,links,width,height,border,spacing,result.analysis,connections,allowSources)){
        result=Field{};return false;
    }
    const int count=width*height;const double infinity=std::numeric_limits<double>::infinity();
    result.prevailing=legacy.prevailing;result.uStep=legacy.uStep;result.texels.assign(size_t(count)*TexelsPerCell,0);
    for(int i=0;i<count;++i){result.texels[size_t(i)*TexelsPerCell]=legacy.texels[size_t(i)*2];result.texels[size_t(i)*TexelsPerCell+1]=legacy.texels[size_t(i)*2+1];}
    auto&a=result.analysis;
    // Rendered terrain padding may continue a playable water body, but must not
    // contribute area, sources, or connections to the playable classifier.
    std::vector<int> owner(count,-1),paddingQueue;
    auto playable=[&](int i){const int x=i%width,y=i/width;return x>=border && y>=border && x<width-border && y<height-border;};
    if(border>0){
        for(int y=border;y<height-border;++y)for(int x=border;x<width-border;++x)
            if((x==border || x==width-border-1 || y==border || y==height-border-1) && wet[y*width+x]){
                const int i=y*width+x;owner[i]=i;paddingQueue.push_back(i);
            }
        for(size_t k=0;k<paddingQueue.size();++k){const int i=paddingQueue[k],x=i%width,y=i/width;
            auto visit=[&](int j){if(!playable(j) && wet[j] && owner[j]<0){owner[j]=owner[i];paddingQueue.push_back(j);}};
            if(x>0 && (links[i-1]&1))visit(i-1);
            if(x+1<width && (links[i]&1))visit(i+1);
            if(y>0 && (links[i-width]&2))visit(i-width);
            if(y+1<height && (links[i]&2))visit(i+width);
        }
    }
    auto bank=BankVectors(wet,links,width,height,border,spacing,a.clearance);
    for(int i:paddingQueue)if(!playable(i))bank[i]=bank[owner[i]];
    std::vector<uint8_t> active(count,0),source(count,0),phaseAnchor(count,0);
    for(int i=0;i<count;++i)active[i]=wet[i] && std::isfinite(a.distance[i]);
    for(int i:a.sourceCells)source[i]=phaseAnchor[i]=1;
    for(const auto&c:connections)phaseAnchor[c.a]=phaseAnchor[c.b]=1;
    std::array<std::vector<double>,2> phase;
    for(auto&u:phase)u.assign(count,infinity);
    const double hx=a.sourceHeading.x,hy=a.sourceHeading.y;
    double origin=infinity;for(int i:a.sourceCells)origin=(std::min)(origin,hx*(i%width-border)+hy*(i/width-border));
    // Reserve a ghost-cell margin below the earliest source instead of clipping
    // an affine front at zero during dry-corner extrapolation.
    if(std::isfinite(origin))origin-=3;
    struct Component{int id,minx,miny,maxx,maxy;float arrival;std::vector<int>cells;};
    int componentCount=0;for(int id:a.component)componentCount=(std::max)(componentCount,id+1);
    std::vector<Component> components(size_t(componentCount),{-1,width,height,-1,-1,std::numeric_limits<float>::infinity(),{}});
    for(int i=0;i<count;++i)if(active[i]){auto&c=components[size_t(a.component[i])];c.id=a.component[i];
        c.minx=(std::min)(c.minx,i%width);c.maxx=(std::max)(c.maxx,i%width);c.miny=(std::min)(c.miny,i/width);c.maxy=(std::max)(c.maxy,i/width);
        c.arrival=(std::min)(c.arrival,a.distance[i]);c.cells.push_back(i);
    }
    std::sort(components.begin(),components.end(),[](const Component&l,const Component&r){return l.arrival<r.arrival || (l.arrival==r.arrival && l.id<r.id);});
    struct Incoming{int upstream,downstream;double span;};
    std::vector<std::vector<Incoming>> incoming(static_cast<size_t>(componentCount));
    for(const auto&link:connections){int upstream=link.a,downstream=link.b;
        if(a.distance[upstream]>a.distance[downstream])std::swap(upstream,downstream);
        if(!std::isfinite(a.distance[downstream]) || source[downstream])continue;
        incoming[size_t(a.component[downstream])].push_back({upstream,downstream,
            std::hypot(double(upstream%width-downstream%width),double(upstream/width-downstream/width))});
    }
    std::vector<std::vector<double>> anchors(static_cast<size_t>(componentCount));
    std::vector<uint8_t> processed(size_t(componentCount),0),failed(size_t(componentCount),0);
    bool bridgeStable=false;
    // Minimum energy arrival orders first solves, but a later component can
    // supply a shorter geometric route to a far port of an earlier component.
    // Revisit only changed incoming anchors; the common one-bridge case still
    // solves each component once. Never publish an unresolved cyclic iteration.
    constexpr unsigned MaxBridgePasses=32;
    for(unsigned bridgePass=0;bridgePass<MaxBridgePasses;++bridgePass){bool didSolve=false;
    for(const auto&c:components){
        if(c.cells.empty())continue;
        std::vector<double> currentAnchors;
        for(const auto&port:incoming[size_t(c.id)])for(unsigned k=0;k<2;++k)
            currentAnchors.push_back(phase[k][port.upstream]+port.span);
        bool unchanged=processed[size_t(c.id)] && currentAnchors.size()==anchors[size_t(c.id)].size();
        if(unchanged)for(size_t j=0;j<currentAnchors.size();++j){const double before=anchors[size_t(c.id)][j],after=currentAnchors[j];
            if(before!=after && !(std::isfinite(before) && std::isfinite(after) && std::abs(before-after)<=WaterWaveCharts::ConvergenceTolerance)){unchanged=false;break;}
        }
        if(unchanged)continue;
        processed[size_t(c.id)]=1;anchors[size_t(c.id)]=std::move(currentAnchors);didSolve=true;
        bool localNeeded=false;for(int i:c.cells)localNeeded=localNeeded || a.sea[i]<.5f;
        // An all-source sea needs no local solve; its legacy texels stay exact.
        if(!localNeeded){for(int i:c.cells)for(auto&u:phase)u[i]=hx*(i%width-border)+hy*(i/width-border)-origin;continue;}
        const int minx=(std::max)(0,c.minx-1),miny=(std::max)(0,c.miny-1);
        const int cw=(std::min)(width-1,c.maxx+1)-minx+1,ch=(std::min)(height-1,c.maxy+1)-miny+1;
        const size_t localCount=size_t(cw)*ch;
        std::vector<uint8_t> domain(localCount,0),covered(localCount,0);
        std::vector<double> slow(localCount,1);
        std::array<std::vector<double>,2> seeds,solved;
        for(auto&s:seeds)s.assign(localCount,infinity);
        auto local=[&](int i){return (i/width-miny)*cw+i%width-minx;};
        for(int i:c.cells){const int j=local(i);domain[j]=255;
            if(i%width<c.maxx && (links[i]&1) && active[i+1] && a.component[i+1]==c.id)covered[j]|=1;
            if(i/width<c.maxy && (links[i]&2) && active[i+width] && a.component[i+width]==c.id)covered[j]|=2;
            if(source[i])for(auto&s:seeds)s[j]=hx*(i%width-border)+hy*(i/width-border)-origin;
        }
        for(const auto&port:incoming[size_t(c.id)]){
            // Transmission attenuates amplitude only. Phase traverses the actual
            // physical port span, never its logarithmic equivalent energy cost.
            for(unsigned k=0;k<2;++k)if(std::isfinite(phase[k][port.upstream]))
                seeds[k][local(port.downstream)]=(std::min)(seeds[k][local(port.downstream)],phase[k][port.upstream]+port.span);
        }
        bool ok=true;
        for(unsigned k=0;k<2;++k)ok=WaterWaveCharts::SolveChart(domain.data(),cw,ch,slow,seeds[k],hx,hy,k?1:-1,solved[k],covered.data()) && ok;
        failed[size_t(c.id)]=ok?0:1;
        if(!ok){for(auto&u:phase)for(int i:c.cells)u[i]=infinity;continue;}
        for(unsigned k=0;k<2;++k)for(int i:c.cells)phase[k][i]=solved[k][local(i)];
    }
        if(!didSolve || connections.empty()){bridgeStable=true;break;}
    }
    for(uint8_t failure:failed)result.failedComponents+=failure;
    if(!bridgeStable && result.failedComponents==0)result.failedComponents=1;
    result.localConverged=bridgeStable && result.failedComponents==0;
    if(!result.localConverged)for(auto&u:phase)std::fill(u.begin(),u.end(),infinity);
    auto connected=[&](int i,int j){
        if(!active[i] || !active[j] || a.component[i]!=a.component[j])return false;
        if(j==i+1)return (links[i]&1)!=0;
        if(j==i-1)return (links[j]&1)!=0;
        if(j==i+width)return (links[i]&2)!=0;
        if(j==i-width)return (links[j]&2)!=0;
        return false;
    };
    std::array<std::vector<Vector>,2> gradient;
    for(unsigned k=0;k<2;++k){auto&u=phase[k];auto&g=gradient[k];g.assign(count,{0,0});std::vector<double> scratch;
        // C2 patch tangents come from this same gently smoothed scalar phase.
        for(unsigned pass=0;pass<2;++pass)for(unsigned axis=0;axis<2;++axis){scratch=u;
            for(int y=0;y<height;++y)for(int x=0;x<width;++x){const int i=y*width+x,d=axis?width:1;
                if(!phaseAnchor[i] && (axis?y>0 && y+1<height:x>0 && x+1<width) && connected(i,i-d) && connected(i,i+d)
                    && std::isfinite(u[i-d]) && std::isfinite(u[i]) && std::isfinite(u[i+d]))scratch[i]=u[i]*.5+(u[i-d]+u[i+d])*.25;
            }u.swap(scratch);
        }
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){const int i=y*width+x;if(!active[i] || !std::isfinite(u[i]))continue;
            auto derivative=[&](bool axis){const int d=axis?width:1;
                const bool lo=(axis?y>0:x>0) && connected(i,i-d) && std::isfinite(u[i-d]);
                const bool hi=(axis?y+1<height:x+1<width) && connected(i,i+d) && std::isfinite(u[i+d]);
                return float(lo && hi?(u[i+d]-u[i-d])*.5:hi?u[i+d]-u[i]:lo?u[i]-u[i-d]:0);
            };g[i]={derivative(false),derivative(true)};
        }
        for(int i:paddingQueue)if(!playable(i) && std::isfinite(u[owner[i]])){
            const int from=owner[i];g[i]=g[from];
            u[i]=(std::max)(0.0,u[from]+g[i].x*(i%width-from%width)+g[i].y*(i/width-from/width));
        }
        // Nearest-body selection avoids mixing unrelated phases in a one-cell
        // dry halo. Every adjacent patch shares this one extrapolated payload.
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){const int i=y*width+x;if(wet[i])continue;
            int nearest=-1,best=3;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const int nx=x+dx,ny=y+dy;if(nx<0 || nx>=width || ny<0 || ny>=height)continue;
                const int j=ny*width+nx,d=dx*dx+dy*dy;if(wet[j] && std::isfinite(u[j]) && d<best){nearest=j;best=d;}
            }
            if(nearest>=0){g[i]=g[nearest];u[i]=(std::max)(0.0,u[nearest]+g[i].x*(x-nearest%width)+g[i].y*(y-nearest/width));}
        }
    }
    for(int i:paddingQueue)if(!playable(i))active[i]=active[owner[i]];
    std::vector<double> slow(count,1);std::array<std::vector<double>,2> confidence;
    for(unsigned k=0;k<2;++k)confidence[k]=ChartConfidence(active.data(),width,height,slow,phase[k],gradient[k]);
    double maximum=0;for(const auto&u:phase)for(double value:u)if(std::isfinite(value))maximum=(std::max)(maximum,value*spacing);
    // The draw contract packs this as 1+localStep. Even a phase-free calm field
    // needs a representable enabled marker so its suppression envelope is read.
    result.localStep=std::nextafter(float((std::max)(1.0/65536,maximum/65535)),std::numeric_limits<float>::infinity());
    for(int i=0;i<count;++i){
        for(unsigned k=0;k<2;++k)if(std::isfinite(phase[k][i])){
            const uint32_t fixed=uint32_t((std::min)(65535.0,std::round(phase[k][i]*spacing/result.localStep)));
            result.texels[size_t(i)*TexelsPerCell+2+k]=((fixed>>8)<<16)|((fixed&255)<<8)|WaterWaveHeading::EncodeGradient(gradient[k][i].x)|(WaterWaveHeading::EncodeGradient(gradient[k][i].y)<<24);
        }
        if(!wet[i])continue;
        const int classified=owner[i]>=0 && !playable(i)?owner[i]:i;
        double influence=1;const double t=WaterBodyAnalysis::Clamp01(a.sea[classified]*2);influence=1-t*t*(3-2*t);
        // Keep only a quiet background underneath the advected stream detail.
        // Sea samples bypass this envelope, retaining the original spectrum.
        double residual=active[i]?.045+.075*a.energy[classified]:.06;
        uint32_t weights=0;
        if(active[i] && std::isfinite(phase[0][i]) && std::isfinite(phase[1][i])){
            const double delta=(phase[1][i]-phase[0][i])*spacing;
            const double difference=std::hypot(delta,.5*spacing*std::hypot(double(gradient[0][i].x)-gradient[1][i].x,double(gradient[0][i].y)-gradient[1][i].y));
            const double blend=WaterBodyAnalysis::Clamp01(float((difference-.02)/(.25-.02)));
            const double activation=blend*blend*(3-2*blend);
            const double secondary=activation*.5*(1-std::tanh(delta/480));
            if(activation==0)weights=uint32_t(std::lround(a.energy[classified]*confidence[0][i]*255))<<16;
            else weights=PackAmplitude(a.energy[classified],secondary,confidence[0][i],confidence[1][i])&0x00ffff00u;
        }
        result.texels[size_t(i)*TexelsPerCell+4]=weights|uint32_t(std::lround(influence*255))|(uint32_t(std::lround(residual*255))<<24);
        if(result.texels[size_t(i)*TexelsPerCell+4]&255)
            result.texels[size_t(i)*TexelsPerCell+1]=(result.texels[size_t(i)*TexelsPerCell+1]&0xffff0000u)|PackBankVector(bank[i]);
    }
    // Extend only the blend envelope into the dry halo. Weights stay zero and
    // legacy amplitude/validity remain unchanged. Copy the same bank vector to
    // prevent the local projection from falling away at a rendered dry corner.
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){const int i=y*width+x;if(wet[i])continue;int nearest=-1,best=3;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const int nx=x+dx,ny=y+dy;if(nx<0 || nx>=width || ny<0 || ny>=height)continue;
            const int j=ny*width+nx,d=dx*dx+dy*dy;if(wet[j] && d<best){nearest=j;best=d;}
        }
        if(nearest>=0){result.texels[size_t(i)*TexelsPerCell+4]=result.texels[size_t(nearest)*TexelsPerCell+4]&0xff0000ffu;
            if(result.texels[size_t(i)*TexelsPerCell+4]&255)
                result.texels[size_t(i)*TexelsPerCell+1]=(result.texels[size_t(i)*TexelsPerCell+1]&0xffff0000u)|PackBankVector(bank[nearest]);
        }
    }
    return true;
}
}
