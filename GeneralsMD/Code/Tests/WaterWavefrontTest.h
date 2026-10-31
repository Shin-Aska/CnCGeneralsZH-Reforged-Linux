#pragma once
#include "Platform/WaterWavefront.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace WaterWavefrontTest {
using namespace WaterWavefront;

inline void CheckAffine() {
    for(double spacing:{.7,10.0,57.0}) for(double gx:{-2.7,.0,.83}) for(double gy:{-1.3,.0,3.1}) {
        std::array<Corner,4> corners;
        for(unsigned i=0;i<4;++i) {
            const double x=(i%2)*spacing,y=(i/2)*spacing;
            corners[i]={37+gx*x+gy*y,gx,gy,.4+.002*x+.003*y+.0001*x*y,true};
        }
        for(double fx:{0.0,.17,.5,.91,1.0}) for(double fy:{0.0,.23,.7,1.0}) {
            const double x=fx*spacing,y=fy*spacing;
            const Parameters p=Interpolate(corners,fx,fy,spacing);
            CHECK_NEAR(p.phase,37+gx*x+gy*y,2e-12*(1+spacing));
            CHECK_NEAR(p.gradientX,gx,2e-11);CHECK_NEAR(p.gradientY,gy,2e-11);
            CHECK_NEAR(p.hessianXX,0,2e-10);CHECK_NEAR(p.hessianXY,0,2e-10);CHECK_NEAR(p.hessianYY,0,2e-10);
            CHECK_NEAR(p.amplitude,.4+.002*x+.003*y+.0001*x*y,2e-12);
            CHECK_NEAR(p.amplitudeX,.002+.0001*y,2e-12);CHECK_NEAR(p.amplitudeY,.003+.0001*x,2e-12);
        }
    }
}

inline void CheckCellJoins() {
    // Both patches share their edge values and first partials. The remaining
    // corners deliberately disagree strongly; bilinear U cannot pass the normal
    // derivative check, nor can independent rotations about the world origin.
    const Corner original[6]={{-13,.8,-.3,.5,true},{2,-.7,1.2,.7,true},{31,1.8,.4,.9,true},
        {-21,-1.3,.9,.8,true},{17,.3,-.6,.6,true},{7,-.2,-1.1,.4,true}};
    const double spacing=13;
    double oldBilinearMismatch=0;
    for(unsigned mask=1;mask<64;++mask) {
    // Ghost phase/partials remain finite even at dry corners. Adjacent wet and
    // mixed patches must use that same polynomial, with only amplitude gated.
    if(!(mask&27u) || !(mask&54u))continue;
    Corner samples[6];for(unsigned i=0;i<6;++i){samples[i]=original[i];samples[i].valid=(mask&(1u<<i))!=0;}
    const std::array<Corner,4> left={samples[0],samples[1],samples[3],samples[4]};
    const std::array<Corner,4> right={samples[1],samples[2],samples[4],samples[5]};
    for(double fy:{0.0,.07,.23,.5,.81,1.0}) {
        const Parameters a=Interpolate(left,1,fy,spacing),b=Interpolate(right,0,fy,spacing);
        CHECK_NEAR(a.phase,b.phase,1e-12);
        CHECK_NEAR(a.gradientX,b.gradientX,1e-12);CHECK_NEAR(a.gradientY,b.gradientY,1e-12);
        CHECK_NEAR(a.hessianXX,b.hessianXX,1e-12);CHECK_NEAR(a.hessianXY,b.hessianXY,1e-12);CHECK_NEAR(a.hessianYY,b.hessianYY,1e-12);
        CHECK_NEAR(a.amplitude,b.amplitude,1e-12);CHECK_NEAR(a.amplitudeY,b.amplitudeY,1e-12);
        const double step=1e-6;
        const Parameters nearA=Interpolate(left,1-step,fy,spacing),nearB=Interpolate(right,step,fy,spacing);
        CHECK(std::abs(nearA.phase-nearB.phase)<.001);
        CHECK(std::abs(nearA.gradientX-nearB.gradientX)<.001);
        CHECK(std::abs(nearA.gradientY-nearB.gradientY)<.001);
        // Interpolated gradient payloads can agree while the reconstructed scalar
        // phase has a kink. Independently measure both one-sided normal slopes.
        const double leftSlope=(a.phase-nearA.phase)/(step*spacing),rightSlope=(nearB.phase-b.phase)/(step*spacing);
        CHECK(std::abs(leftSlope-rightSlope)<.001);
        CHECK(std::abs(leftSlope-a.gradientX)<.001 && std::abs(rightSlope-b.gradientX)<.001);
        // Second-order one-sided differences of scalar phase independently
        // measure curvature on both sides; copied Hessian payloads cannot pass.
        const double h=.0001,worldStep=h*spacing;
        const double leftCurvature=(2*a.phase-5*Interpolate(left,1-h,fy,spacing).phase
            +4*Interpolate(left,1-2*h,fy,spacing).phase-Interpolate(left,1-3*h,fy,spacing).phase)/(worldStep*worldStep);
        const double rightCurvature=(2*b.phase-5*Interpolate(right,h,fy,spacing).phase
            +4*Interpolate(right,2*h,fy,spacing).phase-Interpolate(right,3*h,fy,spacing).phase)/(worldStep*worldStep);
        CHECK(std::abs(leftCurvature-rightCurvature)<.0001);
        CHECK(std::abs(leftCurvature-a.hessianXX)<.0001 && std::abs(rightCurvature-b.hessianXX)<.0001);
        const double oldLeft=((samples[1].phase-samples[0].phase)*(1-fy)+(samples[4].phase-samples[3].phase)*fy)/spacing;
        const double oldRight=((samples[2].phase-samples[1].phase)*(1-fy)+(samples[5].phase-samples[4].phase)*fy)/spacing;
        oldBilinearMismatch=(std::max)(oldBilinearMismatch,std::abs(oldLeft-oldRight));
    }
    }
    CHECK(oldBilinearMismatch>2); // Arm the C1 check against the previous interpolation.
}

inline void CheckCurvedPatch() {
    // Zero endpoint curvature is an explicit reconstruction choice. For samples
    // from U=x² at x=0,1, its unique quintic constraints give U=2x³-x⁴,
    // rather than exact quadratic reproduction: the center height is 3/16 and
    // the center curvature is 3. This pins the intended interior tradeoff.
    const std::array<Corner,4> corners={Corner{0,0,0,1,true},Corner{1,2,0,1,true},
        Corner{0,0,0,1,true},Corner{1,2,0,1,true}};
    for(double fy:{0.0,.23,.5,.91,1.0}) {
        const Parameters p=Interpolate(corners,.5,fy,1);
        CHECK_NEAR(p.phase,3.0/16,1e-12);CHECK_NEAR(p.gradientX,1,1e-12);CHECK_NEAR(p.hessianXX,3,1e-12);
        CHECK_NEAR(p.gradientY,0,1e-12);CHECK_NEAR(p.hessianXY,0,1e-12);CHECK_NEAR(p.hessianYY,0,1e-12);
        CHECK_NEAR(std::abs(p.phase-.25),1.0/16,1e-12);
        for(double fx:{0.0,1.0})CHECK_NEAR(Interpolate(corners,fx,fy,1).hessianXX,0,1e-12);
    }
}

inline void CheckDerivatives() {
    const std::array<Corner,4> original={Corner{-8,-.7,1.4,.25,true},Corner{17,1.9,-.2,.91,true},
        Corner{4,.3,-1.1,.73,true},Corner{-3,-1.7,.8,.38,true}};
    double maximumGradientError=0,maximumHessianError=0;
    // Every nonempty patch retains the same Hermite phase, including finite dry
    // ghost data. Wet validity gates amplitude, so no quotient seam can appear.
    for(unsigned mask=0;mask<16;++mask) for(double spacing:{.7,13.0,53.0}) {
        auto corners=original;
        for(unsigned i=0;i<4;++i)corners[i].valid=(mask&(1u<<i))!=0;
        for(double fx:{.13,.43,.83}) for(double fy:{.19,.57,.89}) {
            auto at=[&](double x,double y){return Interpolate(corners,x,y,spacing);};
            const Parameters p=at(fx,fy);
            const double values[]={p.phase,p.gradientX,p.gradientY,p.hessianXX,p.hessianXY,p.hessianYY,p.amplitude,p.amplitudeX,p.amplitudeY};
            for(double value:values) CHECK(std::isfinite(value));
            if(mask==0) {for(double value:values) CHECK(value==0);continue;}
            const Parameters full=Interpolate(original,fx,fy,spacing);
            CHECK(p.phase==full.phase && p.gradientX==full.gradientX && p.gradientY==full.gradientY);
            CHECK(p.hessianXX==full.hessianXX && p.hessianXY==full.hessianXY && p.hessianYY==full.hessianYY);
            double wetAmplitude=0;
            for(unsigned i=0;i<4;++i)if(mask&(1u<<i))wetAmplitude+=original[i].amplitude*(i%2?fx:1-fx)*(i/2?fy:1-fy);
            CHECK_NEAR(p.amplitude,wetAmplitude,1e-12);
            const double h=.0001,worldStep=h*spacing;
            const Parameters xp=at(fx+h,fy),xm=at(fx-h,fy),yp=at(fx,fy+h),ym=at(fx,fy-h);
            // Differentiate only returned scalar values: no production basis,
            // gradient helper, or quotient expression enters this oracle.
            const double dx=(xp.phase-xm.phase)/(2*worldStep),dy=(yp.phase-ym.phase)/(2*worldStep);
            const double xx=(xp.phase-2*p.phase+xm.phase)/(worldStep*worldStep);
            const double yy=(yp.phase-2*p.phase+ym.phase)/(worldStep*worldStep);
            const double xy=(at(fx+h,fy+h).phase-at(fx+h,fy-h).phase-at(fx-h,fy+h).phase+at(fx-h,fy-h).phase)/(4*worldStep*worldStep);
            const double gradientTolerance=2e-5/spacing,hessianTolerance=.0002/(spacing*spacing);
            const double gxError=std::abs(p.gradientX-dx),gyError=std::abs(p.gradientY-dy);
            maximumGradientError=(std::max)(maximumGradientError,(std::max)(gxError,gyError));
            maximumHessianError=(std::max)(maximumHessianError,(std::max)(std::abs(p.hessianXX-xx),(std::max)(std::abs(p.hessianXY-xy),std::abs(p.hessianYY-yy))));
            CHECK_NEAR(p.gradientX,dx,gradientTolerance);CHECK_NEAR(p.gradientY,dy,gradientTolerance);
            CHECK_NEAR(p.hessianXX,xx,hessianTolerance);CHECK_NEAR(p.hessianXY,xy,hessianTolerance);CHECK_NEAR(p.hessianYY,yy,hessianTolerance);
            CHECK_NEAR(p.amplitudeX,(xp.amplitude-xm.amplitude)/(2*worldStep),gradientTolerance);
            CHECK_NEAR(p.amplitudeY,(yp.amplitude-ym.amplitude)/(2*worldStep),gradientTolerance);
        }
        for(double fx:{0.0,1.0}) for(double fy:{0.0,1.0}) {
            const Parameters p=Interpolate(corners,fx,fy,spacing);
            const double values[]={p.phase,p.gradientX,p.gradientY,p.hessianXX,p.hessianXY,p.hessianYY,p.amplitude,p.amplitudeX,p.amplitudeY};
            for(double value:values)CHECK(std::isfinite(value));
        }
    }
    // Approach the dry corner with just the opposite corner wet. Normalizing
    // wet support would retain a full-strength wave or introduce a singularity.
    auto oneWet=original;for(unsigned i=0;i<4;++i)oneWet[i].valid=i==0;
    for(double spacing:{.7,13.0,53.0}) for(double epsilon:{.1,.001,.000001,0.0}) {
        const double f=1-epsilon;const Parameters p=Interpolate(oneWet,f,f,spacing);
        CHECK_NEAR(p.amplitude,original[0].amplitude*epsilon*epsilon,1e-16);
        CHECK_NEAR(p.amplitudeX,-original[0].amplitude*epsilon/spacing,1e-15);
        CHECK_NEAR(p.amplitudeY,-original[0].amplitude*epsilon/spacing,1e-15);
        CHECK(std::isfinite(p.phase) && std::abs(p.phase)<1000);
        CHECK(std::isfinite(p.gradientX) && std::isfinite(p.gradientY));
        CHECK(std::abs(p.hessianXX)<5000/(spacing*spacing) && std::abs(p.hessianXY)<5000/(spacing*spacing) && std::abs(p.hessianYY)<5000/(spacing*spacing));
        const Parameters full=Interpolate(original,f,f,spacing);
        CHECK(p.phase==full.phase && p.gradientX==full.gradientX && p.gradientY==full.gradientY);
    }
    std::printf("wavefront scalar finite differences: max gradient %.9g, Hessian %.9g; all 16 validity masks\n",maximumGradientError,maximumHessianError);
}

}
