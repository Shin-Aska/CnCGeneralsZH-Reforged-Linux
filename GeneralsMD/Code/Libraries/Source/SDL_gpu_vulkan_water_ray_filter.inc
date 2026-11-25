/* Water depth ray queries use the same Vulkan device and queue as SDL_GPU. */
#include "SDL_gpu_vulkan_water_ray_filter.h"
#include <math.h>
#define ZHR_WaterRay_VK(name) ((PFN_##name)r->vkGetDeviceProcAddr(r->logicalDevice,#name))
SDL_COMPILE_TIME_ASSERT(ZHR_WaterRay_Push128,sizeof(ZHR_WaterRay_Params)==128);
enum { ZHR_WaterRay_RefitsPerBuild = 63 };
typedef struct ZHR_WaterRay_Descriptor {VkDescriptorSet set;Uint64 serial;bool pending;} ZHR_WaterRay_Descriptor;
struct ZHR_WaterRay_State {
    ZHR_SDLRayScene gpu;
    ZHR_WaterRay_Limits limits;
    ZHR_WaterRay_Stats stats;
    ZHR_WaterRay_Descriptor descriptors[8];
    SDL_GPUCommandBuffer *open;
    VkDeviceSize scratchAlignment;
    VkDeviceAddress tlasAddress;
    Uint32 committedWidth, committedHeight, pendingWidth, pendingHeight;
    Uint32 refitsSinceBuild;
    bool pendingBottomRefit;
    bool poisoned;
};
static bool ZHR_WaterRay_CheckDevice(SDL_GPUDevice *device,VkPhysicalDeviceSubgroupProperties *subgroup,
    VkPhysicalDeviceAccelerationStructurePropertiesKHR *as,char *diagnostic,size_t size) {
    if(!ZHR_SDLRayQuerySupported(device,diagnostic,size))return false;
    VulkanRenderer *r=(VulkanRenderer *)device->driverData;
    SDL_zero(*subgroup);subgroup->sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
    SDL_zero(*as);as->sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
    subgroup->pNext=as;
    VkPhysicalDeviceProperties2 properties={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};properties.pNext=subgroup;
    r->vkGetPhysicalDeviceProperties2KHR(r->physicalDevice,&properties);
    const VkSubgroupFeatureFlags required=VK_SUBGROUP_FEATURE_BASIC_BIT|VK_SUBGROUP_FEATURE_ARITHMETIC_BIT;
    const VkPhysicalDeviceLimits *limits=&r->physicalDeviceProperties.properties.limits;
    const bool supported=(subgroup->supportedStages&VK_SHADER_STAGE_COMPUTE_BIT) &&
        (subgroup->supportedOperations&required)==required && subgroup->subgroupSize &&
        limits->maxComputeWorkGroupInvocations>=256 && limits->maxComputeWorkGroupSize[0]>=16 &&
        limits->maxComputeWorkGroupSize[1]>=16 && limits->maxComputeSharedMemorySize >=8192 &&
        as->maxGeometryCount>=1 && as->maxInstanceCount>=1 && as->maxPrimitiveCount>=1;
    if(diagnostic&&size)SDL_snprintf(diagnostic,size,
        "water reflection ray filter Vulkan: %s subgroup-size=%u stages=0x%x operations=0x%x required=BASIC|ARITHMETIC local=16x16 shared=8192 supported=%d",
        r->physicalDeviceProperties.properties.deviceName,subgroup->subgroupSize,subgroup->supportedStages,subgroup->supportedOperations,(int)supported);
    return supported;
}
bool ZHR_WaterRay_Supported(SDL_GPUDevice *device,char *diagnostic,size_t size) {
    VkPhysicalDeviceSubgroupProperties subgroup;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR as;
    return ZHR_WaterRay_CheckDevice(device,&subgroup,&as,diagnostic,size);
}
static bool ZHR_WaterRay_Buffer(ZHR_WaterRay_State *s,ZHR_RayBuffer *b,VkDeviceSize size,VkBufferUsageFlags usage,const void *data) {
    VulkanRenderer *r=s->gpu.renderer;
    VkBufferCreateInfo create={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    create.size=size;create.usage=usage|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;create.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    if(r->vkCreateBuffer(r->logicalDevice,&create,NULL,&b->buffer)!=VK_SUCCESS)return SDL_SetError("water reflection ray filter buffer creation failed");
    VkMemoryRequirements req;r->vkGetBufferMemoryRequirements(r->logicalDevice,b->buffer,&req);
    if(req.size>s->limits.maxBytes-s->stats.allocatedBytes)return SDL_SetError("water reflection ray filter allocation byte cap exceeded");
    const VkMemoryPropertyFlags flags=data?VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    Uint32 type;
    for(type=0;type<r->memoryProperties.memoryTypeCount;++type)
        if((req.memoryTypeBits&(1u<<type)) && (r->memoryProperties.memoryTypes[type].propertyFlags&flags)==flags)break;
    if(type==r->memoryProperties.memoryTypeCount)return SDL_SetError("water reflection ray filter compatible memory unavailable");
    VkMemoryAllocateFlagsInfo af={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};af.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.pNext=&af;ai.allocationSize=req.size;ai.memoryTypeIndex=type;
    if(r->vkAllocateMemory(r->logicalDevice,&ai,NULL,&b->memory)!=VK_SUCCESS)return SDL_SetError("water reflection ray filter memory allocation failed");
    s->stats.allocatedBytes+=req.size;
    if(r->vkBindBufferMemory(r->logicalDevice,b->buffer,b->memory,0)!=VK_SUCCESS)return SDL_SetError("water reflection ray filter bind failed");
    if(data){
        void *mapped=NULL;
        if(r->vkMapMemory(r->logicalDevice,b->memory,0,size,0,&mapped)!=VK_SUCCESS)return SDL_SetError("water reflection ray filter immutable instance mapping failed");
        SDL_memcpy(mapped,data,size);r->vkUnmapMemory(r->logicalDevice,b->memory);
    }
    VkBufferDeviceAddressInfo address={VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};address.buffer=b->buffer;
    b->address=ZHR_WaterRay_VK(vkGetBufferDeviceAddress)(r->logicalDevice,&address);
    return b->address!=0 || SDL_SetError("water reflection ray filter null buffer address");
}
static bool ZHR_WaterRay_AS(ZHR_WaterRay_State *s,ZHR_RayBuffer *buffer,VkAccelerationStructureKHR *as,
    VkAccelerationStructureBuildGeometryInfoKHR *build,Uint32 count,VkDeviceSize *scratch) {
    VulkanRenderer *r=s->gpu.renderer;
    VkAccelerationStructureBuildSizesInfoKHR sizes={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    ZHR_WaterRay_VK(vkGetAccelerationStructureBuildSizesKHR)(r->logicalDevice,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,build,&count,&sizes);
    if(!ZHR_WaterRay_Buffer(s,buffer,sizes.accelerationStructureSize,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,NULL))return false;
    VkAccelerationStructureCreateInfoKHR create={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    create.buffer=buffer->buffer;create.size=sizes.accelerationStructureSize;create.type=build->type;
    if(ZHR_WaterRay_VK(vkCreateAccelerationStructureKHR)(r->logicalDevice,&create,NULL,as)!=VK_SUCCESS)return SDL_SetError("water reflection ray filter AS creation failed");
    build->dstAccelerationStructure=*as;
    *scratch=SDL_max(*scratch,sizes.buildScratchSize);
    if (build->flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR)
        *scratch=SDL_max(*scratch,sizes.updateScratchSize);
    return true;
}
void ZHR_WaterRay_DestroyAfterIdle(ZHR_WaterRay_State *s) {
    if(!s)return;
    VulkanRenderer *r=s->gpu.renderer;
    if(s->gpu.pipeline)r->vkDestroyPipeline(r->logicalDevice,s->gpu.pipeline,NULL);
    if(s->gpu.layout)r->vkDestroyPipelineLayout(r->logicalDevice,s->gpu.layout,NULL);
    if(s->gpu.pool)r->vkDestroyDescriptorPool(r->logicalDevice,s->gpu.pool,NULL);
    if(s->gpu.setLayout)r->vkDestroyDescriptorSetLayout(r->logicalDevice,s->gpu.setLayout,NULL);
    if(s->gpu.sampler)r->vkDestroySampler(r->logicalDevice,s->gpu.sampler,NULL);
    ZHR_RayOwnGeometryDestroy(&s->gpu);SDL_free(s);
}
ZHR_WaterRay_State *ZHR_WaterRay_Create(SDL_GPUDevice *device,const void *spirv,size_t spirvBytes,const ZHR_WaterRay_Limits *limits) {
    if(!limits || !limits->maxWidth || !limits->maxHeight || !limits->descriptorSlots || limits->descriptorSlots>8 ||
       !limits->maxBytes || !spirv || !spirvBytes || spirvBytes%4){SDL_SetError("water reflection ray filter invalid limits/shader");return NULL;}
    VkPhysicalDeviceSubgroupProperties subgroup;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR as;
    char diagnostic[512];
    if(!ZHR_WaterRay_CheckDevice(device,&subgroup,&as,diagnostic,sizeof(diagnostic))){SDL_SetError("%s",diagnostic);return NULL;}
    VulkanRenderer *r=(VulkanRenderer *)device->driverData;
    const VkPhysicalDeviceLimits *hardware=&r->physicalDeviceProperties.properties.limits;
    const Uint64 columns=((Uint64)limits->maxWidth+31)/32,rows=((Uint64)limits->maxHeight+31)/32,tiles=columns*rows;
    if(limits->maxWidth>hardware->maxImageDimension2D || limits->maxHeight>hardware->maxImageDimension2D ||
       columns>hardware->maxComputeWorkGroupCount[0] || rows>hardware->maxComputeWorkGroupCount[1] ||
       tiles>as.maxPrimitiveCount || tiles>SDL_MAX_UINT32 || tiles*32>hardware->maxStorageBufferRange){
        SDL_SetError("water reflection ray filter extent exceeds AS/storage/dispatch limits");return NULL;
    }
    ZHR_WaterRay_State *s=SDL_calloc(1,sizeof(*s));if(!s)return NULL;
    s->gpu.device=device;s->gpu.renderer=r;s->limits=*limits;
    s->stats.capacityTiles=(Uint32)tiles;s->stats.subgroupSize=subgroup.subgroupSize;s->stats.subgroupOperations=subgroup.supportedOperations;
    s->scratchAlignment=as.minAccelerationStructureScratchOffsetAlignment;
    if(!ZHR_WaterRay_Buffer(s,&s->gpu.vertex,tiles*32,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,NULL))goto fail;
    s->gpu.bottomGeometry.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    s->gpu.bottomGeometry.geometryType=VK_GEOMETRY_TYPE_AABBS_KHR;
    s->gpu.bottomGeometry.flags=VK_GEOMETRY_OPAQUE_BIT_KHR;
    s->gpu.bottomGeometry.geometry.aabbs.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
    s->gpu.bottomGeometry.geometry.aabbs.data.deviceAddress=s->gpu.vertex.address;s->gpu.bottomGeometry.geometry.aabbs.stride=32;
    s->gpu.bottomBuild.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    s->gpu.bottomBuild.type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    s->gpu.bottomBuild.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
        VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    s->gpu.bottomBuild.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    s->gpu.bottomBuild.geometryCount=1;s->gpu.bottomBuild.pGeometries=&s->gpu.bottomGeometry;
    VkDeviceSize scratch=0;
    if(!ZHR_WaterRay_AS(s,&s->gpu.blasBuffer,&s->gpu.blas,&s->gpu.bottomBuild,(Uint32)tiles,&scratch))goto fail;
    VkAccelerationStructureDeviceAddressInfoKHR address={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};address.accelerationStructure=s->gpu.blas;
    VkAccelerationStructureInstanceKHR instance;SDL_zero(instance);
    instance.transform.matrix[0][0]=instance.transform.matrix[1][1]=instance.transform.matrix[2][2]=1;
    instance.mask=255;instance.accelerationStructureReference=ZHR_WaterRay_VK(vkGetAccelerationStructureDeviceAddressKHR)(r->logicalDevice,&address);
    if(!instance.accelerationStructureReference){SDL_SetError("water reflection ray filter null BLAS address");goto fail;}
    if(!ZHR_WaterRay_Buffer(s,&s->gpu.instance,sizeof(instance),VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,&instance))goto fail;
    s->gpu.topGeometry.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;s->gpu.topGeometry.geometryType=VK_GEOMETRY_TYPE_INSTANCES_KHR;
    s->gpu.topGeometry.geometry.instances.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    s->gpu.topGeometry.geometry.instances.data.deviceAddress=s->gpu.instance.address;
    s->gpu.topBuild.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    s->gpu.topBuild.type=VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    s->gpu.topBuild.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
        VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    s->gpu.topBuild.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    s->gpu.topBuild.geometryCount=1;s->gpu.topBuild.pGeometries=&s->gpu.topGeometry;s->gpu.topRange.primitiveCount=1;
    if(!ZHR_WaterRay_AS(s,&s->gpu.tlasBuffer,&s->gpu.tlas,&s->gpu.topBuild,1,&scratch))goto fail;
    if(!ZHR_WaterRay_Buffer(s,&s->gpu.scratch,scratch+s->scratchAlignment,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,NULL))goto fail;
    s->gpu.bottomBuild.scratchData.deviceAddress=s->gpu.topBuild.scratchData.deviceAddress=(s->gpu.scratch.address+s->scratchAlignment-1)&~(s->scratchAlignment-1);
    address.accelerationStructure=s->gpu.tlas;s->tlasAddress=ZHR_WaterRay_VK(vkGetAccelerationStructureDeviceAddressKHR)(r->logicalDevice,&address);
    if(!s->tlasAddress){SDL_SetError("water reflection ray filter null TLAS address");goto fail;}
    VkDescriptorSetLayoutBinding bindings[2]={{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL},{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL}};
    VkDescriptorSetLayoutCreateInfo sl={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};sl.bindingCount=2;sl.pBindings=bindings;
    if(r->vkCreateDescriptorSetLayout(r->logicalDevice,&sl,NULL,&s->gpu.setLayout)!=VK_SUCCESS)goto native_fail;
    VkDescriptorPoolSize poolSizes[2]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,limits->descriptorSlots},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,limits->descriptorSlots}};
    VkDescriptorPoolCreateInfo pool={VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=limits->descriptorSlots;pool.poolSizeCount=2;pool.pPoolSizes=poolSizes;
    if(r->vkCreateDescriptorPool(r->logicalDevice,&pool,NULL,&s->gpu.pool)!=VK_SUCCESS)goto native_fail;
    for(Uint32 i=0;i<limits->descriptorSlots;++i){
        VkDescriptorSetAllocateInfo alloc={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};alloc.descriptorPool=s->gpu.pool;alloc.descriptorSetCount=1;alloc.pSetLayouts=&s->gpu.setLayout;
        if(r->vkAllocateDescriptorSets(r->logicalDevice,&alloc,&s->descriptors[i].set)!=VK_SUCCESS)goto native_fail;
    }
    VkPushConstantRange push={VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(ZHR_WaterRay_Params)};
    VkPipelineLayoutCreateInfo layout={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&s->gpu.setLayout;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;
    if(r->vkCreatePipelineLayout(r->logicalDevice,&layout,NULL,&s->gpu.layout)!=VK_SUCCESS)goto native_fail;
    VkSamplerCreateInfo sampler={VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.magFilter=sampler.minFilter=VK_FILTER_NEAREST;
    sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if(r->vkCreateSampler(r->logicalDevice,&sampler,NULL,&s->gpu.sampler)!=VK_SUCCESS)goto native_fail;
    VkShaderModuleCreateInfo module={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};module.codeSize=spirvBytes;module.pCode=spirv;
    VkShaderModule shader=VK_NULL_HANDLE;if(r->vkCreateShaderModule(r->logicalDevice,&module,NULL,&shader)!=VK_SUCCESS)goto native_fail;
    VkComputePipelineCreateInfo pipeline={VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pipeline.layout=s->gpu.layout;
    pipeline.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pipeline.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline.stage.module=shader;pipeline.stage.pName="main";
    VkResult result=r->vkCreateComputePipelines(r->logicalDevice,VK_NULL_HANDLE,1,&pipeline,NULL,&s->gpu.pipeline);
    r->vkDestroyShaderModule(r->logicalDevice,shader,NULL);if(result!=VK_SUCCESS)goto native_fail;
    return s;
native_fail:
    SDL_SetError("water reflection ray filter native pipeline/descriptor allocation failed");
fail:
    ZHR_WaterRay_DestroyAfterIdle(s);return NULL;
}
static bool ZHR_WaterRay_OwnsRegion(VulkanRenderer *r,const VulkanMemoryUsedRegion *region){
    if(!region||!region->allocation)return false;
    for(Uint32 i=0;i<r->memoryProperties.memoryTypeCount;++i)if(region->allocation->allocator==&r->memoryAllocator->subAllocators[i])return true;
    return false;
}
bool ZHR_WaterRay_Record(ZHR_WaterRay_State *s,SDL_GPUCommandBuffer *command,SDL_GPUTexture *depth,Uint32 width,Uint32 height,const ZHR_WaterRay_Params *params,Uint32 out[4]){
    if(out)SDL_memset(out,0,4*sizeof(*out));
    if(!s||!command||!depth||!params||!out)return SDL_SetError("water reflection ray filter missing record argument");
    ++s->stats.records;
    if(s->poisoned || (s->open&&s->open!=command))return SDL_SetError("water reflection ray filter poisoned or different uncommitted CB");
    VulkanCommandBuffer *cb=ZHR_RayCommand(&s->gpu,command);if(!cb)return false;
    if(!width||!height||width>s->limits.maxWidth||height>s->limits.maxHeight)return SDL_SetError("water reflection ray filter record exceeds fixed extent capacity");
    for(Uint32 i=0;i<32;++i)if(!isfinite(((const float *)params)[i]))return SDL_SetError("water reflection ray filter nonfinite parameters");
    if(!(params->viewport[2]>0&&params->viewport[3]>0))return SDL_SetError("water reflection ray filter invalid viewport");
    VulkanTextureContainer *tc=(VulkanTextureContainer *)depth;
    if(tc->header.info.type!=SDL_GPU_TEXTURETYPE_2D||tc->header.info.format!=SDL_GPU_TEXTUREFORMAT_R32_FLOAT||tc->header.info.sample_count!=SDL_GPU_SAMPLECOUNT_1||
       tc->header.info.width!=width||tc->header.info.height!=height||!(tc->header.info.usage&SDL_GPU_TEXTUREUSAGE_SAMPLER))return SDL_SetError("water reflection ray filter input must be exact-extent 2D R32F sampled depth");
    ZHR_WaterRay_Descriptor *descriptor=NULL;
    for(Uint32 i=0;i<s->limits.descriptorSlots;++i)if(!s->descriptors[i].pending&&s->descriptors[i].serial<=s->stats.completedSerial){descriptor=s->descriptors+i;break;}
    if(!descriptor){++s->stats.descriptorRefusals;return SDL_SetError("water reflection ray filter fill descriptor slots still pending");}
    VulkanRenderer *r=s->gpu.renderer;
    SDL_LockRWLockForReading(r->defragLock);
    if(!tc->activeTexture||!ZHR_WaterRay_OwnsRegion(r,tc->activeTexture->usedRegion)){SDL_UnlockRWLock(r->defragLock);return SDL_SetError("water reflection ray filter foreign depth texture");}
    VulkanTextureSubresource *sub=VULKAN_INTERNAL_FetchTextureSubresource(tc,0,0);
    /* All fallible preparation ends here. Descriptor is completed/unborrowed,
     * and instance/input/AS handles were allocated once, never CPU-mutated. */
    VkDescriptorImageInfo image={s->gpu.sampler,sub->parent->fullView,VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorBufferInfo buffer={s->gpu.vertex.buffer,0,(VkDeviceSize)s->stats.capacityTiles*32};
    VkWriteDescriptorSet writes[2];SDL_zeroa(writes);
    for(Uint32 i=0;i<2;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=descriptor->set;writes[i].dstBinding=i;writes[i].descriptorCount=1;}
    writes[0].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[0].pImageInfo=&image;
    writes[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[1].pBufferInfo=&buffer;
    r->vkUpdateDescriptorSets(r->logicalDevice,2,writes,0,NULL);
    VkMemoryBarrier previous={VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    previous.srcAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR|VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    previous.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR|VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    r->vkCmdPipelineBarrier(cb->commandBuffer,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,0,1,&previous,0,NULL,0,NULL);
    VULKAN_INTERNAL_TrackTexture(cb,sub->parent);
    VULKAN_INTERNAL_TextureSubresourceTransitionFromDefaultUsage(r,cb,VULKAN_TEXTURE_USAGE_MODE_COMPUTE_STORAGE_READ,sub);
    r->vkCmdBindPipeline(cb->commandBuffer,VK_PIPELINE_BIND_POINT_COMPUTE,s->gpu.pipeline);
    r->vkCmdBindDescriptorSets(cb->commandBuffer,VK_PIPELINE_BIND_POINT_COMPUTE,s->gpu.layout,0,1,&descriptor->set,0,NULL);
    ZHR_WaterRay_VK(vkCmdPushConstants)(cb->commandBuffer,s->gpu.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(*params),params);
    const Uint32 columns=(width+31)/32,rows=(height+31)/32;
    r->vkCmdDispatch(cb->commandBuffer,columns,rows,1);
    VULKAN_INTERNAL_TextureSubresourceTransitionToDefaultUsage(r,cb,VULKAN_TEXTURE_USAGE_MODE_COMPUTE_STORAGE_READ,sub);
    SDL_UnlockRWLock(r->defragLock);
    VkMemoryBarrier fill={VK_STRUCTURE_TYPE_MEMORY_BARRIER};fill.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;fill.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    r->vkCmdPipelineBarrier(cb->commandBuffer,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,1,&fill,0,NULL,0,NULL);
    s->gpu.bottomRange.primitiveCount=columns*rows;
    // The tile shader keeps every primitive active. Rebuild for a changed
    // extent, and periodically refresh the tree as depth moves during a pan.
    s->pendingWidth=width;
    s->pendingHeight=height;
    s->pendingBottomRefit=!s->open && s->committedWidth==width && s->committedHeight==height
        && s->refitsSinceBuild<ZHR_WaterRay_RefitsPerBuild;
    s->gpu.bottomBuild.mode=s->pendingBottomRefit
        ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    s->gpu.bottomBuild.srcAccelerationStructure=s->pendingBottomRefit ? s->gpu.blas : VK_NULL_HANDLE;
    // The top level always has the same single instance and only needs its
    // bounds updated after the first submitted build.
    const bool topRefit=s->stats.committedSerial!=0;
    s->gpu.topBuild.mode=topRefit
        ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    s->gpu.topBuild.srcAccelerationStructure=topRefit ? s->gpu.tlas : VK_NULL_HANDLE;
    const VkAccelerationStructureBuildRangeInfoKHR *bottom=&s->gpu.bottomRange,*top=&s->gpu.topRange;
    ZHR_WaterRay_VK(vkCmdBuildAccelerationStructuresKHR)(cb->commandBuffer,1,&s->gpu.bottomBuild,&bottom);
    ZHR_RayBuildBarrier(r,cb->commandBuffer,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);
    ZHR_WaterRay_VK(vkCmdBuildAccelerationStructuresKHR)(cb->commandBuffer,1,&s->gpu.topBuild,&top);
    VkMemoryBarrier ready={VK_STRUCTURE_TYPE_MEMORY_BARRIER};ready.srcAccessMask=VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;ready.dstAccessMask=VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    r->vkCmdPipelineBarrier(cb->commandBuffer,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,1,&ready,0,NULL,0,NULL);
    s->open=command;descriptor->pending=true;++s->stats.successfulRecords;++s->stats.pendingDescriptors;s->stats.lastTileCount=columns*rows;
    out[0]=(Uint32)s->tlasAddress;out[1]=(Uint32)(s->tlasAddress>>32);
    out[2]=(Uint32)s->gpu.vertex.address;out[3]=(Uint32)(s->gpu.vertex.address>>32);return true;
}
bool ZHR_WaterRay_Commit(ZHR_WaterRay_State *s,SDL_GPUCommandBuffer *command,Uint64 serial){
    if(!s||s->poisoned||s->open!=command||serial<=s->stats.committedSerial)return SDL_SetError("water reflection ray filter invalid commit");
    for(Uint32 i=0;i<s->limits.descriptorSlots;++i)if(s->descriptors[i].pending){s->descriptors[i].serial=serial;s->descriptors[i].pending=false;}
    s->committedWidth=s->pendingWidth;
    s->committedHeight=s->pendingHeight;
    s->refitsSinceBuild=s->pendingBottomRefit ? s->refitsSinceBuild+1 : 0;
    s->stats.committedSerial=serial;s->open=NULL;return true;
}
bool ZHR_WaterRay_Collect(ZHR_WaterRay_State *s,Uint64 serial){
    if(!s||s->poisoned||serial<s->stats.completedSerial||serial>s->stats.committedSerial)return SDL_SetError("water reflection ray filter invalid completion");
    s->stats.completedSerial=serial;s->stats.pendingDescriptors=0;
    for(Uint32 i=0;i<s->limits.descriptorSlots;++i)if(s->descriptors[i].pending||s->descriptors[i].serial>serial)++s->stats.pendingDescriptors;
    return true;
}
bool ZHR_WaterRay_Abandon(ZHR_WaterRay_State *s,SDL_GPUCommandBuffer *cancelled){
    if(!s||s->poisoned||(s->open&&s->open!=cancelled))return SDL_SetError("water reflection ray filter invalid abandon");
    for(Uint32 i=0;i<s->limits.descriptorSlots;++i)if(s->descriptors[i].pending){s->descriptors[i].pending=false;--s->stats.pendingDescriptors;}
    s->open=NULL;return true;
}
void ZHR_WaterRay_Poison(ZHR_WaterRay_State *s){if(s)s->poisoned=true;}
ZHR_WaterRay_Stats ZHR_WaterRay_GetStats(const ZHR_WaterRay_State *s){ZHR_WaterRay_Stats stats;SDL_zero(stats);return s?s->stats:stats;}
#undef ZHR_WaterRay_VK
