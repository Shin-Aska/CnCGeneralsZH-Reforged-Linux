// Shared GPU material for preview and production hazard clouds.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#ifndef SDL_HAZARD_CLOUD_SHADER_H
#define SDL_HAZARD_CLOUD_SHADER_H

#include <string>

// Matches the fragment shader's b0 exactly; all matrices retain the renderer's
// row-major convention. The preview and game use the same constant contract.
struct SdlHazardCloudUniforms {
    float WorldFromClip[16];
    float CameraPosition[4];
    float Viewport[4];                 // origin including half pixel, width, height
    float Scene[4];                    // inverse target size, depth available, seconds
    float Field0[4];                   // world centre XYZ, horizontal radius
    float Field1[4];
    float Shape0[4];                   // half height, density gain, palette, formation
    float Shape1[4];
    float Footprints[4];               // field 0/1 fringe beyond hazard radius, reserved ZW
    float Source0[4];                  // release, outward travel, dissipation, source age
    float Source1[4];
    float Settings[4];                 // active fields, nuclear heat pixels, enabled, maximum steps
    float Interaction[4];              // world origin XY, inverse world width/height
    float FlowSettings[4];             // enabled, maximum displacement, reserved, reserved
    float Cache0[4];                   // voxel counts XYZ, atlas slice columns
    float Cache1[4];
    float CachePass[4];                // field index for cache construction, reserved YZW
    float PaletteMain[4][4];           // per-palette main RGB, overall brightness
    float PaletteShadow[4][4];         // per-palette shadow RGB, optical thickness multiplier
    float Terrain0[4];                 // minimum ground Z, maximum ground Z, enabled, reserved
    float Terrain1[4];
};
static_assert(sizeof(SdlHazardCloudUniforms) == 480, "hazard volume uniform layout");

std::string SdlHazardCloudVertexShader();
std::string SdlHazardCloudPixelShader(bool composite = false);
std::string SdlHazardCloudCacheShader(bool lighting);
std::string SdlHazardCloudHeatShader();

#endif
