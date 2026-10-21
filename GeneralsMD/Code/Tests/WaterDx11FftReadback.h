#pragma once
#include "WaterDx11SurfaceFixture.h"
#include "WaterFftReference.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace WaterDx11FftTest {
using WaterDx11SurfaceTest::Release;

struct Image {
    unsigned side;
    std::vector<float> pixels;
    Image():side(0){}
};

inline std::uint64_t Fingerprint(const Image &image)
{
    std::uint64_t result=1469598103934665603ull;
    const unsigned char *bytes=reinterpret_cast<const unsigned char *>(image.pixels.data());
    for(std::size_t i=0;i<image.pixels.size()*sizeof(float);++i)
        result=(result^bytes[i])*1099511628211ull;
    return result;
}

inline bool Read(ID3D11Device *device,ID3D11DeviceContext *context,
    ID3D11ShaderResourceView *view,unsigned layer,unsigned mip,Image &result)
{
    if(!view)return false;
    ID3D11Resource *resource=NULL;ID3D11Texture2D *texture=NULL,*staging=NULL;
    view->GetResource(&resource);
    const HRESULT query=resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void **>(&texture));
    Release(resource);if(FAILED(query))return false;
    D3D11_TEXTURE2D_DESC desc={};texture->GetDesc(&desc);
    const bool valid=desc.Format==DXGI_FORMAT_R32G32B32A32_FLOAT
        &&layer<desc.ArraySize&&mip<desc.MipLevels&&desc.SampleDesc.Count==1;
    if(!valid){Release(texture);return false;}
    const unsigned subresource=D3D11CalcSubresource(mip,layer,desc.MipLevels);
    desc.Width=(std::max)(1u,desc.Width>>mip);desc.Height=(std::max)(1u,desc.Height>>mip);
    desc.MipLevels=desc.ArraySize=1;desc.Usage=D3D11_USAGE_STAGING;
    desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    bool ok=desc.Width==desc.Height&&SUCCEEDED(device->CreateTexture2D(&desc,NULL,&staging));
    if(ok){
        context->CopySubresourceRegion(staging,0,0,0,0,texture,subresource,NULL);
        D3D11_MAPPED_SUBRESOURCE mapped={};
        ok=SUCCEEDED(context->Map(staging,0,D3D11_MAP_READ,0,&mapped));
        if(ok){
            result.side=desc.Width;result.pixels.resize(std::size_t(desc.Width)*desc.Height*4);
            for(unsigned y=0;y<desc.Height;++y)std::memcpy(&result.pixels[y*desc.Width*4],
                static_cast<const unsigned char *>(mapped.pData)+y*mapped.RowPitch,desc.Width*4*sizeof(float));
            context->Unmap(staging,0);
        }
    }
    Release(staging);Release(texture);return ok;
}

inline bool Compare(const Image &actual,const WaterFftReference::Image &expected,
    const char *label,unsigned layer,unsigned mip)
{
    if(actual.side!=expected.side||actual.pixels.size()!=expected.pixels.size())return false;
    unsigned failures=0;double maxError=0;
    for(std::size_t i=0;i<actual.pixels.size();++i){
        maxError=(std::max)(maxError,std::fabs(double(actual.pixels[i])-expected.pixels[i]));
        if(!WaterFftReference::WithinFp32(actual.pixels[i],expected.pixels[i])){
            if(!failures)std::printf("  fft mismatch case=%s layer=%u mip=%u scalar=%zu actual=%.17g expected=%.17g\n",
                label,layer,mip,i,double(actual.pixels[i]),expected.pixels[i]);
            ++failures;
        }
    }
    const unsigned x=actual.side/3,y=actual.side/2;
    const std::size_t at=(std::size_t(y)*actual.side+x)*4;
    std::printf("  fft readback case=%s layer=%u mip=%u side=%u hash=%016llX failures=%u maxError=%.17g point=%u,%u actual=%.9g,%.9g,%.9g,%.9g expected=%.17g,%.17g,%.17g,%.17g\n",
        label,layer,mip,actual.side,static_cast<unsigned long long>(Fingerprint(actual)),failures,maxError,x,y,
        double(actual.pixels[at]),double(actual.pixels[at+1]),double(actual.pixels[at+2]),double(actual.pixels[at+3]),
        expected.pixels[at],expected.pixels[at+1],expected.pixels[at+2],expected.pixels[at+3]);
    std::fflush(stdout);return failures==0;
}

inline std::array<WaterFftReference::InitialSpectrum,3> IndependentInputs(const std::vector<float> &input)
{
    const unsigned side=WaterFftReference::CanonicalResolution;
    if(input.size()!=std::size_t(3)*side*side*4)throw std::invalid_argument("native FFT test initial spectrum extent");
    std::array<WaterFftReference::InitialSpectrum,3> result;
    for(unsigned c=0;c<3;++c){
        result[c].resize(side*side);
        for(unsigned at=0;at<side*side;++at)for(unsigned channel=0;channel<4;++channel)
            result[c][at][channel]=input[(std::size_t(c)*side*side+at)*4+channel];
    }
    return result;
}
}

