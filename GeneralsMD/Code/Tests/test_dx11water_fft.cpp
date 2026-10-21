#include "WaterDx11SurfaceFixture.h"
#include "WaterFftReference.h"

namespace {
using namespace WaterDx11SurfaceTest;
struct FftVertex { float x,y,z,nx,ny,nz; unsigned colour; float u,v,damping,level; };
const Pixel Background = {0.32f,0.42f,0.52f,1};

void adapter(Fixture &f)
{
    IDXGIDevice *dxgi=NULL;IDXGIAdapter *gpu=NULL;DXGI_ADAPTER_DESC description={};
    CHECK(SUCCEEDED(f.device.Get_Device()->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void **>(&dxgi))));
    if(dxgi)CHECK(SUCCEEDED(dxgi->GetAdapter(&gpu)));
    UINT support=0;CHECK(SUCCEEDED(f.device.Get_Device()->CheckFormatSupport(DXGI_FORMAT_R32G32B32A32_FLOAT,&support)));
    if(gpu){CHECK(SUCCEEDED(gpu->GetDesc(&description)));
        LARGE_INTEGER version={};const HRESULT versionResult=gpu->CheckInterfaceSupport(__uuidof(IDXGIDevice),&version);
        std::printf("  fft adapter-format-support-rgba32f=%u driver-version-query-hr=%ld driver-version-raw=%lld driver-version-available=%u\n",support,long(versionResult),static_cast<long long>(version.QuadPart),SUCCEEDED(versionResult));
        std::printf("  fft adapter=%ls vendor=%u device=%u software=%u feature=%u debug=%u\n",description.Description,
            description.VendorId,description.DeviceId,f.device.Is_Software(),unsigned(f.device.Get_Feature_Level()),f.device.Has_Debug_Layer());}
    Release(gpu);Release(dxgi);
}

bool changed(const Pixel &p)
{
    return std::fabs(p.r-Background.r)+std::fabs(p.g-Background.g)+std::fabs(p.b-Background.b)>0.005f;
}

struct Fields {
    Texture displacement[3];
    ID3D11Texture2D *array;
    ID3D11ShaderResourceView *arrayView;
    double height;
    Fields():array(NULL),arrayView(NULL),height(0){}
    ~Fields(){Release(arrayView);Release(array);}
    bool Initialise(Fixture &f)
    {
        WaterFftReference::PackedSpectra spectra=WaterFftReference::MakePackedSpectra(4);
        spectra[1][2*4+2].real=0.02;
        const WaterFftReference::LayerPair independent=WaterFftReference::TransformPackedFields(spectra,4);
        height=independent[0].pixels[1];
        CHECK_NEAR(height,0.02,1e-12);
        for(unsigned i=0;i<16;++i)CHECK_NEAR(independent[0].pixels[i*4+1],height,1e-12);
        Pixel maps[6]={};maps[0].g=static_cast<float>(height);
        for(unsigned i=0;i<3;++i)
            if(!displacement[i].Set(f.device.Get_Device(),maps[i*2]))return false;
        D3D11_TEXTURE2D_DESC desc={};desc.Width=desc.Height=desc.MipLevels=1;
        desc.ArraySize=6;desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data[6]={};
        for(unsigned i=0;i<6;++i){data[i].pSysMem=&maps[i];data[i].SysMemPitch=sizeof(Pixel);}
        if(FAILED(f.device.Get_Device()->CreateTexture2D(&desc,data,&array))
            ||FAILED(f.device.Get_Device()->CreateShaderResourceView(array,NULL,&arrayView)))return false;
        for(unsigned i=0;i<3;++i){
            f.water->Set_Test_Resource(&displacement[i],displacement[i].view);
            f.request.inputs.surface.fftDisplacement[i]={&displacement[i],WaterRenderPacket::ResourceType::Texture2D,true,1,1,1,1};
        }
        f.water->Set_Test_Resource(this,arrayView);
        f.request.inputs.surface.fftArray={this,WaterRenderPacket::ResourceType::Texture2DArray,true,1,1,6,6};
        return true;
    }
};

void geometry(Fixture &f,FftVertex (&vertices)[4],float z)
{
    const FftVertex quad[4]={{-.5f,-.25f,z,1,1,1,0xffffffff,0,1,1,0},
        {-.5f,.25f,z,1,1,1,0xffffffff,0,0,1,0},
        {.5f,.25f,z,1,1,1,0xffffffff,1,0,1,0},
        {.5f,-.25f,z,1,1,1,0xffffffff,1,1,1,0}};
    std::memcpy(vertices,quad,sizeof(quad));
    f.batches[0].vertices=vertices;f.batches[0].vertexBytes=sizeof(vertices);
    f.batches[0].vertexLayout=0x252;f.batches[0].vertexStride=sizeof(FftVertex);
    f.request.inputs.scene.projection[9]=1;f.request.inputs.scene.projection[13]=2;
    f.backend.Set_Transform(D3DTS_PROJECTION,f.request.inputs.scene.projection);
    f.request.inputs.surface.heightWaveCageAvailable=false;
    f.request.inputs.surface.fftParameters={0,0,0,0};
}

bool render(Fixture &f,const char *label,std::vector<Pixel> &pixels)
{
    bool issued=false;f.Draw(label,&issued);CHECK(issued);
    const bool ok=f.Read(pixels);CHECK(ok);
    if(!ok)return false;
    const Pixel upper=pixels[14*Fixture::Width+32],middle=pixels[24*Fixture::Width+32];
    std::printf("  fft raster=%s upper[32,14]=%.9g,%.9g,%.9g,%.9g middle[32,24]=%.9g,%.9g,%.9g,%.9g changed=%u,%u\n",
        label,upper.r,upper.g,upper.b,upper.a,middle.r,middle.g,middle.b,middle.a,changed(upper),changed(middle));
    std::fflush(stdout);return issued;
}
}

TEST(fft_analytic_pin)
{
    Fixture f;CHECK(f.Initialise());if(!f.water)return;adapter(f);
    CHECK(f.Scene());if(!f.colourSurface)return;
    FftVertex vertices[4];geometry(f,vertices,0);
    std::vector<Pixel> pixels;if(!render(f,"analytic-original",pixels))return;
    CHECK(!changed(pixels[14*Fixture::Width+32]));CHECK(changed(pixels[24*Fixture::Width+32]));
    CHECK(f.Scene());geometry(f,vertices,.4f);
    if(!render(f,"analytic-positive-height-control",pixels))return;
    CHECK(changed(pixels[14*Fixture::Width+32]));CHECK(!changed(pixels[24*Fixture::Width+32]));
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}

TEST(fft_displacement_dc_oracle)
{
    Fixture f;CHECK(f.Initialise());if(!f.water)return;adapter(f);
    CHECK(f.Scene());if(!f.colourSurface)return;
    FftVertex vertices[4];geometry(f,vertices,0);
    Fields fields;CHECK(fields.Initialise(f));if(!fields.arrayView)return;
    f.request.fftEligibility=WaterRenderBridge::FftEligibility::ProjectedFlat;
    CHECK(f.water->Set_Test_Fft_Fields(fields.displacement[0].view,fields.displacement[1].view,fields.displacement[2].view,fields.arrayView));
    f.request.inputs.surface.fftAvailable=true;
    std::printf("  fft isolatedVS override=on; independent centered DFT side=4 channel=height DC=%.17g all-other-fields=0 expected-positive-Z; probe interior for Z in [0.35,0.45], projected clip y in [0.1,0.7], no clipping\n",fields.height);
    std::vector<Pixel> pixels;if(!render(f,"DC-field-displacement",pixels))return;
    CHECK(changed(pixels[14*Fixture::Width+32]));
    CHECK(!changed(pixels[24*Fixture::Width+32]));
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
    CHECK(f.water->Set_Test_Fft_Fields(NULL,NULL,NULL,NULL));
    f.water->Clear_Test_Resources();
}


#include "WaterDx11FftNumericalTests.h"

#include "WaterDx11FftLifecycleTests.h"

#include "WaterDx11FftGeometryTests.h"
#include "WaterDx11FftStateTests.h"
#include "WaterDx11FftFailureTests.h"


#include "WaterDx11FftEndToEndTests.h"

