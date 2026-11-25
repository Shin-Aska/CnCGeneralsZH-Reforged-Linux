#pragma once
#include <SDL3/SDL.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ZHR_WaterRay_State ZHR_WaterRay_State;
typedef struct ZHR_WaterRay_Limits {
    Uint32 maxWidth,maxHeight;
    Uint32 descriptorSlots; /* 1..8, normally 3; fence-retired fill descriptor sets */
    Uint64 maxBytes;        /* cache-owned VkDeviceMemory allocation cap */
} ZHR_WaterRay_Limits;
typedef struct ZHR_WaterRay_Params {
    /* D3D ROW-VECTOR inverse float[16], copied VERBATIM to GLSL mat4 columns.
     * This intentionally performs NO transpose. Matches clipRow*inverse on CPU. */
    float inverse[16];
    float clipZ[4],clipW[4];
    float viewport[4]; /* x,y,width,height of original SSR view, positive extent */
    float camera[4];
} ZHR_WaterRay_Params;
typedef struct ZHR_WaterRay_Stats {
    Uint64 records,successfulRecords,descriptorRefusals,allocatedBytes;
    Uint64 committedSerial,completedSerial;
    Uint32 capacityTiles,lastTileCount,pendingDescriptors,subgroupSize;
    Uint32 subgroupOperations;
} ZHR_WaterRay_Stats;
/* Requires same-device Vulkan ray query+BDA plus COMPUTE subgroup BASIC|ARITHMETIC,
 * compute local16x16 (256 invocations), >=8192 shared bytes. No subgroup fallback.
 * Fill ABI: set0 binding0 combined R32 sampler (texelFetch), binding1 storage buffer;
 * ZHR_WaterRay_Params push=128 bytes; dispatch ceil(width/32),ceil(height/32),1 workgroups.
 * Shader must produce one 32-byte slab per tile: VkAabbPositionsKHR first6 floats,
 * conservative top world Z float6, padding float7. Depth coordinates are depth*1024.
 * One-pixel halo and conservative arithmetic are fill-shader responsibilities.
 */
bool ZHR_WaterRay_Supported(SDL_GPUDevice *,char *diagnostic,size_t capacity);
ZHR_WaterRay_State *ZHR_WaterRay_Create(SDL_GPUDevice *,const void *fillSpirv,size_t spirvBytes,const ZHR_WaterRay_Limits *);
/* Records on the SAME SDL unified queue, OUTSIDE any active pass. Returns
 * {TLAS low32,TLAS high32,slabs BDA low32,slabs BDA high32} only on success.
 * Every refusal writes zero to address and records ZERO native commands for that
 * call. Earlier successful records in the CB remain committable.
 *
 * CRITICAL ORDER: submit ALL previous fragment readers of returned addresses
 * before recording/submitting the next fill. This includes flushing any queued
 * engine draw packets. One fixed-capacity AS/buffer pair is rebuilt in place;
 * barriers order previous fragment reads before overwrites on the GPU. Handles
 * never reallocate or retire until DestroyAfterIdle. Addresses are for following
 * draws, never for an older unsubmitted draw after a later fill was enqueued.
 * Input must be same-device 2D single-sample SAMPLER R32_FLOAT with exact extent.
 * Sampler state is point/clamp; only mip0 sampled. Texture storage is SDL-tracked.
 */
bool ZHR_WaterRay_Record(ZHR_WaterRay_State *,SDL_GPUCommandBuffer *,SDL_GPUTexture *depth,Uint32 width,Uint32 height,
    const ZHR_WaterRay_Params *,Uint32 address[4]);
/* One uncommitted CB per state; multiple Record calls on that CB consume separate
 * descriptor slots. After successful SDL submit Commit with a strictly increasing
 * serial. Collect only after observing a covering SDL fence for ALL <=serial.
 * These fences protect fill descriptors, not fragment reads: the queue-order rule
 * above protects later reads without requiring graphics completion fences.
 * No internal waits/submits/readbacks. Busy slots refuse conservatively.
 */
bool ZHR_WaterRay_Commit(ZHR_WaterRay_State *,SDL_GPUCommandBuffer *,Uint64 serial);
bool ZHR_WaterRay_Collect(ZHR_WaterRay_State *,Uint64 completedSerial);
/* Cancel SDL CB FIRST, then Abandon. In-place previous committed data is unchanged
 * by cancelled fill commands. Never use addresses returned by abandoned records.
 * Failed/ambiguous submission -> Poison; caller Flush + GPU idle before teardown. */
bool ZHR_WaterRay_Abandon(ZHR_WaterRay_State *,SDL_GPUCommandBuffer *cancelled);
void ZHR_WaterRay_Poison(ZHR_WaterRay_State *);
ZHR_WaterRay_Stats ZHR_WaterRay_GetStats(const ZHR_WaterRay_State *);
/* Caller must submit or cancel all commands referencing state, flush queued
 * graphics readers, and establish ZHR_SDLRayWaitForRetirement IDLE or DEVICE_LOST
 * BEFORE calling this function. Device loss permits teardown only, never reuse.
 * It does not wait. Do not destroy on a mere build fence: later graphics may read. */
void ZHR_WaterRay_DestroyAfterIdle(ZHR_WaterRay_State *);
#ifdef __cplusplus
}
#endif
