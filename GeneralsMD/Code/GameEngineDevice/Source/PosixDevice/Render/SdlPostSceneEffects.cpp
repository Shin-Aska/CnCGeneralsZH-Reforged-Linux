#include "SdlPostSceneEffects.h"
#include "SdlPostShader.h"
#include "SdlRenderMatrix.h"
#include "postsceneeffects.h"
#include "sdl3shadercompile.h"
#include "sdl3target.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
enum Effect { Ease, Soften, Memory, March, Blur, Composite, EffectCount };
const char *body(Effect effect) {
    const char *sources[]={SHROUD_EASE_SHADER_BODY,SHROUD_SOFTEN_SHADER_BODY,SHROUD_MEMORY_SHADER_BODY,
        SUN_SHAFTS_SHADER_BODY,SUN_SHAFTS_BLUR_SHADER_BODY,SUN_SHAFTS_COMPOSITE_SHADER_BODY};
    return sources[effect];
}
unsigned format_slot(unsigned format) {
    const unsigned formats[]={SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM,SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT};
    for(unsigned i=0;i<5;++i)if(format==formats[i])return i;
    return 5;
}
}
struct SdlPostSceneEffects::Impl {
    struct Program { SDL_GPUShader *Shader=nullptr; SDL_GPUGraphicsPipeline *Pipeline[5]{}; unsigned Samplers=0,Uniforms=0; } Programs[EffectCount];
    SdlGpuFrame *Owner=nullptr;
    SDL_GPUShader *Vertex=nullptr;
    SDL_GPUSampler *Sampler=nullptr;
    SDL_GPUTexture *White=nullptr, *Held[2]{}, *Soft=nullptr, *Half[2]{}, *Copy=nullptr;
    unsigned Width=0,Height=0,Scale=0,Current=0,HalfWidth=0,HalfHeight=0,CopyWidth=0,CopyHeight=0,CopyFormat=0;
    uint64_t Identity=0;
    float Settle=0,FogLight=-1;
    bool HasFog=false,SoftReady=false;
    bool MemoryLogged=false;
    float LoggedShaftSamples=0;
    std::string LastError;
    bool fail(const std::string &why) {
        if(LastError!=why)std::fprintf(stderr,"native SDL scene effects: %s\n",why.c_str());
        LastError=why;return false;
    }
    void drop(SDL_GPUTexture *&texture) {
        if(texture)Owner->Release_After_Batch(texture,nullptr);
        texture=nullptr;
    }
    bool target(SDL_GPUTexture *&texture,unsigned width,unsigned height,unsigned format) {
        SDL_GPUTextureCreateInfo info{};info.type=SDL_GPU_TEXTURETYPE_2D;info.format=SDL_GPUTextureFormat(format);
        info.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width=width;info.height=height;info.layer_count_or_depth=info.num_levels=1;
        SDL_GPUTexture *next=SDL_CreateGPUTexture(Owner->Device(),&info);
        if(!next)return fail(SDL_GetError());
        drop(texture);texture=next;return true;
    }
    bool program(Effect effect,unsigned format) {
        const unsigned slot=format_slot(format);if(slot==5)return fail("unsupported effect target format");
        auto *device=Owner->Device();std::string source,error;std::vector<unsigned char> spirv;
        if(!Vertex && (!Sdl_Post_Vertex_Source(source)||!SDL3_Compile_HLSL_To_SPIRV(source,true,spirv,error)
            ||!(Vertex=SDL3_Create_Shader(device,spirv,true,error))))return fail(error);
        if(!Sampler) {
            SDL_GPUSamplerCreateInfo info{};info.min_filter=info.mag_filter=SDL_GPU_FILTER_LINEAR;
            info.address_mode_u=info.address_mode_v=info.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            Sampler=SDL_CreateGPUSampler(device,&info);if(!Sampler)return fail(SDL_GetError());
        }
        if(!White) {
            SDL_GPUTextureCreateInfo info{};info.type=SDL_GPU_TEXTURETYPE_2D;info.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            info.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;info.width=info.height=info.layer_count_or_depth=info.num_levels=1;
            White=SDL_CreateGPUTexture(device,&info);if(!White)return fail(SDL_GetError());
            uint32_t offset=0;auto *bytes=Owner->Upload_Space(4,offset);if(!bytes)return fail("fallback upload failed");
            std::memset(bytes,255,4);Owner->Queue_Texture_Upload(White,0,1,1,offset);
        }
        Program &p=Programs[effect];
        if(!p.Shader) {
            source=std::string(PostShader_Pixel_Prologue())+body(effect);
            size_t at=0;while((at=source.find("VertexOutput",at))!=std::string::npos){source.replace(at,12,"Input");at+=5;}
            if (!SDL3_Shader_Retarget(source, false) || !SDL3_Compile_HLSL_To_SPIRV(source, false, spirv, error))
                return fail(error);
            SDL3_Shader_Slots(spirv,false,p.Samplers,p.Uniforms);
            if(p.Samplers>5||p.Uniforms>2)return fail("effect exceeds native shader bindings");
            p.Shader = SDL3_Create_Shader(device, spirv, false, error);
            if (!p.Shader) return fail(error);
        }
        if(!p.Pipeline[slot]) {
            SDL_GPUColorTargetDescription colour{};colour.format=SDL_GPUTextureFormat(format);
            if(effect==::Memory || effect==Composite) {
                auto &blend=colour.blend_state;blend.enable_blend=true;blend.enable_color_write_mask=true;
                blend.color_write_mask=SDL_GPU_COLORCOMPONENT_R|SDL_GPU_COLORCOMPONENT_G|SDL_GPU_COLORCOMPONENT_B;
                blend.src_color_blendfactor=effect==Composite?SDL_GPU_BLENDFACTOR_ONE:SDL_GPU_BLENDFACTOR_SRC_ALPHA;
                blend.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;blend.color_blend_op=SDL_GPU_BLENDOP_ADD;
                blend.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ZERO;blend.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
                blend.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
            }
            SDL_GPUGraphicsPipelineCreateInfo info{};info.vertex_shader=Vertex;info.fragment_shader=p.Shader;
            info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
            info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;info.rasterizer_state.enable_depth_clip=true;
            info.target_info.color_target_descriptions=&colour;info.target_info.num_color_targets=1;
            p.Pipeline[slot]=SDL_CreateGPUGraphicsPipeline(device,&info);if(!p.Pipeline[slot])return fail(SDL_GetError());
        }
        return true;
    }
    bool draw(Effect effect,SDL_GPUTexture *destination,unsigned width,unsigned height,unsigned format,
        SDL_GPUTexture *source,SDL_GPUTexture *depth,SDL_GPUTexture *shroud,SDL_GPUTexture *sun,SDL_GPUTexture *terrain,
        const PostSceneEffectConstants &constants) {
        const auto &p=Programs[effect];SdlRecordedDraw draw{};draw.Pipeline=p.Pipeline[format_slot(format)];
        draw.Count=3;draw.Viewport[2]=float(width);draw.Viewport[3]=float(height);draw.Viewport[5]=1;
        draw.Scissor[2]=int(width);draw.Scissor[3]=int(height);draw.SamplerCount=p.Samplers;
        SDL_GPUTexture *textures[]={source,depth,shroud,sun,terrain};
        for(unsigned i=0;i<p.Samplers;++i){draw.Textures[i]=textures[i]?textures[i]:White;draw.Samplers[i]=Sampler;
            if(draw.Textures[i]==destination)return fail("effect samples its destination");}
        const SdlNativeUniformBlock blocks[]={{1,0,&constants,sizeof(constants)},{1,1,&constants,sizeof(constants)}};
        const SdlTarget target={destination,nullptr,width,height,0,format};
        return Owner->Record_Native_Draw(target,draw,blocks,p.Uniforms)||fail("effect draw refused");
    }
    bool inputs(SdlGpuFrame &frame,const SdlTarget &scene,const SdlPostSceneInputs &in) {
        return Owner==&frame && scene.Colour && scene.Depth && in.SampledDepth && in.SampledDepth!=scene.Colour
            && in.SampledDepth!=scene.Depth && in.DepthWidth==scene.Width && in.DepthHeight==scene.Height
            && in.DepthFormat==SDL_GPU_TEXTUREFORMAT_R32_FLOAT && in.ActualSamples==1
            && in.Capture.Source.Colour==scene.Colour && in.Capture.Source.Depth==scene.Depth
            && in.Capture.Phase==SdlSceneCapturePhase::BeforeTransparent
            && in.Capture.Frame==frame.Scene_Frame() && in.Capture.Generation==frame.Scene_Generation();
    }
};
SdlPostSceneEffects::SdlPostSceneEffects():State(new Impl){}
SdlPostSceneEffects::~SdlPostSceneEffects(){if(State->Owner)Release(*State->Owner);delete State;}
SDL_GPUTexture *SdlPostSceneEffects::Soft_Shroud() const {return State->SoftReady?State->Soft:nullptr;}
bool SdlPostSceneEffects::Fogged() const {return State->SoftReady && State->HasFog;}
const std::string &SdlPostSceneEffects::Error() const{return State->LastError;}
bool SdlPostSceneEffects::Soften_Shroud(SdlGpuFrame &frame,SDL_GPUTexture *cells,unsigned width,unsigned height,
    uint64_t identity,float seconds,bool written,float fogLight,bool fogged) {
    Impl &s=*State;if(s.Owner && s.Owner!=&frame)return s.fail("effect frame owner changed without release");s.Owner=&frame;
    if(!cells){s.drop(s.Held[0]);s.drop(s.Held[1]);s.drop(s.Soft);s.SoftReady=false;s.Width=s.Height=0;s.Identity=0;s.FogLight=-1;return true;}
    if(!width||!height||!identity||!std::isfinite(seconds)||!std::isfinite(fogLight))return s.fail("invalid shroud input");
    const unsigned format=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM;
    if(!s.program(Ease,format)||!s.program(Soften,format))return false;
    const bool fresh = !s.SoftReady || s.Width != width || s.Height != height || s.Identity != identity;
    if(fresh){unsigned scale=4;while(scale>1 && std::max(width,height)*scale>4096)--scale;
        if(!s.target(s.Held[0],width,height,format)||!s.target(s.Held[1],width,height,format)
            ||!s.target(s.Soft,width*scale,height*scale,format)){s.SoftReady=false;return false;}
        s.Width=width;s.Height=height;s.Scale=scale;s.Identity=identity;s.Current=0;}
    s.FogLight=fogLight;if(written||fresh)s.Settle=.6f;s.HasFog=fogged||s.Settle>0;
    if(s.Settle<=0)return s.SoftReady;
    seconds=std::max(seconds,0.f);s.Settle-=seconds;
    PostSceneEffectConstants c{};c.TexelSize[0]=1.f/width;c.TexelSize[1]=1.f/height;c.TexelSize[2]=float(width);c.TexelSize[3]=float(height);
    c.BlurDirection[0]=fresh?1.f:1.f-std::exp(-seconds/.1f);c.BlurDirection[1]=fresh?1.f:seconds/.4f;
    const unsigned next=1-s.Current;
    if (!s.draw(Ease,s.Held[next],width,height,format,cells,fresh?cells:s.Held[s.Current],nullptr,nullptr,nullptr,c)) {
        s.SoftReady = false;
        return false;
    }
    s.Current=next;c.BlurDirection[0]=float(s.Scale);c.BlurDirection[1]=0;
    s.SoftReady=s.draw(Soften,s.Soft,width*s.Scale,height*s.Scale,format,s.Held[next],nullptr,nullptr,nullptr,nullptr,c);
    if(s.SoftReady && fresh)std::fprintf(stderr,"native SDL shroud: eased %ux%u cells, cubic filter %ux%u, visual input only\n",
        width,height,width*s.Scale,height*s.Scale);
    return s.SoftReady;
}
bool SdlPostSceneEffects::Memory(SdlGpuFrame &frame,const SdlTarget &scene,const SdlPostSceneInputs &in) {
    Impl &s=*State;if(!s.inputs(frame,scene,in)||!Fogged()||!in.Shroud||s.FogLight<0)return false;
    const unsigned format=SdlGpuFrame::Target_Colour_Format(scene);if(!s.program(::Memory,format))return false;
    if(!s.Copy || s.CopyWidth!=scene.Width || s.CopyHeight!=scene.Height || s.CopyFormat!=format){
        if(!s.target(s.Copy,scene.Width,scene.Height,format))return false;s.CopyWidth=scene.Width;s.CopyHeight=scene.Height;s.CopyFormat=format;}
    const int32_t rect[]={0,0,int32_t(scene.Width),int32_t(scene.Height)};
    frame.Record_Blit(scene.Colour,rect,s.Copy,rect,false);
    PostSceneEffectConstants c{};c.BlurDirection[0]=s.FogLight;c.HazeSun[2]=1;
    std::memcpy(c.WorldFromClip,in.WorldFromClip,sizeof(c.WorldFromClip));std::memcpy(c.HazeShroudRect,in.ShroudRect,sizeof(c.HazeShroudRect));
    const bool drawn=s.draw(::Memory,scene.Colour,scene.Width,scene.Height,format,s.Copy,in.SampledDepth,in.Shroud,nullptr,nullptr,c);
    if(drawn && !s.MemoryLogged){std::fprintf(stderr,"native SDL shroud: fog-memory colour pass enabled\n");s.MemoryLogged=true;}
    return drawn;
}
bool SdlPostSceneEffects::Sun_Shafts(SdlGpuFrame &frame,const SdlTarget &scene,const SdlPostSceneInputs &in,
    const RenderPost::SunShaftSettings &settings,SDL_GPUTexture *sunMap,const float sunFromWorld[16],
    float bias,float shadowStrength,SDL_GPUTexture *heights,const float mapping[3]) {
    Impl &s=*State;if(!s.Owner)s.Owner=&frame;if(!s.inputs(frame,scene,in)||settings.Strength<=0||!sunMap)return false;
    if(settings.Samples<1 || settings.Samples>32 || settings.WaterCount<0 || settings.WaterCount>8)
        return s.fail("invalid sun-shaft sampling limits");
    const unsigned format=SdlGpuFrame::Target_Colour_Format(scene),halfFormat=SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT;
    if(!s.program(March,halfFormat)||!s.program(Blur,halfFormat)||!s.program(Composite,format))return false;
    const unsigned width=(scene.Width+1)/2,height=(scene.Height+1)/2;
    if(!s.Half[0]||s.HalfWidth!=width||s.HalfHeight!=height){if(!s.target(s.Half[0],width,height,halfFormat)
        ||!s.target(s.Half[1],width,height,halfFormat))return false;s.HalfWidth=width;s.HalfHeight=height;}
    PostSceneEffectConstants c{};std::memcpy(c.WorldFromClip,in.WorldFromClip,sizeof(c.WorldFromClip));
    std::memcpy(c.SunFromWorld,sunFromWorld,sizeof(c.SunFromWorld));if(!render_inverse(sunFromWorld,c.WorldFromSun))return false;
    std::memcpy(c.HazeShroudRect,in.ShroudRect,sizeof(c.HazeShroudRect));c.HazeSun[2]=in.Shroud?1.f:0.f;
    c.WorldAoProjection[0]=in.ProjectionScale[0];c.WorldAoProjection[1]=in.ProjectionScale[1];
    c.Shafts[0]=settings.Ground;c.Shafts[1]=settings.Top;c.Shafts[2]=settings.Falloff;c.Shafts[3]=settings.Samples;
    c.ShaftSun[0]=settings.TowardSun[0];c.ShaftSun[1]=settings.TowardSun[1];c.ShaftSun[2]=settings.Glare;c.ShaftSun[3]=bias;
    c.ShaftLook[0]=settings.Scale;c.ShaftLook[1]=settings.Band;c.ShaftLook[2]=settings.Dark;c.ShaftLook[3]=settings.Slack;
    c.ShaftGlare[0]=settings.ShallowTop;c.ShaftGlare[1]=settings.ShallowSpan;c.ShaftGlare[2]=settings.WaterCount;c.ShaftGlare[3]=settings.Side;
    std::memcpy(c.ShaftColour,settings.Colour,sizeof(settings.Colour));c.ShaftColour[3]=settings.Strength;
    std::memcpy(c.ShaftWater,settings.WaterBoxes,sizeof(c.ShaftWater));for(unsigned i=0;i<8;++i)c.ShaftWaterHeight[i][0]=settings.WaterHeights[i];
    c.ShaftLimit[0]=settings.DarkCap*shadowStrength;c.ShaftLimit[1]=settings.ShadedMost;c.ShaftLimit[2]=settings.Floor;c.ShaftLimit[3]=settings.NormalOffset;
    c.ShaftBand[0]=settings.Radius;c.ShaftBand[1]=settings.StreakWidth;c.ShaftBand[2]=settings.StreakLength;
    if(heights&&mapping){std::memcpy(c.ShaftTerrain,mapping,3*sizeof(float));c.ShaftTerrain[3]=1;}
    const float *values=reinterpret_cast<const float *>(&c);
    for(unsigned i=0;i<sizeof(c)/sizeof(float);++i)if(!std::isfinite(values[i]))return s.fail("non-finite sun-shaft inputs");
    c.TexelSize[0]=1.f/scene.Width;c.TexelSize[1]=1.f/scene.Height;c.TexelSize[2]=float(scene.Width);c.TexelSize[3]=float(scene.Height);
    if(!s.draw(March,s.Half[0],width,height,halfFormat,nullptr,in.SampledDepth,in.Shroud,sunMap,heights,c))return false;
    c.TexelSize[0]=1.f/width;c.TexelSize[1]=1.f/height;c.TexelSize[2]=float(width);c.TexelSize[3]=float(height);c.BlurDirection[0]=1;
    if(!s.draw(Blur,s.Half[1],width,height,halfFormat,s.Half[0],in.SampledDepth,in.Shroud,sunMap,heights,c))return false;
    c.BlurDirection[0]=0;c.BlurDirection[1]=1;
    if(!s.draw(Blur,s.Half[0],width,height,halfFormat,s.Half[1],in.SampledDepth,in.Shroud,sunMap,heights,c))return false;
    c.BlurDirection[1]=0;
    const bool drawn=s.draw(Composite,scene.Colour,scene.Width,scene.Height,format,s.Half[0],in.SampledDepth,in.Shroud,sunMap,heights,c);
    if(drawn && s.LoggedShaftSamples!=settings.Samples){
        std::fprintf(stderr,"native SDL sun shafts: half-size march+blur+composite, samples=%.0f terrain=%u water-areas=%.0f\n",
            settings.Samples,heights?1u:0u,settings.WaterCount);s.LoggedShaftSamples=settings.Samples;}
    return drawn;
}
bool SdlPostSceneEffects::Release(SdlGpuFrame &frame) {
    Impl &s=*State;if(s.Owner && s.Owner!=&frame)return false;if(!frame.Flush())return false;
    auto *device=frame.Device();for(auto &p:s.Programs){for(auto *&pipeline:p.Pipeline){if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);pipeline=nullptr;}
        if(p.Shader)SDL_ReleaseGPUShader(device,p.Shader);p.Shader=nullptr;}
    if(s.Vertex)SDL_ReleaseGPUShader(device,s.Vertex);s.Vertex=nullptr;if(s.Sampler)SDL_ReleaseGPUSampler(device,s.Sampler);s.Sampler=nullptr;
    for(auto **texture:{&s.White,&s.Held[0],&s.Held[1],&s.Soft,&s.Half[0],&s.Half[1],&s.Copy})s.drop(*texture);
    s.Owner=nullptr;s.SoftReady=false;return true;
}
