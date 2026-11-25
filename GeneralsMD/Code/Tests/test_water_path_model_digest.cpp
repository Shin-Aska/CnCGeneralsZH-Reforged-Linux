#include "test_harness.h"
#include "Platform/WaterPathModelDigest.h"
#include <array>
#include <cstring>
#include <vector>

namespace {
struct Model {
    std::array<unsigned char, 96> vertices{};
    std::array<unsigned char, 24> indices{}, uv{}, colours{}, materials{}, shaders{}, textures{};
    unsigned flags = 0;
};

template <class Sink> void Fields(const Model &m, Sink sink) {
    sink(&m.flags, sizeof(m.flags));
    for (const auto *field :{&m.indices, &m.uv, &m.colours, &m.materials, &m.shaders, &m.textures})
        sink(field->data(), field->size());
    sink(m.vertices.data(), m.vertices.size());
}
WaterPathModelDigest::Digest Compute(const Model &m) {
    WaterPathModelDigest::Hash hash;
    Fields(m, [&](const void *p, size_t n) {
        hash.Bytes(p, n);
    });
    return {hash.value, hash.bytes};
}
// Independent byte-at-a-time oracle, without the production Hash or memo.
uint64_t Uncached(const Model &m) {
    std::vector<unsigned char> bytes;
    Fields(m, [&](const void *p, size_t n) {
        const auto *b = static_cast<const unsigned char *>(p);
        bytes.insert(bytes.end(), b, b + n);
    });
    uint64_t value = UINT64_C(1469598103934665603);
    for (unsigned char b : bytes)
        value = (value ^ b) * UINT64_C(1099511628211);
    return value;
}
}

TEST(water_path_model_digest_shares_work_only_for_identical_model_identity)
{
    Model a, b;
    WaterPathModelDigest::RefreshMemo memo;
    unsigned calls = 0;
    auto first = [&]() {
        ++calls;
        return Compute(a);
    };
    CHECK_EQ(memo.Get(&a, first), Uncached(a));
    for (unsigned i = 0; i < 100; ++i)
        CHECK_EQ(memo.Get(&a, first), Uncached(a));
    CHECK_EQ(calls, 1u);
    CHECK_EQ(memo.stats.hits, 100u);
    CHECK_EQ(memo.stats.misses, 1u);
    CHECK_EQ(memo.Get(&b, [&]() {
        ++calls;
        return Compute(b);
    }), Uncached(b));
    CHECK_EQ(calls, 2u);
    CHECK_EQ(memo.stats.misses, 2u);
    CHECK_EQ(memo.stats.savedBytes, 100 * Compute(a).bytes);
}

TEST(water_path_model_digest_observes_every_model_input_again_on_next_refresh)
{
    Model model;
    const uint64_t initial = Uncached(model);
    // Includes geometry, topology, UVs, colours, material/shader state, texture
    // resource/initialization bytes and flags. Same pointer, newly scoped memo.
    for (auto *field :{&model.indices, &model.uv, &model.colours, &model.materials, &model.shaders,
        &model.textures}) {
        for (unsigned char &byte : *field) {
            byte = 73;
            WaterPathModelDigest::RefreshMemo refresh;
            CHECK_EQ(refresh.Get(&model, [&]() {
                return Compute(model);
            }), Uncached(model));
            CHECK_NE(Uncached(model), initial);
            byte = 0;
        }
    }
    for (unsigned char &byte : model.vertices) {
        byte = 17;
        WaterPathModelDigest::RefreshMemo refresh;
        CHECK_EQ(refresh.Get(&model, [&]() {
            return Compute(model);
        }), Uncached(model));
        CHECK_NE(Uncached(model), initial);
        byte = 0;
    }
    model.flags = 1;
    WaterPathModelDigest::RefreshMemo refresh;
    CHECK_EQ(refresh.Get(&model, [&]() {
        return Compute(model);
    }), Uncached(model));
    CHECK_NE(Uncached(model), initial);
}

TEST(water_path_model_digest_verification_detects_and_does_not_use_stale_content)
{
    Model model;
    WaterPathModelDigest::RefreshMemo memo(true);
    CHECK_EQ(memo.Get(&model, [&]() {
        return Compute(model);
    }), Uncached(model));
    CHECK_EQ(memo.Get(&model, [&]() {
        return Compute(model);
    }), Uncached(model));
    model.textures[7] = 1;
    CHECK_EQ(memo.Get(&model, [&]() {
        return Compute(model);
    }), Uncached(model));
    CHECK_EQ(memo.stats.verified, 2u);
    CHECK_EQ(memo.stats.mismatches, 1u);
    CHECK_EQ(memo.Get(&model, [&]() {
        return Compute(model);
    }), Uncached(model));
    CHECK_EQ(memo.stats.mismatches, 1u);
}

TEST(water_path_model_digest_keeps_transform_visibility_and_skin_pose_per_instance)
{
    Model model;
    WaterPathModelDigest::RefreshMemo memo;
    std::array<float, 12> transform{};
    std::array<float, 9> pose{};
    int meshA = 0, meshB = 0;
    auto fingerprint = [&](const void *mesh, float visibility, bool skin) {
        return WaterPathModelDigest::Instance(memo.Get(&model, [&]() {
            return Compute(model);
        }), mesh, &model,
            transform.data(), sizeof(transform), visibility, pose.data(), skin ? sizeof(pose) : 0);
    };
    const uint64_t first = fingerprint(&meshA, 1, true);
    CHECK_EQ(first, WaterPathModelDigest::Instance(Uncached(model), &meshA, &model, transform.data(),
        sizeof(transform), 1, pose.data(), sizeof(pose)));
    CHECK_NE(first, fingerprint(&meshB, 1, true));
    CHECK_NE(first, fingerprint(&meshA, .5f, true));
    CHECK_NE(first, fingerprint(&meshA, 1, false));
    transform[3] = 10;
    CHECK_NE(first, fingerprint(&meshA, 1, true));
    transform[3] = 0;
    pose[2] = 3;
    const uint64_t posed = fingerprint(&meshA, 1, true);
    CHECK_NE(first, posed);
    CHECK_EQ(posed, WaterPathModelDigest::Instance(Uncached(model), &meshA, &model, transform.data(),
        sizeof(transform), 1, pose.data(), sizeof(pose)));
    CHECK_EQ(memo.stats.misses, 1u);
    CHECK_EQ(memo.stats.hits, 5u);
}

TEST(water_path_exact_model_cache_compares_current_bytes_on_every_refresh)
{
    Model model;
    WaterPathModelDigest::ExactCache cache;
    auto get=[&]() {return cache.Get(&model,[&](auto &sink) {
        Fields(model,[&](const void *data,size_t count){sink.Bytes(data,count);});
    });};
    cache.BeginRefresh();
    const auto original = get();
    CHECK_EQ(original.value, Uncached(model));
    CHECK_NE(original.revision, 0u);
    CHECK_EQ(cache.stats.misses, 1u);
    cache.EndRefresh();
    cache.BeginRefresh();
    WaterPathModelDigest::RefreshMemo memo(false, false, &cache);
    uint64_t revision = 0;
    CHECK_EQ(memo.Get(&model, get, &revision), original.value);
    CHECK_EQ(revision, original.revision);
    CHECK_EQ(cache.stats.hits, 1u);
    revision = 0;
    CHECK_EQ(memo.Get(&model, get, &revision), original.value);
    CHECK_EQ(revision, original.revision);
    cache.EndRefresh();
    for(auto *field:{&model.indices,&model.uv,&model.colours,&model.materials,&model.shaders,&model.textures}) {
        for(unsigned char &value:*field) {
            value=29;cache.BeginRefresh();CHECK_EQ(get().value,Uncached(model));CHECK_EQ(cache.stats.misses,1u);cache.EndRefresh();
            cache.BeginRefresh();CHECK_EQ(get().value,Uncached(model));CHECK_EQ(cache.stats.hits,1u);cache.EndRefresh();
            value=0;cache.BeginRefresh();CHECK_EQ(get().value,Uncached(model));CHECK_EQ(cache.stats.misses,1u);cache.EndRefresh();
        }
    }
    for(unsigned char &value:model.vertices) {
        value=41;cache.BeginRefresh();CHECK_EQ(get().value,Uncached(model));CHECK_EQ(cache.stats.misses,1u);cache.EndRefresh();
        value=0;cache.BeginRefresh();CHECK_EQ(get().value,Uncached(model));cache.EndRefresh();
    }
    model.flags=8;cache.BeginRefresh();CHECK_EQ(get().value,Uncached(model));CHECK_EQ(cache.stats.misses,1u);cache.EndRefresh();
    CHECK(cache.StoredBytes()<=16u*1024u*1024u);
    cache.BeginRefresh();cache.EndRefresh();CHECK_EQ(cache.StoredBytes(),0u); // owner disappeared
    model.flags = 0;
    cache.BeginRefresh();
    CHECK_NE(get().revision, original.revision);
    cache.EndRefresh();
}

TEST(water_path_exact_model_cache_preserves_byte_stream_length_and_budget_fallback)
{
    std::vector<unsigned char> current(97,13);
    WaterPathModelDigest::ExactCache cache(128);
    int identity=0;
    auto get=[&]() {return cache.Get(&identity,[&](auto &sink){sink.Bytes(current.data(),current.size());});};
    auto expected=[&]() {WaterPathModelDigest::Hash hash;hash.Bytes(current.data(),current.size());return hash.value;};
    uint64_t previousRevision = 0;
    unsigned previousCount = 0;
    for(unsigned count:{97u,97u,98u,0u,127u,129u,97u}) {
        current.resize(count,31);cache.BeginRefresh();const auto result=get();
        CHECK_EQ(result.value,expected());CHECK_EQ(result.bytes,current.size());CHECK(cache.StoredBytes()<=128);
        if (count > 128) {
            CHECK_EQ(cache.stats.bypasses, 1u);
            CHECK_EQ(cache.StoredBytes(), 0u);
            CHECK_EQ(result.revision, 0u);
        } else if (count == previousCount) {
            CHECK_EQ(result.revision, previousRevision);
        } else {
            CHECK_NE(result.revision, 0u);
            CHECK_NE(result.revision, previousRevision);
        }
        previousCount = count;
        previousRevision = result.revision;
        cache.EndRefresh();
    }
    cache.Clear();CHECK_EQ(cache.StoredBytes(),0u);
    cache.BeginRefresh();
    const auto rebuilt = get();
    CHECK_EQ(rebuilt.value, expected());
    CHECK_NE(rebuilt.revision, previousRevision);
    CHECK_EQ(cache.stats.misses, 1u);
    cache.EndRefresh();
}
