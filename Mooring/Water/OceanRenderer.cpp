#include "ShaderLab.h"
#include "OceanRenderer.h"
#include "WaterProfile.h"
#include "../Simulation/Hydrodynamics.h"
#include "../GraphicsMath.h"
#include "Common/Graphics/Interfaces/IGraphics.h"
#include "Common/Resources/ResourceLoader/Interfaces/IResourceLoader.h"
#include "Common/Resources/ResourceLoader/ThirdParty/OpenSource/tinyimageformat/tinyimageformat_decode.h"
#include "Common/Resources/ResourceLoader/ThirdParty/OpenSource/tinyimageformat/tinyimageformat_encode.h"
#include "Common/Graphics/FSL/defaults.h"
#include "../Shaders/Ocean.srt.h"
#include "../Shaders/Water.srt.h"
#include "../Shaders/SkyFilter.srt.h"
#include "Common/Utilities/Interfaces/ILog.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "Common/Utilities/Interfaces/IMemory.h"

// The resource loader uses this backend copy operation as well. Readback is
// confined to the startup verification; simulation never waits for GPU water.
extern "C" void cmdUpdateBuffer(TFCmd*, TFBuffer*, uint64_t, TFBuffer*, uint64_t, uint64_t);
namespace mooring
{
struct OceanPrograms
{
    TFShader*   computeShaders[5];
    TFPipeline* computePipelines[5];
    TFShader*   effectsShader;
    TFPipeline* effectsPipeline;
    TFShader*   skyShader;
    TFPipeline* skyPipeline;
    TFShader*   skyFilterShader;
    TFPipeline* skyFilterPipeline;
    TFShader*   cloudShader;
    TFPipeline* cloudPipeline;
    TFShader*   sprayShader;
    TFPipeline* sprayPipeline;
    TFShader*   drawShader;
    TFPipeline* drawPipeline;
    TFShader*   lightShader;
    TFPipeline* lightPipeline;
};
struct OceanRenderer
{
    OceanPrograms        programs;
    TFRenderer*          renderer;
    SpectrumMode         renderSpectrum[4 * OceanSpectrumSize * OceanSpectrumSize];
    WavePacket           packetUpload[MaxWavePackets + MaxHullWakes];
    unsigned             size, logSize, passes, surfacePass;
    TFBuffer*            spectrum[2];
    TFBuffer*            transform;
    TFBuffer*            surface[2];
    TFBuffer*            normals;
    TFBuffer*            twiddles;
    TFBuffer*            uniform[2][32];
    TFBuffer*            camera[2];
    TFBuffer*            packets[2];
    TFBuffer*            local;
    TFDescriptorSet*     computeSets;
    TFBuffer*            foam[2];
    TFBuffer*            spray[2];
    TFBuffer*            crests[2];
    TFBuffer*            whitewater[2];
    TFBuffer*            localWhitewater[2];
    TFTexture*           sky;
    TFTextureDescriptor* skyMips[11];
    TFDescriptorSet*     skyFilterSets;
    TFBuffer*            skyFilterParameters[11];
    TFTexture*           cloudNoise;
    bool                 cloudReady;
    Vec3                 previousFoamPatch, wind;
    float                effectsTime;
    bool                 effectsValid;
    Vec3                 skyEye;
    float4               skySun;
    float                skyCover;
    unsigned             particleCapacity, gridSize, effectsFrame;
    TFBuffer*            gridIndices[3];
    TFDescriptorSet*     drawSets;
    TFTexture*           foamTexture;
    uint32_t             revision[2];
    float                lastTime, waterDepth;
    Vec3                 patchCenter, current;
    uint64_t             bytes;
};
static TFBuffer* buffer(OceanRenderer* waterRenderer, uint64_t size, uint32_t descriptors, uint32_t stride = 16,
                        TFResourceMemoryUsage memory = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY, const void* data = nullptr)
{
    TFBuffer*        out = nullptr;
    TFBufferLoadDesc load = {};
    load.mDesc.mSize = size;
    load.mDesc.mDescriptors = static_cast<TFDescriptorType>(descriptors);
    load.mDesc.mMemoryUsage = memory;
    load.mDesc.mStructStride = stride;
    load.mDesc.mElementCount = uint32_t(size / stride);
    if (memory != TF_RESOURCE_MEMORY_USAGE_GPU_ONLY)
        load.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
    load.pData = data;
    load.ppBuffer = &out;
    addResource(&load, nullptr);
    waterRenderer->bytes += size;
    return out;
}
static void upload(TFBuffer* buffer, const void* data, size_t size)
{
    MTRACY_ZONE("upload");
    TFBufferUpdateDesc update = { buffer };
    beginUpdateResource(&update);
    memcpy(update.pMappedData, data, size);
    endUpdateResource(&update);
}
static void barrier(TFCmd* cmd, TFBuffer* buffer)
{
    TFBufferBarrier bufferBarrier = { buffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS };
    cmdResourceBarrier(cmd, 1, &bufferBarrier, 0, nullptr, 0, nullptr);
}
// The Metal backend rebuilds argument-buffer residency on each update.
// Rebind the complete set when scene targets change, including cloud storage.
static void bindOceanResources(OceanRenderer* waterRenderer, TFRenderTarget* opaque = nullptr, TFRenderTarget* reflection = nullptr,
                               TFRenderTarget* shadow = nullptr, TFRenderTarget* waterLight = nullptr)
{
    MTRACY_ZONE("bindOceanResources");
    for (unsigned frameIndex = 0; frameIndex < 2; ++frameIndex)
    {
        TFDescriptorData data[27] = {};
        data[0].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gWater);
        data[0].ppBuffers = &waterRenderer->camera[frameIndex];
        data[1].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gWaterSurface);
        data[1].ppBuffers = &waterRenderer->surface[frameIndex];
        data[2].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gWaterNormals);
        data[2].ppBuffers = &waterRenderer->normals;
        data[3].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gLocalSurface);
        data[3].ppBuffers = &waterRenderer->local;
        data[4].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gFoamHistory);
        data[4].ppBuffers = &waterRenderer->foam[1 - frameIndex];
        data[5].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gFoamOutput);
        data[5].ppBuffers = &waterRenderer->foam[frameIndex];
        data[6].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gSprayHistory);
        data[6].ppBuffers = &waterRenderer->spray[1 - frameIndex];
        data[7].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gSprayOutput);
        data[7].ppBuffers = &waterRenderer->spray[frameIndex];
        data[8].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gFoamDraw);
        data[8].ppBuffers = &waterRenderer->foam[frameIndex];
        data[9].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gSprayDraw);
        data[9].ppBuffers = &waterRenderer->spray[frameIndex];
        data[10].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gFoamTexture);
        data[10].ppTextures = &waterRenderer->foamTexture;
        data[11].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gSkyRadiance);
        data[11].ppTextures = &waterRenderer->sky;
        data[12].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gSkyOutput);
        data[12].mUseTextureDescriptors = true;
        data[12].ppTextureDescriptors = &waterRenderer->skyMips[0];
        data[13].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gCloudNoiseTexture);
        data[13].ppTextures = &waterRenderer->cloudNoise;
        data[14].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gCloudNoiseOutput);
        data[14].ppTextures = &waterRenderer->cloudNoise;
        data[15].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gPreviousWaterSurface);
        data[15].ppBuffers = &waterRenderer->surface[1 - frameIndex];
        data[16].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gWhitewater);
        data[16].ppBuffers = &waterRenderer->whitewater[frameIndex];
        data[17].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gLocalWhitewater);
        data[17].ppBuffers = &waterRenderer->localWhitewater[frameIndex];
        data[18].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gLocalWhitewaterHistory);
        data[18].ppBuffers = &waterRenderer->localWhitewater[1 - frameIndex];
        data[19].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gLocalWhitewaterOutput);
        data[19].ppBuffers = &waterRenderer->localWhitewater[frameIndex];
        data[20].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gCrestHistory);
        data[20].ppBuffers = &waterRenderer->crests[1 - frameIndex];
        data[21].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gCrestDraw);
        data[21].ppBuffers = &waterRenderer->crests[frameIndex];
        data[22].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gCrestOutput);
        data[22].ppBuffers = &waterRenderer->crests[frameIndex];
        if (opaque)
        {
            data[23].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gOpaque);
            data[23].ppTextures = &opaque->pTexture;
            data[24].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gReflection);
            data[24].ppTextures = &reflection->pTexture;
            data[25].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gShadow);
            data[25].ppTextures = &shadow->pTexture;
            data[26].mIndex = SRT_RES_IDX(WaterDraw, PerFrame, gWaterLight);
            data[26].ppTextures = &waterLight->pTexture;
        }
        shaderLabUpdateDescriptorSet(waterRenderer->renderer, frameIndex, waterRenderer->drawSets, opaque ? 27 : 23, data);
    }
}
static void loadOceanComputeShaders(TFRenderer* renderer, OceanPrograms& programs)
{
    const char* names[] = { "ocean_spectrum.comp", "ocean_fft.comp", "ocean_surface.comp", "ocean_packets.comp", "ocean_mip.comp" };
    for (unsigned shaderIndex = 0; shaderIndex < 5; ++shaderIndex)
    {
        TFShaderLoadDesc shader = {};
        shader.mComp.pFileName = names[shaderIndex];
        shaderLabAddShader(renderer, &shader, &programs.computeShaders[shaderIndex]);
        TFPipelineDesc pipeline = {};
        pipeline.mType = TF_PIPELINE_TYPE_COMPUTE;
        pipeline.pName = names[shaderIndex];
        pipeline.mComputeDesc.pShaderProgram = programs.computeShaders[shaderIndex];
        PIPELINE_LAYOUT_DESC(pipeline, nullptr, SRT_LAYOUT_DESC(OceanCompute, PerFrame), nullptr, nullptr);
        shaderLabAddPipeline(renderer, &pipeline, &programs.computePipelines[shaderIndex]);
    }
    TFShaderLoadDesc effects = {};
    effects.mComp.pFileName = "water_effects.comp";
    shaderLabAddShader(renderer, &effects, &programs.effectsShader);
    TFPipelineDesc effectPipeline = {};
    effectPipeline.mType = TF_PIPELINE_TYPE_COMPUTE;
    effectPipeline.mComputeDesc.pShaderProgram = programs.effectsShader;
    PIPELINE_LAYOUT_DESC(effectPipeline, nullptr, SRT_LAYOUT_DESC(WaterDraw, PerFrame), nullptr, nullptr);
    effectPipeline.pName = "Whitewater transport and particles";
    shaderLabAddPipeline(renderer, &effectPipeline, &programs.effectsPipeline);
    effects.mComp.pFileName = "water_sky.comp";
    shaderLabAddShader(renderer, &effects, &programs.skyShader);
    effectPipeline.mComputeDesc.pShaderProgram = programs.skyShader;
    effectPipeline.pName = "Sky radiance";
    shaderLabAddPipeline(renderer, &effectPipeline, &programs.skyPipeline);
    effects.mComp.pFileName = "cloud_noise.comp";
    shaderLabAddShader(renderer, &effects, &programs.cloudShader);
    effectPipeline.mComputeDesc.pShaderProgram = programs.cloudShader;
    effectPipeline.pName = "Cloud volume noise";
    shaderLabAddPipeline(renderer, &effectPipeline, &programs.cloudPipeline);
    effects.mComp.pFileName = "water_sky_filter.comp";
    shaderLabAddShader(renderer, &effects, &programs.skyFilterShader);
    effectPipeline.mComputeDesc.pShaderProgram = programs.skyFilterShader;
    PIPELINE_LAYOUT_DESC(effectPipeline, nullptr, SRT_LAYOUT_DESC(SkyFilter, PerFrame), nullptr, nullptr);
    effectPipeline.pName = "Sky reflection mip filter";
    shaderLabAddPipeline(renderer, &effectPipeline, &programs.skyFilterPipeline);
}
OceanRenderer* createOceanRenderer(TFRenderer* renderer, unsigned fftSize)
{
    MTRACY_ZONE("createOceanRenderer");
    ASSERT(fftSize == 256 || fftSize == 512);
    auto* waterRenderer = tf_new(OceanRenderer);
    waterRenderer->renderer = renderer;
    waterRenderer->size = fftSize;
    for (unsigned mipLevel = fftSize; mipLevel > 1; mipLevel /= 2)
        ++waterRenderer->logSize;
    waterRenderer->surfacePass = 3;
    waterRenderer->passes = waterRenderer->logSize + 5;
    Complex twiddles[256];
    for (unsigned twiddleIndex = 0; twiddleIndex < fftSize / 2; ++twiddleIndex)
    {
        float phase = 6.28318530718f * twiddleIndex / fftSize;
        twiddles[twiddleIndex] = { std::cos(phase), std::sin(phase) };
    }
    waterRenderer->twiddles =
        buffer(waterRenderer, fftSize / 2 * sizeof(Complex), TF_DESCRIPTOR_TYPE_BUFFER, 8, TF_RESOURCE_MEMORY_USAGE_GPU_ONLY, twiddles);
    TFTextureDesc sky = {};
    sky.mWidth = fftSize * 4;
    sky.mHeight = fftSize * 2;
    sky.mDepth = sky.mArraySize = 1;
    sky.mMipLevels = waterRenderer->logSize + 2;
    sky.mSampleCount = TF_SAMPLE_COUNT_1;
    sky.mFormat = TinyImageFormat_R16G16B16A16_SFLOAT;
    sky.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE | TF_DESCRIPTOR_TYPE_RW_TEXTURE;
    sky.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
    sky.pName = "Filtered sky radiance";
    TFTextureLoadDesc skyLoad = {};
    skyLoad.pDesc = &sky;
    skyLoad.ppTexture = &waterRenderer->sky;
    addResource(&skyLoad, nullptr);
    waitForAllResourceLoads();
    waterRenderer->bytes += 16ull * (16ull * fftSize * fftSize - 1) / 3;
    for (unsigned level = 0; level < sky.mMipLevels; ++level)
    {
        TFTextureDescriptorDesc mip = {};
        mip.pTexture = waterRenderer->sky;
        mip.mFormat = sky.mFormat;
        mip.mDescriptors = TF_DESCRIPTOR_TYPE_RW_TEXTURE;
        mip.mBaseMipLevel = level;
        mip.mMipLevelCount = 1;
        addTextureDescriptor(renderer, &mip, &waterRenderer->skyMips[level]);
    }
    auto* indices = static_cast<uint32_t*>(tf_malloc(512 * 512 * 6 * sizeof(uint32_t)));
    for (unsigned level = 0; level < 3; ++level)
    {
        unsigned grid = 128u << level, count = 0;
        for (unsigned row = 0; row < grid; ++row)
            for (unsigned column = 0; column < grid; ++column)
            {
                uint32_t topLeft = row * (grid + 1) + column, topRight = topLeft + 1, bottomLeft = topLeft + grid + 2,
                         bottomRight = topLeft + grid + 1;
                for (uint32_t index : { topLeft, topRight, bottomLeft, topLeft, bottomLeft, bottomRight })
                    indices[count++] = index;
            }
        waterRenderer->gridIndices[level] =
            buffer(waterRenderer, count * sizeof(uint32_t), TF_DESCRIPTOR_TYPE_INDEX_BUFFER, 4, TF_RESOURCE_MEMORY_USAGE_GPU_ONLY, indices);
        waitForAllResourceLoads();
    }
    tf_free(indices);
    const uint64_t bytes = 4ull * fftSize * fftSize * 16, mipBytes = 4ull * (4ull * fftSize * fftSize - 1) / 3 * 16;
    // A transform group loads its entire row/column before writing it back.
    // Groups own disjoint cells, so both axes safely reuse this single buffer.
    waterRenderer->transform = buffer(waterRenderer, bytes, TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER);
    // These fields have no temporal history. Compute and draws share one
    // ordered graphics queue, with Forge encoder barriers before each reuse.
    waterRenderer->normals = buffer(waterRenderer, mipBytes, TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER);
    waterRenderer->local =
        buffer(waterRenderer, LocalWaterSize * LocalWaterSize * 16, TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER);
    for (unsigned frameIndex = 0; frameIndex < 2; ++frameIndex)
    {
        waterRenderer->spectrum[frameIndex] =
            buffer(waterRenderer, sizeof waterRenderer->renderSpectrum, TF_DESCRIPTOR_TYPE_BUFFER, 16, TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU);
        waterRenderer->surface[frameIndex] = buffer(waterRenderer, mipBytes, TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER);
        // Only bands 0 and 2 produce whitewater; band 0 already includes band 1's strain.
        waterRenderer->whitewater[frameIndex] =
            buffer(waterRenderer, mipBytes / 4, TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER, 8);
        waterRenderer->localWhitewater[frameIndex] =
            buffer(waterRenderer, 256 * 256 * 8, TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER, 8);
        waterRenderer->foam[frameIndex] = buffer(waterRenderer, 256 * 256 * 16, TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER);
        waterRenderer->spray[frameIndex] = buffer(waterRenderer, 4096 * 3 * 16, TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER);
        waterRenderer->crests[frameIndex] = buffer(waterRenderer, 256 * 16, TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER);
        waterRenderer->camera[frameIndex] =
            buffer(waterRenderer, sizeof(WaterParameters), TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16, TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU);
        waterRenderer->packets[frameIndex] =
            buffer(waterRenderer, sizeof waterRenderer->packetUpload, TF_DESCRIPTOR_TYPE_BUFFER, 16, TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU);
        for (unsigned passIndex = 0; passIndex < waterRenderer->passes; ++passIndex)
            waterRenderer->uniform[frameIndex][passIndex] =
                buffer(waterRenderer, sizeof(OceanParameters), TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16, TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU);
    }
    waitForAllResourceLoads();
    loadOceanComputeShaders(waterRenderer->renderer, waterRenderer->programs);
    TFDescriptorSetDesc set = SRT_SET_DESC(OceanCompute, PerFrame, 2 * waterRenderer->passes, 0);
    addDescriptorSet(renderer, &set, &waterRenderer->computeSets);
    for (unsigned frameIndex = 0; frameIndex < 2; ++frameIndex)
        for (unsigned passIndex = 0; passIndex < waterRenderer->passes; ++passIndex)
        {
            TFBuffer* source = waterRenderer->transform;
            TFBuffer* destination = waterRenderer->transform;
            if (passIndex == waterRenderer->surfacePass)
                destination = waterRenderer->surface[frameIndex];
            if (passIndex == waterRenderer->surfacePass + 1)
                destination = waterRenderer->local;
            if (passIndex > waterRenderer->surfacePass + 1)
            {
                source = waterRenderer->surface[frameIndex];
                destination = waterRenderer->surface[frameIndex];
            }
            TFDescriptorData data[10] = {};
            data[0].mIndex = SRT_RES_IDX(OceanCompute, PerFrame, gOcean);
            data[0].ppBuffers = &waterRenderer->uniform[frameIndex][passIndex];
            data[1].mIndex = SRT_RES_IDX(OceanCompute, PerFrame, gSpectrum);
            data[1].ppBuffers = &waterRenderer->spectrum[frameIndex];
            data[2].mIndex = SRT_RES_IDX(OceanCompute, PerFrame, gSource);
            data[2].ppBuffers = &source;
            data[3].mIndex = SRT_RES_IDX(OceanCompute, PerFrame, gDestination);
            data[3].ppBuffers = &destination;
            data[4].mIndex = SRT_RES_IDX(OceanCompute, PerFrame, gHistory);
            data[4].ppBuffers = &waterRenderer->surface[1 - frameIndex];
            data[5].mIndex = SRT_RES_IDX(OceanCompute, PerFrame, gNormals);
            data[5].ppBuffers = &waterRenderer->normals;
            data[6].mIndex = SRT_RES_IDX(OceanCompute, PerFrame, gTwiddles);
            data[6].ppBuffers = &waterRenderer->twiddles;
            data[7].mIndex = SRT_RES_IDX(OceanCompute, PerFrame, gPackets);
            data[7].ppBuffers = &waterRenderer->packets[frameIndex];
            data[8].mIndex = SRT_RES_IDX(OceanCompute, PerFrame, gWhitewaterHistory);
            data[8].ppBuffers = &waterRenderer->whitewater[1 - frameIndex];
            data[9].mIndex = SRT_RES_IDX(OceanCompute, PerFrame, gWhitewaterOutput);
            data[9].ppBuffers = &waterRenderer->whitewater[frameIndex];
            shaderLabUpdateDescriptorSet(renderer, frameIndex * waterRenderer->passes + passIndex, waterRenderer->computeSets,
                                         TF_ARRAY_COUNT(data), data);
        }
    TFTextureLoadDesc foamTexture = {};
    foamTexture.pFileName = "FoamLace.ktx";
    foamTexture.mContainer = TF_TEXTURE_CONTAINER_KTX;
    foamTexture.ppTexture = &waterRenderer->foamTexture;
    addResource(&foamTexture, nullptr);
    waitForAllResourceLoads();
    TFTextureDesc cloud = {};
    cloud.mWidth = cloud.mHeight = cloud.mDepth = 64;
    cloud.mArraySize = cloud.mMipLevels = 1;
    cloud.mSampleCount = TF_SAMPLE_COUNT_1;
    cloud.mFormat = TinyImageFormat_R8G8_UNORM;
    cloud.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE | TF_DESCRIPTOR_TYPE_RW_TEXTURE;
    cloud.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
    cloud.pName = "Periodic cloud shape and erosion";
    TFTextureLoadDesc cloudLoad = {};
    cloudLoad.pDesc = &cloud;
    cloudLoad.ppTexture = &waterRenderer->cloudNoise;
    addResource(&cloudLoad, nullptr);
    waitForAllResourceLoads();
    waterRenderer->bytes += 64 * 64 * 64 * 2;
    set = SRT_SET_DESC(WaterDraw, PerFrame, 2, 0);
    addDescriptorSet(renderer, &set, &waterRenderer->drawSets);
    bindOceanResources(waterRenderer);
    set = SRT_SET_DESC(SkyFilter, PerFrame, waterRenderer->logSize + 1, 0);
    addDescriptorSet(renderer, &set, &waterRenderer->skyFilterSets);
    for (unsigned level = 1; level <= waterRenderer->logSize + 1; ++level)
    {
        SkyFilterParameters parameters = { { fftSize * 2, level, 0, 0 } };
        waterRenderer->skyFilterParameters[level - 1] =
            buffer(waterRenderer, sizeof parameters, TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16, TF_RESOURCE_MEMORY_USAGE_GPU_ONLY, &parameters);
        waitForAllResourceLoads();
        TFDescriptorData data[3] = {};
        data[0].mIndex = SRT_RES_IDX(SkyFilter, PerFrame, gFilter);
        data[0].ppBuffers = &waterRenderer->skyFilterParameters[level - 1];
        // Both views stay in UAV state while filtering disjoint mip levels.
        data[1].mIndex = SRT_RES_IDX(SkyFilter, PerFrame, gRadiance);
        data[1].mUseTextureDescriptors = true;
        data[1].ppTextureDescriptors = &waterRenderer->skyMips[level - 1];
        data[2].mIndex = SRT_RES_IDX(SkyFilter, PerFrame, gFiltered);
        data[2].mUseTextureDescriptors = true;
        data[2].ppTextureDescriptors = &waterRenderer->skyMips[level];
        shaderLabUpdateDescriptorSet(renderer, level - 1, waterRenderer->skyFilterSets, TF_ARRAY_COUNT(data), data);
    }
    return waterRenderer;
}
void setOceanScene(OceanRenderer* waterRenderer, TFRenderTarget* opaque, TFRenderTarget* reflection, TFRenderTarget* shadow,
                   TFRenderTarget* waterLight)
{
    MTRACY_ZONE("setOceanScene");
    bindOceanResources(waterRenderer, opaque, reflection, shadow, waterLight);
}

static void registerOceanShaderLabResources(OceanRenderer* waterRenderer, unsigned frame)
{
#if defined(MOORING_SHADER_LAB)
    const unsigned        current = frame % 2, previous = 1 - current;
    ShaderLabBufferLayout field{ waterRenderer->size,
                                 waterRenderer->size,
                                 4,
                                 waterRenderer->logSize + 1,
                                 16,
                                 4,
                                 ShaderLabScalar::Float,
                                 "X/Z displacement, Y height, W foam" };
    shaderLabRegisterBuffer(waterRenderer, "Water / Surface (current)", waterRenderer->surface[current], field);
    shaderLabRegisterBuffer(waterRenderer, "Water / Surface (previous)", waterRenderer->surface[previous], field);
    field.channels = "XY slope, Z second moment, W Jacobian";
    shaderLabRegisterBuffer(waterRenderer, "Water / Normals", waterRenderer->normals, field);
    field.bands = 2;
    field.stride = 8;
    field.scalar = ShaderLabScalar::Half;
    field.channels = "Foam age mass, air, air depth mass, active breaking";
    shaderLabRegisterBuffer(waterRenderer, "Whitewater / Current", waterRenderer->whitewater[current], field);
    shaderLabRegisterBuffer(waterRenderer, "Whitewater / Previous", waterRenderer->whitewater[previous], field);
    field.width = field.height = 256;
    field.bands = field.mips = 1;
    shaderLabRegisterBuffer(waterRenderer, "Whitewater / Local current", waterRenderer->localWhitewater[current], field);
    shaderLabRegisterBuffer(waterRenderer, "Whitewater / Local previous", waterRenderer->localWhitewater[previous], field);
    field.stride = 16;
    field.scalar = ShaderLabScalar::Float;
    field.channels = "Density, height, velocity X/Z";
    shaderLabRegisterBuffer(waterRenderer, "Foam / Current", waterRenderer->foam[current], field);
    shaderLabRegisterBuffer(waterRenderer, "Foam / Previous", waterRenderer->foam[previous], field);
    field.width = 3;
    field.height = 4096;
    field.channels = "Columns: position/age, velocity/lifetime, metadata";
    shaderLabRegisterBuffer(waterRenderer, "Particles / Current", waterRenderer->spray[current], field);
    shaderLabRegisterBuffer(waterRenderer, "Particles / Previous", waterRenderer->spray[previous], field);
    field.width = 16;
    field.height = 16;
    field.channels = "Position X/Z, emission, age";
    shaderLabRegisterBuffer(waterRenderer, "Crests / Current", waterRenderer->crests[current], field);
    shaderLabRegisterBuffer(waterRenderer, "Crests / Previous", waterRenderer->crests[previous], field);
    field.width = LocalWaterSize;
    field.height = LocalWaterSize;
    field.channels = "Local surface float4";
    shaderLabRegisterBuffer(waterRenderer, "Water / Local surface", waterRenderer->local, field);
    auto linear = [&](const char* name, TFBuffer* buffer, unsigned stride = 16, unsigned components = 4,
                      ShaderLabScalar scalar = ShaderLabScalar::Float)
    {
        ShaderLabBufferLayout layout{ unsigned(buffer->mSize / stride), 1, 1, 1, stride, components, scalar, "Raw components" };
        shaderLabRegisterBuffer(waterRenderer, name, buffer, layout);
    };
    linear("Water / Parameters", waterRenderer->camera[current]);
    linear("Ocean / Spectrum", waterRenderer->spectrum[current]);
    linear("Ocean / FFT transform", waterRenderer->transform);
    linear("Ocean / Twiddles", waterRenderer->twiddles, 8, 2);
    linear("Ocean / Wave packets", waterRenderer->packets[current]);
    for (unsigned pass = 0; pass < waterRenderer->passes; ++pass)
    {
        char name[64];
        snprintf(name, sizeof name, "Ocean / Constants pass %u", pass);
        linear(name, waterRenderer->uniform[current][pass]);
    }
    for (unsigned grid = 0; grid < 3; ++grid)
    {
        char name[64];
        snprintf(name, sizeof name, "Water / Grid indices %u", grid);
        linear(name, waterRenderer->gridIndices[grid], 4, 1, ShaderLabScalar::Uint);
    }
    shaderLabRegisterTexture(waterRenderer, "Sky / Radiance", waterRenderer->sky);
    shaderLabRegisterTexture(waterRenderer, "Clouds / Volume noise", waterRenderer->cloudNoise);
    shaderLabRegisterTexture(waterRenderer, "Foam / Lace texture", waterRenderer->foamTexture);
#endif
}

void bindOceanShaderLabResources(OceanRenderer* waterRenderer, TFCmd* command, unsigned frame)
{
    ::cmdBindDescriptorSet(command, frame % 2, waterRenderer->drawSets);
}

static void loadOceanDrawShaders(TFRenderer* renderer, OceanPrograms& programs, uint32_t format)
{
    MTRACY_ZONE("loadOceanRenderer");
    TFShaderLoadDesc shader = {};
    shader.mVert.pFileName = "water.vert";
    shader.mFrag.pFileName = "water.frag";
    shaderLabAddShader(renderer, &shader, &programs.drawShader);
    TFPipelineDesc pipelineDescription = {};
    pipelineDescription.mType = TF_PIPELINE_TYPE_GRAPHICS;
    PIPELINE_LAYOUT_DESC(pipelineDescription, nullptr, SRT_LAYOUT_DESC(WaterDraw, PerFrame), nullptr, nullptr);
    TFDepthStateDesc depth = {};
    depth.mDepthTest = depth.mDepthWrite = true;
    depth.mDepthFunc = TF_CMP_LEQUAL;
    TFRasterizerStateDesc raster = {};
    raster.mCullMode = TF_CULL_MODE_NONE;
    TinyImageFormat color = static_cast<TinyImageFormat>(format);
    auto&           graphics = pipelineDescription.mGraphicsDesc;
    graphics.pShaderProgram = programs.drawShader;
    graphics.pDepthState = &depth;
    graphics.pRasterizerState = &raster;
    graphics.pColorFormats = &color;
    graphics.mRenderTargetCount = 1;
    graphics.mDepthStencilFormat = TinyImageFormat_D32_SFLOAT;
    graphics.mSampleCount = TF_SAMPLE_COUNT_1;
    graphics.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
    pipelineDescription.pName = "Ocean surface";
    shaderLabAddPipeline(renderer, &pipelineDescription, &programs.drawPipeline);
    shader.mVert.pFileName = "water_light.vert";
    shader.mFrag.pFileName = "water_light.frag";
    shaderLabAddShader(renderer, &shader, &programs.lightShader);
    graphics.pShaderProgram = programs.lightShader;
    color = TinyImageFormat_R32_SFLOAT;
    pipelineDescription.pName = "Water light depth";
    shaderLabAddPipeline(renderer, &pipelineDescription, &programs.lightPipeline);
    color = static_cast<TinyImageFormat>(format);
    shader.mVert.pFileName = "water_spray.vert";
    shader.mFrag.pFileName = "water_spray.frag";
    shaderLabAddShader(renderer, &shader, &programs.sprayShader);
    graphics.pShaderProgram = programs.sprayShader;
    depth.mDepthWrite = false;
    TFBlendStateDesc blend = {};
    blend.mSrcFactors[0] = blend.mSrcAlphaFactors[0] = TF_BC_ONE;
    blend.mDstFactors[0] = blend.mDstAlphaFactors[0] = TF_BC_ONE_MINUS_SRC_ALPHA;
    // The HDR target's alpha stores view depth for atmospheric integration.
    blend.mColorWriteMasks[0] = TF_COLOR_MASK_RED | TF_COLOR_MASK_GREEN | TF_COLOR_MASK_BLUE;
    blend.mRenderTargetMask = TF_BLEND_STATE_TARGET_0;
    graphics.pBlendState = &blend;
    pipelineDescription.pName = "Foam and spray";
    shaderLabAddPipeline(renderer, &pipelineDescription, &programs.sprayPipeline);
}
static void unloadOceanDrawShaders(TFRenderer* renderer, const OceanPrograms& programs)
{
    MTRACY_ZONE("unloadOceanRenderer");
    shaderLabRemovePipeline(renderer, programs.drawPipeline);
    shaderLabRemoveShader(renderer, programs.drawShader);
    shaderLabRemovePipeline(renderer, programs.sprayPipeline);
    shaderLabRemoveShader(renderer, programs.sprayShader);
    shaderLabRemovePipeline(renderer, programs.lightPipeline);
    shaderLabRemoveShader(renderer, programs.lightShader);
}
static void unloadOceanComputeShaders(TFRenderer* renderer, const OceanPrograms& programs)
{
    shaderLabRemovePipeline(renderer, programs.effectsPipeline);
    shaderLabRemoveShader(renderer, programs.effectsShader);
    shaderLabRemovePipeline(renderer, programs.skyPipeline);
    shaderLabRemoveShader(renderer, programs.skyShader);
    shaderLabRemovePipeline(renderer, programs.cloudPipeline);
    shaderLabRemoveShader(renderer, programs.cloudShader);
    shaderLabRemovePipeline(renderer, programs.skyFilterPipeline);
    shaderLabRemoveShader(renderer, programs.skyFilterShader);
    for (unsigned index = 0; index < 5; ++index)
    {
        shaderLabRemovePipeline(renderer, programs.computePipelines[index]);
        shaderLabRemoveShader(renderer, programs.computeShaders[index]);
    }
}
void loadOceanRenderer(OceanRenderer* waterRenderer, uint32_t format)
{
    loadOceanDrawShaders(waterRenderer->renderer, waterRenderer->programs, format);
}
void unloadOceanRenderer(OceanRenderer* waterRenderer) { unloadOceanDrawShaders(waterRenderer->renderer, waterRenderer->programs); }
bool reloadOceanShaders(OceanRenderer* waterRenderer, uint32_t format)
{
    OceanPrograms pending{};
    loadOceanComputeShaders(waterRenderer->renderer, pending);
    loadOceanDrawShaders(waterRenderer->renderer, pending, format);
    bool valid = pending.effectsPipeline && pending.skyPipeline && pending.cloudPipeline && pending.skyFilterPipeline &&
                 pending.drawPipeline && pending.lightPipeline && pending.sprayPipeline;
    for (auto* pipeline : pending.computePipelines)
        valid &= pipeline != nullptr;
    if (valid)
    {
        std::swap(waterRenderer->programs, pending);
        waterRenderer->cloudReady = false;
        waterRenderer->skyCover = -1;
    }
    unloadOceanDrawShaders(waterRenderer->renderer, pending);
    unloadOceanComputeShaders(waterRenderer->renderer, pending);
    return valid;
}

void destroyOceanRenderer(OceanRenderer* waterRenderer)
{
    MTRACY_ZONE("destroyOceanRenderer");
    shaderLabForgetResources(waterRenderer);
    shaderLabRemoveDescriptorSet(waterRenderer->renderer, waterRenderer->skyFilterSets);
    for (unsigned level = 0; level <= waterRenderer->logSize; ++level)
        removeResource(waterRenderer->skyFilterParameters[level]);
    for (unsigned level = 0; level <= waterRenderer->logSize + 1; ++level)
        removeTextureDescriptor(waterRenderer->renderer, waterRenderer->skyMips[level]);
    removeResource(waterRenderer->cloudNoise);
    removeResource(waterRenderer->sky);
    removeResource(waterRenderer->foamTexture);
    for (auto* indices : waterRenderer->gridIndices)
        removeResource(indices);
    shaderLabRemoveDescriptorSet(waterRenderer->renderer, waterRenderer->drawSets);
    shaderLabRemoveDescriptorSet(waterRenderer->renderer, waterRenderer->computeSets);
    unloadOceanComputeShaders(waterRenderer->renderer, waterRenderer->programs);
    for (unsigned frameIndex = 0; frameIndex < 2; ++frameIndex)
    {
        for (unsigned passIndex = 0; passIndex < waterRenderer->passes; ++passIndex)
            removeResource(waterRenderer->uniform[frameIndex][passIndex]);
        removeResource(waterRenderer->spectrum[frameIndex]);
        removeResource(waterRenderer->surface[frameIndex]);
        removeResource(waterRenderer->camera[frameIndex]);
        removeResource(waterRenderer->foam[frameIndex]);
        removeResource(waterRenderer->spray[frameIndex]);
        removeResource(waterRenderer->crests[frameIndex]);
        removeResource(waterRenderer->whitewater[frameIndex]);
        removeResource(waterRenderer->localWhitewater[frameIndex]);
        removeResource(waterRenderer->packets[frameIndex]);
    }
    removeResource(waterRenderer->transform);
    removeResource(waterRenderer->normals);
    removeResource(waterRenderer->local);
    removeResource(waterRenderer->twiddles);
    tf_delete(waterRenderer);
}
void computeOcean(OceanRenderer* waterRenderer, TFCmd* cmd, const Ocean* ocean, unsigned frameIndex, float time, const WaterLook& look)
{
    registerOceanShaderLabResources(waterRenderer, frameIndex);
    MTRACY_ZONE("computeOcean");
    const auto&    sea = seaState(ocean);
    const unsigned fftSize = waterRenderer->size;
    bool           changed = waterRenderer->revision[frameIndex] != oceanRevision(ocean) || time < waterRenderer->lastTime;
    if (changed)
    {
        memcpy(waterRenderer->renderSpectrum, oceanSpectrum(ocean), OceanModeCount * sizeof(SpectrumMode));
        buildRippleSpectrum(sea, waterRenderer->renderSpectrum + OceanModeCount);
        upload(waterRenderer->spectrum[frameIndex], waterRenderer->renderSpectrum, sizeof waterRenderer->renderSpectrum);
        waterRenderer->revision[frameIndex] = oceanRevision(ocean);
    }
    unsigned active = 0;
    for (unsigned packetIndex = 0; packetIndex < MaxWavePackets; ++packetIndex)
        if (oceanPackets(ocean)[packetIndex].lifetime > 0)
            waterRenderer->packetUpload[active++] = oceanPackets(ocean)[packetIndex];
    static_assert(sizeof(HullWake) == sizeof(WavePacket));
    memcpy(waterRenderer->packetUpload + MaxWavePackets, oceanHullWakes(ocean), MaxHullWakes * sizeof(HullWake));
    upload(waterRenderer->packets[frameIndex], waterRenderer->packetUpload, sizeof waterRenderer->packetUpload);
    waterRenderer->patchCenter = oceanPatchCenter(ocean);
    waterRenderer->waterDepth = sea.depth;
    waterRenderer->current = sea.current;
    waterRenderer->wind = { sea.windSpeed * std::cos(sea.windDirection), 0, sea.windSpeed * std::sin(sea.windDirection) };
    if (changed)
        waterRenderer->effectsValid = false;
    OceanParameters params = { { time, std::clamp(time - waterRenderer->lastTime, 0.0f, .2f), sea.depth, sea.choppiness },
                               { fftSize, 0, 0, MaxWavePackets },
                               { 256, 64, 16, 2 },
                               { sea.current.x, 0, sea.current.z, changed ? 0.0f : 1.0f },
                               { waterRenderer->patchCenter.x, waterRenderer->patchCenter.z, float(LocalWaterSize), float(active) } };
    params.foamSettings[0] = look.foamDecay;
    params.foamSettings[1] = look.foamSpread;
    params.foamSettings[2] = look.breakingThreshold;
    for (unsigned band = 0; band < 3; ++band)
        params.breaking[band] = std::min(.98f, oceanMetrics(ocean).breakingThreshold[fftSize == 512][band] + look.breakingThreshold - .55f);
    waterRenderer->lastTime = time;
    for (unsigned passIndex = 0; passIndex < waterRenderer->passes; ++passIndex)
    {
        unsigned pipeline = passIndex == 0                                ? 0
                            : passIndex == waterRenderer->surfacePass     ? 2
                            : passIndex == waterRenderer->surfacePass + 1 ? 3
                            : passIndex > waterRenderer->surfacePass + 1  ? 4
                                                                          : 1;
        if (pipeline == 1)
            params.transform[2] = passIndex - 1;
        if (pipeline == 4)
            params.transform[1] = passIndex - waterRenderer->surfacePass - 1;
        upload(waterRenderer->uniform[frameIndex][passIndex], &params, sizeof params);
        shaderLabBindPipeline(cmd, waterRenderer->programs.computePipelines[pipeline]);
        shaderLabBindDescriptorSet(cmd, frameIndex * waterRenderer->passes + passIndex, waterRenderer->computeSets);
        if (pipeline == 3)
            cmdDispatch(cmd, LocalWaterSize / 8, LocalWaterSize / 8, 1);
        else if (pipeline == 4)
        {
            unsigned mipLevel = fftSize >> params.transform[1];
            cmdDispatch(cmd, (mipLevel + 7) / 8, (mipLevel + 7) / 8, 4);
        }
        else if (pipeline == 1)
            cmdDispatch(cmd, 1, fftSize, 4);
        else
            cmdDispatch(cmd, fftSize / 8, fftSize / 8, 4);
        barrier(cmd, pipeline == 2 || pipeline == 4 ? waterRenderer->surface[frameIndex]
                     : pipeline == 3                ? waterRenderer->local
                                                    : waterRenderer->transform);
        if (pipeline == 2 || pipeline == 4)
        {
            barrier(cmd, waterRenderer->normals);
            barrier(cmd, waterRenderer->whitewater[frameIndex]);
        }
    }
}
void prepareOceanEffects(OceanRenderer* waterRenderer, TFCmd* cmd, const Camera& camera, unsigned frame, float level,
                         const f4x4& shadowMatrix, const WaterLook& look, const Snapshot& boat, const VesselLayout& layout,
                         float4 reflectionBounds, const WaterProfile* profile, float4 shadowBounds)
{
    MTRACY_ZONE("prepareOceanEffects");
    WaterParameters params = {};
    params.viewProjection = cameraMatrix(camera);
    params.shadowMatrix = shadowMatrix;
    auto eye = cameraEye(camera);
    params.eye = float4(toForge(eye));
    params.surface[0] = float(waterRenderer->size);
    params.surface[1] = float(look.gridSize);
    params.surface[2] = .25f;
    params.surface[3] = level;
    params.origin = float4(toForge(camera.focus));
    params.patch[0] = waterRenderer->patchCenter.x;
    params.patch[1] = float(LocalWaterSize);
    params.patch[2] = waterRenderer->patchCenter.z;
    params.screen[0] = float(camera.width);
    params.screen[1] = float(camera.height);
    params.shaderPreview = shaderLabProbeDisplay();
    params.screen[2] = waterRenderer->lastTime;
    params.screen[3] = waterRenderer->waterDepth;
    params.current = float4(toForge(waterRenderer->current));
    look.parameters(params.weather, params.effects);
    look.sunlight(params.sun);
    look.fidelity(params.detail, params.quality, params.optics, params.foamSettings);
    params.history[0] = std::clamp(waterRenderer->lastTime - waterRenderer->effectsTime, 0.0f, .1f);
    params.history[1] = waterRenderer->effectsValid ? 1.0f : 0.0f;
    if (!waterRenderer->effectsValid)
        waterRenderer->effectsFrame = 0;
    ++waterRenderer->effectsFrame;
    params.history[2] = float(waterRenderer->effectsFrame);
    float eyeShiftSquared = f3LengthSqr(f3Sub(toForge(eye), toForge(waterRenderer->skyEye)));
    bool  skyReset = !waterRenderer->effectsValid || waterRenderer->skyCover != look.overcast ||
                    memcmp(&waterRenderer->skySun, &params.sun, sizeof waterRenderer->skySun) != 0 || eyeShiftSquared > 4;
    params.history[3] = skyReset ? 0.0f : .9f;
    waterRenderer->skyEye = eye;
    waterRenderer->skyCover = look.overcast;
    waterRenderer->skySun = params.sun;
    params.boatPosition = float4(toForge(boat.position), layout.beam);
    auto forward = deckToWorld(boat, { 0, 0, 1 });
    params.boatForward[0] = forward.x - boat.position.x;
    params.boatForward[2] = forward.z - boat.position.z;
    float magnitude = std::hypot(params.boatForward[0], params.boatForward[2]);
    if (magnitude > .001f)
    {
        params.boatForward[0] /= magnitude;
        params.boatForward[2] /= magnitude;
    }
    params.boatForward[3] = layout.length;
    params.hulls[0] = layout.hullSpacing;
    params.hulls[1] = layout.beam - layout.hullSpacing;
    params.hulls[2] = float(layout.propellerCount);
    for (unsigned propellerIndex = 0; propellerIndex < layout.propellerCount; ++propellerIndex)
    {
        const auto& propeller = layout.propellers[propellerIndex];
        auto        position = deckToWorld(boat, propeller.position);
        params.propellers[propellerIndex] = float4(toForge(position), boat.rpm[propellerIndex]);
    }
    params.boatVelocity = float4(toForge(boat.velocity));
    params.wind = float4(toForge(waterRenderer->wind), f3Length(toForge(waterRenderer->wind)));
    Vec3 foamCenter = { std::floor(waterRenderer->patchCenter.x * 4) * .25f, 0, std::floor(waterRenderer->patchCenter.z * 4) * .25f };
    params.foamPatch[0] = foamCenter.x;
    params.foamPatch[1] = foamCenter.z;
    params.foamPatch[2] = waterRenderer->previousFoamPatch.x;
    params.foamPatch[3] = waterRenderer->previousFoamPatch.z;
    params.trace[0] = float(look.refractionSteps);
    params.trace[1] = look.debugGain;
    params.trace[2] = float(look.reflectionSteps);
    params.trace[3] = float(look.reflectionDivisor);
    params.seabed = { look.seabedColor[0], look.seabedColor[1], look.seabedColor[2], 0 };
    params.foamAppearance[0] = look.foamRelief;
    params.foamAppearance[1] = look.foamBreakup;
    params.foamAppearance[2] = look.foamWaveFlow;
    params.foamAppearance[3] = look.foamClumpHeight;
    params.lightProjection = lightProjection({ camera.focus.x, level, camera.focus.z }, params.sun.getXYZ(), look.waterLightSize, 256, 512);
    params.volume = { 512, float(look.scatteringSamples), look.bubbleScattering, look.volumeContrast };
    params.reflectionBounds = reflectionBounds;
    params.shadowBounds = shadowBounds;
    upload(waterRenderer->camera[frame], &params, sizeof params);
    beginWaterPass(cmd, profile, WaterEffects);
    if (!waterRenderer->cloudReady)
    {
        shaderLabBindPipeline(cmd, waterRenderer->programs.cloudPipeline);
        shaderLabBindDescriptorSet(cmd, frame, waterRenderer->drawSets);
        cmdDispatch(cmd, 16, 16, 16);
        TFTextureBarrier cloud = { waterRenderer->cloudNoise, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_SHADER_RESOURCE };
        cmdResourceBarrier(cmd, 0, nullptr, 1, &cloud, 0, nullptr);
        waterRenderer->cloudReady = true;
    }
    shaderLabBindPipeline(cmd, waterRenderer->programs.effectsPipeline);
    shaderLabBindDescriptorSet(cmd, frame, waterRenderer->drawSets);
    cmdDispatch(cmd, 32, 32, 1);
    barrier(cmd, waterRenderer->foam[frame]);
    barrier(cmd, waterRenderer->spray[frame]);
    barrier(cmd, waterRenderer->crests[frame]);
    barrier(cmd, waterRenderer->localWhitewater[frame]);
    endWaterPass(cmd, profile, WaterEffects);
    beginWaterPass(cmd, profile, WaterSky);
    TFTextureBarrier skyBarrier = { waterRenderer->sky, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_UNORDERED_ACCESS };
    cmdResourceBarrier(cmd, 0, nullptr, 1, &skyBarrier, 0, nullptr);
    shaderLabBindPipeline(cmd, waterRenderer->programs.skyPipeline);
    shaderLabBindDescriptorSet(cmd, frame, waterRenderer->drawSets);
    cmdDispatch(cmd, waterRenderer->size / 2, waterRenderer->size / 4, 1);
    skyBarrier.mCurrentState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
    cmdResourceBarrier(cmd, 0, nullptr, 1, &skyBarrier, 0, nullptr);
    if (params.history[0] > 0 || skyReset)
    {
        shaderLabBindPipeline(cmd, waterRenderer->programs.skyFilterPipeline);
        for (unsigned mipLevel = 1; mipLevel <= waterRenderer->logSize + 1; ++mipLevel)
        {
            unsigned height = (waterRenderer->size * 2) >> mipLevel;
            shaderLabBindDescriptorSet(cmd, mipLevel - 1, waterRenderer->skyFilterSets);
            cmdDispatch(cmd, (height * 2 + 7) / 8, (height + 7) / 8, 1);
            cmdResourceBarrier(cmd, 0, nullptr, 1, &skyBarrier, 0, nullptr);
        }
    }
    skyBarrier.mNewState = TF_RESOURCE_STATE_SHADER_RESOURCE;
    cmdResourceBarrier(cmd, 0, nullptr, 1, &skyBarrier, 0, nullptr);
    endWaterPass(cmd, profile, WaterSky);
    waterRenderer->previousFoamPatch = foamCenter;
    waterRenderer->effectsTime = waterRenderer->lastTime;
    waterRenderer->effectsValid = true;
    waterRenderer->particleCapacity = unsigned(look.particles);
    waterRenderer->gridSize = unsigned(look.gridSize);
}
void drawOcean(OceanRenderer* waterRenderer, TFCmd* cmd, unsigned frame)
{
    MTRACY_ZONE("drawOcean");
    shaderLabBindPipeline(cmd, waterRenderer->programs.drawPipeline);
    shaderLabBindDescriptorSet(cmd, frame, waterRenderer->drawSets);
    cmdBindIndexBuffer(cmd,
                       waterRenderer->gridIndices[waterRenderer->gridSize == 128   ? 0
                                                  : waterRenderer->gridSize == 256 ? 1
                                                                                   : 2],
                       TF_INDEX_TYPE_UINT32, 0);
    cmdDrawIndexed(cmd, waterRenderer->gridSize * waterRenderer->gridSize * 6, 0, 0);
    if (waterRenderer->particleCapacity)
    {
        shaderLabBindPipeline(cmd, waterRenderer->programs.sprayPipeline);
        cmdDraw(cmd, waterRenderer->particleCapacity * WATER_PARTICLE_VERTICES, 0);
    }
}
void drawOceanLight(OceanRenderer* waterRenderer, TFCmd* cmd, unsigned frame)
{
    MTRACY_ZONE("drawOceanLight");
    shaderLabBindPipeline(cmd, waterRenderer->programs.lightPipeline);
    shaderLabBindDescriptorSet(cmd, frame, waterRenderer->drawSets);
    cmdBindIndexBuffer(cmd,
                       waterRenderer->gridIndices[waterRenderer->gridSize == 128   ? 0
                                                  : waterRenderer->gridSize == 256 ? 1
                                                                                   : 2],
                       TF_INDEX_TYPE_UINT32, 0);
    cmdDrawIndexed(cmd, waterRenderer->gridSize * waterRenderer->gridSize * 6, 0, 0);
}
bool verifyOceanGPU(OceanRenderer* waterRenderer, TFQueue* queue)
{
    MTRACY_ZONE("verifyOceanGPU");
    // Validation must not clear the live world's wake history on a graphics reload.
    Ocean*         ocean = createOcean(SeaState{});
    const unsigned fftSize = waterRenderer->size;
    const uint64_t bytes = 4ull * (4ull * fftSize * fftSize - 1) / 3 * 16, localBytes = LocalWaterSize * LocalWaterSize * 16;
    const uint64_t stateOffset = bytes * 2 + localBytes, readbackBytes = stateOffset + bytes / 4;
    auto*          readback = buffer(waterRenderer, readbackBytes, TF_DESCRIPTOR_TYPE_RW_BUFFER, 16, TF_RESOURCE_MEMORY_USAGE_GPU_TO_CPU);
    waitForAllResourceLoads();
    TFCmdPool*    pool = nullptr;
    TFCmdPoolDesc pd = {};
    pd.pQueue = queue;
    initCmdPool(waterRenderer->renderer, &pd, &pool);
    TFCmd*    cmd = nullptr;
    TFCmdDesc cd = {};
    cd.pPool = pool;
    initCmd(waterRenderer->renderer, &cd, &cmd);
    TFFence* fence = nullptr;
    initFence(waterRenderer->renderer, &fence);
    emitWavePacket(ocean, { 8, 0, 0 }, { 1, 0, 0 }, 3, 200, true);
    emitWavePacket(ocean, { 26, 0, 27 }, { 1, 0, 0 }, 12, 1000);
    HullWake testWake{ { -5, 0, 1 }, .25f, { .6f, 0, .8f }, 12, { 1.8f, 0, 2.4f }, 1.5f, 5, 7, {} };
    setHullWake(ocean, 0, testWake);
    advanceWavePackets(ocean, 1, { 0, 0, 0 }, true);
    updateOcean(ocean, .75);
    beginCmd(cmd);
    computeOcean(waterRenderer, cmd, ocean, 0, .75f);
    cmdUpdateBuffer(cmd, readback, 0, waterRenderer->surface[0], 0, bytes);
    cmdUpdateBuffer(cmd, readback, bytes, waterRenderer->normals, 0, bytes);
    cmdUpdateBuffer(cmd, readback, bytes * 2, waterRenderer->local, 0, localBytes);
    endCmd(cmd);
    FlushResourceUpdateDesc flush = {};
    flushResourceUpdates(&flush);
    TFQueueSubmitDesc submit = {};
    submit.mCmdCount = 1;
    submit.ppCmds = &cmd;
    submit.pSignalFence = fence;
    submit.mWaitSemaphoreCount = flush.pOutSubmittedSemaphore ? 1 : 0;
    submit.ppWaitSemaphores = &flush.pOutSubmittedSemaphore;
    queueSubmit(queue, &submit);
    waitForFences(waterRenderer->renderer, 1, &fence);
    const auto* data = static_cast<const float*>(readback->pCpuMappedAddress);
    float       maxError = 0;
    bool        valid = true;
    for (unsigned band = 0; band < 3; ++band)
        for (unsigned row = 0; row < 128; row += 7)
            for (unsigned column = 0; column < 128; column += 11)
            {
                auto     cpu = sampleOceanBand(ocean, band, column * OceanLengths[band] / 128, row * OceanLengths[band] / 128);
                unsigned sampleIndex = band * fftSize * fftSize + (row * fftSize / 128) * fftSize + column * fftSize / 128;
                float    error = std::max(std::fabs(data[sampleIndex * 4 + 1] - cpu.height),
                                          std::max(std::fabs(data[sampleIndex * 4] - cpu.displacement.x),
                                                   std::fabs(data[sampleIndex * 4 + 2] - cpu.displacement.z)));
                bool     finite = std::isfinite(data[sampleIndex * 4]) && std::isfinite(data[sampleIndex * 4 + 1]) &&
                              std::isfinite(data[sampleIndex * 4 + 2]) && std::isfinite(data[sampleIndex * 4 + 3]);
                valid &= finite && std::isfinite(error) && error < .0005f;
                maxError = std::max(maxError, error);
                if (!finite)
                    LOGF(eERROR, "Non-finite ocean displacement in band %u at %u,%u", band, column, row);
            }
    LOGF(valid ? eINFO : eERROR, "Ocean FFT %ux%u x 3: GPU/CPU height and displacement %s; max error %.7f m", fftSize, fftSize,
         valid ? "PASS" : "FAIL", maxError);
    float mipError = 0;
    bool  mipValid = true;
    for (unsigned channel = 0; channel < 2; ++channel)
    {
        const float* values = data + channel * bytes / 4;
        for (unsigned mipLevel = fftSize / 2; mipLevel >= 1; mipLevel /= 2)
            for (unsigned band = 0; band < 4; ++band)
            {
                unsigned parent = mipLevel * 2, source = 16 * ((fftSize * fftSize - parent * parent) / 3) + band * parent * parent;
                unsigned target = 16 * ((fftSize * fftSize - mipLevel * mipLevel) / 3) + band * mipLevel * mipLevel;
                for (unsigned row = 0; row < mipLevel; row += std::max(1u, mipLevel / 7))
                    for (unsigned column = 0; column < mipLevel; column += std::max(1u, mipLevel / 7))
                    {
                        unsigned dest = (target + row * mipLevel + column) * 4;
                        for (unsigned channelIndex = 0; channelIndex < 4; ++channelIndex)
                        {
                            float expected = 0;
                            for (unsigned rowOffset = 0; rowOffset < 2; ++rowOffset)
                                for (unsigned columnOffset = 0; columnOffset < 2; ++columnOffset)
                                    expected +=
                                        values[(source + (row * 2 + rowOffset) * parent + column * 2 + columnOffset) * 4 + channelIndex] *
                                        .25f;
                            float error = std::fabs(values[dest + channelIndex] - expected);
                            mipValid &= std::isfinite(values[dest + channelIndex]) && std::isfinite(error) && error < .00002f;
                            mipError = std::max(mipError, error);
                        }
                        if (channel == 1)
                            mipValid &= values[dest + 2] + .00002f >= values[dest] * values[dest] + values[dest + 1] * values[dest + 1];
                    }
            }
    }
    valid &= mipValid;
    LOGF(mipValid ? eINFO : eERROR, "Ocean displacement and slope-moment mip chain %s; max error %.7f", mipValid ? "PASS" : "FAIL",
         mipError);
    float       localError = 0;
    const auto* patch = data + bytes * 2 / 4;
    for (unsigned row = 0; row < LocalWaterSize; row += 3)
        for (unsigned column = 0; column < LocalWaterSize; column += 3)
        {
            auto     cpu = sampleWavePackets(ocean, float(column) * 64 / LocalWaterSize - 32, float(row) * 64 / LocalWaterSize - 32);
            unsigned sampleIndex = (row * LocalWaterSize + column) * 4;
            valid &= std::isfinite(patch[sampleIndex]) && std::isfinite(patch[sampleIndex + 1]) && std::isfinite(patch[sampleIndex + 2]);
            localError = std::max(localError, std::fabs(cpu.height - patch[sampleIndex]));
            localError = std::max(localError, std::fabs(cpu.dx - patch[sampleIndex + 1]));
            localError = std::max(localError, std::fabs(cpu.dz - patch[sampleIndex + 2]));
        }
    valid &= localError < .00005f;
    LOGF(valid ? eINFO : eERROR, "Wave packets: GPU/CPU height and slope %s; max error %.7f", valid ? "PASS" : "FAIL", localError);
    // Run the complete compute chain, including effects, on both frame buffers.
    // Effects must never overwrite the spectral surface or its normal moments.
    SeaState original = seaState(ocean), calm = original;
    calm.windWaveHeight = .03f;
    calm.swellHeight = .04f;
    calm.windSpeed = 1;
    calm.fetch = 3000;
    configureOcean(ocean, calm);
    float        maximumCalmFoam = 0, minimumJacobian = 1;
    bool         crestValid = true;
    unsigned     emittingCrests = 0, trackedCrests = 0;
    const float* crests = data + bytes * 2 / 4;
    float        previousCrests[256 * 4] = {};
    Camera       testCamera{};
    Snapshot     testBoat{};
    testBoat.rotation.w = 1;
    const auto testLayout = oceanis401Layout();
    const auto testShadow = f4x4Identity();
    for (unsigned test = 0; test < 60; ++test)
    {
        // Wind alone must not turn an undeformed surface into a whitecap.
        // This also exercises thresholds very close to unit surface stretch.
        if (test == 30)
        {
            calm.windSpeed = 3;
            calm.windWaveHeight = calm.swellHeight = 0;
            configureOcean(ocean, calm);
        }
        unsigned frameIndex = test % 2;
        resetCmdPool(waterRenderer->renderer, pool);
        beginCmd(cmd);
        computeOcean(waterRenderer, cmd, ocean, frameIndex, test / 30.0f);
        prepareOceanEffects(waterRenderer, cmd, testCamera, frameIndex, calm.level, testShadow, WaterLook{}, testBoat, testLayout);
        cmdUpdateBuffer(cmd, readback, 0, waterRenderer->surface[frameIndex], 0, bytes);
        cmdUpdateBuffer(cmd, readback, bytes, waterRenderer->normals, 0, bytes);
        cmdUpdateBuffer(cmd, readback, bytes * 2, waterRenderer->crests[frameIndex], 0, sizeof previousCrests);
        endCmd(cmd);
        flushResourceUpdates(&flush);
        submit.pSignalFence = fence;
        submit.mWaitSemaphoreCount = flush.pOutSubmittedSemaphore ? 1 : 0;
        submit.ppWaitSemaphores = &flush.pOutSubmittedSemaphore;
        queueSubmit(queue, &submit);
        waitForFences(waterRenderer->renderer, 1, &fence);
        for (unsigned sampleIndex = 0; sampleIndex < bytes / 16; ++sampleIndex)
        {
            maximumCalmFoam = std::max(maximumCalmFoam, std::fabs(data[sampleIndex * 4 + 3]));
            minimumJacobian = std::min(minimumJacobian, data[bytes / 4 + sampleIndex * 4 + 3]);
        }
        for (unsigned sampleIndex = 0; sampleIndex < 256; ++sampleIndex)
            crestValid &= crests[sampleIndex * 4 + 2] == 0 && crests[sampleIndex * 4 + 3] == 0;
    }
    bool calmValid = maximumCalmFoam < .000001f && minimumJacobian > .42f;
    valid &= calmValid;
    LOGF(calmValid ? eINFO : eERROR, "Calm FFT + effects + history isolation %s: foam %.7f, minimum Jacobian %.7f",
         calmValid ? "PASS" : "FAIL", maximumCalmFoam, minimumJacobian);
    SeaState rough = original;
    rough.windSpeed = 26;
    rough.windWaveHeight = 5;
    rough.swellHeight = 2.5f;
    rough.swellPeriod = 10;
    rough.fetch = 65000;
    rough.depth = 30;
    rough.choppiness = 1;
    configureOcean(ocean, rough);
    float    maximumStormFoam = 0, minimumStormJacobian = 1, maximumStormCoverage = 0;
    bool     whitewaterValid = true;
    unsigned oldFoam = 0, activeBreakers = 0;
    float    maximumAirDepth = 0;
    for (unsigned test = 0; test < 120; ++test)
    {
        unsigned frameIndex = test % 2;
        resetCmdPool(waterRenderer->renderer, pool);
        beginCmd(cmd);
        computeOcean(waterRenderer, cmd, ocean, frameIndex, test / 30.0f);
        prepareOceanEffects(waterRenderer, cmd, testCamera, frameIndex, rough.level, testShadow, WaterLook{}, testBoat, testLayout);
        cmdUpdateBuffer(cmd, readback, 0, waterRenderer->surface[frameIndex], 0, bytes);
        cmdUpdateBuffer(cmd, readback, bytes, waterRenderer->normals, 0, bytes);
        cmdUpdateBuffer(cmd, readback, stateOffset, waterRenderer->whitewater[frameIndex], 0, bytes / 4);
        cmdUpdateBuffer(cmd, readback, bytes * 2, waterRenderer->crests[frameIndex], 0, sizeof previousCrests);
        endCmd(cmd);
        flushResourceUpdates(&flush);
        submit.pSignalFence = fence;
        submit.mWaitSemaphoreCount = flush.pOutSubmittedSemaphore ? 1 : 0;
        submit.ppWaitSemaphores = &flush.pOutSubmittedSemaphore;
        queueSubmit(queue, &submit);
        waitForFences(waterRenderer->renderer, 1, &fence);
        unsigned covered = 0;
        for (unsigned sampleIndex = 0; sampleIndex < 3 * fftSize * fftSize; ++sampleIndex)
        {
            valid &= std::isfinite(data[sampleIndex * 4 + 3]) && data[sampleIndex * 4 + 3] >= 0 && data[sampleIndex * 4 + 3] <= 1;
            covered += data[sampleIndex * 4 + 3] > .05f;
            maximumStormFoam = std::max(maximumStormFoam, data[sampleIndex * 4 + 3]);
            minimumStormJacobian = std::min(minimumStormJacobian, data[bytes / 4 + sampleIndex * 4 + 3]);
        }
        maximumStormCoverage = std::max(maximumStormCoverage, float(covered) / (3 * fftSize * fftSize));
        for (unsigned sampleIndex = 0; sampleIndex < 256; ++sampleIndex)
        {
            const float* crest = crests + sampleIndex * 4;
            const float* previousCrest = previousCrests + sampleIndex * 4;
            crestValid &= std::isfinite(crest[0] + crest[1] + crest[2] + crest[3]) && crest[2] >= 0 && crest[2] <= 1 && crest[3] >= 0 &&
                          crest[3] < 2.54f;
            emittingCrests += crest[2] > .05f;
            if (test > 0 && crest[3] > previousCrest[3] && previousCrest[3] > 0)
            {
                float dx = crest[0] - previousCrest[0], dz = crest[1] - previousCrest[1];
                crestValid &= dx * dx + dz * dz < .801f;
                ++trackedCrests;
            }
        }
        memcpy(previousCrests, crests, sizeof previousCrests);
        const auto* state = reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(data) + stateOffset);
        for (unsigned sampleIndex = 0; sampleIndex < 2 * fftSize * fftSize; ++sampleIndex)
        {
            float    age = TinyImageFormat_HalfAsUintToFloat(state[sampleIndex * 4]);
            float    air = TinyImageFormat_HalfAsUintToFloat(state[sampleIndex * 4 + 1]);
            float    depth = TinyImageFormat_HalfAsUintToFloat(state[sampleIndex * 4 + 2]) / std::max(.00001f, air);
            float    breaking = TinyImageFormat_HalfAsUintToFloat(state[sampleIndex * 4 + 3]);
            unsigned surfaceIndex = sampleIndex < fftSize * fftSize ? sampleIndex : sampleIndex + fftSize * fftSize;
            float    foam = data[surfaceIndex * 4 + 3];
            whitewaterValid &= std::isfinite(age + air + depth + breaking) && age >= 0 && age <= foam * 30 + .001f && air >= 0 &&
                               air <= 2 && depth >= 0 && depth < 1.51f && breaking >= 0 && breaking <= 1;
            oldFoam += foam > .02f && age > foam * .5f && breaking == 0;
            activeBreakers += breaking > .1f;
            maximumAirDepth = std::max(maximumAirDepth, depth);
        }
    }
    bool stormValid = maximumStormFoam > .1f && maximumStormCoverage > .001f && maximumStormCoverage < .25f;
    valid &= stormValid;
    LOGF(stormValid ? eINFO : eERROR, "Storm crest production %s: maximum foam %.6f, coverage >.05 %.2f%%, minimum Jacobian %.6f",
         stormValid ? "PASS" : "FAIL", maximumStormFoam, 100 * maximumStormCoverage, minimumStormJacobian);
    whitewaterValid &= oldFoam > 0 && activeBreakers > 0 && maximumAirDepth > .2f;
    valid &= whitewaterValid;
    LOGF(whitewaterValid ? eINFO : eERROR,
         "Transported whitewater %s: %u old foam samples without breaking, %u active breaker samples, maximum air depth %.3f m",
         whitewaterValid ? "PASS" : "FAIL", oldFoam, activeBreakers, maximumAirDepth);
    // A redraw at a different aspect ratio must not restart a breaking event.
    Camera resizedCamera = testCamera;
    resizedCamera.width = 961;
    resizedCamera.height = 1281;
    resetCmdPool(waterRenderer->renderer, pool);
    beginCmd(cmd);
    prepareOceanEffects(waterRenderer, cmd, resizedCamera, 0, rough.level, testShadow, WaterLook{}, testBoat, testLayout);
    cmdUpdateBuffer(cmd, readback, bytes * 2, waterRenderer->crests[0], 0, sizeof previousCrests);
    endCmd(cmd);
    flushResourceUpdates(&flush);
    submit.mWaitSemaphoreCount = flush.pOutSubmittedSemaphore ? 1 : 0;
    submit.ppWaitSemaphores = &flush.pOutSubmittedSemaphore;
    queueSubmit(queue, &submit);
    waitForFences(waterRenderer->renderer, 1, &fence);
    crestValid &= memcmp(previousCrests, crests, sizeof previousCrests) == 0 && emittingCrests > 0 && trackedCrests > 0;
    valid &= crestValid;
    LOGF(crestValid ? eINFO : eERROR, "Crest source continuity, calm isolation and paused resize %s: %u emitting, %u tracked source steps",
         crestValid ? "PASS" : "FAIL", emittingCrests, trackedCrests);
    double rippleVariance = 0;
    for (unsigned sampleIndex = 3 * fftSize * fftSize; sampleIndex < 4 * fftSize * fftSize; ++sampleIndex)
    {
        float moment = data[bytes / 4 + sampleIndex * 4 + 2];
        valid &= std::isfinite(moment) && moment >= 0;
        rippleVariance += moment;
        valid &= data[sampleIndex * 4] == 0 && data[sampleIndex * 4 + 2] == 0 && data[sampleIndex * 4 + 3] == 0;
    }
    float rippleSlope = std::sqrt(rippleVariance / (fftSize * fftSize));
    bool  rippleValid = rippleSlope > .035f && rippleSlope < .09f;
    valid &= rippleValid;
    LOGF(rippleValid ? eINFO : eERROR, "Fine-wave FFT normal energy %s: RMS slope %.6f", rippleValid ? "PASS" : "FAIL", rippleSlope);
    SeaState whitecaps = original;
    whitecaps.windSpeed = 13;
    whitecaps.windWaveHeight = 1.7f;
    whitecaps.swellHeight = .6f;
    whitecaps.swellPeriod = 6;
    whitecaps.fetch = 12000;
    whitecaps.depth = 30;
    whitecaps.choppiness = 1;
    whitecaps.current = {};
    configureOcean(ocean, whitecaps);
    unsigned     active[6] = {}, peakActive[6] = {};
    const float* spray = data + bytes * 2 / 4;
    for (unsigned test = 0; test < 150; ++test)
    {
        unsigned frameIndex = test % 2;
        resetCmdPool(waterRenderer->renderer, pool);
        beginCmd(cmd);
        computeOcean(waterRenderer, cmd, ocean, frameIndex, test / 30.0f);
        prepareOceanEffects(waterRenderer, cmd, testCamera, frameIndex, whitecaps.level, testShadow, WaterLook{}, testBoat, testLayout);
        if (test == 149)
        {
            cmdUpdateBuffer(cmd, readback, 0, waterRenderer->surface[frameIndex], 0, bytes);
            cmdUpdateBuffer(cmd, readback, bytes, waterRenderer->normals, 0, bytes);
        }
        cmdUpdateBuffer(cmd, readback, bytes * 2, waterRenderer->spray[frameIndex], 0, 4096 * 3 * 16);
        endCmd(cmd);
        flushResourceUpdates(&flush);
        submit.pSignalFence = fence;
        submit.mWaitSemaphoreCount = flush.pOutSubmittedSemaphore ? 1 : 0;
        submit.ppWaitSemaphores = &flush.pOutSubmittedSemaphore;
        queueSubmit(queue, &submit);
        waitForFences(waterRenderer->renderer, 1, &fence);
        memset(active, 0, sizeof active);
        for (unsigned sampleIndex = 0; sampleIndex < 4096; ++sampleIndex)
            if (spray[sampleIndex * 12 + 7] > 0)
            {
                unsigned type = unsigned(spray[sampleIndex * 12 + 8]);
                valid &= type < TF_ARRAY_COUNT(active) && std::isfinite(spray[sampleIndex * 12 + 1]);
                if (type < TF_ARRAY_COUNT(active))
                    ++active[type];
            }
        for (unsigned sampleIndex = 0; sampleIndex < TF_ARRAY_COUNT(active); ++sampleIndex)
            peakActive[sampleIndex] = std::max(peakActive[sampleIndex], active[sampleIndex]);
    }
    for (unsigned band = 0; band < 3; ++band)
    {
        double   mean = 0;
        float    maximum = 0, minimumJ = 1;
        unsigned coverage = 0;
        for (unsigned sampleIndex = band * fftSize * fftSize; sampleIndex < (band + 1) * fftSize * fftSize; ++sampleIndex)
        {
            float foam = data[sampleIndex * 4 + 3];
            mean += foam;
            maximum = std::max(maximum, foam);
            minimumJ = std::min(minimumJ, data[bytes / 4 + sampleIndex * 4 + 3]);
            coverage += foam > .05f;
            valid &= std::isfinite(foam) && foam >= 0 && foam <= 1;
        }
        LOGF(eINFO, "Whitecaps band %u: foam mean %.6f, maximum %.6f, coverage >.05 %.2f%%, minimum Jacobian %.6f", band,
             mean / (fftSize * fftSize), maximum, 100.0f * coverage / (fftSize * fftSize), minimumJ);
        valid &= mean / (fftSize * fftSize) < .35 && (band != 2 || maximum > .02f);
    }
    bool whitecapParticles = peakActive[0] + peakActive[3] > 0 && peakActive[5] > 0;
    valid &= whitecapParticles;
    LOGF(whitecapParticles ? eINFO : eERROR, "Intermittent whitecap particles %s: peak %u drops, %u rafts, %u sheets, %u mist, %u froth",
         whitecapParticles ? "PASS" : "FAIL", peakActive[0], peakActive[2], peakActive[3], peakActive[4], peakActive[5]);
    // With births, dissipation and current disabled, a marked surface parcel
    // retains its foam while the wave moves it. Reuse the normal readback area.
    resetCmdPool(waterRenderer->renderer, pool);
    beginCmd(cmd);
    cmdUpdateBuffer(cmd, readback, bytes, waterRenderer->surface[1], 0, bytes);
    WaterLook materialTest;
    materialTest.foamDecay = materialTest.foamSpread = 0;
    materialTest.breakingThreshold = -10;
    computeOcean(waterRenderer, cmd, ocean, 0, 5.1f, materialTest);
    cmdUpdateBuffer(cmd, readback, 0, waterRenderer->surface[0], 0, bytes);
    endCmd(cmd);
    flushResourceUpdates(&flush);
    submit.mWaitSemaphoreCount = flush.pOutSubmittedSemaphore ? 1 : 0;
    submit.ppWaitSemaphores = &flush.pOutSubmittedSemaphore;
    queueSubmit(queue, &submit);
    waitForFences(waterRenderer->renderer, 1, &fence);
    float materialError = 0, movingHeight = 0;
    for (unsigned sampleIndex = 0; sampleIndex < 3 * fftSize * fftSize; ++sampleIndex)
    {
        materialError = std::max(materialError, std::fabs(data[sampleIndex * 4 + 3] - data[bytes / 4 + sampleIndex * 4 + 3]));
        movingHeight = std::max(movingHeight, std::fabs(data[sampleIndex * 4 + 1] - data[bytes / 4 + sampleIndex * 4 + 1]));
    }
    bool materialValid = materialError < .00001f && movingHeight > .01f;
    valid &= materialValid;
    LOGF(materialValid ? eINFO : eERROR, "Material foam transport %s: density error %.7f, moving wave %.3f m",
         materialValid ? "PASS" : "FAIL", materialError, movingHeight);
    configureOcean(ocean, calm);
    testBoat.velocity = { 0, 0, 2.6f };
    for (unsigned test = 0; test < 150; ++test)
    {
        unsigned frameIndex = test % 2;
        resetCmdPool(waterRenderer->renderer, pool);
        beginCmd(cmd);
        testBoat.position.z = test / 30.0f * 2.6f;
        advanceWavePackets(ocean, 1.0f / 30, testBoat.position, false);
        computeOcean(waterRenderer, cmd, ocean, frameIndex, test / 30.0f);
        prepareOceanEffects(waterRenderer, cmd, testCamera, frameIndex, calm.level, testShadow, WaterLook{}, testBoat, catamaranLayout());
        if (test == 149)
            cmdUpdateBuffer(cmd, readback, bytes * 2, waterRenderer->spray[frameIndex], 0, 4096 * 3 * 16);
        endCmd(cmd);
        flushResourceUpdates(&flush);
        submit.mWaitSemaphoreCount = flush.pOutSubmittedSemaphore ? 1 : 0;
        submit.ppWaitSemaphores = &flush.pOutSubmittedSemaphore;
        queueSubmit(queue, &submit);
        waitForFences(waterRenderer->renderer, 1, &fence);
    }
    memset(active, 0, sizeof active);
    float flightHeight = 0;
    for (unsigned sampleIndex = 0; sampleIndex < 4096; ++sampleIndex)
        if (spray[sampleIndex * 12 + 7] > 0)
        {
            unsigned type = unsigned(spray[sampleIndex * 12 + 8]);
            valid &= type < TF_ARRAY_COUNT(active) && std::isfinite(spray[sampleIndex * 12 + 1]);
            if (type < TF_ARRAY_COUNT(active))
                ++active[type];
            if (type == 0 || type == 3)
                flightHeight = std::max(flightHeight, spray[sampleIndex * 12 + 1] - calm.level);
        }
    bool hullParticles = active[0] > 20 && active[2] > 20 && active[3] > 20 && active[4] > 5 && flightHeight > .2f;
    valid &= hullParticles;
    LOGF(hullParticles ? eINFO : eERROR,
         "Powered catamaran particles %s: %u drops, %u foam rafts, %u sheets, %u mist; flight height %.3f m",
         hullParticles ? "PASS" : "FAIL", active[0], active[2], active[3], active[4], flightHeight);
    // A crest particle can land outside the 64 m wake field. Its motion must
    // still include the global current instead of treating missing history as
    // stagnant water. Seed one such raft in the previous fixed GPU pool.
    float              distantFoam[12] = { 100, 0, 100, 0, 0, 0, 0, 2, WATER_FOAM, .1f, 0, .5f };
    TFBufferUpdateDesc particleUpdate = { waterRenderer->spray[1] };
    particleUpdate.mSize = sizeof distantFoam;
    beginUpdateResource(&particleUpdate);
    memcpy(particleUpdate.pMappedData, distantFoam, sizeof distantFoam);
    endUpdateResource(&particleUpdate);
    resetCmdPool(waterRenderer->renderer, pool);
    beginCmd(cmd);
    WaterLook driftLook;
    driftLook.foamWaveFlow = 0;
    computeOcean(waterRenderer, cmd, ocean, 0, 5);
    prepareOceanEffects(waterRenderer, cmd, testCamera, 0, calm.level, testShadow, driftLook, testBoat, catamaranLayout());
    cmdUpdateBuffer(cmd, readback, bytes * 2, waterRenderer->spray[0], 0, sizeof distantFoam);
    endCmd(cmd);
    flushResourceUpdates(&flush);
    submit.mWaitSemaphoreCount = flush.pOutSubmittedSemaphore ? 1 : 0;
    submit.ppWaitSemaphores = &flush.pOutSubmittedSemaphore;
    queueSubmit(queue, &submit);
    waitForFences(waterRenderer->renderer, 1, &fence);
    float expectedX = 100 + (calm.current.x + std::cos(calm.windDirection) * calm.windSpeed * .006f) / 30;
    float expectedZ = 100 + (calm.current.z + std::sin(calm.windDirection) * calm.windSpeed * .006f) / 30;
    bool  distantValid = std::fabs(spray[0] - expectedX) < .00002f && std::fabs(spray[2] - expectedZ) < .00002f && spray[8] == WATER_FOAM;
    valid &= distantValid;
    LOGF(distantValid ? eINFO : eERROR, "Distant foam current advection %s", distantValid ? "PASS" : "FAIL");
    // A weather edit must remove the previous cloud field immediately, even
    // while paused. Reuse the startup readback; the live cache stays on GPU.
    bool        skyValid = true;
    float       cloudyOpacity = 0, skyEnergyError = 0;
    const auto* skyData = static_cast<const uint16_t*>(readback->pCpuMappedAddress);
    for (unsigned test = 0; test < 4; ++test)
    {
        WaterLook look;
        look.overcast = test == 1 || test == 2 ? 1.0f : 0.0f;
        unsigned frameIndex = test % 2;
        resetCmdPool(waterRenderer->renderer, pool);
        beginCmd(cmd);
        prepareOceanEffects(waterRenderer, cmd, testCamera, frameIndex, calm.level, testShadow, look, testBoat, catamaranLayout());
        endCmd(cmd);
        flushResourceUpdates(&flush);
        submit.mWaitSemaphoreCount = flush.pOutSubmittedSemaphore ? 1 : 0;
        submit.ppWaitSemaphores = &flush.pOutSubmittedSemaphore;
        queueSubmit(queue, &submit);
        waitForFences(waterRenderer->renderer, 1, &fence);
        double opacity = 0, reference[3] = {};
        for (unsigned level = 0; level < waterRenderer->sky->mMipLevels; ++level)
        {
            // Startup only: use Forge's texture readback, including its row alignment.
            TFTextureCopyDesc copy = {};
            copy.pTexture = waterRenderer->sky;
            copy.pBuffer = readback;
            copy.mTextureMipLevel = level;
            copy.mTextureState = TF_RESOURCE_STATE_SHADER_RESOURCE;
            copy.mQueueType = TF_QUEUE_TYPE_GRAPHICS;
            TFSyncToken token = 0;
            copyResource(&copy, &token);
            waitForToken(&token);
            const unsigned height = (fftSize * 2) >> level, width = height * 2;
            const unsigned alignment = std::max(1u, waterRenderer->renderer->pGpu->mUploadBufferTextureRowAlignment);
            const unsigned stride = ((width * 8 + alignment - 1) / alignment) * alignment / 2;
            double         sum[3] = {}, weight = 0;
            for (unsigned row = 0; row < height; ++row)
            {
                const double area = std::sin((row + .5) * 3.14159265359 / height);
                weight += area * width;
                for (unsigned column = 0; column < width; ++column)
                    for (unsigned channelIndex = 0; channelIndex < 4; ++channelIndex)
                    {
                        const float value = TinyImageFormat_HalfAsUintToFloat(skyData[row * stride + column * 4 + channelIndex]);
                        skyValid &= std::isfinite(value) && value >= 0;
                        if (channelIndex < 3)
                            sum[channelIndex] += value * area;
                        else
                        {
                            if (level == 0)
                                opacity += value;
                            skyValid &= value <= 1 && (look.overcast > 0 || value == 0);
                        }
                    }
            }
            // Every mip must preserve the spherical mean, including the poles.
            for (unsigned channelIndex = 0; channelIndex < 3; ++channelIndex)
            {
                const double mean = sum[channelIndex] / weight;
                if (level == 0)
                    reference[channelIndex] = mean;
                else
                    skyEnergyError = std::max(skyEnergyError, float(std::fabs(mean - reference[channelIndex])));
            }
        }
        if (look.overcast > 0)
        {
            cloudyOpacity = float(opacity / (8 * fftSize * fftSize));
            skyValid &= cloudyOpacity > .04f;
        }
    }
    skyValid &= skyEnergyError < .003f;
    valid &= skyValid;
    LOGF(skyValid ? eINFO : eERROR, "Cloud history and paused weather reset %s: cloudy opacity %.3f", skyValid ? "PASS" : "FAIL",
         cloudyOpacity);
    LOGF(skyValid ? eINFO : eERROR, "Sky reflection filter: spherical radiance error %.6f", skyEnergyError);
    float              borderFoam[4] = { .6f, 0, 0, 0 };
    const uint64_t     borderOffset = (128 * 256 + 244) * 16;
    TFBufferUpdateDesc foamUpdate = { waterRenderer->foam[1] };
    foamUpdate.mDstOffset = borderOffset;
    foamUpdate.mSize = sizeof borderFoam;
    beginUpdateResource(&foamUpdate);
    memcpy(foamUpdate.pMappedData, borderFoam, sizeof borderFoam);
    endUpdateResource(&foamUpdate);
    uint16_t           borderState[4] = { TinyImageFormat_FloatToHalfAsUint(1.2f), TinyImageFormat_FloatToHalfAsUint(.4f),
                                          TinyImageFormat_FloatToHalfAsUint(.12f), 0 };
    TFBufferUpdateDesc stateUpdate = { waterRenderer->localWhitewater[1] };
    stateUpdate.mDstOffset = borderOffset / 2;
    stateUpdate.mSize = sizeof borderState;
    beginUpdateResource(&stateUpdate);
    memcpy(stateUpdate.pMappedData, borderState, sizeof borderState);
    endUpdateResource(&stateUpdate);
    resetCmdPool(waterRenderer->renderer, pool);
    beginCmd(cmd);
    prepareOceanEffects(waterRenderer, cmd, testCamera, 0, calm.level, testShadow, WaterLook{}, testBoat, catamaranLayout());
    cmdUpdateBuffer(cmd, readback, 0, waterRenderer->foam[0], borderOffset, 16);
    cmdUpdateBuffer(cmd, readback, 16, waterRenderer->foam[0], 128 * 256 * 16, 16);
    cmdUpdateBuffer(cmd, readback, 32, waterRenderer->foam[0], (128 * 256 + 255) * 16, 16);
    cmdUpdateBuffer(cmd, readback, 48, waterRenderer->localWhitewater[0], borderOffset / 2, sizeof borderState);
    endCmd(cmd);
    flushResourceUpdates(&flush);
    submit.mWaitSemaphoreCount = flush.pOutSubmittedSemaphore ? 1 : 0;
    submit.ppWaitSemaphores = &flush.pOutSubmittedSemaphore;
    queueSubmit(queue, &submit);
    waitForFences(waterRenderer->renderer, 1, &fence);
    bool borderValid = std::fabs(data[0] - .6f) < .000001f;
    borderValid &= memcmp(reinterpret_cast<const uint8_t*>(data) + 48, borderState, 3 * sizeof(uint16_t)) == 0;
    for (unsigned sampleIndex = 1; sampleIndex < 3; ++sampleIndex)
        borderValid &= data[sampleIndex * 4 + 2] == calm.current.x && data[sampleIndex * 4 + 3] == calm.current.z;
    valid &= borderValid;
    LOGF(borderValid ? eINFO : eERROR, "Paused foam density, age, air depth and local-flow boundary %s: retained density %.6f",
         borderValid ? "PASS" : "FAIL", data[0]);
    configureOcean(ocean, original);
    exitFence(waterRenderer->renderer, fence);
    exitCmd(waterRenderer->renderer, cmd);
    exitCmdPool(waterRenderer->renderer, pool);
    removeResource(readback);
    waterRenderer->bytes -= readbackBytes;
    destroyOcean(ocean);
    waterRenderer->revision[0] = waterRenderer->revision[1] = 0;
    waterRenderer->lastTime = 0;
    return valid;
}
uint64_t   oceanGPUBytes(const OceanRenderer* waterRenderer) { return waterRenderer->bytes; }
TFTexture* oceanSkyTexture(const OceanRenderer* waterRenderer, unsigned& height)
{
    height = waterRenderer->size * 2;
    return waterRenderer->sky;
}
TFTexture* oceanCloudNoise(const OceanRenderer* waterRenderer) { return waterRenderer->cloudNoise; }
} // namespace mooring
