#include "Ocean.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "Common_3/Utilities/Interfaces/ILog.h"
#include "Common_3/Utilities/Interfaces/IMemory.h"

namespace mooring {
constexpr float Pi = 3.14159265358979323846f;
constexpr unsigned N = OceanSpectrumSize, Cells = N*N;
static uint32_t generation;
struct Surface { float height, dx, dz, vx, vy, vz, ax, ay, az; };
struct Ocean {
    SeaState settings;
    SpectrumMode modes[OceanModeCount];
    Surface surface[OceanModeCount];
    float orbitalFactor[OceanModeCount];
    Complex height[Cells], derivative[Cells], work[Cells], scratch[Cells], twiddles[N/2];
    float representativeK[OceanBands];
    OceanMetrics metrics;
    WavePacket packets[MaxWavePackets];
    HullWake hullWakes[MaxHullWakes];
    uint32_t packetOwners[MaxWavePackets]; // Physics only; GPU packets need no ownership data.
    Vec3 patchCenter;
    unsigned packetCursor[WavePacketBuckets],activePackets,droppedPackets,activeHullWakes;
    uint32_t revision;
    double time;
};
static Complex add(Complex a,Complex b) { return {a.real+b.real,a.imaginary+b.imaginary}; }
static Complex sub(Complex a,Complex b) { return {a.real-b.real,a.imaginary-b.imaginary}; }
static Complex mul(Complex a,Complex b) { return {a.real*b.real-a.imaginary*b.imaginary,a.real*b.imaginary+a.imaginary*b.real}; }
static Complex scale(Complex a,float s) { return {a.real*s,a.imaginary*s}; }
static Complex timesI(Complex a) { return {-a.imaginary,a.real}; }
static unsigned mirror(unsigned x,unsigned y) { return ((N-y)%N)*N+(N-x)%N; }
static int signedIndex(unsigned i) { return i<N/2 ? int(i) : int(i)-int(N); }
float dispersion(float k,float depth) { return std::sqrt(9.81f*k*std::tanh(k*depth)); }
float groupVelocity(float k,float depth) {
    if(k<1e-6f) return std::sqrt(9.81f*depth);
    const float t=std::tanh(k*depth), omega=dispersion(k,depth);
    return 9.81f*(t+k*depth*(1-t*t))/(2*omega);
}
float wakeWavelength(float phaseSpeed,float depth) {
    if(phaseSpeed<=0 || phaseSpeed*phaseSpeed>=9.81f*depth) return 0;
    // omega/k = U cos(theta), with finite-depth dispersion. At the critical
    // depth speed this direction has no stationary gravity-wave solution.
    float low=0,high=9.81f/(phaseSpeed*phaseSpeed);
    for(unsigned i=0;i<24;++i) {
        float k=(low+high)*.5f;
        if(9.81f*std::tanh(k*depth)>k*phaseSpeed*phaseSpeed) low=k; else high=k;
    }
    return 2*Pi/((low+high)*.5f);
}
Vec3 orbitalAttenuation(float k,float waterDepth,float below) {
    float h=std::max(.001f,waterDepth), z=std::clamp(below,0.0f,h);
    if(k<1e-6f) return {1,1-z/h,1};
    // cosh[k(h-z)]/cosh(kh), sinh[k(h-z)]/sinh(kh), expressed
    // without overflowing exponentials. No vertical flow through the bottom.
    float horizontal=std::exp(-k*z)*(1+std::exp(-2*k*(h-z)))/(1+std::exp(-2*k*h));
    float vertical=std::exp(-k*z)*std::expm1(-2*k*(h-z))/std::expm1(-2*k*h);
    return {horizontal,vertical,horizontal};
}
void inverseFFT(Complex* data,Complex* scratch,const Complex* twiddles,unsigned size) {
    MTRACY_ZONE("inverseFFT");
    // Stockham autosort: each pass reads two contiguous half-arrays and
    // interleaves butterflies. No bit reversal, temporary heap, or trig in the passes.
    Complex* src=data; Complex* dst=scratch;
    for(unsigned axis=0;axis<2;++axis) {
        for(unsigned stride=1;stride<size;stride*=2) {
            // Traverse contiguous columns together on the vertical axis.
            // Butterflies within a stage are independent; their arithmetic
            // stays identical while both reads and writes become sequential.
            for(unsigned major=0;major<(axis?size/2:size);++major)
            for(unsigned minor=0;minor<(axis?size:size/2);++minor) {
                unsigned row=axis?minor:major, j=axis?major:minor;
                unsigned k=j&(stride-1), output=2*(j-k)+k;
                unsigned a=axis ? j*size+row : row*size+j;
                unsigned b=axis ? (j+size/2)*size+row : a+size/2;
                unsigned lo=axis ? output*size+row : row*size+output;
                unsigned hi=axis ? lo+stride*size : lo+stride;
                Complex rotated=mul(src[b],twiddles[k*(size/(2*stride))]);
                dst[lo]=add(src[a],rotated); dst[hi]=sub(src[a],rotated);
            }
            std::swap(src,dst);
        }
    }
    if(src!=data) memcpy(data,src,size*size*sizeof(Complex));
}
static uint32_t hash(uint32_t v) { v^=v>>16; v*=0x7feb352du; v^=v>>15; v*=0x846ca68bu; return v^(v>>16); }
static float uniform(uint32_t key) { return (float(hash(key)>>8)+.5f)/16777216.0f; }
static Complex gaussian(uint32_t key) {
    float r=std::sqrt(-2*std::log(uniform(key))), phase=2*Pi*uniform(key^0x9e3779b9u);
    return {r*std::cos(phase),r*std::sin(phase)};
}
void buildRippleSpectrum(const SeaState& sea,SpectrumMode* modes) {
    MTRACY_ZONE("buildRippleSpectrum");
    double slopeVariance=0;
    for(unsigned y=0;y<N;++y) for(unsigned x=0;x<N;++x) {
        unsigned i=y*N+x; modes[i]={};
        float kx=signedIndex(x)*Pi,kz=signedIndex(y)*Pi,k=std::hypot(kx,kz);
        if(k<2*Pi/.30f || k>2*Pi/.035f || x==N/2 || y==N/2) continue;
        float alignment=(kx*std::cos(sea.windDirection)+kz*std::sin(sea.windDirection))/k;
        float power=std::exp(-k*k*.000025f)*(.15f+.85f*alignment*alignment)/(k*k*k*k);
        auto amplitude=scale(gaussian(i^sea.seed^0xb82415afu),std::sqrt(power));
        modes[i]={amplitude.real,amplitude.imaginary,std::sqrt(9.81f*k+.000074f*k*k*k),k};
        slopeVariance+=2*(amplitude.real*amplitude.real+amplitude.imaginary*amplitude.imaginary)*k*k;
    }
    // Short wind ripples disappear before long swell does. A square-root wind
    // ramp left conspicuous capillary texture even in the calm preset.
    float wind=std::clamp((sea.windSpeed-.75f)/4.25f,0.0f,1.0f);
    float rmsSlope=.065f*wind*wind*(3-2*wind);
    float scaleFactor=slopeVariance>0 ? rmsSlope/std::sqrt(slopeVariance) : 0;
    for(unsigned i=0;i<Cells;++i) { modes[i].real*=scaleFactor; modes[i].imaginary*=scaleFactor; }
}
static float spreading(float theta,float direction,float s) {
    float angle=std::remainder(theta-direction,2*Pi);
    float normalization=std::exp(std::lgamma(s+1)-std::lgamma(s+.5f))/(2*std::sqrt(Pi));
    return normalization*std::pow(std::max(0.0f,std::cos(angle*.5f)),2*s);
}
static float density(const SeaState& sea,float omega,float theta,bool swell) {
    float peak=swell ? 2*Pi/sea.swellPeriod : 22*std::cbrt(9.81f*9.81f/(std::max(.5f,sea.windSpeed)*sea.fetch));
    float ratio=omega/peak;
    if(ratio<.25f) return 0;
    float sigma=omega<=peak ? .07f : .09f;
    float r=std::exp(-.5f*std::pow((omega-peak)/(sigma*peak),2));
    float spectrum=std::pow(omega,-5)*std::exp(-1.25f*std::pow(peak/omega,4))*std::pow(swell?5.0f:3.3f,r);
    float spread=swell ? 28 : std::clamp(16*std::pow(ratio,omega<=peak?5.0f:-2.5f),1.0f,32.0f);
    return spectrum*spreading(theta,swell?sea.swellDirection:sea.windDirection,spread);
}
static Surface interpolate(const Ocean* o,unsigned band,float x,float z);
static void calibrateWhitecaps(Ocean* o) {
    MTRACY_ZONE("calibrateWhitecaps");
    // Tessendorf/Reinhardt/Gao: choose a minimum-stretch threshold from the
    // realised distribution and an observed wind/whitecap relation. This is
    // configuration work; no histogram, allocation or readback runs per frame.
    float wind=o->settings.windSpeed;
    float fraction=wind<3?0:std::min(.35f,3.84e-6f*std::pow(wind,3.41f));
    o->metrics.whitecapFraction=fraction;
    for(auto& thresholds:o->metrics.breakingThreshold) { thresholds[0]=thresholds[2]=0; thresholds[1]=-2; }
    if(fraction==0) return;
    for(unsigned preset=0;preset<2;++preset) {
        // Match each GPU's central derivative in frequency space. Differencing
        // the coarser CPU grid underestimated short-wave compression severely.
        for(unsigned band=0;band<OceanBands;++band) for(unsigned field=0;field<2;++field) {
            float delta=2*Pi/OceanLengths[band], spacing=OceanLengths[band]/float(256u<<preset);
            const auto* modes=o->modes+band*Cells;
            for(unsigned y=0;y<N;++y) for(unsigned x=0;x<N;++x) {
                unsigned i=y*N+x; auto a=modes[i],b=modes[mirror(x,y)];
                Complex h={a.real+b.real,a.imaginary-b.imaginary};
                float kx=signedIndex(x)*delta,kz=signedIndex(y)*delta;
                float dx=std::sin(kx*spacing)/spacing,dz=std::sin(kz*spacing)/spacing;
                float factor=-o->orbitalFactor[band*Cells+i]/delta*o->settings.choppiness;
                o->work[i]=field==0?add(scale(h,kx*dx*factor),timesI(scale(h,kz*dz*factor))):scale(h,(kx*dz+kz*dx)*.5f*factor);
            }
            inverseFFT(o->work,o->scratch,o->twiddles,N);
            for(unsigned i=0;i<Cells;++i) {
                auto& s=o->surface[band*Cells+i];
                if(field==0) { s.ax=o->work[i].real; s.ay=o->work[i].imaginary; } else s.az=o->work[i].real;
            }
        }
        auto& thresholds=o->metrics.breakingThreshold[preset];
        for(unsigned band:{0u,2u}) {
            unsigned histogram[1024]={}, size=band==0?256u<<preset:N;
            float spacing=OceanLengths[band]/size;
            for(unsigned y=0;y<size;++y) for(unsigned x=0;x<size;++x) {
                auto gradient=interpolate(o,band,x*spacing,y*spacing);
                if(band==0) {
                    auto medium=interpolate(o,1,x*spacing,y*spacing);
                    gradient.ax+=medium.ax; gradient.ay+=medium.ay; gradient.az+=medium.az;
                }
                float stretch=1+(gradient.ax+gradient.ay-std::hypot(gradient.ax-gradient.ay,2*gradient.az))*.5f;
                unsigned bin=unsigned(std::clamp((stretch+2)*256,0.0f,1023.0f)); ++histogram[bin];
            }
            // Split coverage between larger crests and short whitecaps. This is
            // visual tuning; the wind relation is not an exact coverage law.
            // Account for the shorter remnant lifetime: the former .861
            // feedback factor under-filled active caps after decay increased.
            float coverage=fraction*(band==0?.85f:.15f), alpha=.70f;
            float quantile=coverage*(1-alpha)/(1-alpha*coverage)*size*size;
            unsigned bin=0, total=histogram[0];
            while(total<quantile && bin<1023) total+=histogram[++bin];
            thresholds[band]=std::min(.98f,(bin+.5f)/256-2);
        }
    }
}
void configureOcean(Ocean* o,const SeaState& settings) {
    MTRACY_ZONE("configureOcean");
    o->settings=settings;
    auto& sea=o->settings;
    sea.depth=std::max(.5f,sea.depth); sea.fetch=std::max(50.0f,sea.fetch);
    sea.swellPeriod=std::clamp(sea.swellPeriod,1.5f,18.0f);
    sea.windWaveHeight=std::max(0.0f,sea.windWaveHeight); sea.swellHeight=std::max(0.0f,sea.swellHeight);
    sea.choppiness=std::clamp(sea.choppiness,0.0f,1.0f);
    memset(o->modes,0,sizeof o->modes); memset(o->surface,0,sizeof o->surface); o->metrics={};
    clearWavePackets(o); o->patchCenter={};
    for(unsigned i=0;i<N/2;++i) { float angle=2*Pi*i/N; o->twiddles[i]={std::cos(angle),std::sin(angle)}; }
    // Separate, non-overlapping wavelength bands preserve total spectral energy.
    // The 0.30 m cutoff is shared by all rendering presets and CPU queries.
    constexpr float limits[]={0,2*Pi/16,2*Pi/4,2*Pi/.30f};
    for(unsigned source=0;source<2;++source) {
        double variance=0;
        for(unsigned band=0;band<OceanBands;++band) {
            float delta=2*Pi/OceanLengths[band];
            for(unsigned y=0;y<N;++y) for(unsigned x=0;x<N;++x) {
                unsigned idx=band*Cells+y*N+x;
                float kx=signedIndex(x)*delta,kz=signedIndex(y)*delta,k=std::hypot(kx,kz);
                Complex amplitude={};
                if(k>limits[band] && k<=limits[band+1] && x!=N/2 && y!=N/2) {
                    float omega=dispersion(k,sea.depth);
                    float power=density(sea,omega,std::atan2(kz,kx),source==1)*groupVelocity(k,sea.depth)/k*delta*delta;
                    // A smooth short-wave rolloff avoids concentrating slope
                    // energy at the hard 30 cm cutoff (Tessendorf, Eq. 41).
                    // The separate fine band supplies wind-driven ripples.
                    power*=std::exp(-k*k*.12f*.12f);
                    amplitude=scale(gaussian(idx^sea.seed^(source*0x1234567u)),std::sqrt(std::max(0.0f,power)*.25f));
                    o->modes[idx].omega=omega; o->modes[idx].waveNumber=k;
                }
                // Reuse the surface storage during construction; no temporary allocation.
                o->surface[idx].dx=amplitude.real; o->surface[idx].dz=amplitude.imaginary;
                variance+=2*(amplitude.real*amplitude.real+amplitude.imaginary*amplitude.imaginary);
            }
        }
        float height=source ? sea.swellHeight : sea.windWaveHeight;
        float normalization=variance>0 ? height/(4*std::sqrt(variance)) : 0;
        for(unsigned i=0;i<OceanModeCount;++i) {
            o->modes[i].real+=o->surface[i].dx*normalization;
            o->modes[i].imaginary+=o->surface[i].dz*normalization;
        }
    }
    double energy=0,slopeEnergy=0;
    for(unsigned band=0;band<OceanBands;++band) {
        double e=0,weightedK=0;
        for(unsigned i=0;i<Cells;++i) { auto m=o->modes[band*Cells+i];
            // Geometry and water depth only change on configuration. Avoid
            // repeating this transcendental in every field of every physics step.
            o->orbitalFactor[band*Cells+i]=m.waveNumber>0 ?
                2*Pi/OceanLengths[band]/m.waveNumber/std::tanh(m.waveNumber*sea.depth) : 0;
            double power=2*(m.real*m.real+m.imaginary*m.imaginary);
            e+=power; weightedK+=power*m.waveNumber; slopeEnergy+=power*m.waveNumber*m.waveNumber;
        }
        o->representativeK[band]=e>0 ? weightedK/e : 0; energy+=e;
    }
    o->metrics.significantHeight=4*std::sqrt(energy); o->metrics.rmsSlope=std::sqrt(slopeEnergy);
    memset(o->surface,0,sizeof o->surface); o->revision=++generation;
    calibrateWhitecaps(o);
    updateOcean(o,0); // Restore acceleration fields used as calibration scratch.
}
Ocean* createOcean(const SeaState& settings) {
    MTRACY_ZONE("createOcean"); auto* o=tf_new(Ocean); configureOcean(o,settings); return o; }
void destroyOcean(Ocean* o) {
    MTRACY_ZONE("destroyOcean"); tf_delete(o); }
void updateOcean(Ocean* o,double time) {
    MTRACY_ZONE("updateOcean");
    o->time=time; ++o->metrics.updates;
    if(o->metrics.significantHeight==0) return;
    for(unsigned band=0;band<OceanBands;++band) {
        auto* modes=o->modes+band*Cells; auto* surface=o->surface+band*Cells;
        for(unsigned y=0;y<N;++y) for(unsigned x=0;x<N;++x) {
            unsigned i=y*N+x; auto a=modes[i],b=modes[mirror(x,y)];
            if(a.real==0 && a.imaginary==0 && b.real==0 && b.imaginary==0) {
                o->height[i]=o->derivative[i]={};
                continue;
            }
            float phase=std::remainder(a.omega*time,2.0*Pi); Complex e={std::cos(phase),-std::sin(phase)};
            auto first=mul({a.real,a.imaginary},e);
            auto second=mul({b.real,-b.imaginary},{e.real,-e.imaginary});
            float advection=std::remainder((signedIndex(x)*o->settings.current.x+signedIndex(y)*o->settings.current.z)*
                                           (2*Pi/OceanLengths[band])*time,2.0*Pi);
            Complex currentPhase={std::cos(advection),-std::sin(advection)};
            o->height[i]=mul(add(first,second),currentPhase);
            // Orbital velocity is the material derivative; uniform-current
            // advection affects phase, not the intrinsic orbital frequency.
            o->derivative[i]=mul(scale(timesI(sub(second,first)),a.omega),currentPhase);
        }
        for(unsigned field=0;field<5;++field) {
            for(unsigned y=0;y<N;++y) for(unsigned x=0;x<N;++x) {
                unsigned i=y*N+x; auto h=o->height[i],dh=o->derivative[i];
                if(field==0) o->work[i]=add(h,timesI(dh));
                else if(field==4) o->work[i]=scale(h,-modes[i].omega*modes[i].omega);
                else {
                    float factor=o->orbitalFactor[band*Cells+i];
                    auto horizontal=timesI(field==2 ? dh : h);
                    if(field==3) horizontal=scale(horizontal,-modes[i].omega*modes[i].omega);
                    o->work[i]=add(scale(horizontal,signedIndex(x)*factor),timesI(scale(horizontal,signedIndex(y)*factor)));
                }
            }
            inverseFFT(o->work,o->scratch,o->twiddles,N);
            for(unsigned i=0;i<Cells;++i) {
                auto v=o->work[i];
                if(field==0) { surface[i].height=v.real; surface[i].vy=v.imaginary; }
                else if(field==1) { surface[i].dx=v.real*o->settings.choppiness; surface[i].dz=v.imaginary*o->settings.choppiness; }
                else if(field==2) { surface[i].vx=v.real; surface[i].vz=v.imaginary; }
                else if(field==3) { surface[i].ax=v.real; surface[i].az=v.imaginary; }
                else surface[i].ay=v.real;
            }
        }
    }
}
static Surface interpolate(const Ocean* o,unsigned band,float x,float z) {
    float u=x/OceanLengths[band]*N,v=z/OceanLengths[band]*N;
    int ix=int(std::floor(u)),iy=int(std::floor(v)); float tx=u-std::floor(u),ty=v-std::floor(v);
    const auto* field=o->surface+band*Cells; Surface out={};
    for(unsigned j=0;j<2;++j) for(unsigned i=0;i<2;++i) {
        float w=(i?tx:1-tx)*(j?ty:1-ty);
        auto s=field[((iy+int(j))&(N-1))*N+((ix+int(i))&(N-1))];
        out.height+=s.height*w; out.dx+=s.dx*w; out.dz+=s.dz*w;
        out.vx+=s.vx*w; out.vy+=s.vy*w; out.vz+=s.vz*w;
        out.ax+=s.ax*w; out.ay+=s.ay*w; out.az+=s.az*w;
    }
    return out;
}
WaterSample sampleOceanBand(const Ocean* o,unsigned band,float x,float z) {
    MTRACY_FINE_ZONE("sampleOceanBand");
    auto s=interpolate(o,band,x,z); float step=OceanLengths[band]/N;
    auto l=interpolate(o,band,x-step,z),r=interpolate(o,band,x+step,z);
    auto b=interpolate(o,band,x,z-step),t=interpolate(o,band,x,z+step);
    float dx=(r.height-l.height)/(2*step),dz=(t.height-b.height)/(2*step);
    float jxx=1+(r.dx-l.dx)/(2*step),jzz=1+(t.dz-b.dz)/(2*step);
    float jxz=(t.dx-b.dx)/(2*step),jzx=(r.dz-l.dz)/(2*step);
    float determinant=std::max(.25f,jxx*jzz-jxz*jzx);
    return {s.height,{s.dx,0,s.dz},{-(dx*jzz-dz*jzx)/determinant,1,-(dz*jxx-dx*jxz)/determinant},{s.vx,s.vy,s.vz},{s.ax,s.ay,s.az}};
}
WaterSample sampleOcean(const Ocean* o,float x,float z,float depth,uint32_t excludeBody) {
    MTRACY_FINE_ZONE("sampleOcean");
    if(o->metrics.significantHeight==0 && !o->activePackets && !o->activeHullWakes)
        return {o->settings.level,{},{0,1,0},o->settings.current,{}};
    WaterSample out={}; out.height=o->settings.level; out.normal={0,1,0}; out.velocity=o->settings.current;
    if(o->metrics.significantHeight>0) {
        // Invert horizontal displacement to query at the visible surface's
        // world coordinate. Flat-water wakes need none of these FFT lookups.
        float qx=x,qz=z;
        for(unsigned iteration=0;iteration<3;++iteration) {
            float dx=0,dz=0;
            for(unsigned band=0;band<OceanBands;++band) { auto s=interpolate(o,band,qx,qz); dx+=s.dx; dz+=s.dz; }
            qx=x-dx; qz=z-dz;
        }
        for(unsigned band=0;band<OceanBands;++band) {
            auto s=sampleOceanBand(o,band,qx,qz); out.height+=s.height;
            out.displacement.x+=s.displacement.x; out.displacement.z+=s.displacement.z;
            out.normal.x+=s.normal.x; out.normal.z+=s.normal.z;
            // Band-filtered orbital velocity uses the energy-weighted wave
            // number; the surface solution itself uses all spectral modes.
            auto attenuation=orbitalAttenuation(o->representativeK[band],o->settings.depth,depth);
            out.velocity.x+=s.velocity.x*attenuation.x; out.velocity.y+=s.velocity.y*attenuation.y; out.velocity.z+=s.velocity.z*attenuation.z;
            out.acceleration.x+=s.acceleration.x*attenuation.x; out.acceleration.y+=s.acceleration.y*attenuation.y;
            out.acceleration.z+=s.acceleration.z*attenuation.z;
        }
    }
    auto local=sampleWavePackets(o,x,z,depth,excludeBody); out.height+=local.height;
    out.normal.x-=local.dx; out.normal.z-=local.dz; out.velocity.y+=local.verticalVelocity;
    out.velocity.x+=local.velocityX; out.velocity.z+=local.velocityZ;
    out.acceleration.x+=local.accelerationX; out.acceleration.z+=local.accelerationZ;
    float length=std::sqrt(out.normal.x*out.normal.x+1+out.normal.z*out.normal.z);
    out.normal.x/=length; out.normal.y/=length; out.normal.z/=length; return out;
}
bool emitWavePacket(Ocean* o,Vec3 position,Vec3 direction,float wavelength,float energy,bool pair,uint32_t sourceBody) {
    MTRACY_ZONE("emitWavePacket");
    if(energy<=0) return true;
    float norm=std::hypot(direction.x,direction.z); if(norm<.001f) return false;
    // Frequencies start in their own bucket, then borrow unused slots. A pair
    // of hulls must not lose one side of its wake while other buckets are empty.
    unsigned bucket=unsigned(std::clamp(std::round(std::log2(16/std::clamp(wavelength,.5f,16.0f))*1.4f),0.0f,7.0f));
    // Buckets control capacity, not frequency. Rounding wavelength broke
    // phase coherence between emissions from a steadily moving hull.
    float lambda=std::clamp(wavelength,.5f,32.0f), radius=lambda*.75f;
    unsigned slot=MaxWavePackets;
    for(unsigned i=0;i<MaxWavePackets;++i) {
        unsigned candidate=(bucket*WavePacketsPerBucket+o->packetCursor[bucket]+i)%MaxWavePackets;
        if(o->packets[candidate].lifetime<=0) { slot=candidate; o->packetCursor[bucket]=(o->packetCursor[bucket]+i+1)%MaxWavePackets; break; }
    }
    if(slot==MaxWavePackets) { ++o->droppedPackets; return false; }
    // The compact (1-r^2/R^2)^2 envelope has an integral of its square of pi R^2/5.
    // Averaging cos^2 over the carrier gives E ~= rho g A^2 pi R^2 / 10.
    float amplitude=std::min(lambda*.06f,std::sqrt(10*energy/(1025*9.81f*Pi*radius*radius)));
    WavePacket p={}; p.freeX=p.x=position.x; p.freeZ=p.z=position.z;
    p.amplitude=amplitude; p.freeAmplitude=pair?amplitude:0;
    p.freeDirectionX=p.directionX=direction.x/norm; p.freeDirectionZ=p.directionZ=direction.z/norm;
    p.waveNumber=2*Pi/lambda; p.omega=dispersion(p.waveNumber,o->settings.depth); p.radius=radius;
    p.lifetime=12; o->packets[slot]=p; o->packetOwners[slot]=sourceBody; ++o->activePackets; return true;
}
void advanceWavePackets(Ocean* o,float dt,Vec3 center,bool reflectDock) {
    MTRACY_ZONE("advanceWavePackets");
    o->patchCenter=center;
    for(unsigned i=0;i<MaxWavePackets;++i) {
        auto& p=o->packets[i];
        if(p.lifetime<=0) continue;
        p.age+=dt;
        if(p.age>=p.lifetime) { p.lifetime=0; --o->activePackets; continue; }
        float speed=groupVelocity(p.waveNumber,o->settings.depth);
        p.freeX+=(p.freeDirectionX*speed+o->settings.current.x)*dt;
        p.freeZ+=(p.freeDirectionZ*speed+o->settings.current.z)*dt;
        float oldX=p.x;
        p.x+=(p.directionX*speed+o->settings.current.x)*dt;
        p.z+=(p.directionZ*speed+o->settings.current.z)*dt;
        p.phase=std::remainder(p.phase+(p.waveNumber*speed-p.omega)*dt,2*Pi);
        // The prototype dock has vertical faces at x=8.8 and x=11.2, z +/-15.
        if(reflectDock && std::fabs(p.z)<15 && ((oldX<8.8f && p.x>=8.8f)||(oldX>11.2f && p.x<=11.2f))) {
            float wall=oldX<8.8f?8.8f:11.2f;
            p.x=2*wall-p.x; p.directionX=-p.directionX; p.amplitude*=.8f;
            o->packetOwners[i]=UINT32_MAX; // A reflected wake is now an incoming wave.
        }
    }
}
void clearWavePackets(Ocean* o) {
    MTRACY_ZONE("clearWavePackets");
    memset(o->packets,0,sizeof o->packets); memset(o->packetCursor,0,sizeof o->packetCursor);
    memset(o->hullWakes,0,sizeof o->hullWakes);
    o->activePackets=o->droppedPackets=o->activeHullWakes=0;
}
static PacketSample packetContribution(const WavePacket& p,float x,float z,bool free,float depth,float below) {
    MTRACY_FINE_ZONE("packetContribution");
    float amplitude=free?p.freeAmplitude:p.amplitude;
    float dx=x-(free?p.freeX:p.x),dz=z-(free?p.freeZ:p.z);
    float r2=(dx*dx+dz*dz)/(p.radius*p.radius);
    if(r2>=1 || amplitude==0) return {};
    float nx=free?p.freeDirectionX:p.directionX,nz=free?p.freeDirectionZ:p.directionZ;
    float envelope=(1-r2)*(1-r2)*std::exp(-.18f*p.age);
    // Fade the last second to zero instead of deleting a finite wave crest.
    float remaining=std::clamp(p.lifetime-p.age,0.0f,1.0f);
    float life=remaining*remaining*(3-2*remaining), lifeRate=-6*remaining*(1-remaining);
    float phase=p.waveNumber*(dx*nx+dz*nz)+p.phase, cosine=std::cos(phase),sine=std::sin(phase);
    float common=amplitude*life;
    float envelopeDx=-4*dx/(p.radius*p.radius)*(1-r2)*std::exp(-.18f*p.age);
    float envelopeDz=-4*dz/(p.radius*p.radius)*(1-r2)*std::exp(-.18f*p.age);
    float envelopeRate=-groupVelocity(p.waveNumber,depth)*(nx*envelopeDx+nz*envelopeDz)-.18f*envelope;
    auto attenuation=orbitalAttenuation(p.waveNumber,depth,below);
    float horizontal=common*envelope*p.omega/std::tanh(p.waveNumber*depth)*cosine*attenuation.x;
    return {common*envelope*cosine, common*(envelopeDx*cosine-envelope*p.waveNumber*nx*sine),
            common*(envelopeDz*cosine-envelope*p.waveNumber*nz*sine),
            (common*(envelopeRate*cosine+envelope*p.omega*sine)+amplitude*lifeRate*envelope*cosine)*attenuation.y,
            horizontal*nx,horizontal*nz, -9.81f*common*(envelopeDx*cosine-envelope*p.waveNumber*nx*sine)*attenuation.x,
            -9.81f*common*(envelopeDz*cosine-envelope*p.waveNumber*nz*sine)*attenuation.x};
}
PacketSample sampleWavePackets(const Ocean* o,float x,float z,float below,uint32_t excludeBody) {
    MTRACY_FINE_ZONE("sampleWavePackets");
    PacketSample value={};
    float tx=std::clamp((32-std::fabs(x-o->patchCenter.x))/8,0.0f,1.0f);
    float tz=std::clamp((32-std::fabs(z-o->patchCenter.z))/8,0.0f,1.0f);
    float bx=tx*tx*(3-2*tx), bz=tz*tz*(3-2*tz), blend=bx*bz;
    if(blend==0) return value;
    for(const auto& wake:o->hullWakes) {
        if(wake.amplitude<=0 || (excludeBody!=UINT32_MAX && wake.sourceBody==excludeBody)) continue;
        const float rx=x-wake.position.x,rz=z-wake.position.z;
        const float along=rx*wake.forward.x+rz*wake.forward.z;
        const float across=rx*wake.forward.z-rz*wake.forward.x;
        const unsigned hulls=wake.spacing>0?2:1;
        const float vertical=orbitalAttenuation(2*Pi/wake.length,o->settings.depth,below).y;
        // A bounded moving-pressure approximation: bow pile-up, shoulder
        // drawdown and stern recovery. Dynamic head sets the amplitude; it
        // vanishes at rest and reverses with the hull's through-water motion.
        // Equal positive and negative longitudinal integrals avoid a net mound.
        constexpr float terms[][3]={{.40f,.105f,1},{0,.28f,-.1545f/.28f},{-.46f,.09f,.55f}};
        for(unsigned hull=0;hull<hulls;++hull) {
            float cross=across+(hull==0?-.5f:.5f)*wake.spacing;
            float width=wake.width*.62f+.3f, crossFactor=std::exp(-cross*cross/(width*width));
            if(crossFactor<.00001f) continue;
            for(const auto& term:terms) {
                float span=wake.length*term[1], offset=along-wake.length*term[0];
                float height=wake.amplitude*term[2]*std::exp(-offset*offset/(span*span))*crossFactor;
                float da=-2*offset/(span*span)*height, dc=-2*cross/(width*width)*height;
                float dx=da*wake.forward.x+dc*wake.forward.z,dz=da*wake.forward.z-dc*wake.forward.x;
                value.height+=height; value.dx+=dx; value.dz+=dz;
                value.verticalVelocity+=((o->settings.current.x-wake.velocity.x)*dx+(o->settings.current.z-wake.velocity.z)*dz)*vertical;
            }
        }
    }
    if(o->activePackets) for(unsigned i=0;i<MaxWavePackets;++i) {
        const auto& p=o->packets[i];
        if(p.lifetime<=0 || (excludeBody!=UINT32_MAX && o->packetOwners[i]==excludeBody)) continue;
        auto actual=packetContribution(p,x,z,false,o->settings.depth,below),free=packetContribution(p,x,z,true,o->settings.depth,below);
        value.height+=actual.height-free.height; value.dx+=actual.dx-free.dx;
        value.dz+=actual.dz-free.dz; value.verticalVelocity+=actual.verticalVelocity-free.verticalVelocity;
        value.velocityX+=actual.velocityX-free.velocityX; value.velocityZ+=actual.velocityZ-free.velocityZ;
        value.accelerationX+=actual.accelerationX-free.accelerationX; value.accelerationZ+=actual.accelerationZ-free.accelerationZ;
    }
    // Separate smooth edge weights also keep the derivative continuous at
    // patch corners. A max(abs(x),abs(z)) fade left diagonal normal seams.
    value.dx=value.dx*blend-value.height*bz*6*tx*(1-tx)/8*std::copysign(1.0f,x-o->patchCenter.x);
    value.dz=value.dz*blend-value.height*bx*6*tz*(1-tz)/8*std::copysign(1.0f,z-o->patchCenter.z);
    value.height*=blend; value.verticalVelocity*=blend; value.velocityX*=blend; value.velocityZ*=blend; value.accelerationX*=blend; value.accelerationZ*=blend; return value;
}
const WavePacket* oceanPackets(const Ocean* o) { return o->packets; }
void setHullWake(Ocean* o,unsigned index,const HullWake& wake) {
    ASSERT(index<MaxHullWakes);
    o->activeHullWakes-=o->hullWakes[index].amplitude>0;
    o->activeHullWakes+=wake.amplitude>0;
    o->hullWakes[index]=wake;
}
const HullWake* oceanHullWakes(const Ocean* o) { return o->hullWakes; }
Vec3 oceanPatchCenter(const Ocean* o) { return o->patchCenter; }
unsigned activeWavePackets(const Ocean* o) { return o->activePackets; }
unsigned droppedWavePackets(const Ocean* o) { return o->droppedPackets; }
const SeaState& seaState(const Ocean* o) { return o->settings; }
const SpectrumMode* oceanSpectrum(const Ocean* o) { return o->modes; }
const OceanMetrics& oceanMetrics(const Ocean* o) { return o->metrics; }
uint32_t oceanRevision(const Ocean* o) { return o->revision; }
}
