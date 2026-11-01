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
