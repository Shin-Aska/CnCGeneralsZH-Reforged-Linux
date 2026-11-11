#pragma once
#include "Platform/WaterCleanCollapse.h"
#include <array>
namespace WaterCleanCollapseTest {
inline void Check() {
    using namespace WaterCleanCollapse;
    const float w[3]={.3f,.5f,.7f},wx[3]={.1f,-.2f,.05f},wy[3]={-.2f,.1f,.08f};
    float angles[3]={0,0,AngleRadians*.999f};
    CHECK(Evaluate(angles,w,wx,wy).collapse);
    angles[2]=AngleRadians;CHECK(!Evaluate(angles,w,wx,wy).collapse);
    angles[2]=AngleRadians*1.001f;CHECK(!Evaluate(angles,w,wx,wy).collapse);
    // Equal nonzero turns keep the mean but never collapse: each vertex pivots about itself.
    for(float a:{0.f,.4f,-2.f,3.14159265f,1.04719755f}) {
        const float equal[3]={a,a,a};const auto m=Evaluate(equal,w,wx,wy);
        CHECK(m.collapse==(a==0));CHECK(m.angle==a);CHECK(m.dx==0 && m.dy==0);
    }
    const float wrapped[3]={3.14f,-3.14f,3.139f};CHECK(!Evaluate(wrapped,w,wx,wy).collapse);
    const float turnedOnce[3]={6.2831853f,AngleRadians*.5f,-AngleRadians*.5f};CHECK(Evaluate(turnedOnce,w,wx,wy).collapse);
    const float far[3]={0,AngleRadians,-AngleRadians};CHECK(!Evaluate(far,w,wx,wy).collapse);
    const float nan[3]={0,0,std::nanf("")};CHECK(!Evaluate(nan,w,wx,wy).collapse);
    // The USA 2 carrier: a uniform 60-degree shore turn. The one-read map q(p) about the moving
    // mean pivot of sharp-3 weights folds (negative Jacobian) there; at the threshold it is
    // still a near-identity map. Finite differences over the whole triangle.
    auto worstMap=[](double turn,double &stretch,double &fold) {
        const double tile=300,v[3][2]={{0,0},{tile,0},{tile*.5,tile*.866025404}};
        auto q=[&](double px,double py,double out[2]) {
            const double b2=py/(tile*.866025404),b1=px/tile-.5*b2,b[3]={1-b1-b2,b1,b2};
            double a[3],norm=0,sum=0,mx=0,my=0;
            for(int i=0;i<3;++i){const double c=std::max(0.,std::min(1.,b[i]));a[i]=c*c*c;norm+=a[i]*a[i];}
            for(int i=0;i<3;++i){a[i]/=std::sqrt(norm);sum+=a[i];mx+=a[i]*v[i][0];my+=a[i]*v[i][1];}
            mx/=sum;my/=sum;const double c=std::cos(-turn),s=std::sin(-turn);
            out[0]=mx+c*(px-mx)-s*(py-my);out[1]=my+s*(px-mx)+c*(py-my);
        };
        stretch=0;fold=1e9;const double e=1e-3;
        for(int j=1;j<40;++j)for(int i=1;i<40;++i) {
            const double b2=j/40.,b1=i/40.;if(b1+b2>=1)continue;
            const double px=tile*(b1+.5*b2),py=tile*.866025404*b2;
            double o[2],x[2],y[2];q(px,py,o);q(px+e,py,x);q(px,py+e,y);
            const double a=(x[0]-o[0])/e,b=(y[0]-o[0])/e,c=(x[1]-o[1])/e,d=(y[1]-o[1])/e;
            const double t=a*a+b*b+c*c+d*d,det=a*d-b*c;
            stretch=std::max(stretch,std::sqrt(.5*(t+std::sqrt(std::max(0.,t*t-4*det*det)))));fold=std::min(fold,det);
        }
    };
    double stretch,fold;
    worstMap(1.04719755,stretch,fold);CHECK(fold<0);CHECK(stretch>3);
    const float carrier[3]={1.04719755f,1.04719755f,1.04719755f};CHECK(!Evaluate(carrier,w,wx,wy).collapse);
    worstMap(AngleRadians,stretch,fold);CHECK(fold>.9);CHECK(stretch<1.1);
    CHECK(ShadingLod==.25f);CHECK(AngleDegrees==1.5f);
    // Independent finite differences of S*R(a)*d(v+R(-a)*(p-v))*gain.
    // This includes changing angle, pivot, L2-normalized weights and depth gain.
    using V=std::array<double,3>;
    const double vertices[3][2]={{0,0},{100,0},{50,86.6025404}};
    auto rotate=[](double x,double y,double a){return std::array<double,2>{std::cos(a)*x-std::sin(a)*y,std::sin(a)*x+std::cos(a)*y};};
    auto evaluate=[&](double px,double py,bool derivative,int axis) {
        double b[3]={1-px/100-py/173.2050808,px/100-py/173.2050808,py/86.6025404};
        const double bx[3]={-.01,.01,0},by[3]={-1/173.2050808,-1/173.2050808,1/86.6025404};
        double weight[3],dw[3],norm=0,nd=0;
        for(int i=0;i<3;++i){weight[i]=b[i]*b[i]*b[i];dw[i]=3*b[i]*b[i]*(axis?by[i]:bx[i]);norm+=weight[i]*weight[i];nd+=weight[i]*dw[i];}
        norm=std::sqrt(norm);
        double sum=0,ds=0,angle=0,da=0,vx=0,vy=0,dvx=0,dvy=0;
        const double a[3]={.4,.408,.418};
        for(int i=0;i<3;++i) {
            dw[i]=dw[i]/norm-weight[i]*nd/(norm*norm*norm);weight[i]/=norm;
            sum+=weight[i];ds+=dw[i];angle+=weight[i]*a[i];da+=dw[i]*a[i];
            vx+=weight[i]*vertices[i][0];vy+=weight[i]*vertices[i][1];
            dvx+=dw[i]*vertices[i][0];dvy+=dw[i]*vertices[i][1];
        }
        angle/=sum;da=(da-angle*ds)/sum;
        vx/=sum;vy/=sum;dvx=(dvx-vx*ds)/sum;dvy=(dvy-vy*ds)/sum;
        const auto r=rotate(px-vx,py-vy,-angle);
        const double qx=vx+r[0],qy=vy+r[1];
        const auto qr=rotate((axis?0:1)-dvx,(axis?1:0)-dvy,-angle);
        const double qdx=dvx+qr[0]+r[1]*da,qdy=dvy+qr[1]-r[0]*da;
        const V d={std::sin(.031*qx)+.1*std::cos(.041*qy),.2*std::sin(.023*qy),std::cos(.019*qx+.017*qy)};
        const V jx={.031*std::cos(.031*qx),0,-.019*std::sin(.019*qx+.017*qy)};
        const V jy={-.0041*std::sin(.041*qy),.0046*std::cos(.023*qy),-.017*std::sin(.019*qx+.017*qy)};
        const auto rd=rotate(d[0],d[1],angle);
        const double gain=.6+.001*px+.002*py,dg=axis?.002:.001;
        if(!derivative)return V{gain*sum*rd[0],gain*sum*rd[1],gain*sum*d[2]};
        V jd;for(int k=0;k<3;++k)jd[k]=jx[k]*qdx+jy[k]*qdy;
        jd[0]-=d[1]*da;jd[1]+=d[0]*da;
        const auto rj=rotate(jd[0],jd[1],angle);
        return V{gain*(sum*rj[0]+ds*rd[0])+dg*sum*rd[0],
            gain*(sum*rj[1]+ds*rd[1])+dg*sum*rd[1],gain*(sum*jd[2]+ds*d[2])+dg*sum*d[2]};
    };
    for(double x:{25.,40.,55.})for(double y:{12.,23.})for(int axis=0;axis<2;++axis) {
        const double e=.0001;
        const V plus=evaluate(x+(axis?0:e),y+(axis?e:0),false,axis),minus=evaluate(x-(axis?0:e),y-(axis?e:0),false,axis),analytic=evaluate(x,y,true,axis);
        for(int k=0;k<3;++k)CHECK_NEAR(analytic[k],(plus[k]-minus[k])/(2*e),1e-8);
    }
}
}
