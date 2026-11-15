// Exercise WW3D's real sorting pool, including merging adjacent nodes with equal
// material state. Direct device draws cannot catch coverage lost while queued.
#include "PreRTS.h"
#include "Common/GameMemory.h"
#include "Platform/SunShadow.h"
#include "Platform/RenderPost.h"
#include "Platform/RenderEffects.h"
#include "SdlGpuFrame.h"
#include "SdlSceneContext.h"
#include <cstring>
#include "PosixDevice9.h"
#include "dx8wrapper.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "dx8fvf.h"
#include "sortingrenderer.h"
#include "shader.h"
#include "vertmaterial.h"
#include "ffshader.h"
#include "ww3d.h"
#include "wwmath.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";
static char appPrefix[] = "";
char *gAppPrefix = appPrefix;
RenderWindow ApplicationHWnd = NULL;
Bool ApplicationIsBorderless = FALSE;

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { ++failures; \
    std::printf("FAIL line %d: %s\n", __LINE__, #condition); } } while (0)

// Use the actual wrapper path: direct SetTransform fixtures cannot catch a
// Camera::Apply VIEW still deferred while native PROJECTION is already current.
static void check_deferred_main_camera(PosixDevice9 *device)
{
    DX8Wrapper::Set_Transform(D3DTS_VIEW, Matrix3D(1));
    DX8Wrapper::Set_Projection_Transform_With_Z_Bias(Matrix4x4(true), 0, 1);
    DX8Wrapper::Apply_Render_State_Changes();
    RenderPost::Settings settings; RenderPost::BeginMain(settings);
    Matrix3D camera(1); camera.Set_Translation(Vector3(12, -7, 3));
    DX8Wrapper::Set_Transform(D3DTS_VIEW, camera);
    D3DMATRIX before; CHECK(device->GetTransform(D3DTS_VIEW, &before) == D3D_OK);
    CHECK(before._41 == 0 && before._42 == 0 && before._43 == 0); // deliberately deferred
    const auto first = RenderPost::BeginView(true, 1, 100);
    SdlMainViewSnapshot view; CHECK(PosixRenderer_Main_View(view));
    CHECK(view.ViewProjection[12] == 12 && view.ViewProjection[13] == -7 && view.ViewProjection[14] == 3);
    CHECK(view.WorldFromClip[12] == -12 && view.WorldFromClip[13] == 7 && view.WorldFromClip[14] == -3);
    const auto key = view.ViewKey; RenderPost::EndView(first);
    camera.Set_Translation(Vector3(-4, 9, 2)); DX8Wrapper::Set_Transform(D3DTS_VIEW, camera);
    const auto second = RenderPost::BeginView(true, 1, 100);
    CHECK(PosixRenderer_Main_View(view)); CHECK(view.ViewKey != key);
    CHECK(view.ViewProjection[12] == -4 && view.ViewProjection[13] == 9 && view.ViewProjection[14] == 2);
    CHECK(view.WorldFromClip[12] == 4 && view.WorldFromClip[13] == -9 && view.WorldFromClip[14] == -2);
    RenderPost::EndView(second);
    DX8Wrapper::Set_Transform(D3DTS_VIEW, Matrix3D(1)); DX8Wrapper::Apply_Render_State_Changes();
}

static void check_sorted_model_packets(PosixDevice9 *device)
{
    RenderPost::Settings settings; RenderPost::BeginMain(settings);
    DX8Wrapper::Set_Transform(D3DTS_WORLD, Matrix3D(1)); DX8Wrapper::Set_Transform(D3DTS_VIEW, Matrix3D(1));
    DX8Wrapper::Set_Projection_Transform_With_Z_Bias(Matrix4x4(true), 0, 1);
    const auto scope = RenderPost::BeginView(true, 1, 100);
    auto *frame = device->Get_Gpu(); frame->Set_Target(frame->Back_Buffer_Target());
    frame->Clear_Back_Buffer(true, true, false, 0xFF000000, 1, 0);
    ShaderClass shader; shader.Set_Texturing(ShaderClass::TEXTURING_DISABLE);
    shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE); shader.Set_Src_Blend_Func(ShaderClass::SRCBLEND_SRC_ALPHA);
    shader.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA); DX8Wrapper::Set_Shader(shader);
    auto *material = VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
    DX8Wrapper::Set_Material(material); material->Release_Ref();
    DX8Wrapper::Set_DX8_Render_State(D3DRS_LIGHTING, FALSE); DX8Wrapper::Set_DX8_Render_State(D3DRS_ZENABLE, TRUE);
    DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE, D3DFILL_SOLID); DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILENABLE, FALSE);
    auto *vb = new SortingVertexBufferClass(24); auto *ib = new SortingIndexBufferClass(24);
    {
        VertexBufferClass::WriteLockClass lock(vb); auto *vertices = static_cast<VertexFormatXYZNDUV2 *>(lock.Get_Vertex_Array());
        const unsigned corner[] = {0, 1, 2, 2, 1, 3};
        for (unsigned strip = 0; strip < 4; ++strip) for (unsigned v = 0; v < 6; ++v) {
            const unsigned c = corner[v]; auto &vertex = vertices[strip*6+v]; vertex = {};
            vertex.x = -1 + strip*.5f + ((c&1) ? .5f : 0); vertex.y = (c&2) ? 1 : -1;
            vertex.z = .5f; vertex.nz = 1; vertex.diffuse = 0xFFCC3300;
        }
    }
    { IndexBufferClass::WriteLockClass lock(ib); for (unsigned i = 0; i < 24; ++i) lock.Get_Index_Array()[i] = i; }
    DX8Wrapper::Set_Vertex_Buffer(vb); DX8Wrapper::Set_Index_Buffer(ib, 0);
    const unsigned effects[] = {DRAW_EFFECT_OUTAGE, DRAW_EFFECT_EMP, DRAW_EFFECT_REVEALED, DRAW_EFFECT_NONE};
    const float none[4] = {};
    for (unsigned strip = 0; strip < 4; ++strip) {
        float parameters[4] = {1, 1, .123f, .25f}; RenderEffects::SetDrawEffect(effects[strip], parameters);
        SortingRendererClass::Insert_Triangles(SphereClass(Vector3(0, 0, .5f), 10), strip*6, 2, strip*6, 6);
        RenderEffects::SetDrawEffect(0, none); // queue must own both effect and parameters
        parameters[0] = 999; parameters[1] = 0; parameters[2] = 50; parameters[3] = 0;
    }
    SortingRendererClass::Flush(); std::vector<uint8_t> pixels;
    CHECK(frame->Read_Back(frame->Back_Buffer(), 64, 64, pixels));
    if (pixels.size() == 64*64*4) {
        const auto at = [&](unsigned strip, unsigned channel) { return pixels[(32*64 + strip*16 + 8)*4 + channel]; };
        CHECK(at(3,2) == 204 && at(3,1) == 51 && at(3,0) == 0);
        CHECK(at(0,2) < 204); CHECK(at(1,0) > 0);
        CHECK(at(2,2) != at(3,2) || at(2,1) != at(3,1) || at(2,0) != at(3,0));
    } else CHECK(false);
    float requested[4]; CHECK(RenderEffects::RequestedDrawEffect(requested) == DRAW_EFFECT_NONE);
    vb->Release_Ref(); ib->Release_Ref(); RenderPost::EndView(scope);
}

static void check_sorted_coverage(PosixDevice9 *device, bool reverse)
{
    CHECK(SunShadow::Begin(device, 64));
    DX8Wrapper::Set_Transform(D3DTS_WORLD, Matrix3D(1));
    DX8Wrapper::Set_Transform(D3DTS_VIEW, Matrix3D(1));
    DX8Wrapper::Set_Projection_Transform_With_Z_Bias(Matrix4x4(true), 0, 1);
    ShaderClass shader;
    shader.Set_Texturing(ShaderClass::TEXTURING_DISABLE);
    shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
    shader.Set_Src_Blend_Func(ShaderClass::SRCBLEND_SRC_ALPHA);
    shader.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA);
    DX8Wrapper::Set_Shader(shader);
    VertexMaterialClass *material = VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
    DX8Wrapper::Set_Material(material);
    material->Release_Ref();
    DX8Wrapper::Set_DX8_Render_State(D3DRS_LIGHTING, FALSE);
    DX8Wrapper::Set_DX8_Render_State(D3DRS_ZENABLE, TRUE);
    DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE, D3DFILL_SOLID);
    DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILENABLE, FALSE);

    SortingVertexBufferClass *vb = new SortingVertexBufferClass(24);
    SortingIndexBufferClass *ib = new SortingIndexBufferClass(24);
    {
        VertexBufferClass::WriteLockClass lock(vb);
        auto *vertices = static_cast<VertexFormatXYZNDUV2 *>(lock.Get_Vertex_Array());
        const unsigned corners[] = {0, 1, 2, 2, 1, 3};
        for (unsigned strip = 0; strip < 4; ++strip) {
            const float left = -1.0f + strip * .5f, right = left + .5f;
            const float x[] = {left, right, left, right};
            const float y[] = {-1, -1, 1, 1};
            for (unsigned v = 0; v < 6; ++v) {
                vertices[strip * 6 + v] = {};
                auto &vertex = vertices[strip * 6 + v];
                vertex.x = x[corners[v]]; vertex.y = y[corners[v]];
                vertex.z = .5f; vertex.nz = 1; vertex.diffuse = 0xffffffff;
            }
        }
    }
    {
        IndexBufferClass::WriteLockClass lock(ib);
        for (unsigned i = 0; i < 24; ++i) lock.Get_Index_Array()[i] = i;
    }
    DX8Wrapper::Set_Vertex_Buffer(vb);
    DX8Wrapper::Set_Index_Buffer(ib, 0);
    // Equal spheres force the same sorting pool; all other draw state is identical.
    // These nodes must not merge across different cloak opacities or into opaque.
    const float opacity[] = {0, .5f, 1, 1};
    const float cleared[4] = {};
    for (unsigned entry = 0; entry < 4; ++entry) {
        const unsigned strip = reverse ? 3 - entry : entry;
        const float parameters[] = {0, 0, 0, opacity[strip]};
        SunShadow::SetCasterEffect(device, strip == 3 ? DRAW_EFFECT_NONE : DRAW_EFFECT_CLOAK_SHADOW, parameters);
        SortingRendererClass::Insert_Triangles(SphereClass(Vector3(0, 0, .5f), 10), strip * 6, 2, strip * 6, 6);
        SunShadow::SetCasterEffect(device, DRAW_EFFECT_NONE, cleared);
    }
    SortingRendererClass::Flush();
    CHECK(SunShadow::GetCasterEffect(device).Effect == DRAW_EFFECT_NONE);
    vb->Release_Ref(); ib->Release_Ref();
    DX8Wrapper::Flush_Deferred_D3D9_State();
    CHECK(SunShadow::End(device));
    std::vector<float> depth;
    CHECK(device->Read_Sun_Shadow(depth));
    if (depth.size() == 64 * 64) {
        // Interior 8x32 rectangles contain complete Bayer tiles, away from edges.
        // Cloak contributes half the opacity: 0%, 25%, 50%; ordinary mesh is 100%.
        const unsigned expected[] = {0, 64, 128, 256};
        for (unsigned strip = 0; strip < 4; ++strip) {
            unsigned occupied = 0;
            for (unsigned y = 16; y < 48; ++y)
                for (unsigned x = strip * 16 + 4; x < strip * 16 + 12; ++x)
                    occupied += depth[y * 64 + x] < 1;
            std::printf("sorted coverage%s, strip %u: %u / 256\n", reverse ? " reversed" : "", strip, occupied);
            CHECK(occupied == expected[strip]);
        }
    } else CHECK(false);
    DX8Wrapper::Invalidate_Cached_Render_States();
}

int main()
{
    setenv("ZH_SUN_SHADOW_MODE", "mapped", 1);
    if (!SDL_Init(SDL_INIT_VIDEO)) return 77;
    SDL_SetHint("ZH_OFFSCREEN_FRAMES", "1");
    SDL_SetHint("ZH_OFFSCREEN_HZ", "0");
    initMemoryManager();
    WWMath::Init();
    const bool initialized = DX8Wrapper::Init(NULL);
    if (!initialized || WW3D::Set_Render_Device(0, 64, 64, 32, 1, false, false, false) != WW3D_ERROR_OK) {
        std::printf("SKIP: could not create offscreen renderer: %s\n", SDL_GetError());
        if (initialized) DX8Wrapper::Shutdown();
        WWMath::Shutdown();
        SDL_Quit();
        return 77;
    }
    auto *device = static_cast<PosixDevice9 *>(DX8Wrapper::_Get_D3D_Device());
    if (!device || !device->Get_Gpu()) {
        DX8Wrapper::Shutdown();
        WWMath::Shutdown();
        SDL_Quit();
        return 77;
    }
    check_deferred_main_camera(device);
    WW3D::Enable_Sorting(true);
    check_sorted_coverage(device, false);
    check_sorted_coverage(device, true); // Reuses recycled nodes and reverses the final effect.
    check_sorted_model_packets(device);
    SortingRendererClass::Deinit();
    DX8Wrapper::Shutdown();
    WWMath::Shutdown();
    SDL_Quit();
    return failures ? 1 : 0;
}
