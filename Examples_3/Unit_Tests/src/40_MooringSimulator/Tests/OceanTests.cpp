#include "../Water/Ocean.h"
#include "Common_3/Utilities/Interfaces/ITime.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "Common_3/Utilities/Interfaces/IMemory.h"
using namespace mooring;
static unsigned failures;
#define CHECK(x) do { if(!(x)) { printf("FAIL line %d: %s\n",__LINE__,#x); ++failures; } } while(0)
int main() {
    initMemAlloc(nullptr);
    Complex input[64],actual[64],scratch[64],twiddles[4];
    for(unsigned i=0;i<64;++i) input[i]=actual[i]={std::sin(i*.37f)*.1f,std::cos(i*.19f)*.1f};
    for(unsigned i=0;i<4;++i) twiddles[i]={std::cos(6.2831853f*i/8),std::sin(6.2831853f*i/8)};
    inverseFFT(actual,scratch,twiddles,8);
    float error=0;
    for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x) {
        Complex expected={};
        for(unsigned ky=0;ky<8;++ky) for(unsigned kx=0;kx<8;++kx) {
            double phase=6.283185307179586*(kx*x+ky*y)/8;
            auto v=input[ky*8+kx];
            expected.real+=v.real*std::cos(phase)-v.imaginary*std::sin(phase);
            expected.imaginary+=v.real*std::sin(phase)+v.imaginary*std::cos(phase);
        }
        error=std::max(error,std::fabs(expected.real-actual[y*8+x].real));
        error=std::max(error,std::fabs(expected.imaginary-actual[y*8+x].imaginary));
    }
    CHECK(error<.00002f);
    CHECK(std::fabs(dispersion(2,100)-std::sqrt(19.62f))<.0001f);
    CHECK(std::fabs(groupVelocity(.00001f,2)-std::sqrt(19.62f))<.001f);
    for(float depth:{2.0f,8.0f,100.0f}) {
        float phaseSpeed=3.8f*.81649658f, wavelength=wakeWavelength(phaseSpeed,depth);
        float k=6.28318530718f/wavelength;
        CHECK(std::fabs(dispersion(k,depth)/k-phaseSpeed)<.00001f);
    }
    CHECK(wakeWavelength(5,2)==0);
    float k=6.28318530718f/wakeWavelength(3.8f*.81649658f,100);
    float group=groupVelocity(k,100);
    CHECK(std::fabs(group*.57735027f/(3.8f-group*.81649658f)-1/std::sqrt(8.0f))<.00001f);
    SeaState sea; sea.swellHeight=0; sea.choppiness=0; sea.current={};
    Ocean* ocean=createOcean(sea); Ocean* same=createOcean(sea);
    CHECK(memcmp(oceanSpectrum(ocean),oceanSpectrum(same),OceanModeCount*sizeof(SpectrumMode))==0);
    CHECK(memcmp(oceanMetrics(ocean).breakingThreshold,oceanMetrics(same).breakingThreshold,6*sizeof(float))==0);
    CHECK(std::fabs(oceanMetrics(ocean).significantHeight-sea.windWaveHeight)<.00001f);
    destroyOcean(same);
    SeaState glass=sea; glass.windSpeed=0; glass.choppiness=.2f; glass.windWaveHeight=0; glass.swellHeight=.06f;
    auto* quiet=createOcean(glass);
    CHECK(oceanMetrics(quiet).whitecapFraction==0);
    for(unsigned p=0;p<2;++p) CHECK(oceanMetrics(quiet).breakingThreshold[p][0]==0 && oceanMetrics(quiet).breakingThreshold[p][2]==0);
    destroyOcean(quiet);
    static SpectrumMode ripples[OceanSpectrumSize*OceanSpectrumSize];
    float rippleSlope[3]={};
    for(unsigned setting=0;setting<3;++setting) {
        SeaState wind=sea; wind.windSpeed=setting==0?0:setting==1?1:7;
        buildRippleSpectrum(wind,ripples);
        for(const auto& mode:ripples) {
            CHECK(std::isfinite(mode.real) && std::isfinite(mode.imaginary));
            float power=2*(mode.real*mode.real+mode.imaginary*mode.imaginary);
            rippleSlope[setting]+=power*mode.waveNumber*mode.waveNumber;
            if(power>0) CHECK(mode.waveNumber>=6.2831853f/.30f && mode.waveNumber<=6.2831853f/.035f);
        }
        rippleSlope[setting]=std::sqrt(rippleSlope[setting]);
    }
    CHECK(rippleSlope[0]==0); CHECK(rippleSlope[1]<.002f);
    CHECK(rippleSlope[2]>.04f && rippleSlope[2]<.09f);
    printf("Fine-wave RMS slope: glass %.6f, calm %.6f, breeze %.6f\n",rippleSlope[0],rippleSlope[1],rippleSlope[2]);
    updateOcean(ocean,2);
    auto value=sampleOcean(ocean,7.2f,-11.8f), periodic=sampleOcean(ocean,263.2f,244.2f);
    CHECK(std::fabs(value.height-periodic.height)<.00001f);
    CHECK(std::fabs(value.normal.x-periodic.normal.x)<.00001f);
    updateOcean(ocean,1.999);
    auto prior=sampleOcean(ocean,7.2f,-11.8f);
    float before=prior.height;
    updateOcean(ocean,2.001);
    float after=sampleOcean(ocean,7.2f,-11.8f).height;
    CHECK(std::fabs((after-before)/.002f-value.velocity.y)<.001f);
    auto next=sampleOcean(ocean,7.2f,-11.8f);
    CHECK(std::fabs((next.velocity.x-prior.velocity.x)/.002f-value.acceleration.x)<.003f);
    CHECK(std::fabs((next.velocity.y-prior.velocity.y)/.002f-value.acceleration.y)<.003f);
    CHECK(std::fabs((next.velocity.z-prior.velocity.z)/.002f-value.acceleration.z)<.003f);
    auto allocations=memGetStatistics().accumulatedAllocUnitCount;
    auto bytes=memGetStatistics().totalReportedMemory;
    auto start=getUSec(true);
    for(unsigned i=0;i<120;++i) { updateOcean(ocean,i*FixedStep); auto s=sampleOcean(ocean,i*.1f,-3); CHECK(std::isfinite(s.height)); }
    auto stop=getUSec(true);
    CHECK(memGetStatistics().accumulatedAllocUnitCount==allocations);
    CHECK(memGetStatistics().totalReportedMemory==bytes);
    printf("CPU water: %.3f ms/update, %.2f MiB tracked, FFT reference error %.8f\n",
        double(stop-start)/120000,bytes/1048576.0,error);
    CHECK(emitWavePacket(ocean,{0,0,0},{1,0,0},3,200,true));
    advanceWavePackets(ocean,.5f,{0,0,0},false);
    CHECK(sampleWavePackets(ocean,1,0).height==0); // Identical actual/free fields cancel exactly.
    clearWavePackets(ocean);
    CHECK(emitWavePacket(ocean,{8,0,0},{1,0,0},3,200,false,7));
    CHECK(sampleWavePackets(ocean,8,0,0,7).height==0);
    CHECK(sampleWavePackets(ocean,8,0,0,8).height>0);
    advanceWavePackets(ocean,1,{0,0,0},true);
    CHECK(std::fabs(sampleWavePackets(ocean,8,0,0,7).height)>.0001f); // Reflected wake returns to its maker.
    clearWavePackets(ocean);
    CHECK(emitWavePacket(ocean,{8,0,0},{1,0,0},3,200,true));
    advanceWavePackets(ocean,1,{0,0,0},true);
    CHECK(std::fabs(sampleWavePackets(ocean,8,0).height)>.0001f);
    CHECK(sampleWavePackets(ocean,32,0).height==0);
    CHECK(sampleWavePackets(ocean,32,0).dx==0);
    clearWavePackets(ocean);
    CHECK(emitWavePacket(ocean,{27,0,27},{1,0,0},12,1000));
    // Numerical derivatives across the diagonal must agree with the queried
    // normal. A square max-distance fade had a crease here.
    for(float x:{27.999f,28.0f,28.001f}) {
        auto center=sampleWavePackets(ocean,x,28);
        float dx=(sampleWavePackets(ocean,x+.001f,28).height-sampleWavePackets(ocean,x-.001f,28).height)/.002f;
        float dz=(sampleWavePackets(ocean,x,28.001f).height-sampleWavePackets(ocean,x,27.999f).height)/.002f;
        CHECK(std::fabs(center.dx-dx)<.00003f); CHECK(std::fabs(center.dz-dz)<.00003f);
    }
    clearWavePackets(ocean);
    // One busy frequency can use spare capacity, but cannot grow the pool.
    for(unsigned i=0;i<MaxWavePackets;++i) CHECK(emitWavePacket(ocean,{0,0,0},{1,0,0},3,20));
    CHECK(!emitWavePacket(ocean,{0,0,0},{1,0,0},3,20));
    CHECK(droppedWavePackets(ocean)==1);
    CHECK(sampleWavePackets(ocean,0,0,sea.depth).verticalVelocity==0);
    CHECK(sampleWavePackets(ocean,0,0).velocityX>0);
    advanceWavePackets(ocean,13,{0,0,0}); CHECK(activeWavePackets(ocean)==0);
    CHECK(memGetStatistics().accumulatedAllocUnitCount==allocations);
    clearWavePackets(ocean);
    updateOcean(ocean,2);
    HullWake hullWake{{0,0,0},.25f,{0,0,1},12,{0,0,3},1.5f,5,7,{}};
    setHullWake(ocean,0,hullWake);
    auto bow=sampleWavePackets(ocean,2.5f,4.8f), tunnel=sampleWavePackets(ocean,0,4.8f);
    CHECK(bow.height>.23f && bow.height>tunnel.height*4);
    CHECK(sampleWavePackets(ocean,2.5f,0).height<0);
    CHECK(sampleWavePackets(ocean,2.5f,4.8f,0,7).height==0);
    for(float x:{-3.0f,0.0f,3.0f}) for(float z:{-5.0f,0.0f,4.0f}) {
        auto center=sampleWavePackets(ocean,x,z);
        float dx=(sampleWavePackets(ocean,x+.001f,z).height-sampleWavePackets(ocean,x-.001f,z).height)/.002f;
        float dz=(sampleWavePackets(ocean,x,z+.001f).height-sampleWavePackets(ocean,x,z-.001f).height)/.002f;
        CHECK(std::fabs(center.dx-dx)<.0001f); CHECK(std::fabs(center.dz-dz)<.0001f);
    }
    hullWake.forward.z=-1; hullWake.velocity.z=-3; setHullWake(ocean,0,hullWake);
    CHECK(std::fabs(sampleWavePackets(ocean,2.5f,-4.8f).height-bow.height)<.000001f);
    hullWake.amplitude=0; setHullWake(ocean,0,hullWake);
    CHECK(sampleWavePackets(ocean,2.5f,-4.8f).height==0);
    clearWavePackets(ocean);
    // A calm ocean can have an attached wake before the first travelling
    // packet is emitted. Physics must query that same displaced water.
    SeaState flat=sea; flat.windWaveHeight=flat.swellHeight=0;
    auto* flatOcean=createOcean(flat);
    hullWake.amplitude=.25f; setHullWake(flatOcean,0,hullWake);
    CHECK(activeWavePackets(flatOcean)==0);
    CHECK(sampleOcean(flatOcean,2.5f,-4.8f,0,8).height>flat.level+.23f);
    CHECK(sampleOcean(flatOcean,2.5f,-4.8f,0,7).height==flat.level);
    hullWake.amplitude=0; setHullWake(flatOcean,0,hullWake);
    CHECK(sampleOcean(flatOcean,2.5f,-4.8f).height==flat.level);
    hullWake.amplitude=.25f; setHullWake(flatOcean,0,hullWake); clearWavePackets(flatOcean);
    CHECK(sampleOcean(flatOcean,2.5f,-4.8f).height==flat.level);
    destroyOcean(flatOcean);
    // Use a translation aligned with all three grids so interpolation error
    // cannot mask an incorrect Doppler phase or direction.
    float advectedReference=sampleOcean(ocean,7.2f-2,-11.8f+2).height;
    SeaState flowing=sea; flowing.current={1,0,-1}; configureOcean(ocean,flowing); updateOcean(ocean,2);
    CHECK(std::fabs(sampleOcean(ocean,7.2f,-11.8f).height-advectedReference)<.00001f);
    destroyOcean(ocean);
    auto* world=createWorld(); setEnvironment(world,sea);
    for(unsigned i=0;i<300;++i) step(world);
    allocations=memGetStatistics().accumulatedAllocUnitCount;
    for(unsigned i=0;i<600;++i) step(world);
    auto state=snapshot(world,false);
    CHECK(std::isfinite(state.position.y)); CHECK(std::fabs(state.position.y)<1.5f);
    CHECK(statistics(world).physicsErrors==0);
    CHECK(memGetStatistics().accumulatedAllocUnitCount==allocations);
    CHECK(droppedWavePackets(worldOcean(world))==0);
    printf("Coupled vessel after 15 seconds: height %.3f m, speed %.3f kn\n",state.position.y,state.speedKnots);
    for(unsigned i=0;i<6300;++i) step(world);
    CHECK(statistics(world).steadyStepAllocations==0); CHECK(droppedWavePackets(worldOcean(world))==0);
    CHECK(std::isfinite(snapshot(world,false).position.y)); CHECK(statistics(world).physicsErrors==0);
    for(unsigned i=0;i<12;++i) { resetWorld(world); step(world); }
    CHECK(memGetStatistics().accumulatedAllocUnitCount==allocations);
    destroyWorld(world);
    printf("%s: FFT, dispersion, seed, spectrum energy, periodic boundaries, orbital velocity and floating vessel (%u failures).\n",failures?"FAIL":"PASS",failures);
    fflush(stdout); exitMemAlloc(); return failures?1:0;
}
