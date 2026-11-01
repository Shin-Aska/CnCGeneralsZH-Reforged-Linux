// A model digest may be shared only within one synchronous scene-export walk.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>

namespace WaterPathModelDigest {
struct Hash {
    uint64_t value = UINT64_C(1469598103934665603);
    size_t bytes = 0;
    void Bytes(const void *data, size_t count) {
        bytes += count;
        const auto *input = static_cast<const unsigned char *>(data);
        for (size_t i = 0; i < count; ++i) {
            value ^= input[i];
            value *= UINT64_C(1099511628211);
        }
    }

    template <class T> void Add(const T &value) {
        Bytes(&value, sizeof(value));
    }
};
struct Digest {
    uint64_t value = 0;
    size_t bytes = 0;
};
class RefreshMemo {
public:
    struct Statistics {
        size_t hits = 0, misses = 0, verified = 0, mismatches = 0, hashedBytes = 0, savedBytes = 0,
            verificationBytes = 0;
        uint64_t digestXor = 0;
    } stats;
    explicit RefreshMemo(bool verify = false) : verify(verify) {}

    template <class Compute> uint64_t Get(const void *model, const Compute &compute) {
        auto found = values.find(model);
        if (found == values.end()) {
            const Digest digest = compute();
            ++stats.misses;
            stats.hashedBytes += digest.bytes;
            stats.digestXor ^= digest.value;
            values.emplace(model, digest);
            return digest.value;
        }
        ++stats.hits;
        stats.savedBytes += found->second.bytes;
        if (verify) {
            const Digest fresh = compute();
            ++stats.verified;
            stats.verificationBytes += fresh.bytes;
            if (fresh.value != found->second.value) {
                ++stats.mismatches;
                // Verification must never propagate a detected stale value into the exporter.
                found->second = fresh;
            }
        }
        return found->second.value;
    }
private:
    bool verify;
    std::map<const void *, Digest> values;
};

inline uint64_t Instance(uint64_t modelDigest, const void *mesh, const void *model,
    const void *transform, size_t transformBytes, float visibility, const void *pose, size_t poseBytes) {
    Hash hash;
    hash.Add(mesh);
    hash.Add(model);
    hash.Bytes(transform, transformBytes);
    hash.Add(visibility);
    hash.Add(modelDigest);
    hash.Add(poseBytes);
    if (poseBytes)
        hash.Bytes(pose, poseBytes);
    return hash.value;
}
}
