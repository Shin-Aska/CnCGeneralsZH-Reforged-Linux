// Hardware proof only: one indexed opaque quad, production SDL depth resolve, native Vulkan ray query.
// No map assets, software triangle tracer, or separate Vulkan device/queue.
#include "SdlGpuFrame.h"
#include "SdlHardwareSunShadow.h"
#include "../Libraries/Source/SDL_gpu_vulkan_rayquery.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

int main(int argc,char **argv)
{
    const char *image=nullptr;
    bool require4060=false;
    for(int arg=1;arg<argc;++arg) {
        if(std::strcmp(argv[arg],"--require-rtx4060")==0) require4060=true;
        else image=argv[arg];
    }
    {
        SdlSunDepthTiles tiles;
        tiles.Reset(6144,6144,128,{{0,0,6144,6144}});
        double offset[2]={1024,1024},stride[2]={1,1};
        auto first=tiles.Missing(offset,stride,4096,4096);
        auto area=[](const std::vector<SdlSunDepthTiles::Rectangle> &rects) {
            uint64_t total=0; for(const auto &r:rects) total+=uint64_t(r[2])*r[3]; return total;
        };
        if(area(first)!=4096ull*4096 || !tiles.Ready().empty()) return 1;
        tiles.Submitted(first); offset[0]-=9; offset[1]-=9;
        auto fringe=tiles.Missing(offset,stride,4096,4096);
        if(area(fringe)!=4096ull*256+128ull*128) return 1;
        tiles.Submitted(fringe);
        if(!tiles.Missing(offset,stride,4096,4096).empty()) return 1;
        tiles.Invalidate();
        if(!tiles.Ready().empty()) return 1;
        std::printf("CPU lazy tile readiness + 9-texel fringe: PASS\n");
    }
    SDL_setenv_unsafe("ZH_GPU_RAY_QUERY","1",1);
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr,"SDL: %s\n",SDL_GetError()); return 77; }
    int result=1;
    {
        std::string error;
        std::unique_ptr<SdlGpuFrame> frame(SdlGpuFrame::Create(nullptr,64,64,error));
        if(!frame) { std::fprintf(stderr,"hardware device unavailable: %s\n",error.c_str()); SDL_Quit(); return 77; }
        char support[1024] = {};
        if(!ZHR_SDLRayQuerySupported(frame->Device(),support,sizeof(support))) {
            std::fprintf(stderr,"ray query unsupported: %s\n",support); return 77;
        }
        const float positions[]={-.5f,-.5f,.5f, .5f,-.5f,.5f, .5f,.5f,.5f, -.5f,.5f,.5f};
        const uint32_t indices[]={0,1,2,0,2,3};
        std::unique_ptr<SdlHardwareSunShadow> shadow(SdlHardwareSunShadow::Create(*frame,positions,4,indices,6,error));
        if(!shadow) { std::fprintf(stderr,"ray query scene failed: %s\n",error.c_str()); return 1; }
        std::printf("HARDWARE: %s\n",shadow->Diagnostics().c_str());
        if(require4060 && shadow->Diagnostics().find("RTX 4060")==std::string::npos) {
            std::fprintf(stderr,"This proof requires the NVIDIA RTX 4060.\n"); return 1;
        }
        float inverse[]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        const float sun[]={0,0,1}, reverse[]={0,0,-1};
        const SdlTarget target=frame->Back_Buffer_Target();
        if (!shadow->PrepareComposite(target,error)) {
            std::fprintf(stderr,"composite preflight failed: %s\n",error.c_str()); return 1;
        }
        std::vector<float> mask;
        unsigned failures=0;
        auto check=[&](const char *name,const float *light,float maxDistance,float depth,bool rectangle,unsigned left=16,unsigned top=16) {
            // A constant-depth receiver is deliberately independent of the sole AS occluder.
            // This exercises actual depth attachment -> production R32 resolve -> reconstruction.
            frame->Clear_Back_Buffer(true,true,false,0xff808080,depth,0);
            if(!shadow->Render(target,inverse,light,.001f,maxDistance,error) ||
                !frame->Read_Water_Depth(shadow->Mask(),64,64,mask) || mask.size()!=4096) {
                std::fprintf(stderr,"%s failed: %s / %s\n",name,error.c_str(),SDL_GetError()); ++failures; return;
            }
            unsigned mismatches=0,occluded=0;
            for(unsigned y=0;y<64;++y) for(unsigned x=0;x<64;++x) {
                const float expected=rectangle && x>=left && x<left+32 && y>=top && y<top+32 ? 0.f:1.f;
                if(!std::isfinite(mask[y*64+x]) || std::fabs(mask[y*64+x]-expected)>.00001f) ++mismatches;
                if(mask[y*64+x]<.5f) ++occluded;
            }
            std::printf("%s: pixels=4096 occluded=%u expected=%u mismatches=%u sunny(8,8)=%.6f center(32,32)=%.6f\n",
                name,occluded,rectangle?1024:0,mismatches,mask[8*64+8],mask[32*64+32]);
            if(mismatches) ++failures;
        };
        check("one indexed mesh / toward sun",sun,10,.25f,true);
        if(image && mask.size()==4096) {
            FILE *file=std::fopen(image,"wb");
            if(!file) { std::perror(image); ++failures; }
            else {
                std::fprintf(file,"P5\n64 64\n255\n");
                for(float value:mask) { const unsigned char gray=value>.5f?255:0; std::fwrite(&gray,1,1,file); }
                std::fclose(file); std::printf("EVIDENCE: %s\n",image);
            }
        }
        std::vector<uint8_t> colour;
        if (!shadow->Composite(target,.5f,error) || !frame->Read_Back(target.Colour,64,64,colour) || colour.size()!=4096*4) {
            std::fprintf(stderr,"composite readback failed: %s\n",error.c_str()); ++failures;
        } else {
            unsigned mismatches=0;
            for(unsigned y=0;y<64;++y) for(unsigned x=0;x<64;++x) {
                const unsigned expected=x>=16 && x<48 && y>=16 && y<48 ? 64:128;
                const size_t at=(y*64+x)*4;
                if(colour[at]!=expected || colour[at+1]!=expected || colour[at+2]!=expected || colour[at+3]!=255) ++mismatches;
            }
            std::printf("multiplicative RGB composite: pixels=4096 mismatches=%u alpha=preserved\n",mismatches);
            if(mismatches) ++failures;
        }
        inverse[12]=.25f; inverse[13]=-.125f;
        check("translated inverse VP",sun,10,.25f,true,8,12);
        inverse[12]=inverse[13]=0;
        check("reverse sun control",reverse,10,.25f,false);
        check("short ray control",sun,.1f,.25f,false);
        check("background depth control",sun,10,1.f,false);
        // Shared light-space depth: nearer mapped dynamic casters survive, farther
        // casters remain outside the building footprint, and overlap is one minimum.
        // Far quad is intentionally first in the AS so closest-hit cannot use first-hit.
        const float stacked[]={
            -.5f,-.5f,.6f, .5f,-.5f,.6f, .5f,.5f,.6f, -.5f,.5f,.6f,
            -.5f,-.5f,.4f, .5f,-.5f,.4f, .5f,.5f,.4f, -.5f,.5f,.4f};
        const uint32_t stackedIndices[]={0,1,2,0,2,3,4,5,6,4,6,7};
        const std::vector<uint32_t> meshTriangleEnds = {2, 4};
        std::unique_ptr<SdlHardwareSunShadow> depthShadow(SdlHardwareSunShadow::Create(
            *frame,stacked,8,stackedIndices,12,error));
        SdlTarget sunTarget = {};
        if (!depthShadow || !frame->Sun_Shadow_Target(64,sunTarget,error)) {
            std::fprintf(stderr,"sun depth preflight failed: %s\n",error.c_str()); ++failures;
        } else {
            for (float mappedDepth : {1.f,.75f,.25f}) {
                frame->Set_Target(sunTarget);
                frame->Clear_Back_Buffer(false,true,false,0,mappedDepth,0);
                if (!depthShadow->RenderSunDepth(sunTarget,inverse,error) ||
                    !depthShadow->MergeSunDepth(sunTarget,error) ||
                    !depthShadow->MergeSunDepth(sunTarget,error) || // repeated overlap is idempotent
                    !frame->Read_Depth(sunTarget.Depth,64,64,mask) || mask.size()!=4096) {
                    std::fprintf(stderr,"shared sun depth merge failed: %s / %s\n",error.c_str(),SDL_GetError());
                    ++failures; continue;
                }
                unsigned mismatches=0;
                for(unsigned y=0;y<64;++y) for(unsigned x=0;x<64;++x) {
                    const bool building=x>=16 && x<48 && y>=16 && y<48;
                    const float expected=building && mappedDepth>.4f ? .4f : mappedDepth;
                    if(!std::isfinite(mask[y*64+x]) || std::fabs(mask[y*64+x]-expected)>.00001f) ++mismatches;
                }
                std::printf("shared sun depth: mapped=%.2f center=%.6f outside=%.6f mismatches=%u repeated=min\n",
                    mappedDepth,mask[32*64+32],mask[8*64+8],mismatches);
                if(mismatches) ++failures;
            }
            // Zooming out by an integer stride reuses exact rays from the finer
            // cache. Half a fine texel aligns the coarser pixel-center lattice;
            // the world square projects to x24..39/y24..39 at unchanged depth .4.
            const float zoomInverse[]={2,0,0,0, 0,2,0,0, 0,0,1,0, 1.f/64,-1.f/64,0,1};
            frame->Set_Target(sunTarget);
            frame->Clear_Back_Buffer(false,true,false,0,.75f,0);
            if(!depthShadow->RenderSunDepth(sunTarget,zoomInverse,error) ||
                !depthShadow->MergeSunDepth(sunTarget,error) ||
                !frame->Read_Depth(sunTarget.Depth,64,64,mask) || mask.size()!=4096) {
                std::fprintf(stderr,"integer-stride zoom failed: %s / %s\n",error.c_str(),SDL_GetError()); ++failures;
            } else {
                unsigned mismatches=0;
                for(unsigned y=0;y<64;++y) for(unsigned x=0;x<64;++x) {
                    const float expected=x>=24 && x<40 && y>=24 && y<40 ? .4f : .75f;
                    if(!std::isfinite(mask[y*64+x]) || std::fabs(mask[y*64+x]-expected)>.00001f) ++mismatches;
                }
                std::printf("exact integer-stride zoom from finer cache: mismatches=%u\n",mismatches);
                if(mismatches) ++failures;
            }
            // A texel-aligned camera pan retains the same world lattice and all
            // static Z. Reprojection must translate XY and convert cached depth:
            // world=(clip.x+.25,clip.y+.125,depth+.1), nearest=.4-.1=.3.
            float panInverse[16]; std::memcpy(panInverse,inverse,sizeof(panInverse));
            panInverse[12]=.25f; panInverse[13]=.125f; panInverse[14]=.1f;
            for(bool clipped : {false,true}) {
                float volume[16]; std::memcpy(volume,panInverse,sizeof(volume));
                if(clipped) volume[14]=.5f;
                // z=.4 is then before the new near plane. Reusing its cached
                // closest hit is invalid: a fresh bounded trace must find z=.6.
                const float nearest=clipped ? .1f : .3f;
                frame->Set_Target(sunTarget);
                frame->Clear_Back_Buffer(false,true,false,0,.75f,0);
                if(!depthShadow->RenderSunDepth(sunTarget,volume,error) ||
                    !depthShadow->MergeSunDepth(sunTarget,error) ||
                    !frame->Read_Depth(sunTarget.Depth,64,64,mask) || mask.size()!=4096) {
                    std::fprintf(stderr,"sun pan/clip merge failed: %s / %s\n",error.c_str(),SDL_GetError());
                    ++failures; continue;
                }
                unsigned mismatches=0;
                for(unsigned y=0;y<64;++y) for(unsigned x=0;x<64;++x) {
                    const float expected=x>=8 && x<40 && y>=20 && y<52 ? nearest : .75f;
                    if(!std::isfinite(mask[y*64+x]) || std::fabs(mask[y*64+x]-expected)>.00001f) ++mismatches;
                }
                std::printf("sun %s: nearest=%.6f expected=%.2f mismatches=%u\n",
                    clipped ? "depth-clip fresh fallback" : "lattice pan replay",mask[32*64+24],nearest,mismatches);
                if(mismatches) ++failures;
            }
            // Nonidentity orthographic inverse, in the production column-major ABI:
            // world.x=-.5*clip.y+.2, world.y=2*clip.x-.1, world.z=.8*depth+.1.
            // Include a half-texel displacement (+1/64,-1/64) in clip XY before
            // that transform, as used by the sun capture's D3D viewport correction.
            // Projecting the world [-.5,.5] building square gives pixel centers
            // x=25.1..41.1, y=-13.3..50.7: exactly x25..40, y0..50 on this target.
            // The near building plane z=.4 maps to (.4-.1)/.8=.375 depth.
            const float orthoInverse[]={
                0,2,0,0, -.5f,0,0,0, 0,0,.8f,0,
                .2f+.5f/64.f,-.1f+2.f/64.f,.1f,1};
            frame->Set_Target(sunTarget);
            frame->Clear_Back_Buffer(false,true,false,0,.75f,0);
            if (!depthShadow->RenderSunDepth(sunTarget,orthoInverse,error) ||
                !depthShadow->MergeSunDepth(sunTarget,error) ||
                !frame->Read_Depth(sunTarget.Depth,64,64,mask) || mask.size()!=4096) {
                std::fprintf(stderr,"nonidentity sun merge failed: %s / %s\n",error.c_str(),SDL_GetError());
                ++failures;
            } else {
                unsigned mismatches=0,covered=0;
                for(unsigned y=0;y<64;++y) for(unsigned x=0;x<64;++x) {
                    const float expected=x>=25 && x<41 && y<51 ? .375f : .75f;
                    covered+=mask[y*64+x]<.75f;
                    if(!std::isfinite(mask[y*64+x]) || std::fabs(mask[y*64+x]-expected)>.00001f) ++mismatches;
                }
                std::printf("nonidentity sun min merge: footprint=%u expected=816 inside=%.6f outside=%.6f mismatches=%u half-texel=enabled\n",
                    covered,mask[20*64+30],mask[60*64+60],mismatches);
                if(mismatches || covered!=816) ++failures;
            }
            // Same projection after a fresh mapped clear replays the cached static
            // result. A legacy visibility render uses the same output allocation,
            // so the following sun call must regenerate depth instead of reusing it.
            for (bool legacyInterruption : {false,true}) {
                frame->Set_Target(sunTarget);
                frame->Clear_Back_Buffer(false,true,false,0,.75f,0);
                const bool legacyOK=!legacyInterruption ||
                    depthShadow->Render(sunTarget,orthoInverse,sun,.001f,10,error);
                if (!legacyOK || !depthShadow->RenderSunDepth(sunTarget,orthoInverse,error) ||
                    !depthShadow->MergeSunDepth(sunTarget,error) ||
                    !frame->Read_Depth(sunTarget.Depth,64,64,mask) || mask.size()!=4096) {
                    std::fprintf(stderr,"sun depth replay failed (legacy=%u): %s / %s\n",
                        unsigned(legacyInterruption),error.c_str(),SDL_GetError());
                    ++failures; continue;
                }
                unsigned mismatches=0;
                for(unsigned y=0;y<64;++y) for(unsigned x=0;x<64;++x) {
                    const float expected=x>=25 && x<41 && y<51 ? .375f : .75f;
                    if(!std::isfinite(mask[y*64+x]) || std::fabs(mask[y*64+x]-expected)>.00001f) ++mismatches;
                }
                std::printf("sun depth %s after mapped clear: mismatches=%u\n",
                    legacyInterruption ? "legacy invalidation" : "unchanged projection replay",mismatches);
                if(mismatches) ++failures;
            }
            // Fractional motion needs fresh rays. This shift moves the bottom
            // building edge across row 50 without moving a whole cached texel.
            float fractional[16]; std::memcpy(fractional,orthoInverse,sizeof(fractional));
            fractional[12]+=.004f;
            frame->Set_Target(sunTarget);
            frame->Clear_Back_Buffer(false,true,false,0,.75f,0);
            if(!depthShadow->RenderSunDepth(sunTarget,fractional,error) ||
                !depthShadow->MergeSunDepth(sunTarget,error) ||
                !frame->Read_Depth(sunTarget.Depth,64,64,mask) || mask.size()!=4096) {
                std::fprintf(stderr,"fractional sun shift failed: %s\n",error.c_str()); ++failures;
            } else {
                unsigned mismatches=0;
                for(unsigned y=0;y<64;++y) for(unsigned x=0;x<64;++x) {
                    const float expected=x>=25 && x<41 && y<50 ? .375f : .75f;
                    if(!std::isfinite(mask[y*64+x]) || std::fabs(mask[y*64+x]-expected)>.00001f) ++mismatches;
                }
                std::printf("fractional sun shift: fresh edge mismatches=%u\n",mismatches);
                if(mismatches) ++failures;
            }
            // Failure before recording the optional merge must leave full mapped fallback.
            frame->Set_Target(sunTarget);
            frame->Clear_Back_Buffer(false,true,false,0,.75f,0);
            if (!depthShadow->RenderSunDepth(sunTarget,inverse,error)) {
                std::fprintf(stderr,"fallback query setup failed: %s\n",error.c_str()); ++failures;
            } else {
                frame->Fail_Next_Native_Record_For_Test();
                const bool refused=!depthShadow->MergeSunDepth(sunTarget,error);
                if (!refused || !frame->Read_Depth(sunTarget.Depth,64,64,mask) || mask.size()!=4096) {
                    std::fprintf(stderr,"fallback merge refusal/readback failed\n"); ++failures;
                } else {
                    unsigned mismatches=0;
                    for(float depth : mask) if(!std::isfinite(depth) || std::fabs(depth-.75f)>.00001f) ++mismatches;
                    std::printf("shared sun depth refusal fallback: mapped=0.75 mismatches=%u\n",mismatches);
                    if(mismatches) ++failures;
                }
            }
            // Replace the current AS geometry with a shifted copy of one quad.
            // Queued depth must use new coverage and the existing pipeline.
            float updated[12];
            std::memcpy(updated, stacked, sizeof(updated));
            for (unsigned vertex = 0; vertex < 4; ++vertex) updated[vertex * 3] += .25f;
            frame->Clear_Back_Buffer(false,true,false,0,.75f,0);
            if (!depthShadow->UpdateGeometry(updated,4,stackedIndices,6,error) ||
                !depthShadow->RenderSunDepth(sunTarget,inverse,error) ||
                !depthShadow->MergeSunDepth(sunTarget,error,false) ||
                !frame->Read_Depth(sunTarget.Depth,64,64,mask) || mask.size()!=4096) {
                std::fprintf(stderr,"updated sun geometry failed: %s / %s\n",error.c_str(),SDL_GetError());
                ++failures;
            } else {
                unsigned mismatches = 0;
                for (unsigned y = 0; y < 64; ++y) for (unsigned x = 0; x < 64; ++x) {
                    const float expected = x >= 24 && x < 56 && y >= 16 && y < 48 ? .6f : .75f;
                    if (!std::isfinite(mask[y*64+x]) || std::fabs(mask[y*64+x]-expected)>1e-5f) ++mismatches;
                }
                std::printf("updated sun geometry with queued merge: mismatches=%u\n",mismatches);
                if (mismatches) ++failures;
            }
            // Grow the same scene again after shrinking it. The restored near
            // quad must win over both the far quad and the mapped clear.
            frame->Clear_Back_Buffer(false,true,false,0,.75f,0);
            if (!depthShadow->UpdateGeometry(stacked,8,stackedIndices,12,error) ||
                !depthShadow->RenderSunDepth(sunTarget,inverse,error) ||
                !depthShadow->MergeSunDepth(sunTarget,error) ||
                !frame->Read_Depth(sunTarget.Depth,64,64,mask) || mask.size()!=4096) {
                std::fprintf(stderr,"restored sun geometry failed: %s / %s\n",error.c_str(),SDL_GetError());
                ++failures;
            } else {
                unsigned mismatches = 0;
                for (unsigned y=0; y<64; ++y) for (unsigned x=0; x<64; ++x) {
                    const float expected = x>=16 && x<48 && y>=16 && y<48 ? .4f : .75f;
                    if (!std::isfinite(mask[y*64+x]) || std::fabs(mask[y*64+x]-expected)>1e-5f) ++mismatches;
                }
                std::printf("restored sun geometry after shrink: mismatches=%u\n",mismatches);
                if (mismatches) ++failures;
            }

            // Moving and lowering the left caster must clear its old footprint
            // and update its depth while the separate right caster stays cached.
            const float sideBySide[] = {
                -.875f,-.5f,.4f, -.5f,-.5f,.4f, -.5f,.5f,.4f, -.875f,.5f,.4f,
                 .5f,-.5f,.6f, .875f,-.5f,.6f, .875f,.5f,.6f, .5f,.5f,.6f,
            };
            float moved[24];
            std::memcpy(moved, sideBySide, sizeof(sideBySide));
            for (unsigned vertex = 0; vertex < 4; ++vertex) {
                moved[vertex * 3] += .125f;
                moved[vertex * 3 + 2] = .35f;
            }
            for (unsigned pass = 0; pass < 2; ++pass) {
                frame->Clear_Back_Buffer(false,true,false,0,.75f,0);
                if (!depthShadow->UpdateGeometry(pass ? moved : sideBySide,8,stackedIndices,12,error,meshTriangleEnds) ||
                    !depthShadow->RenderSunDepth(sunTarget,inverse,error) ||
                    !depthShadow->MergeSunDepth(sunTarget,error,false) ||
                    !frame->Read_Depth(sunTarget.Depth,64,64,mask) || mask.size()!=4096) {
                    std::fprintf(stderr,"separate dirty regions failed: %s\n",error.c_str());
                    ++failures;
                    continue;
                }
                unsigned mismatches = 0;
                for (unsigned y = 0; y < 64; ++y) for (unsigned x = 0; x < 64; ++x) {
                    float expected = .75f;
                    if (y >= 16 && y < 48) {
                        if (x >= 4 + pass * 4 && x < 16 + pass * 4) expected = pass ? .35f : .4f;
                        if (x >= 48 && x < 60) expected = .6f;
                    }
                    if (!std::isfinite(mask[y * 64 + x]) || std::fabs(mask[y * 64 + x] - expected) > 1e-5f)
                        ++mismatches;
                }
                std::printf("separate dirty regions pass %u: mismatches=%u\n",pass,mismatches);
                if (mismatches) ++failures;
            }
            frame->Set_Target(target);
        }
        // Geometry outside the first view lives in uninitialized guard tiles.
        // Entering it must populate only the newly exposed cells before merging.
        const float lazyPositions[]={
            -.5f,-.5f,.4f, .5f,-.5f,.4f, .5f,.5f,.4f, -.5f,.5f,.4f,
            1.125f,-.5f,.3f, 1.375f,-.5f,.3f, 1.375f,.5f,.3f, 1.125f,.5f,.3f};
        float lazyTriangles[36];
        for (unsigned i = 0; i < 12; ++i)
            std::memcpy(lazyTriangles + i * 3, lazyPositions + stackedIndices[i] * 3, 3 * sizeof(float));
        const std::vector<uint32_t> firstBuilding = {2};
        std::unique_ptr<SdlHardwareSunShadow> lazyShadow(SdlHardwareSunShadow::Create(
            *frame,lazyTriangles,12,nullptr,12,error,meshTriangleEnds,meshTriangleEnds));
        if(!lazyShadow) { std::fprintf(stderr,"lazy sun setup failed: %s\n",error.c_str()); ++failures; }
        else for(unsigned pass=0;pass<5;++pass) {
            const unsigned pan = pass < 2 ? pass : 2;
            const bool foreignActive = pass != 3;
            if (pass >= 3) {
                const auto &ends = foreignActive ? meshTriangleEnds : firstBuilding;
                const unsigned vertices = ends.back() * 3;
                if (!lazyShadow->UpdateGeometry(lazyTriangles,vertices,nullptr,vertices,error,ends,ends)) {
                    std::fprintf(stderr,"changing active sun buildings failed: %s\n",error.c_str());
                    ++failures;
                    break;
                }
            }
            float lazyInverse[16]; std::memcpy(lazyInverse,inverse,sizeof(lazyInverse));
            lazyInverse[12]=pan*.25f;
            frame->Set_Target(sunTarget);
            frame->Clear_Back_Buffer(false,true,false,0,.75f,0);
            if(!lazyShadow->RenderSunDepth(sunTarget,lazyInverse,error) ||
                !lazyShadow->MergeSunDepth(sunTarget,error) ||
                !frame->Read_Depth(sunTarget.Depth,64,64,mask) || mask.size()!=4096) {
                std::fprintf(stderr,"lazy sun population failed: %s / %s\n",error.c_str(),SDL_GetError());
                ++failures; continue;
            }
            unsigned mismatches=0;
            const unsigned foreignLeft[]={64,60,52}, foreignRight[]={64,64,60};
            for(unsigned y=0;y<64;++y) for(unsigned x=0;x<64;++x) {
                const bool main=x>=16-pan*8 && x<48-pan*8 && y>=16 && y<48;
                const bool entering=foreignActive && x>=foreignLeft[pan] && x<foreignRight[pan] && y>=16 && y<48;
                const float expected=entering ? .3f : main ? .4f : .75f;
                if(!std::isfinite(mask[y*64+x]) || std::fabs(mask[y*64+x]-expected)>.00001f) ++mismatches;
            }
            std::printf("lazy guarded sun population: pan=%u active-buildings=%u mismatches=%u\n",
                pan, foreignActive ? 2u : 1u, mismatches);
            if(mismatches) ++failures;
        }
        frame->Set_Target(target);
        result=failures?1:0;
        std::printf("HARDWARE RAY QUERY %s (%u failed checks)\n",result?"FAIL":"PASS",failures);
    }
    SDL_Quit();
    return result;
}
