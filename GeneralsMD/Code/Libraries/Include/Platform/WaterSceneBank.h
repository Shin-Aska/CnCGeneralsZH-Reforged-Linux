// A portable, render-only water frame. No game objects, device handles or SDL types live here.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <istream>
#include <limits>
#include <utility>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace WaterSceneBank {

static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,"water banks require IEEE binary32");
constexpr uint32_t Version = 1;
constexpr unsigned Stages = 16, RenderStates = 256, StageStates = 33, SamplerStates = 14;
using Matrix = std::array<float,16>;

struct Image {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> bgra;
};
struct Viewport {
    uint32_t x = 0, y = 0, width = 0, height = 0;
    float minZ = 0, maxZ = 1;
};
struct Draw {
    uint32_t program = 0, fvf = 0, stride = 0;
    std::vector<uint8_t> vertices;
    std::vector<uint32_t> indices;
    Matrix world{}, view{}, projection{};
    Viewport viewport;
    std::vector<float> pixelConstants, vertexConstants;
    std::array<uint32_t,RenderStates> renderStates{};
    std::array<std::array<uint32_t,StageStates>,Stages> stageStates{};
    std::array<std::array<uint32_t,SamplerStates>,Stages> samplerStates{};
    std::array<Matrix,Stages> textureMatrices{};
    std::array<Image,Stages> textures;
    uint32_t mirrorStage = 1; // stage 8 is deliberately absent: c1.x drives the production FFT
};
struct Frame {
    uint32_t width = 0, height = 0;
    Image colour;
    std::vector<float> depth; // sampled device depth, including the viewport MinZ/MaxZ
    Image mirror;
    std::array<float,3> camera{};
    std::vector<Draw> draws;
};

// The wire order is the declaration order above, except Frame begins with magic/version.
// Every scalar is little endian (floats use IEEE binary32); arrays of variable length have a
// uint32 element count. Images are width, height, byte count, tightly packed top-first BGRA8.
// Vertex bytes retain the FVF layout, including its little-endian floats and packed colours.
namespace Detail {
constexpr uint64_t MaxBytes = uint64_t(1) << 30;
inline void Check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
struct Writer {
    std::ostream &stream;
    uint64_t remaining = MaxBytes;
    void bytes(const void *p, size_t n) { Check(n<=remaining,"bank exceeds size limit"); remaining-=n; if (n) stream.write(static_cast<const char *>(p),n); Check(bool(stream),"bank write failed"); }
    void scalar(uint32_t n) { const uint8_t b[]={uint8_t(n),uint8_t(n>>8),uint8_t(n>>16),uint8_t(n>>24)}; bytes(b,4); }
    void scalar(float n) { uint32_t bits; std::memcpy(&bits,&n,4); scalar(bits); }
    template<class T, size_t N> void array(const std::array<T,N> &v) { for (const auto &n:v) scalar(n); }
    template<class T> void vector(const std::vector<T> &v) { Check(v.size()<=MaxBytes/sizeof(T),"bank array too large"); scalar(uint32_t(v.size())); for (const auto &n:v) scalar(n); }
    void vector(const std::vector<uint8_t> &v) { scalar(uint32_t(v.size())); bytes(v.data(),v.size()); }
    void image(const Image &v) { scalar(v.width); scalar(v.height); vector(v.bgra); }
};
struct Reader {
    std::istream &stream;
    uint64_t remaining = MaxBytes;
    void bytes(void *p, size_t n) { Check(n<=remaining,"bank exceeds size limit"); remaining-=n; if (n) stream.read(static_cast<char *>(p),n); Check(bool(stream),"truncated bank"); }
    uint32_t u32() { uint8_t b[4]; bytes(b,4); return uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24); }
    void scalar(uint32_t &n) { n=u32(); }
    void scalar(float &n) { const uint32_t bits=u32(); std::memcpy(&n,&bits,4); }
    template<class T, size_t N> void array(std::array<T,N> &v) { for (auto &n:v) scalar(n); }
    template<class T> void vector(std::vector<T> &v, uint64_t limit) { const uint32_t n=u32(); Check(n<=limit && uint64_t(n)*sizeof(T)<=remaining,"invalid bank array size"); v.resize(n); for (auto &x:v) scalar(x); }
    void vector(std::vector<uint8_t> &v, uint64_t limit) { const uint32_t n=u32(); Check(n<=limit && n<=remaining,"invalid bank byte size"); v.resize(n); bytes(v.data(),n); }
    void image(Image &v) { v.width=u32(); v.height=u32(); Check(v.width<=8192 && v.height<=8192,"invalid bank image size"); vector(v.bgra,uint64_t(v.width)*v.height*4); Check(v.bgra.size()==uint64_t(v.width)*v.height*4 && (v.width==0)==(v.height==0),"incomplete bank image"); }
};
inline void Validate(const Frame &f)
{
    Check(f.width && f.height && f.width<=8192 && f.height<=8192,"invalid bank target size");
    const auto image=[](const Image &i) { Check(i.width<=8192 && i.height<=8192 && (i.width==0)==(i.height==0) && i.bgra.size()==uint64_t(i.width)*i.height*4,"incomplete bank image"); };
    image(f.colour); image(f.mirror);
    Check(f.colour.width==f.width && f.colour.height==f.height && f.depth.size()==uint64_t(f.width)*f.height,"incomplete bank snapshot");
    Check(!f.draws.empty() && f.draws.size()<=4096,"invalid bank draw count");
    for (const Draw &d:f.draws) {
        Check(d.stride && d.stride<=4096 && !d.vertices.empty() && d.vertices.size()%d.stride==0,"invalid bank vertices");
        Check(!d.indices.empty() && d.indices.size()%3==0,"invalid bank triangle list");
        for (uint32_t i:d.indices) Check(i<d.vertices.size()/d.stride,"bank index out of range");
        Check(d.pixelConstants.size()>=96*4 && d.pixelConstants.size()<=4096*4 && d.pixelConstants.size()%4==0
            && d.vertexConstants.size()<=256*4 && d.vertexConstants.size()%4==0,"invalid bank constants");
        Check(d.viewport.width && d.viewport.height && uint64_t(d.viewport.x)+d.viewport.width<=f.width
            && uint64_t(d.viewport.y)+d.viewport.height<=f.height && d.viewport.minZ>=0 && d.viewport.maxZ<=1
            && d.viewport.maxZ>d.viewport.minZ && d.mirrorStage==1,"invalid bank viewport or mirror stage");
        for (const auto &i:d.textures) image(i);
        Check(d.textures[8].bgra.empty(),"FFT maps must not be banked");
    }
}
} // namespace Detail

inline bool Write(std::ostream &stream, const Frame &frame, std::string &error)
{
    try {
        Detail::Validate(frame);
        Detail::Writer w{stream};
        w.bytes("ZHWBANK1",8); w.scalar(Version);
        w.scalar(frame.width); w.scalar(frame.height); w.image(frame.colour); w.vector(frame.depth);
        w.image(frame.mirror); w.array(frame.camera); w.scalar(uint32_t(frame.draws.size()));
        for (const Draw &d:frame.draws) {
            w.scalar(d.program); w.scalar(d.fvf); w.scalar(d.stride); w.vector(d.vertices); w.vector(d.indices);
            w.array(d.world); w.array(d.view); w.array(d.projection);
            w.scalar(d.viewport.x); w.scalar(d.viewport.y); w.scalar(d.viewport.width); w.scalar(d.viewport.height);
            w.scalar(d.viewport.minZ); w.scalar(d.viewport.maxZ);
            w.vector(d.pixelConstants); w.vector(d.vertexConstants); w.array(d.renderStates);
            for (const auto &v:d.stageStates) w.array(v);
            for (const auto &v:d.samplerStates) w.array(v);
            for (const auto &v:d.textureMatrices) w.array(v);
            for (const auto &v:d.textures) w.image(v);
            w.scalar(d.mirrorStage);
        }
        error.clear(); return true;
    } catch (const std::exception &e) { error=e.what(); return false; }
}

inline bool Read(std::istream &stream, Frame &frame, std::string &error)
{
    try {
        Detail::Reader r{stream}; Frame f;
        char magic[8]; r.bytes(magic,8);
        Detail::Check(std::memcmp(magic,"ZHWBANK1",8)==0 && r.u32()==Version,"unsupported water bank magic/version");
        f.width=r.u32(); f.height=r.u32();
        Detail::Check(f.width && f.height && f.width<=8192 && f.height<=8192,"invalid bank target size");
        r.image(f.colour); r.vector(f.depth,uint64_t(f.width)*f.height); r.image(f.mirror); r.array(f.camera);
        const uint32_t draws=r.u32(); Detail::Check(draws && draws<=4096,"invalid bank draw count"); f.draws.resize(draws);
        for (Draw &d:f.draws) {
            d.program=r.u32(); d.fvf=r.u32(); d.stride=r.u32(); r.vector(d.vertices,Detail::MaxBytes); r.vector(d.indices,Detail::MaxBytes/4);
            r.array(d.world); r.array(d.view); r.array(d.projection);
            r.scalar(d.viewport.x); r.scalar(d.viewport.y); r.scalar(d.viewport.width); r.scalar(d.viewport.height);
            r.scalar(d.viewport.minZ); r.scalar(d.viewport.maxZ);
            r.vector(d.pixelConstants,4096*4); r.vector(d.vertexConstants,256*4); r.array(d.renderStates);
            for (auto &v:d.stageStates) r.array(v);
            for (auto &v:d.samplerStates) r.array(v);
            for (auto &v:d.textureMatrices) r.array(v);
            for (auto &v:d.textures) r.image(v);
            r.scalar(d.mirrorStage);
        }
        Detail::Validate(f);
        Detail::Check(stream.peek()==std::char_traits<char>::eof(),"trailing bank data");
        frame=std::move(f); error.clear(); return true;
    } catch (const std::exception &e) { error=e.what(); return false; }
}

// D3D's strip winding alternates; fans keep their first vertex as the hub.
enum Topology { Triangles, Strip, Fan };
inline std::vector<uint32_t> TriangleList(const std::vector<uint32_t> &source, Topology topology)
{
    if (topology==Triangles) return source;
    std::vector<uint32_t> result;
    for (size_t i=2;i<source.size();++i) {
        const uint32_t a=topology==Fan?source[0]:source[i-2+(i&1)];
        const uint32_t b=topology==Fan?source[i-1]:source[i-1-(i&1)];
        result.insert(result.end(),{a,b,source[i]});
    }
    return result;
}
} // namespace WaterSceneBank
