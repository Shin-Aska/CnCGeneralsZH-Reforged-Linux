// Synchronous model digest sharing, plus bounded retention proven against current field bytes.
#pragma once
#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <map>
#include <chrono>
#include <cstring>
#include <unordered_map>
#include <vector>

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
    uint64_t revision = 0; // Nonzero only when ExactCache saved the current field bytes.
};
// A cross-refresh hit is proven by comparing every CURRENT stream field against saved bytes.
// Pointer identity selects a candidate only; it never proves unchanged model contents.
class ExactCache {
public:
    struct Statistics { size_t hits=0,misses=0,comparedBytes=0,bypasses=0,mismatches=0; } stats;
    explicit ExactCache(size_t limit=16u*1024u*1024u):Limit(limit){}
    void Clear(){Entries.clear();Used=0;}
    void BeginRefresh(){stats={};if(++Epoch==0){Clear();++Epoch;}}
    void EndRefresh(){for(auto i=Entries.begin();i!=Entries.end();) {
        if(i->second.epoch!=Epoch){Used-=i->second.bytes.capacity();i=Entries.erase(i);}else ++i;
    }}
    size_t StoredBytes() const {return Used;}
    template<class Stream> Digest Get(const void *model,const Stream &stream) {
        auto found=Entries.find(model);
        if(found!=Entries.end()) {
            Compare proof{found->second.bytes};stream(proof);stats.comparedBytes+=proof.compared;
            if(proof.same && proof.offset==found->second.bytes.size()) {
                ++stats.hits;found->second.epoch=Epoch;return found->second.digest;
            }
        }
        ++stats.misses;
        const size_t old=found!=Entries.end()?found->second.bytes.capacity():0;
        Save next(Limit-Used+old);stream(next);
        Digest digest{next.hash.value,next.hash.bytes};
        if(next.keep && (found!=Entries.end() || Entries.size()<4096)) {
            if(found==Entries.end())found=Entries.emplace(model,Entry{}).first;
            Used-=old;found->second.bytes.swap(next.bytes);Used+=found->second.bytes.capacity();
            if (Revision != UINT64_MAX) digest.revision = ++Revision;
            found->second.digest=digest;found->second.epoch=Epoch;
        } else {
            ++stats.bypasses;
            if(found!=Entries.end()){Used-=old;Entries.erase(found);}
        }
        return digest;
    }
private:
    struct Entry { std::vector<unsigned char> bytes;Digest digest;uint64_t epoch=0; };
    struct Compare {
        const std::vector<unsigned char> &saved;size_t offset=0,compared=0;bool same=true;
        void Bytes(const void *data,size_t count) {
            if(same){compared+=count;same=offset<=saved.size() && count<=saved.size()-offset
                && (!count || std::memcmp(saved.data()+offset,data,count)==0);}
            offset+=count;
        }
        template<class T>void Add(const T &value){Bytes(&value,sizeof(value));}
    };
    struct Save {
        Hash hash;std::vector<unsigned char> bytes;size_t limit;bool keep=true;
        explicit Save(size_t limit):limit(limit){}
        void Bytes(const void *data,size_t count) {
            hash.Bytes(data,count);
            if(!keep)return;
            if(count>limit-bytes.size()){keep=false;std::vector<unsigned char>().swap(bytes);return;}
            const size_t needed=bytes.size()+count;
            if(needed>bytes.capacity())bytes.reserve(std::min(limit,std::max(needed,std::max(size_t(4096),bytes.capacity()*2))));
            if(count){const auto *p=static_cast<const unsigned char *>(data);bytes.insert(bytes.end(),p,p+count);}
        }
        template<class T>void Add(const T &value){Bytes(&value,sizeof(value));}
    };
    std::unordered_map<const void *,Entry> Entries;
    size_t Used=0,Limit;
    uint64_t Epoch=0, Revision=0;
};

class RefreshMemo {
public:
    struct Statistics {
        size_t hits = 0, misses = 0, verified = 0, mismatches = 0, hashedBytes = 0, savedBytes = 0,
            verificationBytes = 0;
        uint64_t digestXor = 0,computeNs=0,poseNs=0;
    } stats;
    explicit RefreshMemo(bool verify = false,bool timing = false,ExactCache *proof = nullptr) : verify(verify),timing(timing),proof(proof) {}
    bool Timing() const { return timing; }
    bool Verifying() const { return verify; }
    ExactCache *Proof() const { return proof; }

    template <class Compute> uint64_t Get(const void *model, const Compute &compute, uint64_t *revision = nullptr) {
        const auto evaluate=[&]() {
            if(!timing)return compute();
            const auto start=std::chrono::steady_clock::now();const Digest digest=compute();
            stats.computeNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
            return digest;
        };
        auto found = values.find(model);
        if (found == values.end()) {
            const Digest digest = evaluate();
            ++stats.misses;
            stats.hashedBytes += digest.bytes;
            stats.digestXor ^= digest.value;
            values.emplace(model, digest);
            if (revision) *revision = digest.revision;
            return digest.value;
        }
        ++stats.hits;
        stats.savedBytes += found->second.bytes;
        if (verify) {
            const Digest fresh = evaluate();
            ++stats.verified;
            stats.verificationBytes += fresh.bytes;
            if (fresh.value != found->second.value) {
                ++stats.mismatches;
                // Verification must never propagate a detected stale value into the exporter.
                found->second = fresh;
            }
            found->second.revision = fresh.revision;
        }
        if (revision) *revision = found->second.revision;
        return found->second.value;
    }
private:
    bool verify,timing;
    ExactCache *proof;
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
