/* Private local SDL fork, not submitted upstream.
 * BLAS/TLAS geometry, identity instance, descriptor and build-barrier patterns adapted from
 * Khronos Vulkan-Samples samples/extensions/ray_queries/ray_queries.cpp (Apache-2.0).
 * Copyright (c) 2021-2026 Holochip Corporation; 2024-2026 Arm Limited and Contributors.
 * https://github.com/KhronosGroup/Vulkan-Samples/blob/main/samples/extensions/ray_queries/ray_queries.cpp
 * See sdl3-rayquery-NOTICE.txt for license. */
#include "SDL_gpu_vulkan_rayquery.h"
SDL_COMPILE_TIME_ASSERT(ZHR_SunPushSize, sizeof(ZHR_SDLRayParams) == 128);
SDL_COMPILE_TIME_ASSERT(ZHR_WaterPushSize, sizeof(ZHR_SDLRayWaterParams) == 80);
SDL_COMPILE_TIME_ASSERT(ZHR_WaterPathPushSize, sizeof(ZHR_SDLRayWaterPathParams) == 112);
#define ZHR_VK(name) ((PFN_##name)r->vkGetDeviceProcAddr(r->logicalDevice, #name))
typedef struct ZHR_RayBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceAddress address;
    VkDeviceSize bytes;
} ZHR_RayBuffer;

enum { ZHR_MaxSunBuildings = 128, ZHR_SunBuildingBytes = 32 * 1024 * 1024 };

typedef struct ZHR_RaySunBuilding {
    ZHR_SDLRayScene *scene;
    float *xyz;
    Uint32 vertices;
    Uint64 lastUse;
    VkDeviceAddress address;
    VkDeviceSize bytes;
} ZHR_RaySunBuilding;

typedef struct ZHR_RaySunBuildings {
    ZHR_RaySunBuilding buildings[ZHR_MaxSunBuildings];
    VkDeviceAddress active[ZHR_MaxSunBuildings];
    Uint32 capacity;
    Uint64 maxPrimitives, update;
    VkDeviceSize bytes, peakBytes;
    Uint64 reused, blasBuilds, tlasBuilds, evictions;
} ZHR_RaySunBuildings;

struct ZHR_SDLRayScene {
    SDL_GPUDevice *device;
    VulkanRenderer *renderer;
    ZHR_RayBuffer vertex, index, instance, blasBuffer, tlasBuffer, scratch;
    VkAccelerationStructureKHR blas, tlas;
    VkAccelerationStructureGeometryKHR bottomGeometry, topGeometry;
    VkAccelerationStructureBuildGeometryInfoKHR bottomBuild, topBuild;
    VkAccelerationStructureBuildRangeInfoKHR bottomRange, topRange;
    VkDescriptorSetLayout setLayout;
    VkDescriptorPool pool;
    VkDescriptorSet set;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    VkSampler sampler;
    bool built;
    bool water;
    bool waterPath;
    bool split;
    struct ZHR_SDLRayScene *parts[2];
    ZHR_RaySunBuildings *sunBuildings;
    Uint32 vertexCapacity, primitiveCapacity;
    Uint64 dynamicAllocations, dynamicRebuildReuses, dynamicCountReuses, reserveFallbacks, splitUpdates;
    bool storageProfile;
    VkQueryPool profilePool;
    Uint32 profileValidBits;
    double profileTimestampPeriod, profileBuildMs, profileDispatchMs;
    Uint64 profileBuildSamples, profileDispatchSamples;
    bool profilePending, profileHadBuild;
};

bool ZHR_SDLRayQuerySupported(SDL_GPUDevice *device, char *diagnostic, size_t capacity)
{
    if (!device || SDL_strcmp(SDL_GetGPUDeviceDriver(device), "vulkan")) {
        if (diagnostic && capacity) SDL_snprintf(diagnostic, capacity, "ray query unavailable: SDL backend is not Vulkan");
        return false;
    }
    VulkanRenderer *r = (VulkanRenderer *)device->driverData;
    VkPhysicalDeviceProperties *p = &r->physicalDeviceProperties.properties;
    if (diagnostic && capacity) SDL_snprintf(diagnostic, capacity,
        "SDL Vulkan same-device ray query: %s vendor=0x%04x device=0x%04x API=%u.%u.%u requested=%d optional=%d accelerationStructure=%d rayQuery=%d bufferDeviceAddress=%d",
        p->deviceName, p->vendorID, p->deviceID, VK_VERSION_MAJOR(p->apiVersion), VK_VERSION_MINOR(p->apiVersion), VK_VERSION_PATCH(p->apiVersion),
        r->zhrRayRequested, r->zhrRayOptional, r->zhrRayEnabled, r->zhrRayEnabled, r->zhrRayEnabled);
    return r->zhrRayEnabled;
}

static bool ZHR_RayBufferCreateSized(VulkanRenderer *r, ZHR_RayBuffer *b, VkDeviceSize size,
    VkBufferUsageFlags usage, const void *data, VkDeviceSize dataBytes)
{
    VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.size = size; info.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (r->vkCreateBuffer(r->logicalDevice, &info, NULL, &b->buffer) != VK_SUCCESS) return SDL_SetError("ray query buffer creation failed");
    VkMemoryRequirements req;
    r->vkGetBufferMemoryRequirements(r->logicalDevice, b->buffer, &req);
    b->bytes = req.size;
    VkMemoryPropertyFlags flags = data ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    Uint32 type;
    for (type = 0; type < r->memoryProperties.memoryTypeCount; ++type) {
        if ((req.memoryTypeBits & (1u << type)) && (r->memoryProperties.memoryTypes[type].propertyFlags & flags) == flags) break;
    }
    if (type == r->memoryProperties.memoryTypeCount) return SDL_SetError("ray query memory type unavailable");
    VkMemoryAllocateFlagsInfo af = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
    af.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.pNext = &af; ai.allocationSize = req.size; ai.memoryTypeIndex = type;
    if (r->vkAllocateMemory(r->logicalDevice, &ai, NULL, &b->memory) != VK_SUCCESS ||
        r->vkBindBufferMemory(r->logicalDevice, b->buffer, b->memory, 0) != VK_SUCCESS) return SDL_SetError("ray query memory allocation/bind failed");
    if (data) {
        void *mapped;
        if (r->vkMapMemory(r->logicalDevice, b->memory, 0, dataBytes, 0, &mapped) != VK_SUCCESS) return SDL_SetError("ray query memory mapping failed");
        SDL_memcpy(mapped, data, (size_t)dataBytes);
        r->vkUnmapMemory(r->logicalDevice, b->memory);
    }
    VkBufferDeviceAddressInfo address = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
    address.buffer = b->buffer;
    b->address = ZHR_VK(vkGetBufferDeviceAddress)(r->logicalDevice, &address);
    return b->address != 0 || SDL_SetError("ray query buffer device address is zero");
}

static bool ZHR_RayBufferCreate(VulkanRenderer *r, ZHR_RayBuffer *b, VkDeviceSize size, VkBufferUsageFlags usage, const void *data)
{
    return ZHR_RayBufferCreateSized(r,b,size,usage,data,data ? size : 0);
}

static bool ZHR_RayASCreate(VulkanRenderer *r, ZHR_RayBuffer *buffer, VkAccelerationStructureKHR *handle,
    VkAccelerationStructureBuildGeometryInfoKHR *build, Uint32 count, VkDeviceSize *scratchSize)
{
    VkAccelerationStructureBuildSizesInfoKHR sizes = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    ZHR_VK(vkGetAccelerationStructureBuildSizesKHR)(r->logicalDevice, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, build, &count, &sizes);
    if (!ZHR_RayBufferCreate(r, buffer, sizes.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, NULL)) return false;
    VkAccelerationStructureCreateInfoKHR info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
    info.buffer = buffer->buffer; info.size = sizes.accelerationStructureSize; info.type = build->type;
    if (ZHR_VK(vkCreateAccelerationStructureKHR)(r->logicalDevice, &info, NULL, handle) != VK_SUCCESS) return SDL_SetError("ray query acceleration structure creation failed");
    build->dstAccelerationStructure = *handle;
    if (sizes.buildScratchSize > *scratchSize) *scratchSize = sizes.buildScratchSize;
    return true;
}

/* Opt-in GPU timestamps cover the whole native query shader, not only traversal.
   Results are read nonblocking after the caller's existing scene fence, or idle
   teardown. Disabled scenes allocate no pool and issue no timestamp commands. */
static void ZHR_RayProfileLog(ZHR_SDLRayScene *s, bool final)
{
    if (!s->profileDispatchSamples) return;
    SDL_LogInfo(SDL_LOG_CATEGORY_GPU,
        "Water hardware GPU timing%s: AS-build avg=%.3fms samples=%llu; query-dispatch avg=%.3fms samples=%llu",
        final ? " (scene teardown)" : "", s->profileBuildSamples ? s->profileBuildMs / s->profileBuildSamples : 0.0,
        (unsigned long long)s->profileBuildSamples, s->profileDispatchMs / s->profileDispatchSamples,
        (unsigned long long)s->profileDispatchSamples);
}

static void ZHR_RayProfileRead(ZHR_SDLRayScene *s)
{
    if (!s->profilePool || !s->profilePending) return;
    VulkanRenderer *r = s->renderer;
    Uint64 values[8] = { 0 }; // timestamp, availability for each query
    VkResult result = ZHR_VK(vkGetQueryPoolResults)(r->logicalDevice, s->profilePool, 0, 4,
        sizeof(values), values, 2 * sizeof(Uint64), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    s->profilePending = false;
    if (result != VK_SUCCESS || !values[1] || !values[3] || !values[5] || !values[7]) return;
    Uint64 mask = s->profileValidBits < 64 ? (((Uint64)1 << s->profileValidBits) - 1) : ~(Uint64)0;
    if (s->profileHadBuild) {
        s->profileBuildMs += ((values[2] - values[0]) & mask) * s->profileTimestampPeriod / 1000000.0;
        ++s->profileBuildSamples;
    }
    s->profileDispatchMs += ((values[6] - values[4]) & mask) * s->profileTimestampPeriod / 1000000.0;
    if (++s->profileDispatchSamples % 60 == 0) ZHR_RayProfileLog(s, false);
}

static void ZHR_RayProfileInit(ZHR_SDLRayScene *s)
{
    if (!s->waterPath || (!SDL_getenv("ZH_WATER_PROFILE") && !SDL_getenv("ZH_HARDWARE_WATER_PROFILE"))) return;
    VulkanRenderer *r = s->renderer;
    const char *functions[] = { "vkCreateQueryPool", "vkDestroyQueryPool", "vkCmdResetQueryPool", "vkCmdWriteTimestamp", "vkGetQueryPoolResults" };
    for (Uint32 i = 0; i < SDL_arraysize(functions); ++i)
        if (!r->vkGetDeviceProcAddr(r->logicalDevice, functions[i])) return;
    Uint32 count = 0;
    r->vkGetPhysicalDeviceQueueFamilyProperties(r->physicalDevice, &count, NULL);
    VkQueueFamilyProperties *properties = SDL_calloc(count, sizeof(*properties));
    if (!properties) return;
    r->vkGetPhysicalDeviceQueueFamilyProperties(r->physicalDevice, &count, properties);
    if (r->queueFamilyIndex < count) s->profileValidBits = properties[r->queueFamilyIndex].timestampValidBits;
    SDL_free(properties);
    s->profileTimestampPeriod = r->physicalDeviceProperties.properties.limits.timestampPeriod;
    if (!s->profileValidBits || s->profileValidBits > 64 || s->profileTimestampPeriod <= 0.0) return;
    VkQueryPoolCreateInfo info = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
    info.queryType = VK_QUERY_TYPE_TIMESTAMP; info.queryCount = 4;
    if (ZHR_VK(vkCreateQueryPool)(r->logicalDevice, &info, NULL, &s->profilePool) != VK_SUCCESS)
        s->profilePool = VK_NULL_HANDLE;
}

static void ZHR_RayProfileStamp(ZHR_SDLRayScene *s, VulkanCommandBuffer *cb, Uint32 index, VkPipelineStageFlags nextStage)
{
    if (!s->profilePool) return;
    VulkanRenderer *r = s->renderer;
    ZHR_VK(vkCmdWriteTimestamp)(cb->commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, s->profilePool, index);
    // Start timestamps must complete before the following measured work starts.
    // These execution-only barriers exist only in the requested profiling mode.
    if (nextStage) r->vkCmdPipelineBarrier(cb->commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, nextStage, 0, 0, NULL, 0, NULL, 0, NULL);
}

/* Fence ownership is supplied by the caller; this helper never waits globally. */
static void ZHR_RayOwnGeometryDestroy(ZHR_SDLRayScene *s)
{
    VulkanRenderer *r = s->renderer;
    if (s->tlas) ZHR_VK(vkDestroyAccelerationStructureKHR)(r->logicalDevice, s->tlas, NULL);
    if (s->blas) ZHR_VK(vkDestroyAccelerationStructureKHR)(r->logicalDevice, s->blas, NULL);
    ZHR_RayBuffer *buffers[] = { &s->vertex, &s->index, &s->instance, &s->blasBuffer, &s->tlasBuffer, &s->scratch };
    for (Uint32 i = 0; i < SDL_arraysize(buffers); ++i) {
        if (buffers[i]->buffer) r->vkDestroyBuffer(r->logicalDevice, buffers[i]->buffer, NULL);
        if (buffers[i]->memory) r->vkFreeMemory(r->logicalDevice, buffers[i]->memory, NULL);
        SDL_zero(*buffers[i]);
    }
    s->blas = s->tlas = VK_NULL_HANDLE;
    SDL_zero(s->bottomGeometry); SDL_zero(s->topGeometry);
    SDL_zero(s->bottomBuild); SDL_zero(s->topBuild);
    SDL_zero(s->bottomRange); SDL_zero(s->topRange);
    s->built = false; s->vertexCapacity = s->primitiveCapacity = 0;
}

static void ZHR_RaySunBuildingDestroy(ZHR_RaySunBuilding *building)
{
    if (building->scene) {
        ZHR_RayOwnGeometryDestroy(building->scene);
        SDL_free(building->scene);
    }
    SDL_free(building->xyz);
    SDL_zero(*building);
}

static void ZHR_RayGeometryDestroy(ZHR_SDLRayScene *s)
{
    ZHR_RayOwnGeometryDestroy(s); // TLAS retires before the referenced part BLAS objects
    if (s->sunBuildings) {
        for (Uint32 i = 0; i < ZHR_MaxSunBuildings; ++i) {
            ZHR_RaySunBuildingDestroy(&s->sunBuildings->buildings[i]);
        }
        SDL_free(s->sunBuildings);
        s->sunBuildings = NULL;
    }
    for (Uint32 i = 0; i < 2; ++i) {
        if (s->parts[i]) {
            ZHR_RayOwnGeometryDestroy(s->parts[i]);
            SDL_free(s->parts[i]); s->parts[i] = NULL;
        }
    }
}

ZHR_SDLRayRetirement ZHR_SDLRayWaitForRetirement(SDL_GPUDevice *device)
{
    if (!device) return ZHR_SDLRAY_RETIRE_REFUSED;
    if (SDL_WaitForGPUIdle(device)) return ZHR_SDLRAY_RETIRE_IDLE;
    if (SDL_strcmp(SDL_GetGPUDeviceDriver(device), "vulkan")) return ZHR_SDLRAY_RETIRE_REFUSED;
    VulkanRenderer *r = (VulkanRenderer *)device->driverData;
    SDL_LockMutex(r->submitLock);
    const VkResult result = r->vkDeviceWaitIdle(r->logicalDevice);
    SDL_UnlockMutex(r->submitLock);
    return result == VK_ERROR_DEVICE_LOST ? ZHR_SDLRAY_RETIRE_DEVICE_LOST : ZHR_SDLRAY_RETIRE_REFUSED;
}

void ZHR_SDLRayDestroySceneAfterRetirement(ZHR_SDLRayScene *s)
{
    if (!s) return;
    VulkanRenderer *r = s->renderer;
    if (s->sunBuildings && SDL_getenv("ZH_HARDWARE_SUN_PROFILE")) {
        const ZHR_RaySunBuildings *sun = s->sunBuildings;
        SDL_LogInfo(SDL_LOG_CATEGORY_GPU,
            "Sun building acceleration structures: updates=%llu reused=%llu BLAS-builds=%llu TLAS-builds=%llu evictions=%llu peak-building-bytes=%llu",
            (unsigned long long)sun->update, (unsigned long long)sun->reused,
            (unsigned long long)sun->blasBuilds, (unsigned long long)sun->tlasBuilds,
            (unsigned long long)sun->evictions, (unsigned long long)sun->peakBytes);
    }
    if (s->profilePool) {
        ZHR_RayProfileRead(s);
        ZHR_RayProfileLog(s, true);
        ZHR_VK(vkDestroyQueryPool)(r->logicalDevice, s->profilePool, NULL);
    }
    if (s->pipeline) r->vkDestroyPipeline(r->logicalDevice, s->pipeline, NULL);
    if (s->layout) r->vkDestroyPipelineLayout(r->logicalDevice, s->layout, NULL);
    if (s->pool) r->vkDestroyDescriptorPool(r->logicalDevice, s->pool, NULL);
    if (s->setLayout) r->vkDestroyDescriptorSetLayout(r->logicalDevice, s->setLayout, NULL);
    if (s->sampler) r->vkDestroySampler(r->logicalDevice, s->sampler, NULL);
    ZHR_RayGeometryDestroy(s);
    SDL_free(s);
}

void ZHR_SDLRayDestroyScene(ZHR_SDLRayScene *s)
{
    if (!s) return;
    /* Preserve the existing static-scene compatibility contract. Water recovery
       uses the explicit retirement result and retains ownership on refusal. */
    SDL_WaitForGPUIdle(s->device);
    ZHR_SDLRayDestroySceneAfterRetirement(s);
}

static bool ZHR_RayGeometryCreateCapacity(ZHR_SDLRayScene *s, const float *xyz, Uint32 vertexCount,
    const Uint32 *indices, Uint32 indexCount, bool blasOnly, Uint32 vertexCapacity, Uint32 primitiveCapacity)
{
    VulkanRenderer *r = s->renderer;
    VkBufferUsageFlags input = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if (!ZHR_RayBufferCreateSized(r, &s->vertex, (VkDeviceSize)vertexCapacity * 12, input, xyz, (VkDeviceSize)vertexCount * 12) ||
        (indices && !ZHR_RayBufferCreateSized(r, &s->index, (VkDeviceSize)primitiveCapacity * 12,
            input, indices, (VkDeviceSize)indexCount * 4))) goto fail;
    s->bottomGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    s->bottomGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    s->bottomGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    VkAccelerationStructureGeometryTrianglesDataKHR *tri = &s->bottomGeometry.geometry.triangles;
    tri->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    tri->vertexFormat = VK_FORMAT_R32G32B32_SFLOAT; tri->vertexData.deviceAddress = s->vertex.address;
    tri->vertexStride = 12; tri->maxVertex = vertexCapacity - 1;
    tri->indexType = indices ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_NONE_KHR;
    tri->indexData.deviceAddress = s->index.address;
    s->bottomBuild.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    s->bottomBuild.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    s->bottomBuild.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    s->bottomBuild.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    s->bottomBuild.geometryCount = 1; s->bottomBuild.pGeometries = &s->bottomGeometry;
    s->bottomRange.primitiveCount = indexCount / 3;
    VkDeviceSize scratchSize = 0;
    // Vulkan size-query guarantees allow later BUILD counts/maxVertex no greater
    // than these queried maxima. Active build descriptors remain exact below.
    if (!ZHR_RayASCreate(r, &s->blasBuffer, &s->blas, &s->bottomBuild, primitiveCapacity, &scratchSize)) goto fail;
    tri->maxVertex = vertexCount - 1;
    s->vertexCapacity = vertexCapacity; s->primitiveCapacity = primitiveCapacity;
    if (!blasOnly) {
    VkAccelerationStructureDeviceAddressInfoKHR address = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
    address.accelerationStructure = s->blas;
    VkAccelerationStructureInstanceKHR instance;
    SDL_zero(instance);
    instance.transform.matrix[0][0] = instance.transform.matrix[1][1] = instance.transform.matrix[2][2] = 1.0f;
    instance.mask = 0xff; instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance.accelerationStructureReference = ZHR_VK(vkGetAccelerationStructureDeviceAddressKHR)(r->logicalDevice, &address);
    if (!ZHR_RayBufferCreate(r, &s->instance, sizeof(instance), input, &instance)) goto fail;
    s->topGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    s->topGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    s->topGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    s->topGeometry.geometry.instances.data.deviceAddress = s->instance.address;
    s->topBuild.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    s->topBuild.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    s->topBuild.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    s->topBuild.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    s->topBuild.geometryCount = 1; s->topBuild.pGeometries = &s->topGeometry;
    s->topRange.primitiveCount = 1;
    if (!ZHR_RayASCreate(r, &s->tlasBuffer, &s->tlas, &s->topBuild, 1, &scratchSize)) goto fail;
    }
    VkPhysicalDeviceAccelerationStructurePropertiesKHR properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
    VkPhysicalDeviceProperties2 props = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    props.pNext = &properties;
    r->vkGetPhysicalDeviceProperties2KHR(r->physicalDevice, &props);
    VkDeviceSize align = properties.minAccelerationStructureScratchOffsetAlignment;
    if (!ZHR_RayBufferCreate(r, &s->scratch, scratchSize + align, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, NULL)) goto fail;
    s->bottomBuild.scratchData.deviceAddress = s->topBuild.scratchData.deviceAddress = (s->scratch.address + align - 1) & ~(align - 1);
    return true;
fail:
    return false;
}

static bool ZHR_RayGeometryCreate(ZHR_SDLRayScene *s, const float *xyz, Uint32 vertexCount,
    const Uint32 *indices, Uint32 indexCount, bool blasOnly)
{
    return ZHR_RayGeometryCreateCapacity(s,xyz,vertexCount,indices,indexCount,blasOnly,vertexCount,indexCount/3);
}

static bool ZHR_RayGeometryCreateReserved(ZHR_SDLRayScene *s, const ZHR_SDLRayMeshView *mesh,
    bool blasOnly, Uint32 reserveDivisor, bool *fallback)
{
    VulkanRenderer *r=s->renderer;
    const Uint32 primitives=mesh->indexCount/3;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR properties={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 props={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2}; props.pNext=&properties;
    r->vkGetPhysicalDeviceProperties2KHR(r->physicalDevice,&props);
    if(primitives>properties.maxPrimitiveCount) return SDL_SetError("ray mesh exceeds device primitive limit");
    const Uint64 grownVertices=(Uint64)mesh->vertexCount+mesh->vertexCount/reserveDivisor;
    const Uint64 grownPrimitives=(Uint64)primitives+primitives/reserveDivisor;
    const Uint32 vertexCapacity=(Uint32)SDL_min(grownVertices,(Uint64)SDL_MAX_UINT32);
    const Uint64 deviceLimit=properties.maxPrimitiveCount;
    const Uint64 primitiveLimit=blasOnly ? SDL_min((Uint64)16777216,deviceLimit) : deviceLimit;
    const Uint32 primitiveCapacity=(Uint32)SDL_min(grownPrimitives,primitiveLimit);
    if (primitiveCapacity>=primitives && ZHR_RayGeometryCreateCapacity(s,mesh->xyz,mesh->vertexCount,
        mesh->indices,mesh->indexCount,blasOnly,vertexCapacity,primitiveCapacity)) return true;
    if (vertexCapacity==mesh->vertexCount && primitiveCapacity==primitives) return false;
    ZHR_RayOwnGeometryDestroy(s); // reserve pressure must not sacrifice exact-size fallback
    if (!ZHR_RayGeometryCreate(s,mesh->xyz,mesh->vertexCount,mesh->indices,mesh->indexCount,blasOnly)) return false;
    if (fallback) *fallback=true;
    SDL_ClearError(); return true;
}

/* Culling changes the active counts. Reserve room for the reflected scene and
   the sun casters so the next BUILD can use the same AS and scratch. */
static bool ZHR_RayFlatGeometryCreate(ZHR_SDLRayScene *s, const float *xyz, Uint32 vertexCount,
    const Uint32 *indices, Uint32 indexCount, bool *reserveFallback)
{
    if (s->waterPath) return ZHR_RayGeometryCreate(s, xyz, vertexCount, indices, indexCount, false);
    const ZHR_SDLRayMeshView mesh = {xyz, vertexCount, indices, indexCount};
    return ZHR_RayGeometryCreateReserved(s, &mesh, false, s->water ? 4 : 2, reserveFallback);
}

static ZHR_SDLRayScene *ZHR_RayCreateScene(SDL_GPUDevice *device, const float *xyz, Uint32 vertexCount,
    const Uint32 *indices, Uint32 indexCount, const void *spirv, size_t spirvBytes, bool water, bool waterPath, bool split)
{
    if (!ZHR_SDLRayQuerySupported(device, NULL, 0)) { SDL_SetError("ray query was not enabled on the SDL Vulkan device"); return NULL; }
    const bool needsFlatInput = !split || water;
    if ((needsFlatInput && (!xyz || !indices || !vertexCount || indexCount < 3 || indexCount % 3)) ||
        !spirv || !spirvBytes || spirvBytes % 4) {
        SDL_SetError("invalid ray query scene input"); return NULL;
    }
    if (water && indexCount / 3 > 16777216u) {
        SDL_SetError("water ray query triangle count exceeds exact RGBA32F ID range"); return NULL;
    }
    for (Uint32 i = 0; i < indexCount; ++i) if (indices[i] >= vertexCount) { SDL_SetError("ray query index out of bounds"); return NULL; }
    VulkanRenderer *r = (VulkanRenderer *)device->driverData;
    ZHR_SDLRayScene *s = SDL_calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->device = device; s->renderer = r; s->water = water; s->waterPath = waterPath; s->split = split;
    const char *storageProfile=SDL_getenv("ZH_HARDWARE_WATER_STORAGE_PROFILE");
    s->storageProfile=split && storageProfile && SDL_strcmp(storageProfile,"1")==0;
    if (!split) {
        bool reserveFallback = false;
        if (!ZHR_RayFlatGeometryCreate(s, xyz, vertexCount, indices, indexCount, &reserveFallback))
            goto fail;
        if (water && !waterPath) {
            s->dynamicAllocations = 1;
            s->reserveFallbacks = reserveFallback;
        }
    }
    VkDescriptorSetLayoutBinding bindings[4] = {
        {0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}
    };
    VkDescriptorSetLayoutCreateInfo sl = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    if (water) {
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    }
    if (waterPath) {
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[3].binding = 3; bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[3].descriptorCount = 1; bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    sl.bindingCount = waterPath ? 4 : (water ? 2 : 3); sl.pBindings = bindings;
    if (r->vkCreateDescriptorSetLayout(r->logicalDevice, &sl, NULL, &s->setLayout) != VK_SUCCESS) goto fail;
    VkDescriptorPoolSize poolSizes[3] = {{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1}};
    VkDescriptorPoolCreateInfo pool = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    if (water) {
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    }
    if (waterPath) {
        poolSizes[0].descriptorCount = 2;
        poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }
    pool.maxSets = 1; pool.poolSizeCount = waterPath ? 3 : (water ? 2 : 3); pool.pPoolSizes = poolSizes;
    if (r->vkCreateDescriptorPool(r->logicalDevice, &pool, NULL, &s->pool) != VK_SUCCESS) goto fail;
    VkDescriptorSetAllocateInfo sa = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    sa.descriptorPool = s->pool; sa.descriptorSetCount = 1; sa.pSetLayouts = &s->setLayout;
    if (r->vkAllocateDescriptorSets(r->logicalDevice, &sa, &s->set) != VK_SUCCESS) goto fail;
    VkPushConstantRange push = { VK_SHADER_STAGE_COMPUTE_BIT, 0, waterPath ? sizeof(ZHR_SDLRayWaterPathParams) : (water ? sizeof(ZHR_SDLRayWaterParams) : sizeof(ZHR_SDLRayParams)) };
    VkPipelineLayoutCreateInfo layout = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layout.setLayoutCount = 1; layout.pSetLayouts = &s->setLayout; layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
    if (r->vkCreatePipelineLayout(r->logicalDevice, &layout, NULL, &s->layout) != VK_SUCCESS) goto fail;
    VkSamplerCreateInfo sampler = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = 0;
    if ((!water || waterPath) && r->vkCreateSampler(r->logicalDevice, &sampler, NULL, &s->sampler) != VK_SUCCESS) goto fail;
    VkShaderModuleCreateInfo module = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    module.codeSize = spirvBytes; module.pCode = (const Uint32 *)spirv;
    VkShaderModule shader = VK_NULL_HANDLE;
    if (r->vkCreateShaderModule(r->logicalDevice, &module, NULL, &shader) != VK_SUCCESS) goto fail;
    VkComputePipelineCreateInfo pipeline = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    pipeline.layout = s->layout;
    pipeline.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; pipeline.stage.module = shader; pipeline.stage.pName = "main";
    VkResult result = r->vkCreateComputePipelines(r->logicalDevice, VK_NULL_HANDLE, 1, &pipeline, NULL, &s->pipeline);
    r->vkDestroyShaderModule(r->logicalDevice, shader, NULL);
    if (result != VK_SUCCESS) goto fail;
    ZHR_RayProfileInit(s);
    return s;
fail:
    ZHR_SDLRayDestroyScene(s);
    SDL_SetError("ray query scene creation failed (native allocation or pipeline)");
    return NULL;
}

ZHR_SDLRayScene *ZHR_SDLRayCreateScene(SDL_GPUDevice *device, const float *xyz, Uint32 vertexCount,
    const Uint32 *indices, Uint32 indexCount, const void *spirv, size_t spirvBytes)
{
    return ZHR_RayCreateScene(device, xyz, vertexCount, indices, indexCount, spirv, spirvBytes, false, false, false);
}

ZHR_SDLRayScene *ZHR_SDLRayCreateWaterScene(SDL_GPUDevice *device, const float *xyz, Uint32 vertexCount,
    const Uint32 *indices, Uint32 indexCount, const void *spirv, size_t spirvBytes)
{
    return ZHR_RayCreateScene(device, xyz, vertexCount, indices, indexCount, spirv, spirvBytes, true, false, false);
}

ZHR_SDLRayScene *ZHR_SDLRayCreateWaterPathScene(SDL_GPUDevice *device, const float *xyz, Uint32 vertexCount,
    const Uint32 *indices, Uint32 indexCount, const void *spirv, size_t spirvBytes)
{
    return ZHR_RayCreateScene(device, xyz, vertexCount, indices, indexCount, spirv, spirvBytes, true, true, false);
}

bool ZHR_SDLRayUpdateScene(ZHR_SDLRayScene *s, const float *xyz, Uint32 vertexCount,
    const Uint32 *indices, Uint32 indexCount, bool *changed)
{
    if (changed) *changed = false;
    if (!s || s->split || !xyz || !indices || !vertexCount || indexCount < 3 || indexCount % 3)
        return SDL_SetError("invalid ray query scene update input");
    if (s->water && indexCount / 3 > 16777216u)
        return SDL_SetError("water ray query triangle count exceeds exact RGBA32F ID range");
    for (Uint32 i = 0; i < indexCount; ++i)
        if (indices[i] >= vertexCount) return SDL_SetError("ray query update index out of bounds");
    VulkanRenderer *r = s->renderer;
    const Uint32 primitives = indexCount / 3;
    const bool sameCounts = vertexCount == s->bottomGeometry.geometry.triangles.maxVertex + 1 &&
        primitives == s->bottomRange.primitiveCount;
    const bool oneBounce = s->water && !s->waterPath;
    if (sameCounts || (!s->waterPath && vertexCount <= s->vertexCapacity && primitives <= s->primitiveCapacity)) {
        /* Input allocations are coherent, host visible and never written by the GPU.
           Compare their original bytes directly, avoiding a second persistent CPU mesh. */
        void *vertexData = NULL, *indexData = NULL;
        const size_t vertexBytes = (size_t)vertexCount * 12, indexBytes = (size_t)indexCount * 4;
        if (r->vkMapMemory(r->logicalDevice, s->vertex.memory, 0, vertexBytes, 0, &vertexData) != VK_SUCCESS)
            return SDL_SetError("ray query update vertex mapping failed");
        if (r->vkMapMemory(r->logicalDevice, s->index.memory, 0, indexBytes, 0, &indexData) != VK_SUCCESS) {
            r->vkUnmapMemory(r->logicalDevice, s->vertex.memory);
            return SDL_SetError("ray query update index mapping failed");
        }
        const bool differs = !sameCounts || SDL_memcmp(vertexData, xyz, vertexBytes) != 0 ||
            SDL_memcmp(indexData, indices, indexBytes) != 0;
        if (differs) {
            SDL_memcpy(vertexData, xyz, vertexBytes);
            SDL_memcpy(indexData, indices, indexBytes);
            s->bottomGeometry.geometry.triangles.maxVertex = vertexCount - 1;
            s->bottomRange.primitiveCount = primitives;
            s->built = false;
            if (oneBounce) {
                ++s->dynamicRebuildReuses;
                if (!sameCounts) ++s->dynamicCountReuses;
            }
        }
        r->vkUnmapMemory(r->logicalDevice, s->index.memory);
        r->vkUnmapMemory(r->logicalDevice, s->vertex.memory);
        if (changed) *changed = differs;
        return true;
    }
    /* Allocate the replacement before retiring old geometry. Failure leaves the old
       scene intact and safely destructible. No pipeline/descriptors are recreated. */
    ZHR_SDLRayScene replacement;
    SDL_zero(replacement);
    replacement.renderer = r;
    replacement.water = s->water;
    replacement.waterPath = s->waterPath;
    bool reserveFallback = false;
    if (!ZHR_RayFlatGeometryCreate(&replacement, xyz, vertexCount, indices, indexCount, &reserveFallback)) {
        ZHR_RayGeometryDestroy(&replacement);
        return false;
    }
    ZHR_RayGeometryDestroy(s);
    s->vertex = replacement.vertex; s->index = replacement.index;
    s->instance = replacement.instance; s->blasBuffer = replacement.blasBuffer;
    s->tlasBuffer = replacement.tlasBuffer; s->scratch = replacement.scratch;
    s->blas = replacement.blas; s->tlas = replacement.tlas;
    s->bottomGeometry = replacement.bottomGeometry; s->topGeometry = replacement.topGeometry;
    s->bottomBuild = replacement.bottomBuild; s->topBuild = replacement.topBuild;
    s->bottomRange = replacement.bottomRange; s->topRange = replacement.topRange;
    s->vertexCapacity = replacement.vertexCapacity; s->primitiveCapacity = replacement.primitiveCapacity;
    /* Build descriptors point into the scene itself, not the temporary replacement. */
    s->bottomBuild.pGeometries = &s->bottomGeometry;
    s->topBuild.pGeometries = &s->topGeometry;
    if (oneBounce) {
        ++s->dynamicAllocations;
        if (reserveFallback) ++s->reserveFallbacks;
    }
    if (changed) *changed = true;
    return true;
}

static bool ZHR_RayMeshViewValid(const ZHR_SDLRayMeshView *mesh)
{
    if (!mesh || (!mesh->vertexCount && !mesh->indexCount)) return true;
    if (!mesh->xyz || !mesh->indices || !mesh->vertexCount || mesh->indexCount < 3 || mesh->indexCount % 3)
        return SDL_SetError("invalid split ray-query mesh view");
    for (Uint32 i = 0; i < mesh->indexCount; ++i)
        if (mesh->indices[i] >= mesh->vertexCount) return SDL_SetError("split ray-query local index out of bounds");
    return true;
}

static Uint32 ZHR_RaySplitInstances(VulkanRenderer *r, ZHR_SDLRayScene *const parts[2],
    const Uint32 activeCounts[2], VkAccelerationStructureInstanceKHR instances[2])
{
    Uint32 count = 0, base = 0;
    for (Uint32 i = 0; i < 2; ++i) {
        if (!parts[i]) continue;
        VkAccelerationStructureInstanceKHR *instance = &instances[count++];
        SDL_zero(*instance);
        instance->transform.matrix[0][0] = instance->transform.matrix[1][1] = instance->transform.matrix[2][2] = 1.0f;
        instance->mask = 0xff; instance->flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance->instanceCustomIndex = base;
        VkAccelerationStructureDeviceAddressInfoKHR address = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
        address.accelerationStructure = parts[i]->blas;
        instance->accelerationStructureReference = ZHR_VK(vkGetAccelerationStructureDeviceAddressKHR)(r->logicalDevice, &address);
        base += activeCounts[i];
    }
    return count;
}

static bool ZHR_RayTopCreate(ZHR_SDLRayScene *s, const VkAccelerationStructureInstanceKHR *instances,
    Uint32 count, Uint32 capacity, VkBuildAccelerationStructureFlagsKHR flags)
{
    VulkanRenderer *r = s->renderer;
    const VkAccelerationStructureInstanceKHR empty = {0};
    if (!ZHR_RayBufferCreateSized(r, &s->instance, capacity * sizeof(*instances),
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        count ? instances : &empty, SDL_max(count,1u) * sizeof(*instances))) return false;
    s->topGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    s->topGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    s->topGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    s->topGeometry.geometry.instances.data.deviceAddress = s->instance.address;
    s->topBuild.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    s->topBuild.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    s->topBuild.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    s->topBuild.flags = flags;
    s->topBuild.geometryCount = 1; s->topBuild.pGeometries = &s->topGeometry;
    s->topRange.primitiveCount = count;
    VkDeviceSize scratchSize = 0;
    if (!ZHR_RayASCreate(r, &s->tlasBuffer, &s->tlas, &s->topBuild, capacity, &scratchSize)) return false;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
    VkPhysicalDeviceProperties2 props = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    props.pNext = &properties;
    r->vkGetPhysicalDeviceProperties2KHR(r->physicalDevice, &props);
    VkDeviceSize align = properties.minAccelerationStructureScratchOffsetAlignment;
    if (!ZHR_RayBufferCreate(r, &s->scratch, scratchSize + align, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, NULL)) return false;
    s->topBuild.scratchData.deviceAddress = (s->scratch.address + align - 1) & ~(align - 1);
    return true;
}

static bool ZHR_RaySplitTopCreate(ZHR_SDLRayScene *s, ZHR_SDLRayScene *const parts[2], const Uint32 activeCounts[2])
{
    VkAccelerationStructureInstanceKHR instances[2];
    const Uint32 count = ZHR_RaySplitInstances(s->renderer, parts, activeCounts, instances);
    if (!count) return SDL_SetError("split TLAS requires at least one nonempty mesh");
    return ZHR_RayTopCreate(s, instances, count, count, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
}

static Uint32 ZHR_RaySunFindBuilding(const ZHR_RaySunBuildings *sun, const float *xyz, Uint32 vertices)
{
    for (Uint32 i = 0; i < ZHR_MaxSunBuildings; ++i) {
        const ZHR_RaySunBuilding *building = &sun->buildings[i];
        if (building->scene && building->vertices == vertices &&
            SDL_memcmp(xyz, building->xyz, (size_t)vertices * 12) == 0) return i;
    }
    return ZHR_MaxSunBuildings;
}

static bool ZHR_RaySunAddBuilding(ZHR_SDLRayScene *s, const float *xyz, Uint32 vertices, Uint32 *slot)
{
    VulkanRenderer *r = s->renderer;
    ZHR_RaySunBuildings *sun = s->sunBuildings;
    ZHR_RaySunBuilding next = {0};
    const size_t vertexBytes = (size_t)vertices * 12;
    next.scene = SDL_calloc(1, sizeof(*next.scene));
    if (!next.scene) return false;
    next.scene->renderer = r;
    if (!ZHR_RayGeometryCreate(next.scene, xyz, vertices, NULL, vertices, true)) goto fail;
    VkAccelerationStructureDeviceAddressInfoKHR address = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
    address.accelerationStructure = next.scene->blas;
    next.address = ZHR_VK(vkGetAccelerationStructureDeviceAddressKHR)(r->logicalDevice, &address);
    if (!next.address) { SDL_SetError("sun building has no acceleration structure address"); goto fail; }
    next.bytes = vertexBytes + sizeof(*next.scene) + next.scene->vertex.bytes + next.scene->blasBuffer.bytes + next.scene->scratch.bytes;
    if (next.bytes > ZHR_SunBuildingBytes) { SDL_SetError("sun building exceeds resident geometry budget"); goto fail; }
    // The caller has waited for every use of the preceding TLAS. Drop only
    // buildings absent from the new active list, and rebuild the TLAS before tracing.
    for (;;) {
        *slot = ZHR_MaxSunBuildings;
        Uint32 oldest = ZHR_MaxSunBuildings;
        for (Uint32 i = 0; i < ZHR_MaxSunBuildings; ++i) {
            const ZHR_RaySunBuilding *building = &sun->buildings[i];
            if (!building->scene) {
                if (*slot == ZHR_MaxSunBuildings) *slot = i;
            } else if (building->lastUse != sun->update &&
                       (oldest == ZHR_MaxSunBuildings || sun->buildings[oldest].lastUse > building->lastUse)) {
                oldest = i;
            }
        }
        if (*slot != ZHR_MaxSunBuildings && next.bytes <= ZHR_SunBuildingBytes - sun->bytes) break;
        if (oldest == ZHR_MaxSunBuildings) { SDL_SetError("active sun buildings exceed resident geometry budget"); goto fail; }
        sun->bytes -= sun->buildings[oldest].bytes;
        ZHR_RaySunBuildingDestroy(&sun->buildings[oldest]);
        ++sun->evictions;
    }
    next.xyz = SDL_malloc(vertexBytes);
    if (!next.xyz) goto fail;
    SDL_memcpy(next.xyz, xyz, vertexBytes);
    next.vertices = vertices;
    next.lastUse = sun->update;
    sun->buildings[*slot] = next;
    sun->bytes += next.bytes;
    sun->peakBytes = SDL_max(sun->peakBytes, sun->bytes);
    return true;
fail:
    ZHR_RaySunBuildingDestroy(&next);
    return false;
}

bool ZHR_SDLRayUpdateSunBuildingScene(ZHR_SDLRayScene *s, const float *xyz, Uint32 vertexCount,
    const Uint32 *buildingTriangleEnds, Uint32 buildingCount, bool *changed)
{
    if (changed) *changed = false;
    if (!s || !s->sunBuildings || !xyz || !vertexCount || vertexCount % 3 || !buildingTriangleEnds ||
        !buildingCount || buildingCount > s->sunBuildings->capacity) return SDL_SetError("invalid sun building scene input");
    ZHR_RaySunBuildings *sun = s->sunBuildings;
    Uint32 first = 0;
    for (Uint32 i = 0; i < buildingCount; ++i) {
        const Uint32 end = buildingTriangleEnds[i];
        if (end <= first || end > vertexCount / 3 || end - first > sun->maxPrimitives)
            return SDL_SetError("invalid sun building triangle range");
        first = end;
    }
    if (first != vertexCount / 3) return SDL_SetError("sun-building ranges do not cover the complete scene");

    ++sun->update;
    Uint32 slots[ZHR_MaxSunBuildings];
    first = 0;
    // Protect all current cache hits before allocating anything that could evict them.
    for (Uint32 i = 0; i < buildingCount; ++i) {
        const Uint32 end = buildingTriangleEnds[i];
        const float *source = xyz + (size_t)first * 9;
        const Uint32 vertices = (end - first) * 3;
        slots[i] = ZHR_RaySunFindBuilding(sun, source, vertices);
        if (slots[i] != ZHR_MaxSunBuildings) {
            sun->buildings[slots[i]].lastUse = sun->update;
            ++sun->reused;
        }
        first = end;
    }
    VkAccelerationStructureInstanceKHR instances[ZHR_MaxSunBuildings];
    bool differs = s->topRange.primitiveCount != buildingCount;
    first = 0;
    for (Uint32 i = 0; i < buildingCount; ++i) {
        const Uint32 end = buildingTriangleEnds[i];
        if (slots[i] == ZHR_MaxSunBuildings) {
            const float *source = xyz + (size_t)first * 9;
            const Uint32 vertices = (end - first) * 3;
            slots[i] = ZHR_RaySunFindBuilding(sun, source, vertices);
            if (slots[i] == ZHR_MaxSunBuildings) {
                if (!ZHR_RaySunAddBuilding(s, source, vertices, &slots[i])) return false;
            } else {
                ++sun->reused;
            }
        }
        const ZHR_RaySunBuilding *building = &sun->buildings[slots[i]];
        VkAccelerationStructureInstanceKHR *instance = &instances[i];
        SDL_zero(*instance);
        instance->transform.matrix[0][0] = instance->transform.matrix[1][1] = instance->transform.matrix[2][2] = 1.0f;
        instance->mask = 0xff;
        instance->flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance->accelerationStructureReference = building->address;
        differs |= sun->active[i] != building->address || !building->scene->built;
        first = end;
    }
    if (differs) {
        VulkanRenderer *r = s->renderer;
        void *mapped = NULL;
        const size_t bytes = buildingCount * sizeof(*instances);
        if (r->vkMapMemory(r->logicalDevice, s->instance.memory, 0, bytes, 0, &mapped) != VK_SUCCESS)
            return SDL_SetError("sun building instances could not be mapped");
        SDL_memcpy(mapped, instances, bytes);
        r->vkUnmapMemory(r->logicalDevice, s->instance.memory);
        for (Uint32 i = 0; i < buildingCount; ++i) sun->active[i] = instances[i].accelerationStructureReference;
        s->topRange.primitiveCount = buildingCount;
        s->built = false;
    }
    if (changed) *changed = differs;
    return true;
}

ZHR_SDLRayScene *ZHR_SDLRayCreateSunBuildingScene(SDL_GPUDevice *device, const float *xyz, Uint32 vertexCount,
    const Uint32 *buildingTriangleEnds, Uint32 buildingCount, const void *spirv, size_t spirvBytes)
{
    if (!buildingCount || buildingCount > ZHR_MaxSunBuildings) {
        SDL_SetError("sun building count exceeds instance capacity"); return NULL;
    }
    ZHR_SDLRayScene *s = ZHR_RayCreateScene(device, NULL, 0, NULL, 0, spirv, spirvBytes, false, false, true);
    if (!s) return NULL;
    s->sunBuildings = SDL_calloc(1, sizeof(*s->sunBuildings));
    if (!s->sunBuildings) goto fail;
    VulkanRenderer *r = s->renderer;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
    VkPhysicalDeviceProperties2 props = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    props.pNext = &properties;
    r->vkGetPhysicalDeviceProperties2KHR(r->physicalDevice, &props);
    ZHR_RaySunBuildings *sun = s->sunBuildings;
    sun->capacity = (Uint32)SDL_min((Uint64)ZHR_MaxSunBuildings, properties.maxInstanceCount);
    sun->maxPrimitives = properties.maxPrimitiveCount;
    if (buildingCount > sun->capacity) { SDL_SetError("sun building count exceeds device limits"); goto fail; }
    if (!ZHR_RayTopCreate(s, NULL, 0, sun->capacity, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR)) goto fail;
    sun->bytes = sun->peakBytes = sizeof(*sun) + s->instance.bytes + s->tlasBuffer.bytes + s->scratch.bytes;
    if (sun->bytes > ZHR_SunBuildingBytes ||
        !ZHR_SDLRayUpdateSunBuildingScene(s, xyz, vertexCount, buildingTriangleEnds, buildingCount, NULL)) goto fail;
    return s;
fail:
    ZHR_SDLRayDestroyScene(s);
    return NULL;
}

bool ZHR_SDLRayUpdateWaterPathSplitScene(ZHR_SDLRayScene *s,
    const ZHR_SDLRayMeshView *staticMesh, const ZHR_SDLRayMeshView *dynamicMesh,
    bool *staticChanged, bool *dynamicChanged)
{
    if (staticChanged) *staticChanged = false;
    if (dynamicChanged) *dynamicChanged = false;
    if (!s || !s->split || !s->waterPath) return SDL_SetError("ray-query scene is not split water path mode");
    const ZHR_SDLRayMeshView *views[2] = { staticMesh, dynamicMesh };
    Uint32 total = 0,activeCounts[2]={0,0};
    for (Uint32 i = 0; i < 2; ++i) {
        if (!ZHR_RayMeshViewValid(views[i])) return false;
        Uint32 count = views[i] ? views[i]->indexCount / 3 : (s->parts[i] ? s->parts[i]->bottomRange.primitiveCount : 0);
        if (count > 16777216u - total) return SDL_SetError("split water triangle IDs exceed exact RGBA32F range");
        total += count; activeCounts[i]=count;
    }
    VulkanRenderer *r = s->renderer;
    ZHR_SDLRayScene *next[2] = { s->parts[0], s->parts[1] };
    ZHR_SDLRayScene topReplacement;
    SDL_zero(topReplacement); topReplacement.renderer = r;
    void *mappedVertex[2] = { NULL, NULL }, *mappedIndex[2] = { NULL, NULL }, *mappedInstances = NULL;
    bool changed[2] = { false, false }, allocated[2] = { false, false }, replaceTop = false;
    bool dynamicCountReuse=false,reserveFallback=false;
    // Prepare both parts before changing any existing input bytes, AS handle or ID offset.
    for (Uint32 i = 0; i < 2; ++i) {
        const ZHR_SDLRayMeshView *mesh = views[i];
        if (!mesh) continue;
        ZHR_SDLRayScene *old = s->parts[i];
        if (!mesh->indexCount) { next[i] = NULL; changed[i] = old != NULL; continue; }
        const bool countsEqual=old && mesh->vertexCount==old->bottomGeometry.geometry.triangles.maxVertex+1 &&
            mesh->indexCount==old->bottomRange.primitiveCount*3;
        const bool withinDynamicCapacity=i==1 && old && mesh->vertexCount<=old->vertexCapacity &&
            mesh->indexCount/3<=old->primitiveCapacity;
        if (countsEqual || withinDynamicCapacity) {
            size_t vertexBytes = (size_t)mesh->vertexCount * 12, indexBytes = (size_t)mesh->indexCount * 4;
            void *mapped = NULL;
            if (r->vkMapMemory(r->logicalDevice, old->vertex.memory, 0, vertexBytes, 0, &mapped) != VK_SUCCESS) {
                SDL_SetError("split ray-query vertex mapping failed"); goto fail;
            }
            mappedVertex[i] = mapped; mapped = NULL;
            if (r->vkMapMemory(r->logicalDevice, old->index.memory, 0, indexBytes, 0, &mapped) != VK_SUCCESS) {
                SDL_SetError("split ray-query index mapping failed"); goto fail;
            }
            mappedIndex[i] = mapped;
            changed[i] = !countsEqual || SDL_memcmp(mappedVertex[i], mesh->xyz, vertexBytes) != 0 ||
                SDL_memcmp(mappedIndex[i], mesh->indices, indexBytes) != 0;
            if(i==1 && !countsEqual) dynamicCountReuse=true;
        } else {
            next[i] = SDL_calloc(1, sizeof(*next[i]));
            if (!next[i]) goto fail;
            allocated[i] = true; next[i]->renderer = r;
            const bool created=i==1 ? ZHR_RayGeometryCreateReserved(next[i],mesh,true,4,&reserveFallback)
                : ZHR_RayGeometryCreate(next[i],mesh->xyz,mesh->vertexCount,mesh->indices,mesh->indexCount,true);
            if (!created) goto fail;
            changed[i] = true;
        }
    }
    bool anyChanged = changed[0] || changed[1];
    Uint32 instanceCount = (next[0] != NULL) + (next[1] != NULL);
    VkAccelerationStructureInstanceKHR instances[2];
    if (anyChanged && instanceCount) {
        if (!s->tlas || instanceCount != s->topRange.primitiveCount) {
            if (!ZHR_RaySplitTopCreate(&topReplacement, next, activeCounts)) goto fail;
            replaceTop = true;
        } else {
            ZHR_RaySplitInstances(r, next, activeCounts, instances);
            if (r->vkMapMemory(r->logicalDevice, s->instance.memory, 0, instanceCount * sizeof(*instances), 0, &mappedInstances) != VK_SUCCESS) {
                SDL_SetError("split ray-query instance mapping failed"); goto fail;
            }
        }
    }
    // Commit is infallible: all allocation/mapping validation succeeded above.
    if (replaceTop || (anyChanged && !instanceCount)) {
        ZHR_RayOwnGeometryDestroy(s);
        if (replaceTop) {
            s->instance = topReplacement.instance; s->tlasBuffer = topReplacement.tlasBuffer;
            s->scratch = topReplacement.scratch; s->tlas = topReplacement.tlas;
            s->topGeometry = topReplacement.topGeometry; s->topBuild = topReplacement.topBuild;
            s->topRange = topReplacement.topRange; s->topBuild.pGeometries = &s->topGeometry;
        }
    } else if (mappedInstances) {
        SDL_memcpy(mappedInstances, instances, instanceCount * sizeof(*instances));
        r->vkUnmapMemory(r->logicalDevice, s->instance.memory);
    }
    for (Uint32 i = 0; i < 2; ++i) {
        ZHR_SDLRayScene *old = s->parts[i];
        if (mappedVertex[i]) {
            if (changed[i]) {
                SDL_memcpy(mappedVertex[i], views[i]->xyz, (size_t)views[i]->vertexCount * 12);
                SDL_memcpy(mappedIndex[i], views[i]->indices, (size_t)views[i]->indexCount * 4);
                old->bottomGeometry.geometry.triangles.maxVertex=views[i]->vertexCount-1;
                old->bottomRange.primitiveCount=views[i]->indexCount/3;
                old->built = false;
            }
            r->vkUnmapMemory(r->logicalDevice, old->vertex.memory);
            r->vkUnmapMemory(r->logicalDevice, old->index.memory);
        }
        if (old != next[i] && old) { ZHR_RayOwnGeometryDestroy(old); SDL_free(old); }
        s->parts[i] = next[i];
    }
    s->bottomRange.primitiveCount = total; // full packed material/triangle texture length
    if (anyChanged) s->built = false; // TLAS must refresh bounds even for stable BLAS handles
    ++s->splitUpdates;
    if(allocated[1]) ++s->dynamicAllocations;
    if(mappedVertex[1] && changed[1]) ++s->dynamicRebuildReuses;
    if(dynamicCountReuse) ++s->dynamicCountReuses;
    if(reserveFallback) ++s->reserveFallbacks;
    if(s->storageProfile && (s->splitUpdates==1 || s->splitUpdates%60==0)) {
        ZHR_SDLRayScene *part=s->parts[1];
        SDL_LogInfo(SDL_LOG_CATEGORY_GPU,"Water hardware dynamic storage: updates=%llu allocations=%llu rebuild-reuses=%llu count-reuses=%llu reserve-fallbacks=%llu active=%u/%u capacity=%u/%u",
            (unsigned long long)s->splitUpdates,(unsigned long long)s->dynamicAllocations,
            (unsigned long long)s->dynamicRebuildReuses,(unsigned long long)s->dynamicCountReuses,
            (unsigned long long)s->reserveFallbacks,part ? part->bottomGeometry.geometry.triangles.maxVertex+1 : 0,
            part ? part->bottomRange.primitiveCount : 0,part ? part->vertexCapacity : 0,part ? part->primitiveCapacity : 0);
    }
    if (staticChanged) *staticChanged = changed[0];
    if (dynamicChanged) *dynamicChanged = changed[1];
    return true;
fail:
    // Existing buffers and instance IDs were not modified; only prepared replacements retire.
    for (Uint32 i = 0; i < 2; ++i) {
        if (mappedIndex[i]) r->vkUnmapMemory(r->logicalDevice, s->parts[i]->index.memory);
        if (mappedVertex[i]) r->vkUnmapMemory(r->logicalDevice, s->parts[i]->vertex.memory);
        if (allocated[i]) { ZHR_RayOwnGeometryDestroy(next[i]); SDL_free(next[i]); }
    }
    ZHR_RayOwnGeometryDestroy(&topReplacement);
    return false;
}

bool ZHR_SDLRayGetWaterStorageStats(const ZHR_SDLRayScene *s, ZHR_SDLRayWaterStorageStats *stats)
{
    if (!s || !s->water || !stats || (s->waterPath && !s->split))
        return SDL_SetError("water storage statistics require a one-bounce or split water scene");
    SDL_zero(*stats);
    stats->dynamicAllocations=s->dynamicAllocations; stats->dynamicRebuildReuses=s->dynamicRebuildReuses;
    stats->dynamicCountReuses=s->dynamicCountReuses; stats->reserveFallbacks=s->reserveFallbacks;
    const ZHR_SDLRayScene *part = s->split ? s->parts[1] : s;
    if (part) {
        stats->activeVertices=part->bottomGeometry.geometry.triangles.maxVertex+1;
        stats->activePrimitives=part->bottomRange.primitiveCount;
        stats->vertexCapacity=part->vertexCapacity; stats->primitiveCapacity=part->primitiveCapacity;
        stats->allocationBytes = part->vertex.bytes + part->index.bytes + part->instance.bytes
            + part->blasBuffer.bytes + part->tlasBuffer.bytes + part->scratch.bytes;
    }
    return true;
}

ZHR_SDLRayScene *ZHR_SDLRayCreateWaterPathSplitScene(SDL_GPUDevice *device,
    const ZHR_SDLRayMeshView *staticMesh, const ZHR_SDLRayMeshView *dynamicMesh,
    const void *spirv, size_t spirvBytes)
{
    if (!ZHR_RayMeshViewValid(staticMesh) || !ZHR_RayMeshViewValid(dynamicMesh)) return NULL;
    const ZHR_SDLRayMeshView *first = staticMesh && staticMesh->indexCount ? staticMesh : dynamicMesh;
    if (!first || !first->indexCount) { SDL_SetError("split water path requires a nonempty mesh"); return NULL; }
    ZHR_SDLRayScene *s = ZHR_RayCreateScene(device, first->xyz, first->vertexCount, first->indices,
        first->indexCount, spirv, spirvBytes, true, true, true);
    if (!s) return NULL;
    if (!ZHR_SDLRayUpdateWaterPathSplitScene(s, staticMesh, dynamicMesh, NULL, NULL)) {
        ZHR_SDLRayDestroyScene(s); return NULL;
    }
    return s;
}

static void ZHR_RayBuildBarrier(VulkanRenderer *r, VkCommandBuffer command, VkPipelineStageFlags target)
{
    VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | (target == VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR ? VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR : VK_ACCESS_SHADER_READ_BIT);
    r->vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, target, 0, 1, &barrier, 0, NULL, 0, NULL);
}

static VulkanCommandBuffer *ZHR_RayCommand(ZHR_SDLRayScene *s, SDL_GPUCommandBuffer *command)
{
    /* The common device pointer is refreshed on every SDL command acquisition in all
       modes. Check it before interpreting any backend-specific command buffer fields. */
    CommandBufferCommonHeader *common = (CommandBufferCommonHeader *)command;
    if (common->device != s->device) {
        SDL_SetError("ray query requires a command buffer from the scene's SDL device"); return NULL;
    }
    VulkanCommandBuffer *cb = (VulkanCommandBuffer *)command;
    VulkanRenderer *r = s->renderer;
    if (cb->renderer != r || cb->commandBuffer == VK_NULL_HANDLE) {
        SDL_SetError("ray query requires a valid same-device Vulkan command buffer"); return NULL;
    }
    /* SDL resets submitted/pass bookkeeping only in debug mode. In release these
       flags may describe an earlier use of a recycled command buffer, so follow
       SDL_gpu.c's validation policy and the caller's outside-pass contract. */
    if (s->device->debug_mode && (common->submitted || common->render_pass.in_progress ||
        common->compute_pass.in_progress || common->copy_pass.in_progress)) {
        SDL_SetError("ray query requires an unsubmitted SDL command buffer outside passes"); return NULL;
    }
    return cb;
}

static bool ZHR_RayRecordSunBuildings(ZHR_SDLRayScene *s, VulkanCommandBuffer *cb)
{
    VulkanRenderer *r = s->renderer;
    ZHR_RaySunBuildings *sun = s->sunBuildings;
    VkAccelerationStructureBuildGeometryInfoKHR builds[ZHR_MaxSunBuildings];
    const VkAccelerationStructureBuildRangeInfoKHR *ranges[ZHR_MaxSunBuildings];
    Uint32 count = 0;
    for (Uint32 i = 0; i < ZHR_MaxSunBuildings; ++i) {
        const ZHR_RaySunBuilding *building = &sun->buildings[i];
        ZHR_SDLRayScene *part = building->scene;
        if (part && !part->built && building->lastUse == sun->update) {
            builds[count] = part->bottomBuild;
            ranges[count++] = &part->bottomRange;
            part->built = true;
        }
    }
    if (count) ZHR_VK(vkCmdBuildAccelerationStructuresKHR)(cb->commandBuffer, count, builds, ranges);
    sun->blasBuilds += count;
    return count != 0;
}

static void ZHR_RayRecordBuild(ZHR_SDLRayScene *s, VulkanCommandBuffer *cb)
{
    VulkanRenderer *r = s->renderer;
    if (s->split) {
        if (s->built) return;
        bool builtPart = false;
        if (s->sunBuildings) {
            builtPart = ZHR_RayRecordSunBuildings(s, cb);
            ++s->sunBuildings->tlasBuilds;
        } else {
            for (Uint32 i = 0; i < 2; ++i) {
                ZHR_SDLRayScene *part = s->parts[i];
                if (part && !part->built) {
                    const VkAccelerationStructureBuildRangeInfoKHR *range = &part->bottomRange;
                    ZHR_VK(vkCmdBuildAccelerationStructuresKHR)(cb->commandBuffer, 1, &part->bottomBuild, &range);
                    part->built = true; builtPart = true;
                }
            }
        }
        if (builtPart) ZHR_RayBuildBarrier(r, cb->commandBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);
        const VkAccelerationStructureBuildRangeInfoKHR *range = &s->topRange;
        ZHR_VK(vkCmdBuildAccelerationStructuresKHR)(cb->commandBuffer, 1, &s->topBuild, &range);
        ZHR_RayBuildBarrier(r, cb->commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        s->built = true;
        return;
    }
    if (!s->built) {
        const VkAccelerationStructureBuildRangeInfoKHR *bottom = &s->bottomRange, *top = &s->topRange;
        ZHR_VK(vkCmdBuildAccelerationStructuresKHR)(cb->commandBuffer, 1, &s->bottomBuild, &bottom);
        ZHR_RayBuildBarrier(r, cb->commandBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);
        ZHR_VK(vkCmdBuildAccelerationStructuresKHR)(cb->commandBuffer, 1, &s->topBuild, &top);
        ZHR_RayBuildBarrier(r, cb->commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        s->built = true;
    }
}

static bool ZHR_RayRecordSun(ZHR_SDLRayScene *s, SDL_GPUCommandBuffer *command, SDL_GPUTexture *depth,
    SDL_GPUTexture *mask, Uint32 width, Uint32 height, const ZHR_SDLRayParams *params,
    const Uint32 *rectangles, Uint32 rectangleCount)
{
    const bool bounded = rectangles != NULL;
    if (bounded) {
        if (!rectangleCount || rectangleCount > 4096) return SDL_SetError("invalid sun rectangle count");
        for (Uint32 i=0;i<rectangleCount;++i) {
            const Uint32 *rect = rectangles + i*4;
            if (!rect[2] || !rect[3] || rect[0] > width || rect[1] > height ||
                rect[2] > width-rect[0] || rect[3] > height-rect[1])
                return SDL_SetError("invalid sun dispatch rectangle");
        }
    }
    if (!s || s->water || !command || !depth || !mask || depth == mask || !params || !width || !height) return SDL_SetError("invalid ray query dispatch input");
    VulkanCommandBuffer *cb = ZHR_RayCommand(s, command);
    if (!cb) return false;
    VulkanRenderer *r = s->renderer;
    VulkanTextureContainer *dc = (VulkanTextureContainer *)depth, *mc = (VulkanTextureContainer *)mask;
    if (dc->header.info.format != SDL_GPU_TEXTUREFORMAT_R32_FLOAT || mc->header.info.format != SDL_GPU_TEXTUREFORMAT_R32_FLOAT ||
        dc->header.info.type != SDL_GPU_TEXTURETYPE_2D || mc->header.info.type != SDL_GPU_TEXTURETYPE_2D ||
        dc->header.info.sample_count != SDL_GPU_SAMPLECOUNT_1 || mc->header.info.sample_count != SDL_GPU_SAMPLECOUNT_1 ||
        dc->header.info.width != (bounded ? 1u : width) || dc->header.info.height != (bounded ? 1u : height) ||
        mc->header.info.width != width || mc->header.info.height != height ||
        !(dc->header.info.usage & SDL_GPU_TEXTUREUSAGE_SAMPLER) || !(mc->header.info.usage & (SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE)))
        return SDL_SetError("ray query requires matching 2D single-sample R32_FLOAT sampler depth and storage-write mask");
    VulkanTextureSubresource *ds = VULKAN_INTERNAL_FetchTextureSubresource(dc, 0, 0);
    VulkanTextureSubresource *ms = VULKAN_INTERNAL_FetchTextureSubresource(mc, 0, 0);
    ZHR_RayRecordBuild(s, cb);
    VULKAN_INTERNAL_TrackTexture(cb, ds->parent);
    VULKAN_INTERNAL_TrackTexture(cb, ms->parent);
    VULKAN_INTERNAL_TextureSubresourceTransitionFromDefaultUsage(r, cb, VULKAN_TEXTURE_USAGE_MODE_COMPUTE_STORAGE_READ, ds);
    VULKAN_INTERNAL_TextureSubresourceTransitionFromDefaultUsage(r, cb, VULKAN_TEXTURE_USAGE_MODE_COMPUTE_STORAGE_READ_WRITE, ms);
    VkWriteDescriptorSetAccelerationStructureKHR as = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
    as.accelerationStructureCount = 1; as.pAccelerationStructures = &s->tlas;
    VkDescriptorImageInfo images[2] = {{s->sampler, ds->parent->fullView, VK_IMAGE_LAYOUT_GENERAL}, {VK_NULL_HANDLE, ms->computeWriteView, VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet writes[3]; SDL_zeroa(writes);
    for (Uint32 i=0;i<3;++i) { writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[i].dstSet=s->set; writes[i].dstBinding=i; writes[i].descriptorCount=1; }
    writes[0].descriptorType=VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR; writes[0].pNext=&as;
    writes[1].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[1].pImageInfo=&images[0];
    writes[2].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; writes[2].pImageInfo=&images[1];
    r->vkUpdateDescriptorSets(r->logicalDevice, 3, writes, 0, NULL);
    r->vkCmdBindPipeline(cb->commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, s->pipeline);
    r->vkCmdBindDescriptorSets(cb->commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, s->layout, 0, 1, &s->set, 0, NULL);
    ZHR_SDLRayParams push = *params; push.width=width; push.height=height;
    for (Uint32 i=0;i<(bounded ? rectangleCount : 1u);++i) {
        Uint32 dispatchWidth=width, dispatchHeight=height;
        if (bounded) {
            const Uint32 *rect=rectangles+i*4;
            push.padding[0]=1.0f;
            push.padding[1]=(float)rect[0]; push.padding[2]=(float)rect[1];
            push.padding[3]=(float)rect[2]; push.padding[4]=(float)rect[3];
            dispatchWidth=rect[2]; dispatchHeight=rect[3];
        }
        ZHR_VK(vkCmdPushConstants)(cb->commandBuffer, s->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        r->vkCmdDispatch(cb->commandBuffer, (dispatchWidth+7)/8, (dispatchHeight+7)/8, 1);
    }
    VULKAN_INTERNAL_TextureSubresourceTransitionToDefaultUsage(r, cb, VULKAN_TEXTURE_USAGE_MODE_COMPUTE_STORAGE_READ, ds);
    VULKAN_INTERNAL_TextureSubresourceTransitionToDefaultUsage(r, cb, VULKAN_TEXTURE_USAGE_MODE_COMPUTE_STORAGE_READ_WRITE, ms);
    return true;
}

bool ZHR_SDLRayRecord(ZHR_SDLRayScene *s, SDL_GPUCommandBuffer *command, SDL_GPUTexture *depth,
    SDL_GPUTexture *mask, Uint32 width, Uint32 height, const ZHR_SDLRayParams *params)
{
    return ZHR_RayRecordSun(s,command,depth,mask,width,height,params,NULL,0);
}

bool ZHR_SDLRayRecordSunDepth(ZHR_SDLRayScene *s, SDL_GPUCommandBuffer *command, SDL_GPUTexture *depth,
    SDL_GPUTexture *mask, Uint32 width, Uint32 height, const ZHR_SDLRayParams *params,
    const Uint32 *rectangles, Uint32 rectangleCount)
{
    if (!rectangles) return SDL_SetError("missing sun dispatch rectangles");
    return ZHR_RayRecordSun(s,command,depth,mask,width,height,params,rectangles,rectangleCount);
}

bool ZHR_SDLRayRecordWater(ZHR_SDLRayScene *s, SDL_GPUCommandBuffer *command, SDL_GPUTexture *hits,
    Uint32 width, Uint32 height, const ZHR_SDLRayWaterParams *params)
{
    if (!s || !s->water || s->waterPath || !command || !hits || !params || !width || !height)
        return SDL_SetError("invalid water ray query dispatch input");
    VulkanCommandBuffer *cb = ZHR_RayCommand(s, command);
    if (!cb) return false;
    VulkanRenderer *r = s->renderer;
    VulkanTextureContainer *hc = (VulkanTextureContainer *)hits;
    if (hc->header.info.format != SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT ||
        hc->header.info.type != SDL_GPU_TEXTURETYPE_2D || hc->header.info.sample_count != SDL_GPU_SAMPLECOUNT_1 ||
        hc->header.info.width != width || hc->header.info.height != height ||
        !(hc->header.info.usage & (SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE)))
        return SDL_SetError("water ray query requires matching 2D single-sample RGBA32_FLOAT storage-write output");
    VulkanTextureSubresource *hs = VULKAN_INTERNAL_FetchTextureSubresource(hc, 0, 0);
    ZHR_RayRecordBuild(s, cb);
    VULKAN_INTERNAL_TrackTexture(cb, hs->parent);
    VULKAN_INTERNAL_TextureSubresourceTransitionFromDefaultUsage(r, cb, VULKAN_TEXTURE_USAGE_MODE_COMPUTE_STORAGE_READ_WRITE, hs);
    VkDescriptorImageInfo image = { VK_NULL_HANDLE, hs->computeWriteView, VK_IMAGE_LAYOUT_GENERAL };
    VkWriteDescriptorSetAccelerationStructureKHR as = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
    as.accelerationStructureCount = 1; as.pAccelerationStructures = &s->tlas;
    VkWriteDescriptorSet writes[2]; SDL_zeroa(writes);
    for (Uint32 i = 0; i < 2; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = s->set; writes[i].dstBinding = i; writes[i].descriptorCount = 1;
    }
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; writes[0].pImageInfo = &image;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR; writes[1].pNext = &as;
    r->vkUpdateDescriptorSets(r->logicalDevice, 2, writes, 0, NULL);
    r->vkCmdBindPipeline(cb->commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, s->pipeline);
    r->vkCmdBindDescriptorSets(cb->commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, s->layout, 0, 1, &s->set, 0, NULL);
    ZHR_VK(vkCmdPushConstants)(cb->commandBuffer, s->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(*params), params);
    r->vkCmdDispatch(cb->commandBuffer, (width + 7) / 8, (height + 7) / 8, 1);
    VULKAN_INTERNAL_TextureSubresourceTransitionToDefaultUsage(r, cb, VULKAN_TEXTURE_USAGE_MODE_COMPUTE_STORAGE_READ_WRITE, hs);
    return true;
}

bool ZHR_SDLRayRecordWaterPath(ZHR_SDLRayScene *s, SDL_GPUCommandBuffer *command, SDL_GPUTexture *triangles,
    SDL_GPUTexture *primaryHits, SDL_GPUTexture *transportHits, Uint32 width, Uint32 height,
    const ZHR_SDLRayWaterPathParams *params)
{
    if (!s || !s->waterPath || (s->split && !s->bottomRange.primitiveCount) || !command || !triangles || !primaryHits || !transportHits ||
        triangles == primaryHits || triangles == transportHits || primaryHits == transportHits ||
        !params || !width || !height)
        return SDL_SetError("invalid water path ray query dispatch input");
    VulkanCommandBuffer *cb = ZHR_RayCommand(s, command);
    if (!cb) return false;
    VulkanRenderer *r = s->renderer;
    VulkanTextureContainer *tc = (VulkanTextureContainer *)triangles;
    VulkanTextureContainer *pc = (VulkanTextureContainer *)primaryHits;
    VulkanTextureContainer *sc = (VulkanTextureContainer *)transportHits;
    if (tc->header.info.format != SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT ||
        tc->header.info.type != SDL_GPU_TEXTURETYPE_2D || tc->header.info.sample_count != SDL_GPU_SAMPLECOUNT_1 ||
        !(tc->header.info.usage & SDL_GPU_TEXTUREUSAGE_SAMPLER) ||
        (Uint64)tc->header.info.width * tc->header.info.height < (Uint64)s->bottomRange.primitiveCount * 4)
        return SDL_SetError("water path requires an RGBA32_FLOAT sampler with four texels per triangle");
    VulkanTextureContainer *outputs[2] = { pc, sc };
    for (Uint32 i = 0; i < 2; ++i) {
        const SDL_GPUTextureCreateInfo *info = &outputs[i]->header.info;
        if (info->format != SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT || info->type != SDL_GPU_TEXTURETYPE_2D ||
            info->sample_count != SDL_GPU_SAMPLECOUNT_1 || info->width != width || info->height != height ||
            !(info->usage & (SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE)))
            return SDL_SetError("water path requires matching RGBA32_FLOAT storage-write outputs");
    }
    VulkanTextureSubresource *ts = VULKAN_INTERNAL_FetchTextureSubresource(tc, 0, 0);
    VulkanTextureSubresource *ps = VULKAN_INTERNAL_FetchTextureSubresource(pc, 0, 0);
    VulkanTextureSubresource *ss = VULKAN_INTERNAL_FetchTextureSubresource(sc, 0, 0);
    if (s->profilePool) {
        ZHR_RayProfileRead(s); // caller already waited the preceding query fence
        ZHR_VK(vkCmdResetQueryPool)(cb->commandBuffer, s->profilePool, 0, 4);
        s->profileHadBuild = !s->built;
    }
    ZHR_RayProfileStamp(s, cb, 0, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);
    ZHR_RayRecordBuild(s, cb);
    ZHR_RayProfileStamp(s, cb, 1, 0);
    VULKAN_INTERNAL_TrackTexture(cb, ts->parent);
    VULKAN_INTERNAL_TrackTexture(cb, ps->parent);
    VULKAN_INTERNAL_TrackTexture(cb, ss->parent);
    VULKAN_INTERNAL_TextureSubresourceTransitionFromDefaultUsage(r, cb, VULKAN_TEXTURE_USAGE_MODE_COMPUTE_STORAGE_READ, ts);
    VULKAN_INTERNAL_TextureSubresourceTransitionFromDefaultUsage(r, cb, VULKAN_TEXTURE_USAGE_MODE_COMPUTE_STORAGE_READ_WRITE, ps);
    VULKAN_INTERNAL_TextureSubresourceTransitionFromDefaultUsage(r, cb, VULKAN_TEXTURE_USAGE_MODE_COMPUTE_STORAGE_READ_WRITE, ss);
    VkDescriptorImageInfo images[3] = {
        { VK_NULL_HANDLE, ps->computeWriteView, VK_IMAGE_LAYOUT_GENERAL },
        { s->sampler, ts->parent->fullView, VK_IMAGE_LAYOUT_GENERAL },
        { VK_NULL_HANDLE, ss->computeWriteView, VK_IMAGE_LAYOUT_GENERAL }
    };
    VkWriteDescriptorSetAccelerationStructureKHR as = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
    as.accelerationStructureCount = 1; as.pAccelerationStructures = &s->tlas;
    VkWriteDescriptorSet writes[4]; SDL_zeroa(writes);
    for (Uint32 i = 0; i < 4; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = s->set; writes[i].dstBinding = i; writes[i].descriptorCount = 1;
    }
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; writes[0].pImageInfo = &images[0];
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR; writes[1].pNext = &as;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[2].pImageInfo = &images[1];
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; writes[3].pImageInfo = &images[2];
    r->vkUpdateDescriptorSets(r->logicalDevice, 4, writes, 0, NULL);
    r->vkCmdBindPipeline(cb->commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, s->pipeline);
    r->vkCmdBindDescriptorSets(cb->commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, s->layout, 0, 1, &s->set, 0, NULL);
    ZHR_VK(vkCmdPushConstants)(cb->commandBuffer, s->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(*params), params);
    ZHR_RayProfileStamp(s, cb, 2, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    r->vkCmdDispatch(cb->commandBuffer, (width + 7) / 8, (height + 7) / 8, 1);
    ZHR_RayProfileStamp(s, cb, 3, 0);
    if (s->profilePool) s->profilePending = true;
    VULKAN_INTERNAL_TextureSubresourceTransitionToDefaultUsage(r, cb, VULKAN_TEXTURE_USAGE_MODE_COMPUTE_STORAGE_READ, ts);
    VULKAN_INTERNAL_TextureSubresourceTransitionToDefaultUsage(r, cb, VULKAN_TEXTURE_USAGE_MODE_COMPUTE_STORAGE_READ_WRITE, ps);
    VULKAN_INTERNAL_TextureSubresourceTransitionToDefaultUsage(r, cb, VULKAN_TEXTURE_USAGE_MODE_COMPUTE_STORAGE_READ_WRITE, ss);
    return true;
}
#undef ZHR_VK

#include "SDL_gpu_vulkan_water_ray_filter.inc"
