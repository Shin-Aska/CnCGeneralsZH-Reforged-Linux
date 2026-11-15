// Capability honesty, capture identity, explicit target formats, and delayed native uniforms.
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "SdlSceneCapture.h"
#include "SdlRenderMatrix.h"
#include "SdlSceneContext.h"
#include "SdlGpuParticlesContext.h"
#include "SdlResourceMirror.h"
#include "SdlPipelineCache.h"
#include "../Libraries/Source/WWVegas/WW3D2/sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <cmath>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)

static std::vector<uint16_t> read_half(SdlGpuFrame &frame, const SdlTarget &target)
{
    std::vector<uint16_t> result;
    if (!frame.Flush()) { CHECK(false); return result; }
    SDL_GPUTransferBufferCreateInfo info = {};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD; info.size = target.Width * target.Height * 8;
    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(frame.Device(), &info);
    SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(frame.Device());
    CHECK(transfer && commands);
    if (!transfer || !commands) {
        if (transfer) SDL_ReleaseGPUTransferBuffer(frame.Device(), transfer);
        if (commands) SDL_CancelGPUCommandBuffer(commands);
        return result;
    }
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands); CHECK(copy);
    if (!copy) { SDL_CancelGPUCommandBuffer(commands); SDL_ReleaseGPUTransferBuffer(frame.Device(), transfer); return result; }
    SDL_GPUTextureRegion source = {}; source.texture = target.Colour;
    source.w = target.Width; source.h = target.Height; source.d = 1;
    SDL_GPUTextureTransferInfo destination = {}; destination.transfer_buffer = transfer;
    destination.pixels_per_row = target.Width; destination.rows_per_layer = target.Height;
    SDL_DownloadFromGPUTexture(copy, &source, &destination); SDL_EndGPUCopyPass(copy);
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands); CHECK(fence);
    if (fence) {
        CHECK(SDL_WaitForGPUFences(frame.Device(), true, &fence, 1));
        const void *mapped = SDL_MapGPUTransferBuffer(frame.Device(), transfer, false); CHECK(mapped);
        if (mapped) {
            result.resize(target.Width * target.Height * 4);
            std::memcpy(result.data(), mapped, info.size); SDL_UnmapGPUTransferBuffer(frame.Device(), transfer);
        }
        SDL_ReleaseGPUFence(frame.Device(), fence);
    }
    SDL_ReleaseGPUTransferBuffer(frame.Device(), transfer);
    return result;
}

static void check_cpu()
{
    float identity[16] = {}, worldFromTexture[16], projectionScale[2];
    for (unsigned i = 0; i < 4; ++i) identity[i*5] = 1;
    const float viewport[] = {4, 2, 8, 4, 0.2f, 0.8f};
    CHECK(render_full_texture_to_world(identity, identity, 16, 8, viewport, worldFromTexture, projectionScale));
    const float fullCorners[2][4] = {{-0.5f, 0.5f, 0.2f, 1}, {0.5f, -0.5f, 0.8f, 1}};
    const float cameraCorners[2][4] = {{-1, 1, 0, 1}, {1, -1, 1, 1}};
    for (unsigned i = 0; i < 2; ++i) for (unsigned c = 0; c < 4; ++c) {
        float value = 0;
        for (unsigned j = 0; j < 4; ++j) value += fullCorners[i][j] * worldFromTexture[j*4+c];
        CHECK(std::abs(value - cameraCorners[i][c]) < 0.00001f);
    }
    CHECK(projectionScale[0] == 0.5f && projectionScale[1] == 0.5f);
    IDirect3D9 *api = Direct3DCreate9(D3D_SDK_VERSION);
    CHECK(api);
    if (!api) return;
    D3DPRESENT_PARAMETERS parameters = {};
    parameters.BackBufferWidth = parameters.BackBufferHeight = 16;
    parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
    parameters.Windowed = 1;
    IDirect3DDevice9 *device = nullptr;
    CHECK(api->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, 0, &parameters, &device) == D3D_OK);
    for (unsigned i = 0; i <= unsigned(RendererFeature::Count); ++i) {
        CHECK(!PosixRenderer_Supports_Feature(nullptr, RendererFeature(i)));
        CHECK(!PosixRenderer_Supports_Feature(device, RendererFeature(i)));
    }
    SdlGpuParticlesContext headlessParticles;
    CHECK(!PosixGpuParticles_Context(device, headlessParticles));
    CHECK(!headlessParticles.Frame && !headlessParticles.MirrorTexture && !headlessParticles.DeviceGeneration);
    CHECK(!PosixGpuParticles_Context(nullptr, headlessParticles));
    if (device) device->Release();
    api->Release();

    SdlSceneCaptureRequest base;
    base.Source = {reinterpret_cast<SDL_GPUTexture *>(1), reinterpret_cast<SDL_GPUTexture *>(2), 16, 8};
    base.Frame = 27; base.Generation = 3;
    SdlSceneCaptureRequest other = base;
    CHECK(base.Same_Identity(other));
    other.Colour = !base.Colour; // Different consumers can request channels of the same capture.
    CHECK(base.Same_Identity(other));
    other.Frame++; CHECK(!base.Same_Identity(other)); other = base;
    other.Generation++; CHECK(!base.Same_Identity(other)); other = base;
    other.ViewKey++; CHECK(!base.Same_Identity(other)); other = base;
    other.Phase = SdlSceneCapturePhase::BeforePost; CHECK(!base.Same_Identity(other)); other = base;
    other.Source.Width++; CHECK(!base.Same_Identity(other)); other = base;
    other.Source.Height++; CHECK(!base.Same_Identity(other)); other = base;
    other.Source.Depth = nullptr; CHECK(!base.Same_Identity(other)); other = base;
    other.Source.ColourFormat = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    CHECK(!base.Same_Identity(other));
    CHECK(SdlGpuFrame::Target_Colour_Format(base.Source) == SdlGpuFrame::Target_Format());
    CHECK(SdlGpuFrame::Target_Colour_Format(other.Source) == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
    other.Source.Colour = nullptr;
    CHECK(SdlGpuFrame::Target_Colour_Format(other.Source) == 0);
}

static int check_gpu()
{
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::printf("SKIP SDL: %s\n", SDL_GetError()); return 77; }
    std::string error;
    IDirect3D9 *api = Direct3DCreate9(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS parameters = {};
    parameters.BackBufferWidth = 16; parameters.BackBufferHeight = 8;
    parameters.BackBufferFormat = D3DFMT_X8R8G8B8; parameters.Windowed = 1;
    IDirect3DDevice9 *created = nullptr;
    CHECK(api && api->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, 0, &parameters, &created) == D3D_OK);
    if (!created) { if (api) api->Release(); SDL_Quit(); return 1; }
    PosixDevice9 *device = static_cast<PosixDevice9 *>(created);
    if (device->Create_Gpu_Frame(true) != D3D_OK) {
        std::printf("SKIP GPU: %s\n", SDL_GetError()); device->Release(); api->Release(); SDL_Quit(); return 77;
    }
    SdlGpuFrame *frame = device->Get_Gpu();
    SdlGpuParticlesContext particles;
    CHECK(PosixGpuParticles_Context(device, particles));
    CHECK(particles.Frame == frame && particles.DeviceGeneration && particles.MirrorTexture);
    CHECK(particles.Pass == SdlGpuParticlesPass::Other && !particles.SoftAllowed && particles.AdditiveGain == 1);
    IDirect3DTexture9 *art = nullptr;
    CHECK(device->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &art, nullptr) == D3D_OK);
    if (art) {
        D3DLOCKED_RECT lock = {};
        CHECK(art->LockRect(0, &lock, nullptr, 0) == D3D_OK);
        std::memset(lock.pBits, 255, lock.Pitch * 2); CHECK(art->UnlockRect(0) == D3D_OK);
        SdlGpuParticleTextureView first, same, changed;
        CHECK(particles.MirrorTexture(art, first, error)); CHECK(first.Identity && first.Generation && first.Width == 2 && first.Height == 2 && first.MipLevels == 1);
        CHECK(particles.MirrorTexture(art, same, error)); CHECK(first.Identity == same.Identity && first.Generation == same.Generation && first.Texture == same.Texture);
        CHECK(art->LockRect(0, &lock, nullptr, 0) == D3D_OK);
        std::memset(lock.pBits, 128, lock.Pitch * 2); CHECK(art->UnlockRect(0) == D3D_OK);
        CHECK(particles.MirrorTexture(art, changed, error)); CHECK(changed.Identity == first.Identity && changed.Generation != first.Generation);
        art->Release();
    }

    CHECK(PosixRenderer_Supports_Feature(device, RendererFeature::BattleMarks));
    CHECK(PosixRenderer_Supports_Feature(device, RendererFeature::PerPixelLights));
    CHECK(PosixRenderer_Supports_Feature(device, RendererFeature::HemisphericAmbient));
    CHECK(PosixRenderer_Supports_Feature(device, RendererFeature::ClassicWaterSplashes));
    CHECK(PosixRenderer_Supports_Feature(device, RendererFeature::BuildingCollapse));
    CHECK(PosixRenderer_Supports_Feature(device, RendererFeature::PostProcessing));
    CHECK(PosixRenderer_Supports_Feature(device, RendererFeature::InternalHDR) == bool(SDL_GPUTextureSupportsFormat(frame->Device(),
        SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)));
    CHECK(PosixRenderer_Supports_Feature(device, RendererFeature::AmbientOcclusion) ==
        (frame->Supports_Scene_Depth() && SDL_GPUTextureSupportsFormat(frame->Device(), SDL_GPU_TEXTUREFORMAT_R32_FLOAT,
            SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER) &&
        SDL_GPUTextureSupportsFormat(frame->Device(), SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT,
            SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)));
    CHECK(PosixRenderer_Supports_Feature(device, RendererFeature::AtmosphereHaze) == frame->Supports_Scene_Depth());
    CHECK(PosixRenderer_Supports_Feature(device, RendererFeature::HeatBlastDistortion) ==
        (frame->Supports_Scene_Depth() && PosixRenderer_Supports_Feature(device, RendererFeature::InternalHDR)));
    CHECK(!PosixRenderer_Supports_Feature(device, RendererFeature::Count));
    CHECK(PosixRenderer_Supports_Feature(device, RendererFeature::SoftParticles) == frame->Supports_Scene_Depth());
    CHECK(PosixRenderer_Supports_Feature(device, RendererFeature::ProceduralFire));
    CHECK(PosixRenderer_Supports_Feature(device, RendererFeature::WaterFoam) == frame->Supports_Scene_Depth());
    CHECK(PosixRenderer_Supports_Feature(device, RendererFeature::ModelEffects));
    CHECK(PosixRenderer_Supports_Feature(device, RendererFeature::TeamGlow) == PosixRenderer_Supports_Feature(device, RendererFeature::InternalHDR));
    for (RendererFeature feature : {RendererFeature::GpuParticles, RendererFeature::Weather, RendererFeature::DistanceFieldReachOverlay})
        CHECK(!PosixRenderer_Supports_Feature(device, feature));
    const char *vs = "float4 main(uint id:SV_VertexID):SV_Position { return float4(id==1?3:-1,id==2?3:-1,0,1); }";
    const char *ps = "cbuffer Value:register(b0,space3){float4 colour;};float4 main():SV_Target0{return colour;}";
    std::vector<unsigned char> spirv;
    CHECK(SDL3_Compile_HLSL_To_SPIRV(vs, true, spirv, error));
    SDL_GPUShader *vertex = SDL3_Create_Shader(frame->Device(), spirv, true, error);
    CHECK(SDL3_Compile_HLSL_To_SPIRV(ps, false, spirv, error));
    SDL_GPUShader *pixel = SDL3_Create_Shader(frame->Device(), spirv, false, error);
    SDL_GPUTextureCreateInfo texture = {};
    texture.type = SDL_GPU_TEXTURETYPE_2D;
    texture.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    texture.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture.width = 16; texture.height = 8; texture.layer_count_or_depth = texture.num_levels = 1;
    SDL_GPUTexture *floating = SDL_CreateGPUTexture(frame->Device(), &texture);
    CHECK(vertex && pixel && floating);
    SdlTarget target = {floating, nullptr, 16, 8, 0, SDL_GPU_TEXTUREFORMAT_R32_FLOAT};
    RenderUInt32 states[256] = {};
    states[D3DRS_FILLMODE] = D3DFILL_SOLID; states[D3DRS_CULLMODE] = D3DCULL_NONE;
    states[D3DRS_COLORWRITEENABLE] = 15;
    SdlPipelineKey key;
    CHECK(Sdl_Pipeline_Key(states, D3DPT_TRIANGLELIST, vertex, pixel, D3DFVF_XYZ,
        frame->Target_Colour_Format(target), 0, key, error));
    {
        SdlPipelineCache cache(frame->Device());
        SDL_GPUGraphicsPipeline *pipeline = cache.Pipeline(key);
        CHECK(pipeline);
        SdlRecordedDraw draw = {};
        draw.Pipeline = pipeline; draw.Count = 3;
        std::memset(frame->Stage(36, draw.VertexOffset), 0, 36);
        draw.Viewport[2] = draw.Scissor[2] = 16;
        draw.Viewport[3] = draw.Scissor[3] = 8; draw.Viewport[5] = 1;
        frame->Set_Target(target);
        frame->Clear_Back_Buffer(true, false, false, 0, 1, 0);
        // Same uniform storage is reused before submission: both earlier values must survive.
        float values[4] = {2.5f, 0, 0, 1};
        SdlNativeUniformBlock block = {1, 0, values, sizeof(values)};
        draw.Scissor[2] = 8;
        SdlNativeDrawPacket refused[2] = {{target, draw, &block, 1}, {target, draw, &block, 1}};
        refused[1].Draw.Pipeline = nullptr;
        CHECK(!frame->Record_Native_Draw_Batch(refused, 2));
        std::vector<uint8_t> untouched;
        CHECK(frame->Read_Back(floating, 16, 8, untouched));
        float beforeCommit = -1;
        if (untouched.size() == 16 * 8 * sizeof(float)) std::memcpy(&beforeCommit, untouched.data(), sizeof(float));
        CHECK(beforeCommit == 0); // refusal in packet two must not append packet one
        CHECK(frame->Record_Native_Draw(target, draw, &block, 1));
        values[0] = 4.5f; draw.Scissor[0] = 8;
        CHECK(frame->Record_Native_Draw(target, draw, &block, 1));
        values[0] = 99;
        // Invalid input must leave both the target and the valid batch intact.
        block.Slot = 4;
        CHECK(!frame->Record_Native_Draw(target, draw, &block, 1));
        std::vector<uint8_t> bytes;
        CHECK(frame->Read_Back(floating, 16, 8, bytes));
        if (bytes.size() == 16 * 8 * sizeof(float)) {
            float left = 0, right = 0;
            std::memcpy(&left, bytes.data() + (4 * 16 + 4) * sizeof(float), sizeof(float));
            std::memcpy(&right, bytes.data() + (4 * 16 + 12) * sizeof(float), sizeof(float));
            CHECK(left == 2.5f); CHECK(right == 4.5f);
        } else CHECK(false);
        // Rect clears use this float target's format and have no depth attachment.
        frame->Clear_Rect(0, 0, 8, 8, true, false, false, 0xFF800000, 1, 0);
        CHECK(frame->Read_Back(floating, 16, 8, bytes));
        float cleared = 0, retained = 0;
        if (bytes.size() == 16 * 8 * sizeof(float)) {
            std::memcpy(&cleared, bytes.data() + (4 * 16 + 4) * sizeof(float), sizeof(float));
            std::memcpy(&retained, bytes.data() + (4 * 16 + 12) * sizeof(float), sizeof(float));
            CHECK(cleared > 0.50f && cleared < 0.51f); CHECK(retained == 4.5f);
        }
        // Legacy BGRA clear pipeline remains a distinct cache entry.
        frame->Set_Target(frame->Back_Buffer_Target());
        frame->Clear_Back_Buffer(true, true, true, 0, 1, 0);
        frame->Clear_Rect(0, 0, 16, 8, true, false, false, 0xFFFF0000, 1, 0);
        CHECK(frame->Read_Back(frame->Back_Buffer(), 16, 8, bytes));
        CHECK(bytes.size() == 16 * 8 * 4 && bytes[2] == 255 && bytes[0] == 0);
        states[D3DRS_ALPHABLENDENABLE] = true;
        states[D3DRS_SRCBLEND] = D3DBLEND_BLENDFACTOR; states[D3DRS_DESTBLEND] = D3DBLEND_ONE;
        states[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
        CHECK(Sdl_Pipeline_Key(states, D3DPT_TRIANGLELIST, vertex, pixel, D3DFVF_XYZ,
            frame->Target_Colour_Format(target), 0, key, error));
        draw.Pipeline = cache.Pipeline(key); CHECK(draw.Pipeline);
        draw.FloatBlendFactor = true; draw.BlendConstants[0] = 2; draw.BlendConstants[1] = 2;
        draw.BlendConstants[2] = 2; draw.BlendConstants[3] = 1;
        draw.Scissor[0] = 0; draw.Scissor[2] = 8;
        frame->Set_Target(target); frame->Clear_Back_Buffer(true, false, false, 0, 1, 0);
        values[0] = 2.5f; block.Slot = 0;
        SdlNativeDrawPacket gainPackets[2] = {{target, draw, &block, 1}, {target, draw, &block, 1}};
        draw.BlendConstants[0] = 3; draw.Scissor[0] = 8;
        gainPackets[1].Draw = draw;
        CHECK(frame->Record_Native_Draw_Batch(gainPackets, 2));
        CHECK(frame->Read_Back(floating, 16, 8, bytes));
        float twice = 0, thrice = 0;
        if (bytes.size() == 16 * 8 * sizeof(float)) {
            std::memcpy(&twice, bytes.data() + (4 * 16 + 4) * sizeof(float), sizeof(float));
            std::memcpy(&thrice, bytes.data() + (4 * 16 + 12) * sizeof(float), sizeof(float));
        }
        CHECK(twice == 5.0f); CHECK(thrice == 7.5f);
        frame->Set_Target(frame->Back_Buffer_Target());
        CHECK(!frame->Has_Pending_Work());
        // Final-owner discard drops CPU work without submitting it or changing prior GPU pixels.
        frame->Clear_Back_Buffer(true, false, false, 0xFF0000FF, 1, 0);
        CHECK(frame->Has_Pending_Work());
        frame->Discard_Pending_Work_For_Teardown();
        CHECK(!frame->Has_Pending_Work());
        CHECK(frame->Read_Back(frame->Back_Buffer(), 16, 8, bytes));
        CHECK(bytes.size() == 16 * 8 * 4 && bytes[2] == 255 && bytes[0] == 0);
    }
    SdlTarget sun = {};
    CHECK(frame->Sun_Shadow_Target(16, sun, error));
    CHECK(frame->Target_Depth_Format(sun) == SDL_GPU_TEXTUREFORMAT_D32_FLOAT);
    frame->Set_Target(sun);
    frame->Clear_Back_Buffer(false, true, true, 0, 1, 0);
    frame->Clear_Rect(0, 0, 8, 16, false, true, true, 0, 0.25f, 1);
    std::vector<float> depth;
    CHECK(frame->Read_Depth(sun.Depth, 16, 16, depth));
    if (depth.size() == 256) { CHECK(depth[4 * 16 + 4] == 0.25f); CHECK(depth[4 * 16 + 12] == 1.0f); }
    else CHECK(false);
    frame->Set_Target(frame->Back_Buffer_Target());
    {
        SdlSceneCaptureCache captures(*frame);
        SdlSceneCaptureRequest request;
        request.Source = frame->Back_Buffer_Target(); request.Frame = frame->Scene_Frame();
        request.Generation = frame->Scene_Generation(); request.ViewKey = 11; request.Depth = true;
        SdlSceneDepthCapture opaque, transparent, post, repeated;
        frame->Clear_Back_Buffer(false, true, false, 0, 0.25f, 0);
        SDL_GPUTexture *water = frame->Scene_Depth(request.Source);
        CHECK(captures.Capture(request, opaque)); CHECK(opaque.Texture != water);
        CHECK(captures.Capture(request, repeated)); CHECK(repeated.Texture == opaque.Texture);
        request.Phase = SdlSceneCapturePhase::BeforeTransparent;
        frame->Clear_Back_Buffer(false, true, false, 0, 0.5f, 0);
        CHECK(captures.Capture(request, transparent)); CHECK(transparent.Texture != opaque.Texture);
        request.Phase = SdlSceneCapturePhase::BeforePost;
        frame->Clear_Back_Buffer(false, true, false, 0, 0.75f, 0);
        CHECK(captures.Capture(request, post)); CHECK(post.Texture != transparent.Texture && post.Texture != opaque.Texture);
        const auto sample = [&](const SdlSceneDepthCapture &capture) {
            std::vector<uint8_t> bytes;
            CHECK(frame->Read_Back(capture.Texture, 16, 8, bytes));
            float value = -1;
            if (bytes.size() == 16 * 8 * sizeof(float)) std::memcpy(&value, bytes.data(), sizeof(value));
            return value;
        };
        CHECK(std::abs(sample(opaque) - 0.25f) < 0.00001f);
        CHECK(std::abs(sample(transparent) - 0.5f) < 0.00001f);
        CHECK(std::abs(sample(post) - 0.75f) < 0.00001f);
        request.Phase = SdlSceneCapturePhase::OpaqueScene; ++request.ViewKey;
        CHECK(captures.Capture(request, repeated));
        CHECK(repeated.Identity.ViewKey != opaque.Identity.ViewKey);
        CHECK(std::abs(sample(repeated) - 0.75f) < 0.00001f);
        --request.Generation; CHECK(!captures.Capture(request, repeated)); CHECK(!repeated.Texture);
        ++request.Generation; request.ViewKey = 0;
        CHECK(!captures.Capture(request, repeated)); CHECK(!repeated.Texture);
    }
    {
        RenderPost::Settings settings;
        settings.Chain = "bloom"; settings.BloomIntensity = 0.75f; settings.AdditiveGain = 1.6f;
        IDirect3DSurface9 *back = nullptr;
        CHECK(device->GetRenderTarget(0, &back) == D3D_OK);
        device->Begin_Post(settings);
        SdlTarget hdr = {};
        CHECK(device->Post_Target(back, hdr));
        CHECK(frame->Target_Colour_Format(hdr) == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
        CHECK(device->Post_Replaces_Bloom()); CHECK(device->Post_Additive_Gain(hdr) == 1);
        const unsigned previous = device->Begin_Post_View(true, 1, 100);
        CHECK(device->Post_Additive_Gain(hdr) == 1.6f);
        CHECK(device->Post_Additive_Gain(frame->Back_Buffer_Target()) == 1);
        RenderUInt32 blendStates[256] = {};
        blendStates[D3DRS_FILLMODE] = D3DFILL_SOLID; blendStates[D3DRS_CULLMODE] = D3DCULL_NONE;
        blendStates[D3DRS_COLORWRITEENABLE] = 15; blendStates[D3DRS_ALPHABLENDENABLE] = true;
        blendStates[D3DRS_SRCBLEND] = D3DBLEND_BLENDFACTOR; blendStates[D3DRS_DESTBLEND] = D3DBLEND_ONE;
        blendStates[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
        SdlPipelineKey hdrKey;
        CHECK(Sdl_Pipeline_Key(blendStates, D3DPT_TRIANGLELIST, vertex, pixel, D3DFVF_XYZ,
            frame->Target_Colour_Format(hdr), frame->Target_Depth_Format(hdr), hdrKey, error));
        SdlPipelineCache hdrCache(frame->Device());
        SdlRecordedDraw draw = {}; draw.Pipeline = hdrCache.Pipeline(hdrKey); draw.Count = 3; CHECK(draw.Pipeline);
        draw.Viewport[2] = draw.Scissor[2] = hdr.Width; draw.Viewport[3] = draw.Scissor[3] = hdr.Height; draw.Viewport[5] = 1;
        draw.FloatBlendFactor = true;
        draw.BlendConstants[0] = draw.BlendConstants[1] = draw.BlendConstants[2] = device->Post_Additive_Gain(hdr);
        draw.BlendConstants[3] = 1;
        float rgba[4] = {2.5f, 0.25f, 0.125f, 0.5f};
        SdlNativeUniformBlock block = {1, 0, rgba, sizeof(rgba)};
        frame->Set_Target(hdr); frame->Clear_Back_Buffer(true, true, true, 0, 1, 0);
        CHECK(frame->Record_Native_Draw(hdr, draw, &block, 1));
        const auto halfPixels = read_half(*frame, hdr);
        CHECK(halfPixels.size() == hdr.Width * hdr.Height * 4);
        if (halfPixels.size() >= 4) { CHECK(halfPixels[0] == 0x4400); CHECK(halfPixels[3] == 0x3800); } // RGB4.0, alpha0.5
        device->End_Post_View(previous); CHECK(device->Post_Additive_Gain(hdr) == 1);
        D3DVIEWPORT9 partial = {4, 2, 8, 4, 0, 1};
        CHECK(device->SetViewport(&partial) == D3D_OK);
        const unsigned partialPrevious = device->Begin_Post_View(true, 1, 100);
        CHECK(device->Post_Additive_Gain(hdr) == 1.6f);
        device->End_Post_View(partialPrevious);
        D3DVIEWPORT9 full = {0, 0, 16, 8, 0, 1}; CHECK(device->SetViewport(&full) == D3D_OK);
        device->Finish_Post(false); CHECK(!device->Post_Active());
        std::vector<uint8_t> presented;
        CHECK(frame->Read_Back(frame->Back_Buffer(), 16, 8, presented));
        CHECK(presented.size() == 16 * 8 * 4 && presented[2] == 255);
        // A refused post record gets one ungraded original-scene fallback, without repeating grade/bloom.
        settings.Chain = "bloom,fxaa"; settings.Grade = 1; settings.InternalHDR = true;
        device->Begin_Post(settings); CHECK(device->Post_Target(back, hdr));
        const unsigned recoveryPrevious = device->Begin_Post_View(true, 1, 100);
        frame->Set_Target(hdr); frame->Clear_Back_Buffer(true, true, true, 0xFF4080C0, 1, 0);
        frame->Fail_Next_Native_Record_For_Test();
        device->Finish_Post(false); device->End_Post_View(recoveryPrevious);
        CHECK(!device->Post_Active());
        CHECK(frame->Read_Back(frame->Back_Buffer(), 16, 8, presented));
        CHECK(presented.size() == 16 * 8 * 4 && presented[2] == 0x40 && presented[1] == 0x80 && presented[0] == 0xC0);
        // If normal output and its finite fallback both refuse, retain the prior presentation for HUD.
        frame->Set_Target(frame->Back_Buffer_Target()); frame->Clear_Back_Buffer(true, false, false, 0xFFFFFF00, 1, 0);
        CHECK(frame->Flush());
        device->Begin_Post(settings); CHECK(device->Post_Target(back, hdr));
        const unsigned refusedPrevious = device->Begin_Post_View(true, 1, 100);
        frame->Set_Target(hdr); frame->Clear_Back_Buffer(true, true, true, 0xFF0000FF, 1, 0);
        frame->Fail_Next_Native_Record_For_Test(2);
        device->Finish_Post(false); device->End_Post_View(refusedPrevious);
        CHECK(!device->Post_Active());
        CHECK(frame->Read_Back(frame->Back_Buffer(), 16, 8, presented));
        CHECK(presented.size() == 16 * 8 * 4 && presented[2] == 255 && presented[1] == 255 && presented[0] == 0);
        settings.Chain = "off"; settings.WorldAO = true; settings.InternalHDR = true; settings.Grade = 1;
        device->Begin_Post(settings);
        SdlTarget neutral = {}; CHECK(device->Post_Target(back, neutral));
        CHECK(frame->Target_Colour_Format(neutral) == frame->Target_Format());
        CHECK(!device->Post_Replaces_Bloom()); CHECK(device->Post_Additive_Gain(neutral) == 1);
        frame->Set_Target(neutral); frame->Clear_Back_Buffer(true, false, false, 0xFF4080C0, 1, 0);
        device->Finish_Post(true);
        CHECK(frame->Read_Back(frame->Back_Buffer(), 16, 8, presented));
        CHECK(presented.size() == 16 * 8 * 4 && presented[2] == 0x40 && presented[1] == 0x80 && presented[0] == 0xC0);
        // A copied, same-frame environment owns an ungraded eight-bit scene even
        // with the user chain off. Haze runs before particles and preserves alpha.
        RenderPost::Environment environment;
        environment.MapIdentity = 1; environment.MapGeneration = 1;
        environment.Haze.Strength = 1; environment.Haze.Top = environment.Haze.Thickness = 1;
        environment.Haze.Reach = 1000;
        environment.Haze.Colour[0] = environment.Haze.Colour[1] = environment.Haze.Colour[2] = 1;
        IDirect3DTexture9 *shroud = nullptr;
        CHECK(device->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &shroud, nullptr) == D3D_OK);
        if (shroud) {
            D3DLOCKED_RECT lock = {}; CHECK(shroud->LockRect(0, &lock, nullptr, 0) == D3D_OK);
            std::memset(lock.pBits, 255, 4); CHECK(shroud->UnlockRect(0) == D3D_OK);
            environment.Shroud = shroud;
        }
        device->Set_Post_Environment(environment);
        if (shroud) shroud->Release(); // the copied packet is now the only COM owner
        settings.WorldAO = false; settings.InternalHDR = false; settings.Chain = "off";
        device->Begin_Post(settings); CHECK(device->Post_Active());
        SdlTarget hazed; CHECK(device->Post_Target(back, hazed));
        CHECK(frame->Target_Colour_Format(hazed) == frame->Target_Format());
        CHECK(device->Post_Additive_Gain(hazed) == 1);
        const unsigned hazeScope = device->Begin_Post_View(true, 1, 100);
        frame->Set_Target(hazed); frame->Clear_Back_Buffer(true, true, false, 0x40000000, 0.25f, 0);
        device->Post_Sky(); // haze-only preparation emits no sky
        device->Post_Before_Transparent();
        CHECK(frame->Read_Back(hazed.Colour, 16, 8, presented));
        CHECK(presented.size() == 16 * 8 * 4 && presented[0] > 16 && presented[3] == 0x40);
        const auto onceHaze = presented;
        device->Post_Before_Transparent();
        CHECK(frame->Read_Back(hazed.Colour, 16, 8, presented)); CHECK(presented == onceHaze);
        device->End_Post_View(hazeScope);
        const unsigned hazeReentry = device->Begin_Post_View(true, 1, 100);
        frame->Set_Target(hazed); frame->Clear_Back_Buffer(true, true, false, 0x40000000, 0.25f, 0);
        device->Post_Before_Transparent(); // original sky hook is absent for haze-only settings
        CHECK(frame->Read_Back(hazed.Colour, 16, 8, presented));
        CHECK(presented == onceHaze); // new main key gets the same single application
        device->Finish_Post(false); device->End_Post_View(hazeReentry);
        ++environment.MapGeneration;
        device->Set_Post_Environment(environment);
        device->Begin_Post(settings); CHECK(device->Post_Target(back, hazed));
        const unsigned resetHazeScope = device->Begin_Post_View(true, 1, 100);
        frame->Set_Target(hazed); frame->Clear_Back_Buffer(true, true, false, 0x40000000, 0.25f, 0);
        device->Post_Sky(); device->Post_Before_Transparent(); // real queued shroud reader, intentionally not flushed
        // An authoritative same-frame map reset invalidates prepared borrowed
        // haze before its old references retire; no old fade may run afterward.
        frame->Set_Target(hazed); frame->Clear_Back_Buffer(true, true, false, 0x40402010, 0.25f, 0);
        device->Set_Post_Environment(RenderPost::Environment{});
        device->Post_Before_Transparent();
        CHECK(frame->Read_Back(hazed.Colour, 16, 8, presented));
        CHECK(presented.size() == 16 * 8 * 4 && presented[0] == 0x10 && presented[1] == 0x20 && presented[2] == 0x40 && presented[3] == 0x40);
        device->Finish_Post(false); device->End_Post_View(resetHazeScope);
        // Freshness is consumed by one display pass; stale settings cannot carry
        // a map's haze into the next scene, including an identical pointer epoch.
        device->Begin_Post(settings); CHECK(!device->Post_Active());
        // Real producer dispatch: user-off ownership cannot enable distortion;
        // successful normal COPY may bend it, and failed normal Finish may not.
        std::vector<uint8_t> pattern(16 * 8 * 4);
        for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 16; ++x) {
            const unsigned at = (y * 16 + x) * 4;
            pattern[at] = x * 15; pattern[at+1] = y * 30; pattern[at+2] = x * 12; pattern[at+3] = 0x40;
        }
        const RenderPost::Warp warp = {0.5f, 0.5f, 0.4f, 0.2f, 0.2f, 0.1f, 0.05f, 1};
        const float heat[16] = {0, 0, 0.8f, 0.8f, 0.2f, 0.05f, 1, 0, 1, 100, 0, 20, 0, 0, 0, 0};
        auto distorted = [&](const char *chain, bool neutralOwner, bool refuseNormal) {
            RenderPost::Settings effectSettings;
            effectSettings.Chain = chain; effectSettings.WorldAO = neutralOwner;
            device->Begin_Post(effectSettings);
            SdlTarget scene; CHECK(device->Post_Target(back, scene));
            const unsigned scope = device->Begin_Post_View(true, 1, 100);
            frame->Set_Target(scene); frame->Clear_Back_Buffer(false, true, false, 0, 0.75f, 0);
            CHECK(frame->Flush());
            uint32_t offset = 0; auto *bytes = frame->Upload_Space(uint32_t(pattern.size()), offset);
            CHECK(bytes); if (bytes) std::memcpy(bytes, pattern.data(), pattern.size());
            frame->Queue_Texture_Upload(scene.Colour, 0, 16, 8, offset); CHECK(frame->Flush());
            std::vector<float> depthBefore, depthAfter; CHECK(frame->Read_Depth(scene.Depth, 16, 8, depthBefore));
            if (!refuseNormal) device->Set_Post_Heat(heat, 1);
            device->End_Post_View(scope); // blast producer runs at the existing pre-HUD hook
            device->Set_Post_Warps(&warp, 1);
            if (refuseNormal) frame->Fail_Next_Native_Record_For_Test();
            device->Finish_Post(false);
            std::vector<uint8_t> result; CHECK(frame->Read_Back(frame->Back_Buffer(), 16, 8, result));
            CHECK(frame->Read_Depth(scene.Depth, 16, 8, depthAfter)); CHECK(depthBefore == depthAfter);
            for (size_t at = 3; at < result.size(); at += 4) CHECK(result[at] == 0x40);
            return result;
        };
        CHECK(distorted("off", true, false) == pattern);
        const auto bent = distorted("copy", false, false); CHECK(bent.size() == pattern.size() && bent != pattern);
        CHECK(distorted("copy", false, true) == pattern); // finite fallback never earns normal-Finish proof
        settings.Classic = true; settings.Chain = "bloom";
        device->Begin_Post(settings); CHECK(!device->Post_Active());
        // Main ownership is independent of post activation, including Classic's direct scene.
        SdlMainViewSnapshot mainView;
        CHECK(!PosixRenderer_Main_View(device, mainView)); CHECK(!mainView.Frame);
        const unsigned directScope = device->Begin_Post_View(true, 1, 100);
        CHECK(PosixRenderer_Main_View(device, mainView)); CHECK(mainView.Target.Colour == frame->Back_Buffer());
        CHECK(mainView.ViewKey && !mainView.SampledDepth && mainView.DeviceGeneration);
        const auto mainKey = mainView.ViewKey, deviceEpoch = mainView.DeviceGeneration;
        CHECK(PosixGpuParticles_Context(device, particles));
        CHECK(particles.Pass == SdlGpuParticlesPass::MainScene && particles.MainViewKey == mainKey && !particles.SoftAllowed && particles.AdditiveGain == 1);
        SdlMainViewSnapshot terrain;
        CHECK(PosixRenderer_Capture_Main_View(device, SdlSceneCapturePhase::TerrainOverlay, terrain));
        CHECK(terrain.SampledDepth && terrain.Capture.Phase == SdlSceneCapturePhase::TerrainOverlay);
        CHECK(terrain.DepthFormat == SDL_GPU_TEXTUREFORMAT_R32_FLOAT && terrain.Capture.ViewKey == mainKey);
        // Temporary camera-space setters and explicit nested mirrors cannot replace the primary camera.
        D3DMATRIX temporary = {}; temporary._11 = temporary._22 = temporary._33 = temporary._44 = 1; temporary._41 = 3;
        CHECK(device->SetTransform(D3DTS_VIEW, &temporary) == D3D_OK);
        SdlMainViewSnapshot unchanged;
        CHECK(PosixRenderer_Main_View(device, unchanged));
        CHECK(unchanged.ViewKey == mainKey && !std::memcmp(unchanged.WorldFromClip, mainView.WorldFromClip, sizeof(mainView.WorldFromClip)));
        const unsigned mirrorScope = device->Begin_Post_View(true, 1, 100, true);
        CHECK(!PosixRenderer_Main_View(device, unchanged)); CHECK(!unchanged.Frame);
        CHECK(PosixGpuParticles_Context(device, particles));
        CHECK(particles.Pass == SdlGpuParticlesPass::Reflection && !particles.SoftAllowed && particles.AdditiveGain == 1);
        device->End_Post_View(mirrorScope);
        CHECK(PosixRenderer_Main_View(device, unchanged)); CHECK(unchanged.ViewKey == mainKey);
        device->End_Post_View(directScope); CHECK(!PosixRenderer_Main_View(device, unchanged));
        const unsigned reentry = device->Begin_Post_View(true, 1, 100);
        CHECK(PosixRenderer_Main_View(device, unchanged)); CHECK(unchanged.ViewKey != mainKey);
        CHECK(unchanged.DeviceGeneration == deviceEpoch && unchanged.FrameSerial == mainView.FrameSerial);
        device->End_Post_View(reentry); temporary._41 = 0; CHECK(device->SetTransform(D3DTS_VIEW, &temporary) == D3D_OK);
        settings.Classic = false; settings.WorldAO = false; settings.Chain = "off";
        device->Begin_Post(settings); CHECK(!device->Post_Active());
        const unsigned softScope = device->Begin_Post_View(true, 1, 100);
        frame->Set_Target(frame->Back_Buffer_Target()); frame->Clear_Back_Buffer(true, true, false, 0, 0.5f, 0);
        device->Post_Before_Transparent();
        CHECK(PosixGpuParticles_Context(device, particles)); CHECK(!particles.SoftAllowed); // no active particle owner asks for depth
        SdlMainViewSnapshot particleDepth;
        CHECK(PosixRenderer_Capture_Main_View(device, SdlSceneCapturePhase::BeforeTransparent, particleDepth));
        CHECK(PosixGpuParticles_Context(device, particles));
        CHECK(particles.Pass == SdlGpuParticlesPass::MainScene && particles.SoftAllowed && particles.SoftDepth);
        CHECK(particles.SoftCapture.Phase == SdlSceneCapturePhase::BeforeTransparent && particles.SoftCapture.ViewKey == particles.MainViewKey);
        CHECK(particles.SoftDepth != particles.Target.Depth && particles.SoftDepth != particles.Target.Colour);
        device->End_Post_View(softScope);
        CHECK(PosixGpuParticles_Context(device, particles)); CHECK(!particles.SoftAllowed && particles.Pass == SdlGpuParticlesPass::Other);
        const unsigned staleSoftScope = device->Begin_Post_View(true, 1, 100);
        CHECK(PosixGpuParticles_Context(device, particles)); CHECK(!particles.SoftAllowed);
        device->End_Post_View(staleSoftScope);
        settings.WorldAO = true;
        device->Begin_Post(settings); CHECK(device->Post_Active());
        SdlTarget beforeReset = {}; CHECK(device->Post_Target(back, beforeReset));
        D3DPRESENT_PARAMETERS replacement = device->Get_Present_Parameters();
        replacement.BackBufferWidth = 32; replacement.BackBufferHeight = 8;
        frame->Fail_Next_Resize_For_Test();
        CHECK(device->Reset(&replacement) == D3DERR_OUTOFVIDEOMEMORY);
        SdlTarget retained = {}; CHECK(device->Post_Target(back, retained));
        CHECK(device->Post_Active() && retained.Colour == beforeReset.Colour && retained.Depth == beforeReset.Depth);
        CHECK(frame->Width() == 16);
        CHECK(device->Reset(&replacement) == D3D_OK);
        CHECK(!device->Post_Active()); CHECK(frame->Width() == 32);
        device->Begin_Post(settings); CHECK(device->Post_Active());
        if (back) back->Release(); back = nullptr;
        CHECK(device->GetRenderTarget(0, &back) == D3D_OK);
        SdlTarget afterReset = {}; CHECK(device->Post_Target(back, afterReset)); CHECK(afterReset.Width == 32);
        const unsigned resizedScope = device->Begin_Post_View(true, 1, 100);
        CHECK(PosixRenderer_Main_View(device, unchanged));
        CHECK(unchanged.DeviceGeneration == deviceEpoch && unchanged.Capture.Generation != terrain.Capture.Generation);
        device->End_Post_View(resizedScope);
        frame->Set_Target(afterReset); frame->Clear_Back_Buffer(true, true, true, 0xFF204060, 1, 0);
        device->Finish_Post(true); CHECK(!device->Post_Active());
        if (back) back->Release();
    }
    const auto generation = frame->Scene_Generation(), serial = frame->Scene_Frame();
    CHECK(frame->Present(nullptr)); CHECK(frame->Scene_Frame() == serial + 1);
    CHECK(frame->Resize(32, 8)); CHECK(frame->Scene_Generation() == generation + 1);
    SDL_ReleaseGPUTexture(frame->Device(), floating);
    SDL_ReleaseGPUShader(frame->Device(), vertex); SDL_ReleaseGPUShader(frame->Device(), pixel);
    device->Release(); api->Release();
    SDL_Quit();
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    check_cpu();
    if (failures) return 1;
    if (argc > 1 && !std::strcmp(argv[1], "--gpu")) return check_gpu();
    std::puts("renderer foundation: headless capabilities and capture identities hold");
    return 0;
}
