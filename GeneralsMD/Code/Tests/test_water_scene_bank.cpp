#include "test_harness.h"
#include "Platform/WaterSceneBank.h"
#include <sstream>

static WaterSceneBank::Frame fixture()
{
    WaterSceneBank::Frame f;
    f.width=2; f.height=2;
    f.colour={2,2,{0,1,2,255,3,4,5,255,6,7,8,255,9,10,11,255}};
    f.depth={0.f,.25f,.75f,1.f}; f.mirror={1,1,{9,8,7,6}}; f.camera={12,34,56};
    WaterSceneBank::Draw d;
    d.program=123; d.fvf=456; d.stride=4; d.vertices={1,2,3,4,5,6,7,8,9,10,11,12}; d.indices={0,1,2};
    d.viewport={0,0,2,2,.1f,.9f};
    for (unsigned i=0;i<16;++i) { d.world[i]=float(i); d.view[i]=float(i+20); d.projection[i]=float(i+40); }
    d.pixelConstants.resize(896*4); d.vertexConstants.resize(256*4);
    for (unsigned i=0;i<d.pixelConstants.size();++i) d.pixelConstants[i]=float(i)*.125f;
    for (unsigned i=0;i<d.vertexConstants.size();++i) d.vertexConstants[i]=-float(i);
    for (unsigned i=0;i<256;++i) d.renderStates[i]=i*1234;
    for (unsigned s=0;s<16;++s) {
        for (unsigned i=0;i<33;++i) d.stageStates[s][i]=s*33+i;
        for (unsigned i=0;i<14;++i) d.samplerStates[s][i]=s*14+i;
        for (unsigned i=0;i<16;++i) d.textureMatrices[s][i]=float(s*16+i);
        if (s!=8) d.textures[s]=f.mirror;
    }
    f.draws={d,d}; return f;
}

TEST(water_scene_bank_round_trip_is_little_endian_and_lossless)
{
    const auto source=fixture(); std::string error; std::ostringstream out(std::ios::binary);
    CHECK(WaterSceneBank::Write(out,source,error));
    const std::string bytes=out.str();
    CHECK_EQ(bytes.substr(0,8),std::string("ZHWBANK1"));
    CHECK_EQ(uint8_t(bytes[8]),1); CHECK_EQ(uint8_t(bytes[9]),0); CHECK_EQ(uint8_t(bytes[12]),2);
    WaterSceneBank::Frame f; std::istringstream in(bytes,std::ios::binary);
    CHECK(WaterSceneBank::Read(in,f,error));
    CHECK(f.colour.bgra==source.colour.bgra); CHECK(f.depth==source.depth); CHECK(f.camera==source.camera);
    CHECK(f.mirror.bgra==source.mirror.bgra); CHECK_EQ(f.draws.size(),source.draws.size());
    for (size_t i=0;i<f.draws.size();++i) {
        const auto &a=f.draws[i],&b=source.draws[i];
        CHECK_EQ(a.program,b.program); CHECK_EQ(a.fvf,b.fvf); CHECK_EQ(a.stride,b.stride);
        CHECK(a.vertices==b.vertices); CHECK(a.indices==b.indices);
        CHECK(a.world==b.world); CHECK(a.view==b.view); CHECK(a.projection==b.projection);
        CHECK_EQ(a.viewport.minZ,b.viewport.minZ); CHECK_EQ(a.viewport.maxZ,b.viewport.maxZ);
        CHECK(a.pixelConstants==b.pixelConstants); CHECK(a.vertexConstants==b.vertexConstants);
        CHECK(a.renderStates==b.renderStates); CHECK(a.stageStates==b.stageStates); CHECK(a.samplerStates==b.samplerStates);
        CHECK(a.textureMatrices==b.textureMatrices); CHECK_EQ(a.mirrorStage,b.mirrorStage);
        for (unsigned stage=0;stage<16;++stage) {
            CHECK_EQ(a.textures[stage].width,b.textures[stage].width);
            CHECK_EQ(a.textures[stage].height,b.textures[stage].height); CHECK(a.textures[stage].bgra==b.textures[stage].bgra);
        }
    }
    std::ostringstream again(std::ios::binary); CHECK(WaterSceneBank::Write(again,f,error)); CHECK_EQ(again.str(),bytes);
    for (size_t length : {size_t(0),size_t(12),bytes.size()/2,bytes.size()-1}) {
        std::istringstream truncated(bytes.substr(0,length));
        CHECK(!WaterSceneBank::Read(truncated,f,error)); CHECK(f.depth==source.depth);
    }
    std::istringstream trailing(bytes+"x"); CHECK(!WaterSceneBank::Read(trailing,f,error));
    auto invalid=source; invalid.draws[0].indices[1]=3;
    std::ostringstream bad; CHECK(!WaterSceneBank::Write(bad,invalid,error)); CHECK(bad.str().empty());
    invalid=source; invalid.depth.pop_back(); CHECK(!WaterSceneBank::Write(bad,invalid,error));
    auto noMirror=source; noMirror.mirror={};
    for (auto &d:noMirror.draws) d.textures[1]={};
    std::ostringstream absent; CHECK(WaterSceneBank::Write(absent,noMirror,error));
    std::istringstream absentInput(absent.str()); CHECK(WaterSceneBank::Read(absentInput,f,error)); CHECK(f.mirror.bgra.empty());
    CHECK(f.draws[0].textures[1].bgra.empty());
    std::string corrupt=bytes; corrupt[8]=2; std::istringstream version(corrupt); CHECK(!WaterSceneBank::Read(version,f,error));
}

TEST(water_scene_bank_expands_strips_and_fans)
{
    const std::vector<uint32_t> source={0,1,2,3,4};
    CHECK(WaterSceneBank::TriangleList(source,WaterSceneBank::Strip)==std::vector<uint32_t>({0,1,2,2,1,3,2,3,4}));
    CHECK(WaterSceneBank::TriangleList(source,WaterSceneBank::Fan)==std::vector<uint32_t>({0,1,2,0,2,3,0,3,4}));
    CHECK(WaterSceneBank::TriangleList({0,1,2},WaterSceneBank::Triangles)==std::vector<uint32_t>({0,1,2}));
}
