/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "Platform/WaterCleanOpt.h"
#include <array>
#include <random>

namespace WaterCleanStreamTest {
using V=std::array<double,3>;
struct Surface {V d={},x={},y={};};
inline void Check() {
    using namespace WaterCleanOpt;
    CHECK(Describe(Parse(nullptr))=="stream,collapse=1.5");
    CHECK(Describe(Parse(""))=="collapse=1.5");
    CHECK(Describe(Parse("half,stream,finelite,loop,fused,collapse=5"))=="stream,fused,loop,finelite,half,collapse=5");
    CHECK(Parse("collapse=nan,collapse=inf,collapse=-1,collapse=181,collapse=3x").collapse==1.5f);
    CHECK(Parse(" collapse=0 ").collapse==0);
    CHECK(Parse("collapse=3,collapse=5").collapse==5);
    const auto rotate=[](V v,double a) {const double c=std::cos(a),s=std::sin(a);return V{c*v[0]-s*v[1],s*v[0]+c*v[1],v[2]};};
    // Independent analytic map with nontrivial displacement, slope, and cross
    // derivatives. LOD attenuation stands in for already sampled mip values.
    const auto sample=[](double qx,double qy,int band,int lod) {
        Surface v;const double k=.017*(band+1),f=1./(1+lod*.37);
        const double z=k*qx+.7*k*qy,h=.3*k*qx-1.1*k*qy;
        v.d={f*std::sin(z),f*.8*std::cos(h),f*.6*std::sin(z+h)};
        v.x={f*k*std::cos(z),-f*.24*k*std::sin(h),f*.78*k*std::cos(z+h)};
        v.y={f*.7*k*std::cos(z),f*.88*k*std::sin(h),-f*.24*k*std::cos(z+h)};
        return v;
    };
    std::mt19937 rng(0x3e17);std::uniform_real_distribution<double> random(0,1);
    for(int trial=0;trial<4000;++trial) {
        const bool upper=trial%2;const double tile=100+2900*random(rng),px=tile*random(rng),py=tile*random(rng);
        double u=random(rng),v=random(rng);if(u+v>1) {u=1-u;v=1-v;}
        if(trial%11==0)u=0;if(trial%13==0)v=0;
        const double sharp=1+7*random(rng),side=upper?-1:1;
        const double b[3]={1-u-v,u,v},bx[3]={-side/tile,side/tile,0},by[3]={-.577350269*side/tile,-.577350269*side/tile,1.154700538*side/tile};
        double a[3],ax[3],ay[3],w[3],wx[3],wy[3],norm2=0,nx=0,ny=0;
        double angle[3],vertex[3][2];
        for(int i=0;i<3;++i) {
            a[i]=std::pow(b[i],sharp);const double da=sharp*std::pow(std::max(b[i],1e-12),sharp-1);
            ax[i]=da*bx[i];ay[i]=da*by[i];norm2+=a[i]*a[i];nx+=a[i]*ax[i];ny+=a[i]*ay[i];
            angle[i]=(2*random(rng)-1)*3.141592653589793;
            const double ix=upper?(i==1?0:1):(i==1?1:0),iy=upper?(i==2?0:1):(i==2?1:0);
            vertex[i][0]=(ix+.5*iy)*tile;vertex[i][1]=.866025404*iy*tile;
        }
        const double norm=std::sqrt(norm2);
        for(int i=0;i<3;++i) {w[i]=a[i]/norm;wx[i]=ax[i]/norm-a[i]*nx/(norm2*norm);wy[i]=ay[i]/norm-a[i]*ny/(norm2*norm);}
        double sum2=0,gx=0,gy=0;for(int i=0;i<3;++i){sum2+=w[i]*w[i];gx+=w[i]*wx[i];gy+=w[i]*wy[i];}
        CHECK_NEAR(sum2,1,1e-12);CHECK_NEAR(gx,0,1e-12);CHECK_NEAR(gy,0,1e-12);
        const int lod[3]={trial%3,trial%2,(trial/2)%4};
        const double gain[3][3]={{.7,.001,-.002},{.8,-.002,.003},{.9,.004,.001}};
        Surface reference[2]={},streamed[2]={};
        // Array evaluator: materialize rotated samples, blend per band, then
        // apply the depth gain product rule, exactly as the pass-2 evaluator.
        for(int l=0;l<2;++l)for(int band=0;band<3;++band) {
            Surface values[3],blend;
            for(int i=0;i<3;++i) {
                const V q=rotate({px-vertex[i][0],py-vertex[i][1],0},-angle[i]);
                const Surface m=sample(vertex[i][0]+q[0],vertex[i][1]+q[1],band,l?lod[band]:0);
                const double c=std::cos(angle[i]),s=std::sin(angle[i]);
                V x,y;for(int k=0;k<3;++k){x[k]=m.x[k]*c-m.y[k]*s;y[k]=m.x[k]*s+m.y[k]*c;}
                values[i]={rotate(m.d,angle[i]),rotate(x,angle[i]),rotate(y,angle[i])};
            }
            for(int i=0;i<3;++i)for(int k=0;k<3;++k) {
                blend.d[k]+=w[i]*values[i].d[k];
                blend.x[k]+=w[i]*values[i].x[k]+wx[i]*values[i].d[k];
                blend.y[k]+=w[i]*values[i].y[k]+wy[i]*values[i].d[k];
            }
            for(int k=0;k<3;++k) {
                reference[l].d[k]+=gain[band][0]*blend.d[k];
                reference[l].x[k]+=gain[band][0]*blend.x[k]+gain[band][1]*blend.d[k];
                reference[l].y[k]+=gain[band][0]*blend.y[k]+gain[band][2]*blend.d[k];
            }
        }
        // Stream evaluator: one vertex/rotation/coordinate, then each band's
        // physical and optional shading sample, accumulating directly.
        for(int i=0;i<3;++i) {
            const V q=rotate({px-vertex[i][0],py-vertex[i][1],0},-angle[i]);
            const V qx=rotate({1,0,0},-angle[i]),qy=rotate({0,1,0},-angle[i]);
            // Reconstruct this vertex's weight/gradient from the compact
            // context, independently of the array evaluator's stored values.
            const double wi=std::pow(b[i],sharp)/norm,da=sharp*std::pow(std::max(b[i],1e-12),sharp-1);
            const double wxi=(da*bx[i]-wi*(nx/norm))/norm,wyi=(da*by[i]-wi*(ny/norm))/norm;
            CHECK_NEAR(wi,w[i],1e-12);CHECK_NEAR(wxi,wx[i],1e-12);CHECK_NEAR(wyi,wy[i],1e-12);
            for(int band=0;band<3;++band) {
                Surface physical;
                for(int l=0;l<2;++l) {
                    Surface m;
                    if(!l)physical=sample(vertex[i][0]+q[0],vertex[i][1]+q[1],band,0);
                    m=l && lod[band]>0?sample(vertex[i][0]+q[0],vertex[i][1]+q[1],band,lod[band]):physical;
                    V dx,dy;for(int k=0;k<3;++k){dx[k]=m.x[k]*qx[0]+m.y[k]*qx[1];dy[k]=m.x[k]*qy[0]+m.y[k]*qy[1];}
                    m={rotate(m.d,angle[i]),rotate(dx,angle[i]),rotate(dy,angle[i])};
                    for(int k=0;k<3;++k) {
                        const double d=wi*m.d[k],x=wi*m.x[k]+wxi*m.d[k],y=wi*m.y[k]+wyi*m.d[k];
                        streamed[l].d[k]+=gain[band][0]*d;
                        streamed[l].x[k]+=gain[band][0]*x+gain[band][1]*d;
                        streamed[l].y[k]+=gain[band][0]*y+gain[band][2]*d;
                    }
                }
            }
        }
        for(int l=0;l<2;++l)for(int k=0;k<3;++k) {
            CHECK_NEAR(reference[l].d[k],streamed[l].d[k],1e-12);
            CHECK_NEAR(reference[l].x[k],streamed[l].x[k],1e-12);
            CHECK_NEAR(reference[l].y[k],streamed[l].y[k],1e-12);
        }
    }
}
}
