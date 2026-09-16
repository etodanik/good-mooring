#include "PlatformProbe.h"
#include "Common/Graphics/Interfaces/IGraphics.h"
#include "Common/Resources/ResourceLoader/Interfaces/IResourceLoader.h"
#include "Common/Graphics/FSL/fsl_srt.h"
#include "Common/Graphics/FSL/defaults.h"
#include "Shaders/Platform.srt.h"
#include "Common/Utilities/Interfaces/ILog.h"

bool runPlatformProbe(TFRenderer* renderer, TFQueue* queue)
{
    TFTexture*    texture = nullptr;
    TFTextureDesc textureDesc = {};
    textureDesc.mWidth = textureDesc.mHeight = 8;
    textureDesc.mDepth = textureDesc.mArraySize = textureDesc.mMipLevels = 1;
    textureDesc.mSampleCount = TF_SAMPLE_COUNT_1;
    textureDesc.mFormat = TinyImageFormat_R8G8B8A8_UNORM;
    textureDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
    textureDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
    TFTextureLoadDesc textureLoad = {};
    textureLoad.pDesc = &textureDesc;
    textureLoad.ppTexture = &texture;
    addResource(&textureLoad, nullptr);
    TFTextureUpdateDesc update = { texture, 0, 1, 0, 1, TF_RESOURCE_STATE_SHADER_RESOURCE };
    beginUpdateResource(&update);
    auto subresource = update.getSubresourceUpdateDesc(0, 0);
    for (unsigned row = 0; row < 8; ++row)
        for (unsigned column = 0; column < 8; ++column)
        {
            uint8_t* texel = subresource.pMappedData + row * subresource.mDstRowStride + column * 4;
            texel[0] = 1 + column + row * 8;
            texel[1] = texel[2] = 0;
            texel[3] = 255;
        }
    endUpdateResource(&update);

    TFBuffer*        results = nullptr;
    TFBufferLoadDesc bufferLoad = {};
    bufferLoad.mDesc.mSize = 64 * sizeof(uint32_t);
    bufferLoad.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_BUFFER;
    bufferLoad.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_TO_CPU;
    bufferLoad.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
    bufferLoad.mDesc.mStructStride = sizeof(uint32_t);
    bufferLoad.mDesc.mElementCount = 64;
    bufferLoad.ppBuffer = &results;
    addResource(&bufferLoad, nullptr);
    waitForAllResourceLoads();
    FlushResourceUpdateDesc flush = {};
    flushResourceUpdates(&flush);

    TFShader*        shader = nullptr;
    TFShaderLoadDesc shaderLoad = {};
    shaderLoad.mComp.pFileName = "platform_probe.comp";
    addShader(renderer, &shaderLoad, &shader);
    TFPipelineDesc pipelineDesc = {};
    pipelineDesc.mType = TF_PIPELINE_TYPE_COMPUTE;
    pipelineDesc.mComputeDesc.pShaderProgram = shader;
    PIPELINE_LAYOUT_DESC(pipelineDesc, nullptr, SRT_LAYOUT_DESC(PlatformProbe, PerFrame), nullptr, nullptr);
    TFPipeline* pipeline = nullptr;
    addPipeline(renderer, &pipelineDesc, &pipeline);
    TFDescriptorSetDesc setDesc = SRT_SET_DESC(PlatformProbe, PerFrame, 1, 0);
    TFDescriptorSet*    descriptors = nullptr;
    addDescriptorSet(renderer, &setDesc, &descriptors);
    TFDescriptorData data[2] = {};
    data[0].mIndex = SRT_RES_IDX(PlatformProbe, PerFrame, gPattern);
    data[0].ppTextures = &texture;
    data[1].mIndex = SRT_RES_IDX(PlatformProbe, PerFrame, gResults);
    data[1].ppBuffers = &results;
    updateDescriptorSet(renderer, 0, descriptors, 2, data);

    TFCmdPool*    pool = nullptr;
    TFCmdPoolDesc poolDesc = {};
    poolDesc.pQueue = queue;
    initCmdPool(renderer, &poolDesc, &pool);
    TFCmd*    cmd = nullptr;
    TFCmdDesc cmdDesc = {};
    cmdDesc.pPool = pool;
    initCmd(renderer, &cmdDesc, &cmd);
    TFFence* fence = nullptr;
    initFence(renderer, &fence);
    beginCmd(cmd);
    cmdBindDescriptorSet(cmd, 0, descriptors);
    cmdBindPipeline(cmd, pipeline);
    cmdDispatch(cmd, 1, 1, 1);
    endCmd(cmd);
    TFQueueSubmitDesc submit = {};
    submit.mCmdCount = 1;
    submit.ppCmds = &cmd;
    submit.pSignalFence = fence;
    submit.mWaitSemaphoreCount = flush.pOutSubmittedSemaphore ? 1 : 0;
    submit.ppWaitSemaphores = &flush.pOutSubmittedSemaphore;
    queueSubmit(queue, &submit);
    waitForFences(renderer, 1, &fence);

    bool        valid = true;
    const auto* values = static_cast<const uint32_t*>(results->pCpuMappedAddress);
    for (unsigned texelIndex = 0; texelIndex < 64; ++texelIndex)
        if (values[texelIndex] != 1 + texelIndex * 8)
        {
            LOGF(eERROR, "Platform probe: texel %u expected %u, got %u", texelIndex, 1 + texelIndex * 8, values[texelIndex]);
            valid = false;
            break;
        }
    LOGF(valid ? eINFO : eERROR, "Platform compute and texture upload: %s", valid ? "PASS (64 texels)" : "FAIL");
    exitFence(renderer, fence);
    exitCmd(renderer, cmd);
    exitCmdPool(renderer, pool);
    removeDescriptorSet(renderer, descriptors);
    removePipeline(renderer, pipeline);
    removeShader(renderer, shader);
    removeResource(results);
    removeResource(texture);
    return valid;
}
