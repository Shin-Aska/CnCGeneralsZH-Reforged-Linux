/* Private Zero Hour Reforged SDL Vulkan extension. Not an upstream SDL API. */
#ifndef ZHR_SDL_GPU_VULKAN_RAYQUERY_H
#define ZHR_SDL_GPU_VULKAN_RAYQUERY_H
#include <SDL3/SDL.h>
#ifdef __cplusplus
extern "C" {
#endif
#define ZHR_SDL_RAY_QUERY_PROPERTY "zhr.vulkan.ray_query"
/* Best effort on the normally ranked Vulkan adapter; does not select a backend/GPU. */
#define ZHR_SDL_RAY_QUERY_OPTIONAL_PROPERTY "zhr.vulkan.ray_query_optional"
typedef struct ZHR_SDLRayScene ZHR_SDLRayScene;
typedef struct ZHR_SDLRayParams {
    float inverseViewProjection[16];
    float lightDirection[4];
    float originBias, maxDistance;
    Uint32 width, height;
    float padding[8];
} ZHR_SDLRayParams;
/* Vulkan only; diagnostic includes the physical device and enabled feature state. */
bool ZHR_SDLRayQuerySupported(SDL_GPUDevice *, char *diagnostic, size_t capacity);
/* CPU arrays copied. One opaque, two-sided indexed mesh; positions already in world space. */
ZHR_SDLRayScene *ZHR_SDLRayCreateScene(SDL_GPUDevice *, const float *xyz, Uint32 vertexCount,
    const Uint32 *indices, Uint32 indexCount, const void *spirv, size_t spirvBytes);
/* Outside SDL passes. Builds on first record. Submit every successful record, then wait its
   SDL fence before recording this scene again (one descriptor set and scratch allocation).
   Do not cancel the first build command buffer. Depth/mask must be distinct 2D R32_FLOAT,
   single sample, depth SAMPLER and mask COMPUTE_STORAGE_WRITE; dimensions must match.
   SDL retains textures through command completion. Scene must outlive recorded commands. */
bool ZHR_SDLRayRecord(ZHR_SDLRayScene *, SDL_GPUCommandBuffer *, SDL_GPUTexture *depth,
    SDL_GPUTexture *mask, Uint32 width, Uint32 height, const ZHR_SDLRayParams *);
/* Sun-only bounded dispatch. Same scene/fence/lifetime policy as RayRecord, but
   depth is an unused same-device 1x1 R32 sampler. Rectangles are packed x,y,w,h
   inside the output; shader uses padding[1..4] for rectangle origin/extent and
   padding[0]=1 for orthographic sun depth. One submission records all rectangles.
   Output outside the rectangles is retained/undefined and must not be sampled. */
bool ZHR_SDLRayRecordSunDepth(ZHR_SDLRayScene *, SDL_GPUCommandBuffer *, SDL_GPUTexture *depth,
    SDL_GPUTexture *mask, Uint32 width, Uint32 height, const ZHR_SDLRayParams *,
    const Uint32 *rectangles, Uint32 rectangleCount);
/* One-bounce water closest-hit query. Four vec4s plus maxDistance/padding exactly
   match the compute shader's 80-byte push block. Ray generation and plane start
   are defined by the supplied shader; no native Vulkan handles cross this API. */
typedef struct ZHR_SDLRayWaterParams {
    float OriginWater[4];
    float RayTopLeft[4];
    float RayDx[4];
    float RayDy[4];
    float maxDistance;
    float padding[3];
} ZHR_SDLRayWaterParams;
/* Triangle order is preserved. At most 2^24 triangles so RGBA32F hit IDs are exact.
   Shader set 0: binding 0 rgba32f storage image, binding 1 acceleration structure;
   local size 8x8. Output RGBA = committed distance, primitive ID, barycentric U,V.
   Miss primitive ID is -1. Use closest-hit ray queries with opaque two-sided input. */
ZHR_SDLRayScene *ZHR_SDLRayCreateWaterScene(SDL_GPUDevice *, const float *xyz, Uint32 vertexCount,
    const Uint32 *indices, Uint32 indexCount, const void *spirv, size_t spirvBytes);
/* Same acquire/outside-pass, submit, SDL fence wait and scene lifetime contract as
   ZHR_SDLRayRecord. Output must be matching single-sample 2D RGBA32_FLOAT with
   COMPUTE_STORAGE_WRITE usage, owned by this SDL device. No input textures. */
bool ZHR_SDLRayRecordWater(ZHR_SDLRayScene *, SDL_GPUCommandBuffer *, SDL_GPUTexture *hits,
    Uint32 width, Uint32 height, const ZHR_SDLRayWaterParams *);
/* Full Modern water transport: primary closest hit, direct sun occlusion and one
   cosine-hemisphere continuation. SunDirection.w is 1 iff direct sun has radiance.
   The supplied shader owns sampling/ray semantics; this exact block is 112 bytes. */
typedef struct ZHR_SDLRayWaterPathParams {
    float OriginWater[4];
    float RayTopLeft[4];
    float RayDx[4];
    float RayDy[4];
    float SunDirection[4];
    float maxDistance, shadowDistance, secondaryDistance, epsilon;
    Uint32 sampleIndex, padding[3];
} ZHR_SDLRayWaterPathParams;
ZHR_SDLRayScene *ZHR_SDLRayCreateWaterPathScene(SDL_GPUDevice *, const float *xyz, Uint32 vertexCount,
    const Uint32 *indices, Uint32 indexCount, const void *spirv, size_t spirvBytes);
/* Set 0 bindings: 0 rgba32f primary {T,id,U,V}, 1 TLAS, 2 combined nearest sampler
   of packed RGBA32F triangle edges (four texels per primitive, in original order),
   3 rgba32f transport {secondary ID/-1, continuation z, secondary winding sign,
   sun visibility}. Primary miss ID is -1. Local size 8x8, output imageSize bounds.
   All three textures are distinct, same-device, single-sample 2D RGBA32_FLOAT;
   triangles needs SAMPLER usage, outputs matching width/height and storage-write.
   Same SDL command submission/fence/lifetime policy as the one-bounce API. */
bool ZHR_SDLRayRecordWaterPath(ZHR_SDLRayScene *, SDL_GPUCommandBuffer *, SDL_GPUTexture *triangles,
    SDL_GPUTexture *primaryHits, SDL_GPUTexture *transportHits, Uint32 width, Uint32 height,
    const ZHR_SDLRayWaterPathParams *);
/* Update copied geometry without a global idle or compute-pipeline recreation.
   Caller must have submitted and waited the scene's previous SDL query fence,
   and must hold no unsubmitted command buffer referencing this scene. Exact XYZ
   and index bytes unchanged: changed=false, no AS build. Same counts reuse all
   geometry/AS buffers. Sun and one-bounce water also reuse them within reserved capacity.
   Changed geometry schedules a full BUILD on the next Record variant. Pipeline,
   descriptor layout/set, and sampler survive. On false use ordinary teardown and
   fallback; the scene remains safely destructible. changed may be NULL. */
bool ZHR_SDLRayUpdateScene(ZHR_SDLRayScene *, const float *xyz, Uint32 vertexCount,
    const Uint32 *indices, Uint32 indexCount, bool *changed);
/* Sun casters use consecutive world-space triangles. Each exclusive triangle
   end delimits one building. Unchanged buildings retain their BLAS as the
   active list changes. Update uses the same waited-fence contract as above.
   On false, retire the scene and retain the regular mapped caster pass. */
ZHR_SDLRayScene *ZHR_SDLRayCreateSunBuildingScene(SDL_GPUDevice *, const float *xyz, Uint32 vertexCount,
    const Uint32 *buildingTriangleEnds, Uint32 buildingCount, const void *spirv, size_t spirvBytes);
bool ZHR_SDLRayUpdateSunBuildingScene(ZHR_SDLRayScene *, const float *xyz, Uint32 vertexCount,
    const Uint32 *buildingTriangleEnds, Uint32 buildingCount, bool *changed);
/* Full hardware water path split: packed triangle/material rows are static first,
   then dynamic. Local indices are relative to each mesh's vertex array. Shader
   IDs must add rayQueryGetIntersectionInstanceCustomIndexEXT to PrimitiveIndex;
   TLAS custom indices are 0 and the current static packed triangle count. */
typedef struct ZHR_SDLRayMeshView {
    const float *xyz;
    Uint32 vertexCount;
    const Uint32 *indices;
    Uint32 indexCount;
} ZHR_SDLRayMeshView;
ZHR_SDLRayScene *ZHR_SDLRayCreateWaterPathSplitScene(SDL_GPUDevice *,
    const ZHR_SDLRayMeshView *staticMesh, const ZHR_SDLRayMeshView *dynamicMesh,
    const void *spirv, size_t spirvBytes);
/* Same submitted-and-waited scene fence contract as UpdateScene. NULL retains a
   part; explicit zero vertex/index counts removes it. Updates are prepared before
   commit; failure leaves the old scene safely usable/destructible. Unchanged part
   BLAS resources survive. Dirty part BLAS plus TLAS rebuild on next RecordWaterPath.
   All-empty updates succeed but RecordWaterPath refuses until geometry returns.
   Dynamic part1 keeps bounded ~1.25x vertex/primitive/BLAS/scratch capacity across
   nonempty count changes; static part0 remains exact-sized. Inputs copy active
   bytes only. BUILD uses active counts, never reserved tail or UPDATE mode.
   Combined UpdateScene is rejected for split scenes. No global idle on update. */
bool ZHR_SDLRayUpdateWaterPathSplitScene(ZHR_SDLRayScene *,
    const ZHR_SDLRayMeshView *staticOrNull, const ZHR_SDLRayMeshView *dynamicOrNull,
    bool *staticChanged, bool *dynamicChanged);
/* Reports the mutable geometry of a one-bounce scene or the dynamic part of
   a split water scene. Every BUILD uses the exact active counts. */
typedef struct ZHR_SDLRayWaterStorageStats {
    Uint64 dynamicAllocations, dynamicRebuildReuses, dynamicCountReuses, reserveFallbacks, allocationBytes;
    Uint32 activeVertices, activePrimitives, vertexCapacity, primitiveCapacity;
} ZHR_SDLRayWaterStorageStats;
bool ZHR_SDLRayGetWaterStorageStats(const ZHR_SDLRayScene *, ZHR_SDLRayWaterStorageStats *);
typedef enum ZHR_SDLRayRetirement {
    ZHR_SDLRAY_RETIRE_REFUSED = 0,
    ZHR_SDLRAY_RETIRE_IDLE,
    ZHR_SDLRAY_RETIRE_DEVICE_LOST
} ZHR_SDLRayRetirement;
/* All CPU-side readers/recorders must first be submitted or cancelled. Uses SDL's
 * normal idle/command retirement. A refusal is terminal only if a same-device,
 * submit-lock-protected native idle result proves VK_ERROR_DEVICE_LOST. Other
 * failures retain ownership; neither an SDL error string nor a fence proves loss. */
ZHR_SDLRayRetirement ZHR_SDLRayWaitForRetirement(SDL_GPUDevice *);
/* Requires the above IDLE or DEVICE_LOST result and no remaining CPU readers.
 * Device loss permits destruction, never reuse/recreation on that device. */
void ZHR_SDLRayDestroySceneAfterRetirement(ZHR_SDLRayScene *);
/* Waits SDL_WaitForGPUIdle; call before destroying device, after submitting commands. */
void ZHR_SDLRayDestroyScene(ZHR_SDLRayScene *);
#ifdef __cplusplus
}
#endif
#endif
