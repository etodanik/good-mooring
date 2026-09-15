#include "OceanRenderer.h"
#include "WaterProfile.h"
#include "../Simulation/Hydrodynamics.h"
#include "../GraphicsMath.h"
#include "Common_3/Graphics/Interfaces/IGraphics.h"
#include "Common_3/Resources/ResourceLoader/Interfaces/IResourceLoader.h"
#include "Common_3/Resources/ResourceLoader/ThirdParty/OpenSource/tinyimageformat/tinyimageformat_decode.h"
#include "Common_3/Resources/ResourceLoader/ThirdParty/OpenSource/tinyimageformat/tinyimageformat_encode.h"
#include "Common_3/Graphics/FSL/defaults.h"
#include "../Shaders/Ocean.srt.h"
#include "../Shaders/Water.srt.h"
#include "../Shaders/SkyFilter.srt.h"
#include "Common_3/Utilities/Interfaces/ILog.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "Common_3/Utilities/Interfaces/IMemory.h"

// The resource loader uses this backend copy operation as well. Readback is
// confined to the startup verification; simulation never waits for GPU water.
extern "C" void cmdUpdateBuffer(TFCmd*,TFBuffer*,uint64_t,TFBuffer*,uint64_t,uint64_t);
namespace mooring {
struct OceanRenderer {
    TFRenderer* renderer;
    SpectrumMode renderSpectrum[4*OceanSpectrumSize*OceanSpectrumSize];
    WavePacket packetUpload[MaxWavePackets+MaxHullWakes];
    unsigned size,logSize,passes,surfacePass;
    TFBuffer* spectrum[2]; TFBuffer* transform; TFBuffer* surface[2]; TFBuffer* normals; TFBuffer* twiddles;
    TFBuffer* uniform[2][32]; TFBuffer* camera[2]; TFBuffer* packets[2]; TFBuffer* local;
    TFShader* computeShaders[5]; TFPipeline* computePipelines[5]; TFDescriptorSet* computeSets;
    TFBuffer* foam[2]; TFBuffer* spray[2]; TFBuffer* crests[2];
    TFBuffer* whitewater[2]; TFBuffer* localWhitewater[2];
    TFShader* effectsShader; TFPipeline* effectsPipeline;
    TFTexture* sky; TFTextureDescriptor* skyMips[11]; TFShader* skyShader; TFPipeline* skyPipeline;
    TFShader* skyFilterShader; TFPipeline* skyFilterPipeline; TFDescriptorSet* skyFilterSets;
    TFBuffer* skyFilterParameters[11];
    TFTexture* cloudNoise; TFShader* cloudShader; TFPipeline* cloudPipeline; bool cloudReady;
    TFShader* sprayShader; TFPipeline* sprayPipeline;
    Vec3 previousFoamPatch,wind; float effectsTime; bool effectsValid;
    Vec3 skyEye; float4 skySun; float skyCover;
    unsigned particleCapacity,gridSize,effectsFrame;
    TFBuffer* gridIndices[3];
    TFShader* drawShader; TFPipeline* drawPipeline; TFDescriptorSet* drawSets;
    TFShader* lightShader; TFPipeline* lightPipeline;
    TFTexture* foamTexture;
    uint32_t revision[2]; float lastTime,waterDepth;
    Vec3 patchCenter,current;
    uint64_t bytes;
};
static TFBuffer* buffer(OceanRenderer* r,uint64_t size,uint32_t descriptors,uint32_t stride=16,
                        TFResourceMemoryUsage memory=TF_RESOURCE_MEMORY_USAGE_GPU_ONLY,const void* data=nullptr) {
    TFBuffer* out=nullptr; TFBufferLoadDesc load={};
    load.mDesc.mSize=size; load.mDesc.mDescriptors=static_cast<TFDescriptorType>(descriptors); load.mDesc.mMemoryUsage=memory;
    load.mDesc.mStructStride=stride; load.mDesc.mElementCount=uint32_t(size/stride);
    if(memory!=TF_RESOURCE_MEMORY_USAGE_GPU_ONLY) load.mDesc.mFlags=TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
    load.pData=data; load.ppBuffer=&out; addResource(&load,nullptr); r->bytes+=size; return out;
}
static void upload(TFBuffer* buffer,const void* data,size_t size) {
    MTRACY_ZONE("upload");
    TFBufferUpdateDesc update={buffer}; beginUpdateResource(&update); memcpy(update.pMappedData,data,size); endUpdateResource(&update);
}
static void barrier(TFCmd* cmd,TFBuffer* buffer) {
    TFBufferBarrier b={buffer,TF_RESOURCE_STATE_UNORDERED_ACCESS,TF_RESOURCE_STATE_UNORDERED_ACCESS};
    cmdResourceBarrier(cmd,1,&b,0,nullptr,0,nullptr);
}
// The Metal backend rebuilds argument-buffer residency on each update.
// Rebind the complete set when scene targets change, including cloud storage.
static void bindOceanResources(OceanRenderer* r,TFRenderTarget* opaque=nullptr,TFRenderTarget* reflection=nullptr,
                               TFRenderTarget* shadow=nullptr,TFRenderTarget* waterLight=nullptr) {
    MTRACY_ZONE("bindOceanResources");
    for(unsigned f=0;f<2;++f) {
        TFDescriptorData data[27]={};
        data[0].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gWater); data[0].ppBuffers=&r->camera[f];
        data[1].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gWaterSurface); data[1].ppBuffers=&r->surface[f];
        data[2].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gWaterNormals); data[2].ppBuffers=&r->normals;
        data[3].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gLocalSurface); data[3].ppBuffers=&r->local;
        data[4].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gFoamHistory); data[4].ppBuffers=&r->foam[1-f];
        data[5].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gFoamOutput); data[5].ppBuffers=&r->foam[f];
        data[6].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gSprayHistory); data[6].ppBuffers=&r->spray[1-f];
        data[7].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gSprayOutput); data[7].ppBuffers=&r->spray[f];
        data[8].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gFoamDraw); data[8].ppBuffers=&r->foam[f];
        data[9].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gSprayDraw); data[9].ppBuffers=&r->spray[f];
        data[10].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gFoamTexture); data[10].ppTextures=&r->foamTexture;
        data[11].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gSkyRadiance); data[11].ppTextures=&r->sky;
        data[12].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gSkyOutput);
        data[12].mUseTextureDescriptors=true; data[12].ppTextureDescriptors=&r->skyMips[0];
        data[13].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gCloudNoiseTexture); data[13].ppTextures=&r->cloudNoise;
        data[14].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gCloudNoiseOutput); data[14].ppTextures=&r->cloudNoise;
        data[15].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gPreviousWaterSurface); data[15].ppBuffers=&r->surface[1-f];
        data[16].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gWhitewater); data[16].ppBuffers=&r->whitewater[f];
        data[17].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gLocalWhitewater); data[17].ppBuffers=&r->localWhitewater[f];
        data[18].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gLocalWhitewaterHistory); data[18].ppBuffers=&r->localWhitewater[1-f];
        data[19].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gLocalWhitewaterOutput); data[19].ppBuffers=&r->localWhitewater[f];
        data[20].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gCrestHistory); data[20].ppBuffers=&r->crests[1-f];
        data[21].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gCrestDraw); data[21].ppBuffers=&r->crests[f];
        data[22].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gCrestOutput); data[22].ppBuffers=&r->crests[f];
        if(opaque) {
            data[23].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gOpaque); data[23].ppTextures=&opaque->pTexture;
            data[24].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gReflection); data[24].ppTextures=&reflection->pTexture;
            data[25].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gShadow); data[25].ppTextures=&shadow->pTexture;
            data[26].mIndex=SRT_RES_IDX(WaterDraw,PerFrame,gWaterLight); data[26].ppTextures=&waterLight->pTexture;
        }
        updateDescriptorSet(r->renderer,f,r->drawSets,opaque?27:23,data);
    }
}
OceanRenderer* createOceanRenderer(TFRenderer* renderer,unsigned n) {
    MTRACY_ZONE("createOceanRenderer");
    ASSERT(n==256 || n==512);
    auto* r=tf_new(OceanRenderer); r->renderer=renderer; r->size=n;
    for(unsigned i=n;i>1;i/=2) ++r->logSize;
    r->surfacePass=3;
    r->passes=r->logSize+5;
    Complex twiddles[256];
    for(unsigned i=0;i<n/2;++i) { float phase=6.28318530718f*i/n; twiddles[i]={std::cos(phase),std::sin(phase)}; }
    r->twiddles=buffer(r,n/2*sizeof(Complex),TF_DESCRIPTOR_TYPE_BUFFER,8,TF_RESOURCE_MEMORY_USAGE_GPU_ONLY,twiddles);
    TFTextureDesc sky={}; sky.mWidth=n*4; sky.mHeight=n*2; sky.mDepth=sky.mArraySize=1;
    sky.mMipLevels=r->logSize+2; sky.mSampleCount=TF_SAMPLE_COUNT_1;
    sky.mFormat=TinyImageFormat_R16G16B16A16_SFLOAT;
    sky.mDescriptors=TF_DESCRIPTOR_TYPE_TEXTURE|TF_DESCRIPTOR_TYPE_RW_TEXTURE;
    sky.mStartState=TF_RESOURCE_STATE_SHADER_RESOURCE; sky.pName="Filtered sky radiance";
    TFTextureLoadDesc skyLoad={}; skyLoad.pDesc=&sky; skyLoad.ppTexture=&r->sky;
    addResource(&skyLoad,nullptr); waitForAllResourceLoads(); r->bytes+=16ull*(16ull*n*n-1)/3;
    for(unsigned level=0;level<sky.mMipLevels;++level) {
        TFTextureDescriptorDesc mip={}; mip.pTexture=r->sky; mip.mFormat=sky.mFormat;
        mip.mDescriptors=TF_DESCRIPTOR_TYPE_RW_TEXTURE; mip.mBaseMipLevel=level; mip.mMipLevelCount=1;
        addTextureDescriptor(renderer,&mip,&r->skyMips[level]);
    }
    auto* indices=static_cast<uint32_t*>(tf_malloc(512*512*6*sizeof(uint32_t)));
    for(unsigned level=0;level<3;++level) {
        unsigned grid=128u<<level, count=0;
        for(unsigned y=0;y<grid;++y) for(unsigned x=0;x<grid;++x) {
            uint32_t a=y*(grid+1)+x, b=a+1, c=a+grid+2, d=a+grid+1;
            for(uint32_t index:{a,b,c,a,c,d}) indices[count++]=index;
        }
        r->gridIndices[level]=buffer(r,count*sizeof(uint32_t),TF_DESCRIPTOR_TYPE_INDEX_BUFFER,4,TF_RESOURCE_MEMORY_USAGE_GPU_ONLY,indices);
        waitForAllResourceLoads();
    }
    tf_free(indices);
    const uint64_t bytes=4ull*n*n*16, mipBytes=4ull*(4ull*n*n-1)/3*16;
    // A transform group loads its entire row/column before writing it back.
    // Groups own disjoint cells, so both axes safely reuse this single buffer.
    r->transform=buffer(r,bytes,TF_DESCRIPTOR_TYPE_BUFFER|TF_DESCRIPTOR_TYPE_RW_BUFFER);
    // These fields have no temporal history. Compute and draws share one
    // ordered graphics queue, with Forge encoder barriers before each reuse.
    r->normals=buffer(r,mipBytes,TF_DESCRIPTOR_TYPE_BUFFER|TF_DESCRIPTOR_TYPE_RW_BUFFER);
    r->local=buffer(r,LocalWaterSize*LocalWaterSize*16,TF_DESCRIPTOR_TYPE_BUFFER|TF_DESCRIPTOR_TYPE_RW_BUFFER);
    for(unsigned f=0;f<2;++f) {
        r->spectrum[f]=buffer(r,sizeof r->renderSpectrum,TF_DESCRIPTOR_TYPE_BUFFER,16,TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU);
        r->surface[f]=buffer(r,mipBytes,TF_DESCRIPTOR_TYPE_BUFFER|TF_DESCRIPTOR_TYPE_RW_BUFFER);
        // Only bands 0 and 2 produce whitewater; band 0 already includes band 1's strain.
        r->whitewater[f]=buffer(r,mipBytes/4,TF_DESCRIPTOR_TYPE_BUFFER|TF_DESCRIPTOR_TYPE_RW_BUFFER,8);
        r->localWhitewater[f]=buffer(r,256*256*8,TF_DESCRIPTOR_TYPE_BUFFER|TF_DESCRIPTOR_TYPE_RW_BUFFER,8);
        r->foam[f]=buffer(r,256*256*16,TF_DESCRIPTOR_TYPE_BUFFER|TF_DESCRIPTOR_TYPE_RW_BUFFER);
        r->spray[f]=buffer(r,4096*3*16,TF_DESCRIPTOR_TYPE_BUFFER|TF_DESCRIPTOR_TYPE_RW_BUFFER);
        r->crests[f]=buffer(r,256*16,TF_DESCRIPTOR_TYPE_BUFFER|TF_DESCRIPTOR_TYPE_RW_BUFFER);
        r->camera[f]=buffer(r,sizeof(WaterParameters),TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER,16,TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU);
        r->packets[f]=buffer(r,sizeof r->packetUpload,TF_DESCRIPTOR_TYPE_BUFFER,16,TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU);
        for(unsigned p=0;p<r->passes;++p)
            r->uniform[f][p]=buffer(r,sizeof(OceanParameters),TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER,16,TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU);
    }
    waitForAllResourceLoads();
    const char* names[]={"ocean_spectrum.comp","ocean_fft.comp","ocean_surface.comp","ocean_packets.comp","ocean_mip.comp"};
    for(unsigned i=0;i<5;++i) {
        TFShaderLoadDesc shader={}; shader.mComp.pFileName=names[i]; addShader(renderer,&shader,&r->computeShaders[i]);
        TFPipelineDesc pipeline={}; pipeline.mType=TF_PIPELINE_TYPE_COMPUTE; pipeline.pName=names[i]; pipeline.mComputeDesc.pShaderProgram=r->computeShaders[i];
        PIPELINE_LAYOUT_DESC(pipeline,nullptr,SRT_LAYOUT_DESC(OceanCompute,PerFrame),nullptr,nullptr);
        addPipeline(renderer,&pipeline,&r->computePipelines[i]);
    }
    TFDescriptorSetDesc set=SRT_SET_DESC(OceanCompute,PerFrame,2*r->passes,0); addDescriptorSet(renderer,&set,&r->computeSets);
    for(unsigned f=0;f<2;++f) for(unsigned p=0;p<r->passes;++p) {
        TFBuffer* source=r->transform;
        TFBuffer* destination=r->transform;
        if(p==r->surfacePass) destination=r->surface[f];
        if(p==r->surfacePass+1) destination=r->local;
        if(p>r->surfacePass+1) { source=r->surface[f]; destination=r->surface[f]; }
        TFDescriptorData data[10]={};
        data[0].mIndex=SRT_RES_IDX(OceanCompute,PerFrame,gOcean); data[0].ppBuffers=&r->uniform[f][p];
        data[1].mIndex=SRT_RES_IDX(OceanCompute,PerFrame,gSpectrum); data[1].ppBuffers=&r->spectrum[f];
        data[2].mIndex=SRT_RES_IDX(OceanCompute,PerFrame,gSource); data[2].ppBuffers=&source;
        data[3].mIndex=SRT_RES_IDX(OceanCompute,PerFrame,gDestination); data[3].ppBuffers=&destination;
        data[4].mIndex=SRT_RES_IDX(OceanCompute,PerFrame,gHistory); data[4].ppBuffers=&r->surface[1-f];
        data[5].mIndex=SRT_RES_IDX(OceanCompute,PerFrame,gNormals); data[5].ppBuffers=&r->normals;
        data[6].mIndex=SRT_RES_IDX(OceanCompute,PerFrame,gTwiddles); data[6].ppBuffers=&r->twiddles;
        data[7].mIndex=SRT_RES_IDX(OceanCompute,PerFrame,gPackets); data[7].ppBuffers=&r->packets[f];
        data[8].mIndex=SRT_RES_IDX(OceanCompute,PerFrame,gWhitewaterHistory); data[8].ppBuffers=&r->whitewater[1-f];
        data[9].mIndex=SRT_RES_IDX(OceanCompute,PerFrame,gWhitewaterOutput); data[9].ppBuffers=&r->whitewater[f];
        updateDescriptorSet(renderer,f*r->passes+p,r->computeSets,TF_ARRAY_COUNT(data),data);
    }
    TFTextureLoadDesc foamTexture={}; foamTexture.pFileName="FoamLace.ktx";
    foamTexture.mContainer=TF_TEXTURE_CONTAINER_KTX; foamTexture.ppTexture=&r->foamTexture;
    addResource(&foamTexture,nullptr); waitForAllResourceLoads();
    TFTextureDesc cloud={}; cloud.mWidth=cloud.mHeight=cloud.mDepth=64;
    cloud.mArraySize=cloud.mMipLevels=1; cloud.mSampleCount=TF_SAMPLE_COUNT_1;
    cloud.mFormat=TinyImageFormat_R8G8_UNORM;
    cloud.mDescriptors=TF_DESCRIPTOR_TYPE_TEXTURE|TF_DESCRIPTOR_TYPE_RW_TEXTURE;
    cloud.mStartState=TF_RESOURCE_STATE_UNORDERED_ACCESS; cloud.pName="Periodic cloud shape and erosion";
    TFTextureLoadDesc cloudLoad={}; cloudLoad.pDesc=&cloud; cloudLoad.ppTexture=&r->cloudNoise;
    addResource(&cloudLoad,nullptr); waitForAllResourceLoads(); r->bytes+=64*64*64*2;
    set=SRT_SET_DESC(WaterDraw,PerFrame,2,0); addDescriptorSet(renderer,&set,&r->drawSets);
    bindOceanResources(r);
    TFShaderLoadDesc effects={}; effects.mComp.pFileName="water_effects.comp";
    addShader(renderer,&effects,&r->effectsShader);
    TFPipelineDesc effectPipeline={}; effectPipeline.mType=TF_PIPELINE_TYPE_COMPUTE;
    effectPipeline.mComputeDesc.pShaderProgram=r->effectsShader;
    PIPELINE_LAYOUT_DESC(effectPipeline,nullptr,SRT_LAYOUT_DESC(WaterDraw,PerFrame),nullptr,nullptr);
    effectPipeline.pName="Whitewater transport and particles"; addPipeline(renderer,&effectPipeline,&r->effectsPipeline);
    effects.mComp.pFileName="water_sky.comp"; addShader(renderer,&effects,&r->skyShader);
    effectPipeline.mComputeDesc.pShaderProgram=r->skyShader;
    effectPipeline.pName="Sky radiance"; addPipeline(renderer,&effectPipeline,&r->skyPipeline);
    effects.mComp.pFileName="cloud_noise.comp"; addShader(renderer,&effects,&r->cloudShader);
    effectPipeline.mComputeDesc.pShaderProgram=r->cloudShader;
    effectPipeline.pName="Cloud volume noise"; addPipeline(renderer,&effectPipeline,&r->cloudPipeline);
    effects.mComp.pFileName="water_sky_filter.comp"; addShader(renderer,&effects,&r->skyFilterShader);
    effectPipeline.mComputeDesc.pShaderProgram=r->skyFilterShader;
    PIPELINE_LAYOUT_DESC(effectPipeline,nullptr,SRT_LAYOUT_DESC(SkyFilter,PerFrame),nullptr,nullptr);
    effectPipeline.pName="Sky reflection mip filter"; addPipeline(renderer,&effectPipeline,&r->skyFilterPipeline);
    set=SRT_SET_DESC(SkyFilter,PerFrame,r->logSize+1,0); addDescriptorSet(renderer,&set,&r->skyFilterSets);
    for(unsigned level=1;level<=r->logSize+1;++level) {
        SkyFilterParameters parameters={{n*2,level,0,0}};
        r->skyFilterParameters[level-1]=buffer(r,sizeof parameters,TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER,16,TF_RESOURCE_MEMORY_USAGE_GPU_ONLY,&parameters);
        waitForAllResourceLoads();
        TFDescriptorData data[3]={};
        data[0].mIndex=SRT_RES_IDX(SkyFilter,PerFrame,gFilter); data[0].ppBuffers=&r->skyFilterParameters[level-1];
        // Both views stay in UAV state while filtering disjoint mip levels.
        data[1].mIndex=SRT_RES_IDX(SkyFilter,PerFrame,gRadiance);
        data[1].mUseTextureDescriptors=true; data[1].ppTextureDescriptors=&r->skyMips[level-1];
        data[2].mIndex=SRT_RES_IDX(SkyFilter,PerFrame,gFiltered);
        data[2].mUseTextureDescriptors=true; data[2].ppTextureDescriptors=&r->skyMips[level];
        updateDescriptorSet(renderer,level-1,r->skyFilterSets,TF_ARRAY_COUNT(data),data);
    }
    return r;
}
void setOceanScene(OceanRenderer* r,TFRenderTarget* opaque,TFRenderTarget* reflection,TFRenderTarget* shadow,TFRenderTarget* waterLight) {
    MTRACY_ZONE("setOceanScene");
    bindOceanResources(r,opaque,reflection,shadow,waterLight);
}

void loadOceanRenderer(OceanRenderer* r,uint32_t format) {
    MTRACY_ZONE("loadOceanRenderer");
    TFShaderLoadDesc shader={}; shader.mVert.pFileName="water.vert"; shader.mFrag.pFileName="water.frag";
    addShader(r->renderer,&shader,&r->drawShader);
    TFPipelineDesc p={}; p.mType=TF_PIPELINE_TYPE_GRAPHICS;
    PIPELINE_LAYOUT_DESC(p,nullptr,SRT_LAYOUT_DESC(WaterDraw,PerFrame),nullptr,nullptr);
    TFDepthStateDesc depth={}; depth.mDepthTest=depth.mDepthWrite=true; depth.mDepthFunc=TF_CMP_LEQUAL;
    TFRasterizerStateDesc raster={}; raster.mCullMode=TF_CULL_MODE_NONE;
    TinyImageFormat color=static_cast<TinyImageFormat>(format);
    auto& g=p.mGraphicsDesc; g.pShaderProgram=r->drawShader; g.pDepthState=&depth; g.pRasterizerState=&raster;
    g.pColorFormats=&color; g.mRenderTargetCount=1; g.mDepthStencilFormat=TinyImageFormat_D32_SFLOAT;
    g.mSampleCount=TF_SAMPLE_COUNT_1; g.mPrimitiveTopo=TF_PRIMITIVE_TOPO_TRI_LIST;
    p.pName="Ocean surface"; addPipeline(r->renderer,&p,&r->drawPipeline);
    shader.mVert.pFileName="water_light.vert"; shader.mFrag.pFileName="water_light.frag";
    addShader(r->renderer,&shader,&r->lightShader); g.pShaderProgram=r->lightShader;
    color=TinyImageFormat_R32_SFLOAT; p.pName="Water light depth"; addPipeline(r->renderer,&p,&r->lightPipeline);
    color=static_cast<TinyImageFormat>(format);
    shader.mVert.pFileName="water_spray.vert"; shader.mFrag.pFileName="water_spray.frag";
    addShader(r->renderer,&shader,&r->sprayShader); g.pShaderProgram=r->sprayShader;
    depth.mDepthWrite=false;
    TFBlendStateDesc blend={}; blend.mSrcFactors[0]=blend.mSrcAlphaFactors[0]=TF_BC_ONE;
    blend.mDstFactors[0]=blend.mDstAlphaFactors[0]=TF_BC_ONE_MINUS_SRC_ALPHA;
    // The HDR target's alpha stores view depth for atmospheric integration.
    blend.mColorWriteMasks[0]=TF_COLOR_MASK_RED|TF_COLOR_MASK_GREEN|TF_COLOR_MASK_BLUE;
    blend.mRenderTargetMask=TF_BLEND_STATE_TARGET_0;
    g.pBlendState=&blend; p.pName="Foam and spray"; addPipeline(r->renderer,&p,&r->sprayPipeline);
}
void unloadOceanRenderer(OceanRenderer* r) {
    MTRACY_ZONE("unloadOceanRenderer"); removePipeline(r->renderer,r->drawPipeline); removeShader(r->renderer,r->drawShader);
    removePipeline(r->renderer,r->sprayPipeline); removeShader(r->renderer,r->sprayShader);
    removePipeline(r->renderer,r->lightPipeline); removeShader(r->renderer,r->lightShader); }
void destroyOceanRenderer(OceanRenderer* r) {
    MTRACY_ZONE("destroyOceanRenderer");
    removeDescriptorSet(r->renderer,r->skyFilterSets);
    removePipeline(r->renderer,r->skyFilterPipeline); removeShader(r->renderer,r->skyFilterShader);
    for(unsigned level=0;level<=r->logSize;++level) removeResource(r->skyFilterParameters[level]);
    for(unsigned level=0;level<=r->logSize+1;++level) removeTextureDescriptor(r->renderer,r->skyMips[level]);
    removePipeline(r->renderer,r->cloudPipeline); removeShader(r->renderer,r->cloudShader); removeResource(r->cloudNoise);
    removePipeline(r->renderer,r->skyPipeline); removeShader(r->renderer,r->skyShader); removeResource(r->sky);
    removePipeline(r->renderer,r->effectsPipeline); removeShader(r->renderer,r->effectsShader);
    removeResource(r->foamTexture);
    for(auto* indices:r->gridIndices) removeResource(indices);
    removeDescriptorSet(r->renderer,r->drawSets); removeDescriptorSet(r->renderer,r->computeSets);
    for(unsigned i=0;i<5;++i) { removePipeline(r->renderer,r->computePipelines[i]); removeShader(r->renderer,r->computeShaders[i]); }
    for(unsigned f=0;f<2;++f) {
        for(unsigned p=0;p<r->passes;++p) removeResource(r->uniform[f][p]);
        removeResource(r->spectrum[f]); removeResource(r->surface[f]);
        removeResource(r->camera[f]);
        removeResource(r->foam[f]); removeResource(r->spray[f]); removeResource(r->crests[f]);
        removeResource(r->whitewater[f]); removeResource(r->localWhitewater[f]);
        removeResource(r->packets[f]);
    }
    removeResource(r->transform); removeResource(r->normals); removeResource(r->local);
    removeResource(r->twiddles); tf_delete(r);
}
void computeOcean(OceanRenderer* r,TFCmd* cmd,const Ocean* ocean,unsigned f,float time,const WaterLook& look) {
    MTRACY_ZONE("computeOcean");
    const auto& sea=seaState(ocean); const unsigned n=r->size;
    bool changed=r->revision[f]!=oceanRevision(ocean) || time<r->lastTime;
    if(changed) {
        memcpy(r->renderSpectrum,oceanSpectrum(ocean),OceanModeCount*sizeof(SpectrumMode));
        buildRippleSpectrum(sea,r->renderSpectrum+OceanModeCount);
        upload(r->spectrum[f],r->renderSpectrum,sizeof r->renderSpectrum); r->revision[f]=oceanRevision(ocean);
    }
    unsigned active=0;
    for(unsigned i=0;i<MaxWavePackets;++i) if(oceanPackets(ocean)[i].lifetime>0) r->packetUpload[active++]=oceanPackets(ocean)[i];
    static_assert(sizeof(HullWake)==sizeof(WavePacket));
    memcpy(r->packetUpload+MaxWavePackets,oceanHullWakes(ocean),MaxHullWakes*sizeof(HullWake));
    upload(r->packets[f],r->packetUpload,sizeof r->packetUpload);
    r->patchCenter=oceanPatchCenter(ocean); r->waterDepth=sea.depth; r->current=sea.current;
    r->wind={sea.windSpeed*std::cos(sea.windDirection),0,sea.windSpeed*std::sin(sea.windDirection)};
    if(changed) r->effectsValid=false;
    OceanParameters params={{time,std::clamp(time-r->lastTime,0.0f,.2f),sea.depth,sea.choppiness},{n,0,0,MaxWavePackets},
                              {256,64,16,2},{sea.current.x,0,sea.current.z,changed?0.0f:1.0f},
                              {r->patchCenter.x,r->patchCenter.z,float(LocalWaterSize),float(active)}};
    params.foamSettings[0]=look.foamDecay; params.foamSettings[1]=look.foamSpread; params.foamSettings[2]=look.breakingThreshold;
    for(unsigned band=0;band<3;++band) params.breaking[band]=std::min(.98f,oceanMetrics(ocean).breakingThreshold[n==512][band]+look.breakingThreshold-.55f);
    r->lastTime=time;
    for(unsigned p=0;p<r->passes;++p) {
        unsigned pipeline=p==0?0:p==r->surfacePass?2:p==r->surfacePass+1?3:p>r->surfacePass+1?4:1;
        if(pipeline==1) params.transform[2]=p-1;
        if(pipeline==4) params.transform[1]=p-r->surfacePass-1;
        upload(r->uniform[f][p],&params,sizeof params);
        cmdBindPipeline(cmd,r->computePipelines[pipeline]);
        cmdBindDescriptorSet(cmd,f*r->passes+p,r->computeSets);
        if(pipeline==3) cmdDispatch(cmd,LocalWaterSize/8,LocalWaterSize/8,1);
        else if(pipeline==4) { unsigned m=n>>params.transform[1]; cmdDispatch(cmd,(m+7)/8,(m+7)/8,4); }
        else if(pipeline==1) cmdDispatch(cmd,1,n,4);
        else cmdDispatch(cmd,n/8,n/8,4);
        barrier(cmd,pipeline==2 || pipeline==4?r->surface[f]:pipeline==3?r->local:r->transform);
        if(pipeline==2 || pipeline==4) { barrier(cmd,r->normals); barrier(cmd,r->whitewater[f]); }
    }
}
void prepareOceanEffects(OceanRenderer* r,TFCmd* cmd,const Camera& camera,unsigned frame,float level,const f4x4& shadowMatrix,const WaterLook& look,const Snapshot& boat,const VesselLayout& layout,float4 reflectionBounds,const WaterProfile* profile,float4 shadowBounds) {
    MTRACY_ZONE("prepareOceanEffects");
    WaterParameters params={}; params.viewProjection=cameraMatrix(camera); params.shadowMatrix=shadowMatrix; auto eye=cameraEye(camera);
    params.eye=float4(toForge(eye));
    params.surface[0]=float(r->size); params.surface[1]=float(look.gridSize); params.surface[2]=.25f; params.surface[3]=level;
    params.origin=float4(toForge(camera.focus));
    params.patch[0]=r->patchCenter.x; params.patch[1]=float(LocalWaterSize); params.patch[2]=r->patchCenter.z;
    params.screen[0]=float(camera.width); params.screen[1]=float(camera.height); params.screen[2]=r->lastTime; params.screen[3]=r->waterDepth;
    params.current=float4(toForge(r->current));
    look.parameters(params.weather,params.effects); look.sunlight(params.sun);
    look.fidelity(params.detail,params.quality,params.optics,params.foamSettings);
    params.history[0]=std::clamp(r->lastTime-r->effectsTime,0.0f,.1f); params.history[1]=r->effectsValid?1.0f:0.0f;
    if(!r->effectsValid) r->effectsFrame=0;
    ++r->effectsFrame;
    params.history[2]=float(r->effectsFrame);
    float eyeShiftSquared=f3LengthSqr(f3Sub(toForge(eye),toForge(r->skyEye)));
    bool skyReset=!r->effectsValid || r->skyCover!=look.overcast || memcmp(&r->skySun,&params.sun,sizeof r->skySun)!=0 || eyeShiftSquared>4;
    params.history[3]=skyReset?0.0f:.9f;
    r->skyEye=eye; r->skyCover=look.overcast; r->skySun=params.sun;
    params.boatPosition=float4(toForge(boat.position),layout.beam);
    auto forward=deckToWorld(boat,{0,0,1});
    params.boatForward[0]=forward.x-boat.position.x; params.boatForward[2]=forward.z-boat.position.z;
    float magnitude=std::hypot(params.boatForward[0],params.boatForward[2]);
    if(magnitude>.001f) { params.boatForward[0]/=magnitude; params.boatForward[2]/=magnitude; }
    params.boatForward[3]=layout.length;
    params.hulls[0]=layout.hullSpacing; params.hulls[1]=layout.beam-layout.hullSpacing;
    params.hulls[2]=float(layout.propellerCount);
    for(unsigned p=0;p<layout.propellerCount;++p) {
        const auto& propeller=layout.propellers[p];
        auto position=deckToWorld(boat,propeller.position);
        params.propellers[p]=float4(toForge(position),boat.rpm[p]);
    }
    params.boatVelocity=float4(toForge(boat.velocity)); params.wind=float4(toForge(r->wind),f3Length(toForge(r->wind)));
    Vec3 foamCenter={std::floor(r->patchCenter.x*4)*.25f,0,std::floor(r->patchCenter.z*4)*.25f};
    params.foamPatch[0]=foamCenter.x; params.foamPatch[1]=foamCenter.z;
    params.foamPatch[2]=r->previousFoamPatch.x; params.foamPatch[3]=r->previousFoamPatch.z;
    params.trace[0]=float(look.refractionSteps);
    params.trace[1]=look.debugGain;
    params.trace[2]=float(look.reflectionSteps);
    params.trace[3]=float(look.reflectionDivisor);
    params.seabed={look.seabedColor[0],look.seabedColor[1],look.seabedColor[2],0};
    params.foamAppearance[0]=look.foamRelief;
    params.foamAppearance[1]=look.foamBreakup;
    params.foamAppearance[2]=look.foamWaveFlow;
    params.foamAppearance[3]=look.foamClumpHeight;
    params.lightProjection=lightProjection({camera.focus.x,level,camera.focus.z},params.sun.getXYZ(),look.waterLightSize,256,512);
    params.volume={512,float(look.scatteringSamples),look.bubbleScattering,look.volumeContrast};
    params.reflectionBounds=reflectionBounds;
    params.shadowBounds=shadowBounds;
    upload(r->camera[frame],&params,sizeof params);
    beginWaterPass(cmd,profile,WaterEffects);
    if(!r->cloudReady) {
        cmdBindPipeline(cmd,r->cloudPipeline); cmdBindDescriptorSet(cmd,frame,r->drawSets); cmdDispatch(cmd,16,16,16);
        TFTextureBarrier cloud={r->cloudNoise,TF_RESOURCE_STATE_UNORDERED_ACCESS,TF_RESOURCE_STATE_SHADER_RESOURCE};
        cmdResourceBarrier(cmd,0,nullptr,1,&cloud,0,nullptr); r->cloudReady=true;
    }
    cmdBindPipeline(cmd,r->effectsPipeline); cmdBindDescriptorSet(cmd,frame,r->drawSets); cmdDispatch(cmd,32,32,1);
    barrier(cmd,r->foam[frame]); barrier(cmd,r->spray[frame]);
    barrier(cmd,r->crests[frame]);
    barrier(cmd,r->localWhitewater[frame]);
    endWaterPass(cmd,profile,WaterEffects);
    beginWaterPass(cmd,profile,WaterSky);
    TFTextureBarrier skyBarrier={r->sky,TF_RESOURCE_STATE_SHADER_RESOURCE,TF_RESOURCE_STATE_UNORDERED_ACCESS};
    cmdResourceBarrier(cmd,0,nullptr,1,&skyBarrier,0,nullptr);
    cmdBindPipeline(cmd,r->skyPipeline); cmdBindDescriptorSet(cmd,frame,r->drawSets);
    cmdDispatch(cmd,r->size/2,r->size/4,1);
    skyBarrier.mCurrentState=TF_RESOURCE_STATE_UNORDERED_ACCESS;
    cmdResourceBarrier(cmd,0,nullptr,1,&skyBarrier,0,nullptr);
    if(params.history[0]>0 || skyReset) {
        cmdBindPipeline(cmd,r->skyFilterPipeline);
        for(unsigned level=1;level<=r->logSize+1;++level) {
            unsigned height=(r->size*2)>>level;
            cmdBindDescriptorSet(cmd,level-1,r->skyFilterSets);
            cmdDispatch(cmd,(height*2+7)/8,(height+7)/8,1);
            cmdResourceBarrier(cmd,0,nullptr,1,&skyBarrier,0,nullptr);
        }
    }
    skyBarrier.mNewState=TF_RESOURCE_STATE_SHADER_RESOURCE;
    cmdResourceBarrier(cmd,0,nullptr,1,&skyBarrier,0,nullptr);
    endWaterPass(cmd,profile,WaterSky);
    r->previousFoamPatch=foamCenter; r->effectsTime=r->lastTime; r->effectsValid=true;
    r->particleCapacity=unsigned(look.particles); r->gridSize=unsigned(look.gridSize);
}
void drawOcean(OceanRenderer* r,TFCmd* cmd,unsigned frame) {
    MTRACY_ZONE("drawOcean");
    cmdBindPipeline(cmd,r->drawPipeline); cmdBindDescriptorSet(cmd,frame,r->drawSets);
    cmdBindIndexBuffer(cmd,r->gridIndices[r->gridSize==128?0:r->gridSize==256?1:2],TF_INDEX_TYPE_UINT32,0);
    cmdDrawIndexed(cmd,r->gridSize*r->gridSize*6,0,0);
    if(r->particleCapacity) { cmdBindPipeline(cmd,r->sprayPipeline); cmdDraw(cmd,r->particleCapacity*WATER_PARTICLE_VERTICES,0); }
}
void drawOceanLight(OceanRenderer* r,TFCmd* cmd,unsigned frame) {
    MTRACY_ZONE("drawOceanLight");
    cmdBindPipeline(cmd,r->lightPipeline); cmdBindDescriptorSet(cmd,frame,r->drawSets);
    cmdBindIndexBuffer(cmd,r->gridIndices[r->gridSize==128?0:r->gridSize==256?1:2],TF_INDEX_TYPE_UINT32,0);
    cmdDrawIndexed(cmd,r->gridSize*r->gridSize*6,0,0);
}
bool verifyOceanGPU(OceanRenderer* r,TFQueue* queue) {
    MTRACY_ZONE("verifyOceanGPU");
    // Validation must not clear the live world's wake history on a graphics reload.
    Ocean* ocean=createOcean(SeaState{});
    const unsigned n=r->size; const uint64_t bytes=4ull*(4ull*n*n-1)/3*16,localBytes=LocalWaterSize*LocalWaterSize*16;
    const uint64_t stateOffset=bytes*2+localBytes,readbackBytes=stateOffset+bytes/4;
    auto* readback=buffer(r,readbackBytes,TF_DESCRIPTOR_TYPE_RW_BUFFER,16,TF_RESOURCE_MEMORY_USAGE_GPU_TO_CPU);
    waitForAllResourceLoads();
    TFCmdPool* pool=nullptr; TFCmdPoolDesc pd={}; pd.pQueue=queue; initCmdPool(r->renderer,&pd,&pool);
    TFCmd* cmd=nullptr; TFCmdDesc cd={}; cd.pPool=pool; initCmd(r->renderer,&cd,&cmd);
    TFFence* fence=nullptr; initFence(r->renderer,&fence);
    emitWavePacket(ocean,{8,0,0},{1,0,0},3,200,true);
    emitWavePacket(ocean,{26,0,27},{1,0,0},12,1000);
    HullWake testWake{{-5,0,1},.25f,{.6f,0,.8f},12,{1.8f,0,2.4f},1.5f,5,7,{}};
    setHullWake(ocean,0,testWake);
    advanceWavePackets(ocean,1,{0,0,0},true);
    updateOcean(ocean,.75); beginCmd(cmd); computeOcean(r,cmd,ocean,0,.75f);
    cmdUpdateBuffer(cmd,readback,0,r->surface[0],0,bytes);
    cmdUpdateBuffer(cmd,readback,bytes,r->normals,0,bytes);
    cmdUpdateBuffer(cmd,readback,bytes*2,r->local,0,localBytes); endCmd(cmd);
    FlushResourceUpdateDesc flush={}; flushResourceUpdates(&flush);
    TFQueueSubmitDesc submit={}; submit.mCmdCount=1; submit.ppCmds=&cmd; submit.pSignalFence=fence;
    submit.mWaitSemaphoreCount=flush.pOutSubmittedSemaphore?1:0; submit.ppWaitSemaphores=&flush.pOutSubmittedSemaphore;
    queueSubmit(queue,&submit); waitForFences(r->renderer,1,&fence);
    const auto* data=static_cast<const float*>(readback->pCpuMappedAddress);
    float maxError=0; bool valid=true;
    for(unsigned band=0;band<3;++band) for(unsigned y=0;y<128;y+=7) for(unsigned x=0;x<128;x+=11) {
        auto cpu=sampleOceanBand(ocean,band,x*OceanLengths[band]/128,y*OceanLengths[band]/128);
        unsigned i=band*n*n+(y*n/128)*n+x*n/128;
        float error=std::max(std::fabs(data[i*4+1]-cpu.height),
                   std::max(std::fabs(data[i*4]-cpu.displacement.x),std::fabs(data[i*4+2]-cpu.displacement.z)));
        bool finite=std::isfinite(data[i*4]) && std::isfinite(data[i*4+1]) && std::isfinite(data[i*4+2]) && std::isfinite(data[i*4+3]);
        valid&=finite && std::isfinite(error)&&error<.0005f; maxError=std::max(maxError,error);
        if(!finite) LOGF(eERROR,"Non-finite ocean displacement in band %u at %u,%u",band,x,y);
    }
    LOGF(valid?eINFO:eERROR,"Ocean FFT %ux%u x 3: GPU/CPU height and displacement %s; max error %.7f m",n,n,valid?"PASS":"FAIL",maxError);
    float mipError=0; bool mipValid=true;
    for(unsigned channel=0;channel<2;++channel) {
        const float* values=data+channel*bytes/4;
        for(unsigned m=n/2;m>=1;m/=2) for(unsigned band=0;band<4;++band) {
            unsigned parent=m*2, source=16*((n*n-parent*parent)/3)+band*parent*parent;
            unsigned target=16*((n*n-m*m)/3)+band*m*m;
            for(unsigned y=0;y<m;y+=std::max(1u,m/7)) for(unsigned x=0;x<m;x+=std::max(1u,m/7)) {
                unsigned dest=(target+y*m+x)*4;
                for(unsigned c=0;c<4;++c) {
                    float expected=0;
                    for(unsigned j=0;j<2;++j) for(unsigned i=0;i<2;++i)
                        expected+=values[(source+(y*2+j)*parent+x*2+i)*4+c]*.25f;
                    float error=std::fabs(values[dest+c]-expected);
                    mipValid&=std::isfinite(values[dest+c]) && std::isfinite(error) && error<.00002f;
                    mipError=std::max(mipError,error);
                }
                if(channel==1) mipValid&=values[dest+2]+.00002f>=values[dest]*values[dest]+values[dest+1]*values[dest+1];
            }
        }
    }
    valid&=mipValid;
    LOGF(mipValid?eINFO:eERROR,"Ocean displacement and slope-moment mip chain %s; max error %.7f",mipValid?"PASS":"FAIL",mipError);
    float localError=0;
    const auto* patch=data+bytes*2/4;
    for(unsigned y=0;y<LocalWaterSize;y+=3) for(unsigned x=0;x<LocalWaterSize;x+=3) {
        auto cpu=sampleWavePackets(ocean,float(x)*64/LocalWaterSize-32,float(y)*64/LocalWaterSize-32);
        unsigned i=(y*LocalWaterSize+x)*4;
        valid&=std::isfinite(patch[i])&&std::isfinite(patch[i+1])&&std::isfinite(patch[i+2]);
        localError=std::max(localError,std::fabs(cpu.height-patch[i]));
        localError=std::max(localError,std::fabs(cpu.dx-patch[i+1]));
        localError=std::max(localError,std::fabs(cpu.dz-patch[i+2]));
    }
    valid&=localError<.00005f;
    LOGF(valid?eINFO:eERROR,"Wave packets: GPU/CPU height and slope %s; max error %.7f",valid?"PASS":"FAIL",localError);
    // Run the complete compute chain, including effects, on both frame buffers.
    // Effects must never overwrite the spectral surface or its normal moments.
    SeaState original=seaState(ocean),calm=original;
    calm.windWaveHeight=.03f; calm.swellHeight=.04f; calm.windSpeed=1; calm.fetch=3000; configureOcean(ocean,calm);
    float maximumCalmFoam=0,minimumJacobian=1;
    bool crestValid=true; unsigned emittingCrests=0,trackedCrests=0;
    const float* crests=data+bytes*2/4;
    float previousCrests[256*4]={};
    Camera testCamera{}; Snapshot testBoat{}; testBoat.rotation.w=1;
    const auto testLayout=oceanis401Layout();
    const auto testShadow=f4x4Identity();
    for(unsigned test=0;test<60;++test) {
        // Wind alone must not turn an undeformed surface into a whitecap.
        // This also exercises thresholds very close to unit surface stretch.
        if(test==30) { calm.windSpeed=3; calm.windWaveHeight=calm.swellHeight=0; configureOcean(ocean,calm); }
        unsigned f=test%2; resetCmdPool(r->renderer,pool); beginCmd(cmd);
        computeOcean(r,cmd,ocean,f,test/30.0f);
        prepareOceanEffects(r,cmd,testCamera,f,calm.level,testShadow,WaterLook{},testBoat,testLayout);
        cmdUpdateBuffer(cmd,readback,0,r->surface[f],0,bytes);
        cmdUpdateBuffer(cmd,readback,bytes,r->normals,0,bytes);
        cmdUpdateBuffer(cmd,readback,bytes*2,r->crests[f],0,sizeof previousCrests); endCmd(cmd);
        flushResourceUpdates(&flush); submit.pSignalFence=fence;
        submit.mWaitSemaphoreCount=flush.pOutSubmittedSemaphore?1:0; submit.ppWaitSemaphores=&flush.pOutSubmittedSemaphore;
        queueSubmit(queue,&submit); waitForFences(r->renderer,1,&fence);
        for(unsigned i=0;i<bytes/16;++i) {
            maximumCalmFoam=std::max(maximumCalmFoam,std::fabs(data[i*4+3]));
            minimumJacobian=std::min(minimumJacobian,data[bytes/4+i*4+3]);
        }
        for(unsigned i=0;i<256;++i) crestValid&=crests[i*4+2]==0 && crests[i*4+3]==0;
    }
    bool calmValid=maximumCalmFoam<.000001f && minimumJacobian>.42f;
    valid&=calmValid;
    LOGF(calmValid?eINFO:eERROR,"Calm FFT + effects + history isolation %s: foam %.7f, minimum Jacobian %.7f",calmValid?"PASS":"FAIL",maximumCalmFoam,minimumJacobian);
    SeaState rough=original; rough.windSpeed=26; rough.windWaveHeight=5; rough.swellHeight=2.5f;
    rough.swellPeriod=10; rough.fetch=65000; rough.depth=30; rough.choppiness=1; configureOcean(ocean,rough);
    float maximumStormFoam=0, minimumStormJacobian=1, maximumStormCoverage=0;
    bool whitewaterValid=true; unsigned oldFoam=0,activeBreakers=0; float maximumAirDepth=0;
    for(unsigned test=0;test<120;++test) {
        unsigned f=test%2; resetCmdPool(r->renderer,pool); beginCmd(cmd);
        computeOcean(r,cmd,ocean,f,test/30.0f);
        prepareOceanEffects(r,cmd,testCamera,f,rough.level,testShadow,WaterLook{},testBoat,testLayout);
        cmdUpdateBuffer(cmd,readback,0,r->surface[f],0,bytes);
        cmdUpdateBuffer(cmd,readback,bytes,r->normals,0,bytes);
        cmdUpdateBuffer(cmd,readback,stateOffset,r->whitewater[f],0,bytes/4);
        cmdUpdateBuffer(cmd,readback,bytes*2,r->crests[f],0,sizeof previousCrests); endCmd(cmd);
        flushResourceUpdates(&flush); submit.pSignalFence=fence;
        submit.mWaitSemaphoreCount=flush.pOutSubmittedSemaphore?1:0; submit.ppWaitSemaphores=&flush.pOutSubmittedSemaphore;
        queueSubmit(queue,&submit); waitForFences(r->renderer,1,&fence);
        unsigned covered=0;
        for(unsigned i=0;i<3*n*n;++i) {
            valid&=std::isfinite(data[i*4+3]) && data[i*4+3]>=0 && data[i*4+3]<=1;
            covered+=data[i*4+3]>.05f;
            maximumStormFoam=std::max(maximumStormFoam,data[i*4+3]);
            minimumStormJacobian=std::min(minimumStormJacobian,data[bytes/4+i*4+3]);
        }
        maximumStormCoverage=std::max(maximumStormCoverage,float(covered)/(3*n*n));
        for(unsigned i=0;i<256;++i) {
            const float* c=crests+i*4; const float* p=previousCrests+i*4;
            crestValid&=std::isfinite(c[0]+c[1]+c[2]+c[3]) && c[2]>=0 && c[2]<=1 && c[3]>=0 && c[3]<2.54f;
            emittingCrests+=c[2]>.05f;
            if(test>0 && c[3]>p[3] && p[3]>0) {
                float dx=c[0]-p[0],dz=c[1]-p[1];
                crestValid&=dx*dx+dz*dz<.801f;
                ++trackedCrests;
            }
        }
        memcpy(previousCrests,crests,sizeof previousCrests);
        const auto* state=reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(data)+stateOffset);
        for(unsigned i=0;i<2*n*n;++i) {
            float age=TinyImageFormat_HalfAsUintToFloat(state[i*4]);
            float air=TinyImageFormat_HalfAsUintToFloat(state[i*4+1]);
            float depth=TinyImageFormat_HalfAsUintToFloat(state[i*4+2])/std::max(.00001f,air);
            float breaking=TinyImageFormat_HalfAsUintToFloat(state[i*4+3]);
            unsigned surfaceIndex=i<n*n?i:i+n*n;
            float foam=data[surfaceIndex*4+3];
            whitewaterValid&=std::isfinite(age+air+depth+breaking) && age>=0 && age<=foam*30+.001f && air>=0 && air<=2 && depth>=0 && depth<1.51f && breaking>=0 && breaking<=1;
            oldFoam+=foam>.02f && age>foam*.5f && breaking==0;
            activeBreakers+=breaking>.1f;
            maximumAirDepth=std::max(maximumAirDepth,depth);
        }
    }
    bool stormValid=maximumStormFoam>.1f && maximumStormCoverage>.001f && maximumStormCoverage<.25f;
    valid&=stormValid;
    LOGF(stormValid?eINFO:eERROR,"Storm crest production %s: maximum foam %.6f, coverage >.05 %.2f%%, minimum Jacobian %.6f",
         stormValid?"PASS":"FAIL",maximumStormFoam,100*maximumStormCoverage,minimumStormJacobian);
    whitewaterValid&=oldFoam>0 && activeBreakers>0 && maximumAirDepth>.2f;
    valid&=whitewaterValid;
    LOGF(whitewaterValid?eINFO:eERROR,"Transported whitewater %s: %u old foam samples without breaking, %u active breaker samples, maximum air depth %.3f m",
         whitewaterValid?"PASS":"FAIL",oldFoam,activeBreakers,maximumAirDepth);
    // A redraw at a different aspect ratio must not restart a breaking event.
    Camera resizedCamera=testCamera; resizedCamera.width=961; resizedCamera.height=1281;
    resetCmdPool(r->renderer,pool); beginCmd(cmd);
    prepareOceanEffects(r,cmd,resizedCamera,0,rough.level,testShadow,WaterLook{},testBoat,testLayout);
    cmdUpdateBuffer(cmd,readback,bytes*2,r->crests[0],0,sizeof previousCrests); endCmd(cmd);
    flushResourceUpdates(&flush);
    submit.mWaitSemaphoreCount=flush.pOutSubmittedSemaphore?1:0; submit.ppWaitSemaphores=&flush.pOutSubmittedSemaphore;
    queueSubmit(queue,&submit); waitForFences(r->renderer,1,&fence);
    crestValid&=memcmp(previousCrests,crests,sizeof previousCrests)==0 && emittingCrests>0 && trackedCrests>0;
    valid&=crestValid;
    LOGF(crestValid?eINFO:eERROR,"Crest source continuity, calm isolation and paused resize %s: %u emitting, %u tracked source steps",
         crestValid?"PASS":"FAIL",emittingCrests,trackedCrests);
    double rippleVariance=0;
    for(unsigned i=3*n*n;i<4*n*n;++i) {
        float moment=data[bytes/4+i*4+2];
        valid&=std::isfinite(moment) && moment>=0;
        rippleVariance+=moment;
        valid&=data[i*4]==0 && data[i*4+2]==0 && data[i*4+3]==0;
    }
    float rippleSlope=std::sqrt(rippleVariance/(n*n));
    bool rippleValid=rippleSlope>.035f && rippleSlope<.09f;
    valid&=rippleValid;
    LOGF(rippleValid?eINFO:eERROR,"Fine-wave FFT normal energy %s: RMS slope %.6f",rippleValid?"PASS":"FAIL",rippleSlope);
    SeaState whitecaps=original; whitecaps.windSpeed=13; whitecaps.windWaveHeight=1.7f; whitecaps.swellHeight=.6f;
    whitecaps.swellPeriod=6; whitecaps.fetch=12000; whitecaps.depth=30; whitecaps.choppiness=1; whitecaps.current={};
    configureOcean(ocean,whitecaps);
    unsigned active[6]={},peakActive[6]={};
    const float* spray=data+bytes*2/4;
    for(unsigned test=0;test<150;++test) {
        unsigned f=test%2; resetCmdPool(r->renderer,pool); beginCmd(cmd);
        computeOcean(r,cmd,ocean,f,test/30.0f);
        prepareOceanEffects(r,cmd,testCamera,f,whitecaps.level,testShadow,WaterLook{},testBoat,testLayout);
        if(test==149) {
            cmdUpdateBuffer(cmd,readback,0,r->surface[f],0,bytes);
            cmdUpdateBuffer(cmd,readback,bytes,r->normals,0,bytes);
        }
        cmdUpdateBuffer(cmd,readback,bytes*2,r->spray[f],0,4096*3*16);
        endCmd(cmd); flushResourceUpdates(&flush); submit.pSignalFence=fence;
        submit.mWaitSemaphoreCount=flush.pOutSubmittedSemaphore?1:0; submit.ppWaitSemaphores=&flush.pOutSubmittedSemaphore;
        queueSubmit(queue,&submit); waitForFences(r->renderer,1,&fence);
        memset(active,0,sizeof active);
        for(unsigned i=0;i<4096;++i) if(spray[i*12+7]>0) {
            unsigned type=unsigned(spray[i*12+8]);
            valid&=type<TF_ARRAY_COUNT(active) && std::isfinite(spray[i*12+1]);
            if(type<TF_ARRAY_COUNT(active)) ++active[type];
        }
        for(unsigned i=0;i<TF_ARRAY_COUNT(active);++i) peakActive[i]=std::max(peakActive[i],active[i]);
    }
    for(unsigned band=0;band<3;++band) {
        double mean=0; float maximum=0,minimumJ=1; unsigned coverage=0;
        for(unsigned i=band*n*n;i<(band+1)*n*n;++i) {
            float foam=data[i*4+3]; mean+=foam; maximum=std::max(maximum,foam);
            minimumJ=std::min(minimumJ,data[bytes/4+i*4+3]); coverage+=foam>.05f;
            valid&=std::isfinite(foam) && foam>=0 && foam<=1;
        }
        LOGF(eINFO,"Whitecaps band %u: foam mean %.6f, maximum %.6f, coverage >.05 %.2f%%, minimum Jacobian %.6f",
             band,mean/(n*n),maximum,100.0f*coverage/(n*n),minimumJ);
        valid&=mean/(n*n)<.35 && (band!=2 || maximum>.02f);
    }
    bool whitecapParticles=peakActive[0]+peakActive[3]>0 && peakActive[5]>0;
    valid&=whitecapParticles;
    LOGF(whitecapParticles?eINFO:eERROR,"Intermittent whitecap particles %s: peak %u drops, %u rafts, %u sheets, %u mist, %u froth",
         whitecapParticles?"PASS":"FAIL",peakActive[0],peakActive[2],peakActive[3],peakActive[4],peakActive[5]);
    // With births, dissipation and current disabled, a marked surface parcel
    // retains its foam while the wave moves it. Reuse the normal readback area.
    resetCmdPool(r->renderer,pool); beginCmd(cmd);
    cmdUpdateBuffer(cmd,readback,bytes,r->surface[1],0,bytes);
    WaterLook materialTest; materialTest.foamDecay=materialTest.foamSpread=0; materialTest.breakingThreshold=-10;
    computeOcean(r,cmd,ocean,0,5.1f,materialTest);
    cmdUpdateBuffer(cmd,readback,0,r->surface[0],0,bytes);
    endCmd(cmd); flushResourceUpdates(&flush);
    submit.mWaitSemaphoreCount=flush.pOutSubmittedSemaphore?1:0; submit.ppWaitSemaphores=&flush.pOutSubmittedSemaphore;
    queueSubmit(queue,&submit); waitForFences(r->renderer,1,&fence);
    float materialError=0,movingHeight=0;
    for(unsigned i=0;i<3*n*n;++i) {
        materialError=std::max(materialError,std::fabs(data[i*4+3]-data[bytes/4+i*4+3]));
        movingHeight=std::max(movingHeight,std::fabs(data[i*4+1]-data[bytes/4+i*4+1]));
    }
    bool materialValid=materialError<.00001f && movingHeight>.01f;
    valid&=materialValid;
    LOGF(materialValid?eINFO:eERROR,"Material foam transport %s: density error %.7f, moving wave %.3f m",materialValid?"PASS":"FAIL",materialError,movingHeight);
    configureOcean(ocean,calm); testBoat.velocity={0,0,2.6f};
    for(unsigned test=0;test<150;++test) {
        unsigned f=test%2; resetCmdPool(r->renderer,pool); beginCmd(cmd);
        testBoat.position.z=test/30.0f*2.6f;
        advanceWavePackets(ocean,1.0f/30,testBoat.position,false);
        computeOcean(r,cmd,ocean,f,test/30.0f);
        prepareOceanEffects(r,cmd,testCamera,f,calm.level,testShadow,WaterLook{},testBoat,catamaranLayout());
        if(test==149) cmdUpdateBuffer(cmd,readback,bytes*2,r->spray[f],0,4096*3*16);
        endCmd(cmd); flushResourceUpdates(&flush);
        submit.mWaitSemaphoreCount=flush.pOutSubmittedSemaphore?1:0; submit.ppWaitSemaphores=&flush.pOutSubmittedSemaphore;
        queueSubmit(queue,&submit); waitForFences(r->renderer,1,&fence);
    }
    memset(active,0,sizeof active); float flightHeight=0;
    for(unsigned i=0;i<4096;++i) if(spray[i*12+7]>0) {
        unsigned type=unsigned(spray[i*12+8]);
        valid&=type<TF_ARRAY_COUNT(active) && std::isfinite(spray[i*12+1]);
        if(type<TF_ARRAY_COUNT(active)) ++active[type];
        if(type==0 || type==3) flightHeight=std::max(flightHeight,spray[i*12+1]-calm.level);
    }
    bool hullParticles=active[0]>20 && active[2]>20 && active[3]>20 && active[4]>5 && flightHeight>.2f;
    valid&=hullParticles;
    LOGF(hullParticles?eINFO:eERROR,"Powered catamaran particles %s: %u drops, %u foam rafts, %u sheets, %u mist; flight height %.3f m",
         hullParticles?"PASS":"FAIL",active[0],active[2],active[3],active[4],flightHeight);
    // A crest particle can land outside the 64 m wake field. Its motion must
    // still include the global current instead of treating missing history as
    // stagnant water. Seed one such raft in the previous fixed GPU pool.
    float distantFoam[12]={100,0,100,0, 0,0,0,2, WATER_FOAM,.1f,0,.5f};
    TFBufferUpdateDesc particleUpdate={r->spray[1]}; particleUpdate.mSize=sizeof distantFoam;
    beginUpdateResource(&particleUpdate); memcpy(particleUpdate.pMappedData,distantFoam,sizeof distantFoam); endUpdateResource(&particleUpdate);
    resetCmdPool(r->renderer,pool); beginCmd(cmd);
    WaterLook driftLook; driftLook.foamWaveFlow=0;
    computeOcean(r,cmd,ocean,0,5); prepareOceanEffects(r,cmd,testCamera,0,calm.level,testShadow,driftLook,testBoat,catamaranLayout());
    cmdUpdateBuffer(cmd,readback,bytes*2,r->spray[0],0,sizeof distantFoam); endCmd(cmd); flushResourceUpdates(&flush);
    submit.mWaitSemaphoreCount=flush.pOutSubmittedSemaphore?1:0; submit.ppWaitSemaphores=&flush.pOutSubmittedSemaphore;
    queueSubmit(queue,&submit); waitForFences(r->renderer,1,&fence);
    float expectedX=100+(calm.current.x+std::cos(calm.windDirection)*calm.windSpeed*.006f)/30;
    float expectedZ=100+(calm.current.z+std::sin(calm.windDirection)*calm.windSpeed*.006f)/30;
    bool distantValid=std::fabs(spray[0]-expectedX)<.00002f && std::fabs(spray[2]-expectedZ)<.00002f && spray[8]==WATER_FOAM;
    valid&=distantValid;
    LOGF(distantValid?eINFO:eERROR,"Distant foam current advection %s",distantValid?"PASS":"FAIL");
    // A weather edit must remove the previous cloud field immediately, even
    // while paused. Reuse the startup readback; the live cache stays on GPU.
    bool skyValid=true; float cloudyOpacity=0,skyEnergyError=0;
    const auto* skyData=static_cast<const uint16_t*>(readback->pCpuMappedAddress);
    for(unsigned test=0;test<4;++test) {
        WaterLook look; look.overcast=test==1 || test==2?1.0f:0.0f;
        unsigned f=test%2; resetCmdPool(r->renderer,pool); beginCmd(cmd);
        prepareOceanEffects(r,cmd,testCamera,f,calm.level,testShadow,look,testBoat,catamaranLayout());
        endCmd(cmd); flushResourceUpdates(&flush);
        submit.mWaitSemaphoreCount=flush.pOutSubmittedSemaphore?1:0; submit.ppWaitSemaphores=&flush.pOutSubmittedSemaphore;
        queueSubmit(queue,&submit); waitForFences(r->renderer,1,&fence);
        double opacity=0,reference[3]={};
        for(unsigned level=0;level<r->sky->mMipLevels;++level) {
            // Startup only: use Forge's texture readback, including its row alignment.
            TFTextureCopyDesc copy={}; copy.pTexture=r->sky; copy.pBuffer=readback;
            copy.mTextureMipLevel=level; copy.mTextureState=TF_RESOURCE_STATE_SHADER_RESOURCE;
            copy.mQueueType=TF_QUEUE_TYPE_GRAPHICS;
            TFSyncToken token=0; copyResource(&copy,&token); waitForToken(&token);
            const unsigned height=(n*2)>>level, width=height*2;
            const unsigned alignment=std::max(1u,r->renderer->pGpu->mUploadBufferTextureRowAlignment);
            const unsigned stride=((width*8+alignment-1)/alignment)*alignment/2;
            double sum[3]={},weight=0;
            for(unsigned y=0;y<height;++y) {
                const double area=std::sin((y+.5)*3.14159265359/height); weight+=area*width;
                for(unsigned x=0;x<width;++x) for(unsigned c=0;c<4;++c) {
                    const float value=TinyImageFormat_HalfAsUintToFloat(skyData[y*stride+x*4+c]);
                    skyValid&=std::isfinite(value) && value>=0;
                    if(c<3) sum[c]+=value*area;
                    else {
                        if(level==0) opacity+=value;
                        skyValid&=value<=1 && (look.overcast>0 || value==0);
                    }
                }
            }
            // Every mip must preserve the spherical mean, including the poles.
            for(unsigned c=0;c<3;++c) {
                const double mean=sum[c]/weight;
                if(level==0) reference[c]=mean;
                else skyEnergyError=std::max(skyEnergyError,float(std::fabs(mean-reference[c])));
            }
        }
        if(look.overcast>0) { cloudyOpacity=float(opacity/(8*n*n)); skyValid&=cloudyOpacity>.04f; }
    }
    skyValid&=skyEnergyError<.003f; valid&=skyValid;
    LOGF(skyValid?eINFO:eERROR,"Cloud history and paused weather reset %s: cloudy opacity %.3f",skyValid?"PASS":"FAIL",cloudyOpacity);
    LOGF(skyValid?eINFO:eERROR,"Sky reflection filter: spherical radiance error %.6f",skyEnergyError);
    float borderFoam[4]={.6f,0,0,0};
    const uint64_t borderOffset=(128*256+244)*16;
    TFBufferUpdateDesc foamUpdate={r->foam[1]}; foamUpdate.mDstOffset=borderOffset; foamUpdate.mSize=sizeof borderFoam;
    beginUpdateResource(&foamUpdate); memcpy(foamUpdate.pMappedData,borderFoam,sizeof borderFoam); endUpdateResource(&foamUpdate);
    uint16_t borderState[4]={TinyImageFormat_FloatToHalfAsUint(1.2f),TinyImageFormat_FloatToHalfAsUint(.4f),TinyImageFormat_FloatToHalfAsUint(.12f),0};
    TFBufferUpdateDesc stateUpdate={r->localWhitewater[1]}; stateUpdate.mDstOffset=borderOffset/2; stateUpdate.mSize=sizeof borderState;
    beginUpdateResource(&stateUpdate); memcpy(stateUpdate.pMappedData,borderState,sizeof borderState); endUpdateResource(&stateUpdate);
    resetCmdPool(r->renderer,pool); beginCmd(cmd);
    prepareOceanEffects(r,cmd,testCamera,0,calm.level,testShadow,WaterLook{},testBoat,catamaranLayout());
    cmdUpdateBuffer(cmd,readback,0,r->foam[0],borderOffset,16);
    cmdUpdateBuffer(cmd,readback,16,r->foam[0],128*256*16,16);
    cmdUpdateBuffer(cmd,readback,32,r->foam[0],(128*256+255)*16,16);
    cmdUpdateBuffer(cmd,readback,48,r->localWhitewater[0],borderOffset/2,sizeof borderState);
    endCmd(cmd); flushResourceUpdates(&flush);
    submit.mWaitSemaphoreCount=flush.pOutSubmittedSemaphore?1:0; submit.ppWaitSemaphores=&flush.pOutSubmittedSemaphore;
    queueSubmit(queue,&submit); waitForFences(r->renderer,1,&fence);
    bool borderValid=std::fabs(data[0]-.6f)<.000001f;
    borderValid&=memcmp(reinterpret_cast<const uint8_t*>(data)+48,borderState,3*sizeof(uint16_t))==0;
    for(unsigned i=1;i<3;++i) borderValid&=data[i*4+2]==calm.current.x && data[i*4+3]==calm.current.z;
    valid&=borderValid;
    LOGF(borderValid?eINFO:eERROR,"Paused foam density, age, air depth and local-flow boundary %s: retained density %.6f",borderValid?"PASS":"FAIL",data[0]);
    configureOcean(ocean,original);
    exitFence(r->renderer,fence); exitCmd(r->renderer,cmd); exitCmdPool(r->renderer,pool); removeResource(readback); r->bytes-=readbackBytes;
    destroyOcean(ocean); r->revision[0]=r->revision[1]=0; r->lastTime=0; return valid;
}
uint64_t oceanGPUBytes(const OceanRenderer* r) { return r->bytes; }
TFTexture* oceanSkyTexture(const OceanRenderer* r,unsigned& height) { height=r->size*2; return r->sky; }
TFTexture* oceanCloudNoise(const OceanRenderer* r) { return r->cloudNoise; }
}
