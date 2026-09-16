#pragma once
#include "Common/Graphics/Interfaces/IGraphics.h"
#include "Common/Resources/ResourceLoader/Interfaces/IResourceLoader.h"

namespace mooring
{
struct WaterLook;
struct OceanRenderer;
enum class ShaderLabScalar
{
    Float,
    Half,
    Uint
};
struct ShaderLabBufferLayout
{
    unsigned        width = 1, height = 1, bands = 1, mips = 1, stride = 16, components = 4;
    ShaderLabScalar scalar = ShaderLabScalar::Float;
    const char*     channels = "X / Y / Z / W";
};

void   initShaderLab(TFRenderer*, TFQueue*);
void   exitShaderLab();
bool   reloadShaderLab();
void   updateShaderLab(float elapsed);
void   drawShaderLab(WaterLook&, unsigned width, unsigned height);
void   setShaderLabOpen(bool open);
bool   shaderLabIsOpen();
void   completeShaderLabFrame(unsigned frame);
void   renderShaderLab(TFCmd*, unsigned frame, OceanRenderer*);
float4 shaderLabProbeDisplay();
void   shaderLabRequestReload();
bool   shaderLabAvailable();
// Developer smoke test: 0 running, 1 passed, -1 failed.
int    runShaderLabQA();

void shaderLabRegisterBuffer(const void* owner, const char* name, TFBuffer*, ShaderLabBufferLayout);
void shaderLabRegisterTexture(const void* owner, const char* name, TFTexture*, bool readable = true);
void shaderLabForgetResources(const void* owner);

// Creation and binding wrappers retain the information needed by the browser.
void shaderLabAddShader(TFRenderer*, const TFShaderLoadDesc*, TFShader**);
void shaderLabRemoveShader(TFRenderer*, TFShader*);
void shaderLabAddPipeline(TFRenderer*, const TFPipelineDesc*, TFPipeline**);
void shaderLabRemovePipeline(TFRenderer*, TFPipeline*);
void shaderLabUpdateDescriptorSet(TFRenderer*, uint32_t index, TFDescriptorSet*, uint32_t count, const TFDescriptorData*);
void shaderLabRemoveDescriptorSet(TFRenderer*, TFDescriptorSet*);
void shaderLabBindPipeline(TFCmd*, TFPipeline*);
void shaderLabBindDescriptorSet(TFCmd*, uint32_t index, TFDescriptorSet*);
} // namespace mooring
