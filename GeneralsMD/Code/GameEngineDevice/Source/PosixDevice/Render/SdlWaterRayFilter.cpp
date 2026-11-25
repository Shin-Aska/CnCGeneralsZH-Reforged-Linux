#include "SdlWaterRayFilter.h"
#include "Platform/HardwareRaySun.h"

#if defined(__linux__)
#include "SdlGpuFrame.h"
#include "SdlConstants.h"
#include "SdlWaterRayTileShader.h"
#include "sdl3shadercompile.h"
#include "../../../../Libraries/Source/SDL_gpu_vulkan_rayquery.h"
#include "../../../../Libraries/Source/SDL_gpu_vulkan_water_ray_filter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <string>
#include <vector>
#endif

bool SdlWaterRayFilterSupported(SDL_GPUDevice *device)
{
#if defined(__linux__)
    return HardwareRayTracing::Requested() && ZHR_SDLRayQuerySupported(device, nullptr, 0);
#else
    (void)device;
    return false;
#endif
}

#if defined(__linux__)
namespace {

constexpr unsigned BuildSlots = 3;
constexpr uint64_t AllocationLimit = 64 * 1024 * 1024;
static_assert(sizeof(ZHR_WaterRay_Params) == 128, "water depth tile push constants");

struct ViewKey {
    SDL_GPUTexture *Depth = nullptr;
    SDL_GPUTexture *Colour = nullptr;
    SDL_GPUTexture *Attachment = nullptr;
    uint64_t Device = 0, Frame = 0, Generation = 0, Capture = 0, View = 0;
    unsigned Width = 0, Height = 0;
    ZHR_WaterRay_Params Params = {};
    float Forward[16] = {};

    bool Matches(const ViewKey &other) const
    {
        return Depth == other.Depth && Colour == other.Colour && Attachment == other.Attachment
            && Device == other.Device && Frame == other.Frame && Generation == other.Generation
            && Capture == other.Capture && View == other.View
            && Width == other.Width && Height == other.Height
            && !std::memcmp(&Params, &other.Params, sizeof(Params))
            && !std::memcmp(Forward, other.Forward, sizeof(Forward));
    }
};

struct BuildFence {
    SDL_GPUFence *Fence = nullptr;
    uint64_t Serial = 0;
};

struct Session {
    SDL_GPUDevice *Device = nullptr;
    uint64_t DeviceGeneration = 0, Serial = 0;
    ZHR_WaterRay_State *Native = nullptr;
    unsigned CapacityWidth = 0, CapacityHeight = 0;
    bool Poisoned = false, CreateRefused = false, HaveBuilt = false, HaveAttempt = false;
    bool CapabilityRefused = false, DeviceLost = false;
    uint64_t RequestGeneration = 0;
    unsigned AttemptWidth = 0, AttemptHeight = 0;
    ViewKey Built, Attempt;
    std::array<uint32_t, 4> Addresses = {};
    std::array<BuildFence, BuildSlots> Fences = {};
    unsigned PendingFences = 0;
    uint64_t Calls = 0, Hits = 0, Builds = 0, Refused = 0, Flushes = 0;
    double FlushMS = 0, RecordMS = 0, SubmitMS = 0;
    std::string Error;
};

// All callers and the owner-release callback run on the render thread.
std::map<SdlGpuFrame *, Session> Sessions;

bool profile()
{
    static const bool enabled = std::getenv("ZH_HARDWARE_WATER_PROFILE") != nullptr;
    return enabled;
}

void report(const Session &session, const ViewKey &key)
{
    const auto native = session.Native ? ZHR_WaterRay_GetStats(session.Native) : ZHR_WaterRay_Stats{};
    std::fprintf(stderr,
        "Water ray filter: frame=%llu capture=%llu calls=%llu builds=%llu cache_hits=%llu refused=%llu "
        "flushes=%llu flush_ms=%.4f record_ms=%.4f submit_ms=%.4f pending=%u descriptor_refusals=%llu "
        "allocated_bytes=%llu tiles=%u poisoned=%u\n",
        (unsigned long long)key.Frame, (unsigned long long)key.Capture, (unsigned long long)session.Calls,
        (unsigned long long)session.Builds, (unsigned long long)session.Hits, (unsigned long long)session.Refused,
        (unsigned long long)session.Flushes, session.FlushMS, session.RecordMS, session.SubmitMS,
        session.PendingFences, (unsigned long long)native.descriptorRefusals,
        (unsigned long long)native.allocatedBytes, native.lastTileCount, unsigned(session.Poisoned));
}

bool refuse(Session &session, const char *why, bool poison = false)
{
    if (poison) {
        session.Poisoned = true;
        ZHR_WaterRay_Poison(session.Native);
    }
    ++session.Refused;
    if (session.Error != why) {
        std::fprintf(stderr, "Water ray filter: %s; ordinary SSR\n", why);
        session.Error = why;
    }
    return false;
}

bool collect(Session &session)
{
    unsigned completed = 0;
    while (completed < session.PendingFences
        && SDL_QueryGPUFence(session.Device, session.Fences[completed].Fence))
        ++completed;
    if (!completed) return true;
    if (!ZHR_WaterRay_Collect(session.Native, session.Fences[completed - 1].Serial))
        return refuse(session, "fill descriptor retirement refused", true);

    for (unsigned i = 0; i < completed; ++i)
        SDL_ReleaseGPUFence(session.Device, session.Fences[i].Fence);
    std::move(session.Fences.begin() + completed, session.Fences.begin() + session.PendingFences,
        session.Fences.begin());
    session.PendingFences -= completed;
    return true;
}

bool destroyWhenIdle(Session &session)
{
    // A creation refusal has no native GPU ownership to retire.
    if (!session.Native && !session.PendingFences) return true;
    const auto retirement = ZHR_SDLRayWaitForRetirement(session.Device);
    if (retirement == ZHR_SDLRAY_RETIRE_REFUSED) return false;
    session.DeviceLost |= retirement == ZHR_SDLRAY_RETIRE_DEVICE_LOST;
    for (unsigned i = 0; i < session.PendingFences; ++i)
        SDL_ReleaseGPUFence(session.Device, session.Fences[i].Fence);
    session.PendingFences = 0;
    ZHR_WaterRay_DestroyAfterIdle(session.Native);
    session.Native = nullptr;
    session.HaveBuilt = false;
    session.HaveAttempt = false;
    return true;
}

bool releaseOwner(SdlGpuFrame *owner)
{
    auto found = Sessions.find(owner);
    if (found == Sessions.end()) return true;
    if (owner->Has_Pending_Work() && !owner->Flush()) return false;
    Session &session = found->second;
    if (!destroyWhenIdle(session)) return false;
    if (profile()) report(session, session.Built);
    Sessions.erase(found);
    return true;
}

bool makeViewKey(SdlGpuFrame &owner, const SdlTarget &target, SDL_GPUTexture *depth, uint64_t mainViewKey,
    const SdlWaterPixelConstants &water, ViewKey &key)
{
    uint64_t serial = 0;
    if (!depth || !mainViewKey || owner.Ready_Water_Depth(target, serial) != depth || !serial)
        return false;

    key.Depth = depth;
    key.Colour = target.Colour;
    key.Attachment = target.Depth;
    key.Device = owner.Device_Generation();
    key.Frame = owner.Scene_Frame();
    key.Generation = owner.Scene_Generation();
    key.Capture = serial;
    key.View = mainViewKey;
    key.Width = target.Width;
    key.Height = target.Height;
    std::memcpy(key.Params.inverse, water.WorldFromClip, sizeof(key.Params.inverse));
    std::memcpy(key.Forward, water.ClipFromWorld, sizeof(key.Forward));
    for (unsigned i = 0; i < 4; ++i) {
        key.Params.clipZ[i] = water.ClipFromWorld[i * 4 + 2];
        key.Params.clipW[i] = water.ClipFromWorld[i * 4 + 3];
    }
    std::memcpy(key.Params.viewport, water.Viewport, sizeof(key.Params.viewport));
    std::memcpy(key.Params.camera, water.CameraPosition, sizeof(key.Params.camera));
    // The consuming shader proves its accepted hits are above this floor.
    key.Params.camera[3] = water.WaterParameters[3] - 3.0f;

    const auto allFinite = [](const auto &values) {
        for (float value : values)
            if (!std::isfinite(value)) return false;
        return true;
    };
    return allFinite(key.Params.inverse) && allFinite(key.Params.clipZ) && allFinite(key.Params.clipW)
        && allFinite(key.Params.viewport) && allFinite(key.Params.camera) && allFinite(key.Forward)
        && key.Params.viewport[2] > 0 && key.Params.viewport[3] > 0;
}

bool hasAddresses(const uint32_t addresses[4])
{
    return (addresses[0] || addresses[1]) && (addresses[2] || addresses[3]);
}

} // namespace

bool SdlPrepareWaterRayFilter(SdlGpuFrame &owner, IDirect3DDevice9 *device, const SdlTarget &target,
    SDL_GPUTexture *depth, uint64_t mainViewKey, const SdlWaterPixelConstants &water, uint32_t out[4])
{
    std::fill_n(out, 4, 0u);
    if (!device || !SdlWaterRayFilterSupported(owner.Device()) || !HardwareRayTracing::Available(device)
        || !target.Width || !target.Height || !target.Colour || !target.Depth)
        return false;

    ViewKey view;
    if (!makeViewKey(owner, target, depth, mainViewKey, water, view)) return false;

    auto found = Sessions.find(&owner);
    if (found == Sessions.end()) {
        if (!owner.Register_Owner_Release_Hook(releaseOwner)) return false;
        try {
            found = Sessions.try_emplace(&owner).first;
        } catch (const std::bad_alloc &) {
            owner.Unregister_Owner_Release_Hook(releaseOwner);
            return false;
        }
        found->second.Device = owner.Device();
        found->second.DeviceGeneration = owner.Device_Generation();
    }

    Session &session = found->second;
    ++session.Calls;
    if (session.CapabilityRefused || session.DeviceLost || session.Device != owner.Device()
        || session.DeviceGeneration != owner.Device_Generation())
        return false;
    const uint64_t requestGeneration = HardwareRayTracing::RequestGeneration();
    const bool transition = session.RequestGeneration != requestGeneration
        || session.AttemptWidth != target.Width || session.AttemptHeight != target.Height;
    if ((session.Poisoned || session.CreateRefused) && !transition) return false;
    if (transition && (session.Poisoned || session.CreateRefused))
        session.HaveAttempt = session.HaveBuilt = false;
    // Consume a retry before any flush/wait. A refusal remains quiet until the
    // next explicit toggle or extent change, including an unsuccessful resize.
    session.RequestGeneration = requestGeneration;
    session.AttemptWidth = target.Width;
    session.AttemptHeight = target.Height;
    if (!session.Poisoned && session.HaveBuilt && session.Built.Matches(view)) {
        ++session.Hits;
        std::copy(session.Addresses.begin(), session.Addresses.end(), out);
        return true;
    }
    if (session.HaveAttempt && session.Attempt.Matches(view)) return false;
    session.Attempt = view;
    session.HaveAttempt = true;

    // The fixed native storage is reused for the next depth snapshot. Any
    // queued reader of the old addresses must be submitted before that write.
    const bool profiling = profile();
    const uint64_t flushStart = profiling ? SDL_GetTicksNS() : 0;
    const bool flushed = owner.Flush();
    ++session.Flushes;
    if (profiling) session.FlushMS += double(SDL_GetTicksNS() - flushStart) * 1e-6;
    if (!flushed) return refuse(session, "pre-water snapshot/reader flush refused", true);

    uint64_t ready = 0;
    if (owner.Ready_Water_Depth(target, ready) != depth || ready != view.Capture
        || owner.Scene_Frame() != view.Frame || owner.Scene_Generation() != view.Generation
        || owner.Device_Generation() != view.Device)
        return refuse(session, "snapshot changed while submitting pre-water work");

    if (session.Native && (session.Poisoned || target.Width > session.CapacityWidth || target.Height > session.CapacityHeight)
        && !destroyWhenIdle(session))
        return refuse(session, "resize retirement idle refused", true);
    if (session.DeviceLost) return refuse(session, "native device lost; awaiting device replacement");
    session.Poisoned = session.CreateRefused = false;

    if (!session.Native) {
        char diagnostic[512] = {};
        if (!ZHR_WaterRay_Supported(session.Device, diagnostic, sizeof(diagnostic))) {
            session.CapabilityRefused = true;
            return refuse(session, diagnostic);
        }
        std::vector<unsigned char> spirv;
        std::string message;
        if (!SDL3_Compile_Ray_Query_GLSL(WaterRayTileShader, spirv, message)) {
            session.CreateRefused = true;
            return refuse(session, message.c_str());
        }
        const ZHR_WaterRay_Limits limits = {target.Width, target.Height, BuildSlots, AllocationLimit};
        session.Native = ZHR_WaterRay_Create(session.Device, spirv.data(), spirv.size(), &limits);
        if (!session.Native) {
            session.CreateRefused = true;
            return refuse(session, SDL_GetError());
        }
        session.CapacityWidth = target.Width;
        session.CapacityHeight = target.Height;
        // The retirement above may have cleared the one-attempt guard.
        session.Attempt = view;
        session.HaveAttempt = true;
    }

    if (!collect(session)) return false;
    if (session.PendingFences == BuildSlots)
        return refuse(session, "fill descriptors still in use");

    SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(session.Device);
    if (!commands) return refuse(session, "native fill command buffer unavailable");

    uint32_t addresses[4] = {};
    const uint64_t recordStart = profiling ? SDL_GetTicksNS() : 0;
    const bool recorded = ZHR_WaterRay_Record(session.Native, commands, depth, target.Width, target.Height,
        &view.Params, addresses);
    if (profiling) session.RecordMS += double(SDL_GetTicksNS() - recordStart) * 1e-6;
    if (!recorded) {
        const std::string error = SDL_GetError();
        const bool cancelled = SDL_CancelGPUCommandBuffer(commands);
        const bool abandoned = cancelled && ZHR_WaterRay_Abandon(session.Native, commands);
        return refuse(session, error.c_str(), !abandoned);
    }

    const uint64_t submitStart = profiling ? SDL_GetTicksNS() : 0;
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
    if (profiling) session.SubmitMS += double(SDL_GetTicksNS() - submitStart) * 1e-6;
    if (!fence) return refuse(session, "native fill submission ambiguous", true);

    const uint64_t submitted = ++session.Serial;
    session.Fences[session.PendingFences++] = {fence, submitted};
    if (!ZHR_WaterRay_Commit(session.Native, commands, submitted))
        return refuse(session, "native fill commit refused", true);
    if (!hasAddresses(addresses))
        return refuse(session, "native ray addresses missing", true);

    session.Built = view;
    session.HaveBuilt = true;
    std::copy_n(addresses, 4, session.Addresses.begin());
    std::copy_n(addresses, 4, out);
    session.Error.clear();
    ++session.Builds;
    if (session.Builds == 1 || (profiling && session.Builds % 120 == 0)) report(session, view);
    return true;
}

void SdlValidateWaterRayFilter(SdlGpuFrame &owner, const SdlTarget &target, SDL_GPUTexture *depth,
    uint64_t mainViewKey, const SdlWaterPixelConstants &water, uint32_t addresses[4])
{
    if (!hasAddresses(addresses)) return;
    const auto found = Sessions.find(&owner);
    ViewKey view;
    if (!SdlWaterRayFilterSupported(owner.Device()) || found == Sessions.end()
        || !makeViewKey(owner, target, depth, mainViewKey, water, view)
        || found->second.Poisoned || !found->second.HaveBuilt || !found->second.Built.Matches(view)
        || !std::equal(found->second.Addresses.begin(), found->second.Addresses.end(), addresses))
        std::fill_n(addresses, 4, 0u);
}

#else

bool SdlPrepareWaterRayFilter(SdlGpuFrame &, IDirect3DDevice9 *, const SdlTarget &,
    SDL_GPUTexture *, uint64_t, const SdlWaterPixelConstants &, uint32_t[4])
{
    return false;
}

void SdlValidateWaterRayFilter(SdlGpuFrame &, const SdlTarget &, SDL_GPUTexture *, uint64_t,
    const SdlWaterPixelConstants &, uint32_t[4])
{
}

#endif
