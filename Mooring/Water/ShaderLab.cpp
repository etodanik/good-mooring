#include "ShaderLab.h"
#include "WaterLook.h"
#include "OceanRenderer.h"
#include "../ToolUI.h"
#include "Common/Application/Interfaces/IUI.h"
#include "Common/OS/Interfaces/IInput.h"
#include "Common/OS/Interfaces/IOperatingSystem.h"
#include "Common/Utilities/Interfaces/ILog.h"
#include "Common/Utilities/Interfaces/IFileSystem.h"
#include "Common/Tools/ReloadServer/ReloadClient.h"
#include "Common/Graphics/FSL/defaults.h"
#include "Common/Resources/ResourceLoader/ThirdParty/OpenSource/tinyimageformat/tinyimageformat_query.h"
#include "../Shaders/ShaderLab.srt.h"
#include "../Shaders/Water.srt.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(MOORING_SHADER_LAB)
#import <Foundation/Foundation.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace mooring
{
#if defined(MOORING_SHADER_LAB)
namespace
{
struct ShaderInfo
{
    TFShader*   handle;
    std::string vertex, fragment, compute;
};
struct Binding
{
    const void* handle = nullptr;
    unsigned    slot = 0, set = 0, index = 0;
    uint64_t    offset = 0;
    std::string name;
};
struct PipelineInfo
{
    TFPipeline*          handle = nullptr;
    std::string          name, vertex, fragment, compute;
    bool                 depthTest = false, depthWrite = false, blend = false;
    std::vector<Binding> bindings;
};
struct DescriptorInfo
{
    TFDescriptorSet*     handle;
    unsigned             index;
    std::vector<Binding> bindings;
};
struct Resource
{
    const void*           owner = nullptr;
    std::string           name;
    TFBuffer*             buffer = nullptr;
    TFTexture*            texture = nullptr;
    ShaderLabBufferLayout layout;
    bool                  readable = true;
};
struct Preview
{
    std::string name, source, binary, function;
    unsigned    dimensions = 2, line = 0;
    bool        integer = false;
    TFShader*   shader = nullptr;
    TFPipeline* pipeline = nullptr;
};
struct Probe
{
    std::string name, source;
    unsigned    id, line;
};
std::vector<ShaderInfo>     shaders;
std::vector<PipelineInfo>   pipelines;
std::vector<DescriptorInfo> descriptors;
std::vector<Resource>       resources;
TFPipeline*                 boundPipeline = nullptr;

struct PreviewSettings
{
    int    resolution = 256, component = 0, band = 0, mip = 0, sliceAxis = 0;
    int    sampleX = 0, sampleY = 0;
    float3 origin = { 0, 0, 0 }, extent = { 64, 64, 64 };
    float4 parameters = { .5f, 1, 0, 0 };
    float  minimum = 0, maximum = 1, slice = 0, time = 0;
    float  zoom = 1, panX = 0, panY = 0;
    bool   integer = true, frozen = false, animate = false, signedColor = false;
};
struct SavedPreview
{
    int             category;
    std::string     name;
    PreviewSettings settings;
};
struct Lab: PreviewSettings
{
    TFRenderer*               renderer = nullptr;
    TFQueue*                  queue = nullptr;
    TFRenderTarget*           target = nullptr;
    TFDescriptorSet*          sets = nullptr;
    TFBuffer*                 uniforms[2] = {};
    TFBuffer*                 samples[2] = {};
    TFBuffer*                 dummyBuffer = nullptr;
    TFTexture*                dummyTexture = nullptr;
    TFTexture*                dummyVolume = nullptr;
    TFShader*                 resourceShader = nullptr;
    TFPipeline*               resourcePipeline = nullptr;
    std::vector<Preview>      previews;
    std::vector<Probe>        probes;
    std::vector<float4>       readback;
    std::vector<float>        curve;
    ShaderLabParameters       submitted[2] = {};
    ShaderLabParameters       completed = {};
    unsigned                  submittedGeneration[2] = {}, generation = 1, readbackGeneration = 0;
    unsigned                  allocatedWidth = 0, allocatedHeight = 0;
    int                       category = 0, selected = 0;
    float                     clock = 0, nextPoll = 0, changeTime = -1;
    bool                      visible = false, autoReload = false, open = false;
    toolui::WindowState       window, buildLogWindow;
    bool                      showBuildLog = false, controlsOnly = false, reloadFailed = false;
    std::string               lastEntry[4], returnPipeline;
    std::vector<SavedPreview> savedPreviews;
    char                      search[4][128] = {};
    bool                      pending[2] = {}, freezeRendered = false;
    NSString*                 watchedSignature = nil;
    NSTask*                   server = nil;
    char                      message[8192] = {};
    char                      applied[256] = "";
} lab;

void label(const char* text) { uiLabel(text, TF_ALIGN_LEFT); }
void wrapped(const char* text, float width)
{
    const char* cursor = text;
    while (*cursor)
    {
        const char* end = std::strchr(cursor, '\n');
        std::string line(cursor, end ? size_t(end - cursor) : std::strlen(cursor));
        const float rows = std::max(1.0f, std::ceil(float(uiGetTextWidth(line.c_str())) / std::max(40.0f, width - 32)));
        uiLayoutDynamicRows(rows * uiLayoutGetTextSize("M", 1).y + 4, 1);
        bstring message = bconstfromcstr(line.c_str());
        uiDynamicText(&message, { .88f, .9f, .92f, 1 }, TF_TEXT_MODE_WRAPPED, TF_ALIGN_LEFT);
        cursor += line.size();
        if (*cursor)
            ++cursor;
    }
}
bool button(const char* text) { return UI_WIDGET_IS_PRESSED(uiButton(text)); }
void changed()
{
    ++lab.generation;
    lab.freezeRendered = false;
}
bool scalar(const char* title, float& value, float low, float high, float step)
{
    uiLayoutAutoTextRows(2);
    label(title);
    float before = value;
    // Drawing a control must not clamp a resource's larger default domain.
    uiPropertyFloat(&value, std::min(low, value), std::max(high, value), step);
    return before != value;
}
std::string string(NSDictionary* dictionary, NSString* key)
{
    id value = dictionary[key];
    return [value isKindOfClass:[NSString class]] ? [(NSString*)value UTF8String] : "";
}
NSDictionary* manifest()
{
    void*    memory = nullptr;
    uint32_t length = 0;
    NSData*  data = nil;
    if (platformGetReloadBinary("shader-lab.json", &memory, &length))
        data = [NSData dataWithBytes:memory length:length];
    else
    {
        TFFileStream stream{};
        if (fsOpenStreamFromPath(TF_RD_SHADER_BINARIES, "shader-lab.json", TF_FM_READ, &stream))
        {
            NSMutableData* bytes = [NSMutableData dataWithLength:fsGetStreamFileSize(&stream)];
            fsReadFromStream(&stream, bytes.mutableBytes, bytes.length);
            fsCloseStream(&stream);
            data = bytes;
        }
    }
    id value = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
    return [value isKindOfClass:[NSDictionary class]] ? value : nil;
}
TFBuffer* makeBuffer(uint64_t bytes, TFDescriptorType type, TFResourceMemoryUsage memory, const char* name)
{
    TFBuffer*        buffer = nullptr;
    TFBufferLoadDesc description{};
    description.mDesc.mSize = bytes;
    description.mDesc.mDescriptors = type;
    description.mDesc.mMemoryUsage = memory;
    description.mDesc.mStructStride = 16;
    description.mDesc.mElementCount = unsigned(bytes / 16);
    description.mDesc.pName = name;
    description.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
    description.ppBuffer = &buffer;
    addResource(&description, nullptr);
    return buffer;
}
void releasePreview(Preview& preview)
{
    if (preview.pipeline)
        removePipeline(lab.renderer, preview.pipeline);
    if (preview.shader)
        removeShader(lab.renderer, preview.shader);
    preview.pipeline = nullptr;
    preview.shader = nullptr;
}
bool loadPreview(Preview& preview)
{
    TFShaderLoadDesc shader{};
    shader.mVert.pFileName = "shader_lab.vert";
    shader.mFrag.pFileName = preview.binary.c_str();
    addShader(lab.renderer, &shader, &preview.shader);
    if (!preview.shader)
        return false;
    TFPipelineDesc description{};
    description.mType = TF_PIPELINE_TYPE_GRAPHICS;
    description.pName = preview.name.c_str();
    PIPELINE_LAYOUT_DESC(description, nullptr, SRT_LAYOUT_DESC(WaterDraw, PerFrame), nullptr, SRT_LAYOUT_DESC(ShaderLabResources, PerDraw));
    auto&           graphics = description.mGraphicsDesc;
    TinyImageFormat format = TinyImageFormat_R16G16B16A16_SFLOAT;
    graphics.pShaderProgram = preview.shader;
    graphics.pColorFormats = &format;
    graphics.mRenderTargetCount = 1;
    graphics.mSampleCount = TF_SAMPLE_COUNT_1;
    graphics.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
    TFRasterizerStateDesc raster{};
    raster.mCullMode = TF_CULL_MODE_NONE;
    graphics.pRasterizerState = &raster;
    addPipeline(lab.renderer, &description, &preview.pipeline);
    return preview.pipeline != nullptr;
}
void ensureTarget()
{
    const unsigned width = unsigned(lab.resolution);
    const unsigned height =
        lab.category == 0 && lab.selected < int(lab.previews.size()) && lab.previews[lab.selected].dimensions == 1 ? 1 : width;
    if (lab.allocatedWidth == width && lab.allocatedHeight == height)
        return;
    waitQueueIdle(lab.queue);
    if (lab.target)
        removeRenderTarget(lab.renderer, lab.target);
    for (auto*& sample : lab.samples)
    {
        if (sample)
            removeResource(sample);
        sample = nullptr;
    }
    TFRenderTargetDesc description{};
    description.mWidth = width;
    description.mHeight = height;
    description.mDepth = description.mArraySize = 1;
    description.mSampleCount = TF_SAMPLE_COUNT_1;
    description.mFormat = TinyImageFormat_R16G16B16A16_SFLOAT;
    description.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
    description.mFlags = TF_TEXTURE_CREATION_FLAG_FORCE_2D;
    description.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
    description.pName = "Shader Lab canvas";
    addRenderTarget(lab.renderer, &description, &lab.target);
    for (auto*& sample : lab.samples)
        sample = makeBuffer(uint64_t(width) * height * 16, TF_DESCRIPTOR_TYPE_RW_BUFFER, TF_RESOURCE_MEMORY_USAGE_GPU_TO_CPU,
                            "Shader Lab values");
    waitForAllResourceLoads();
    lab.allocatedWidth = width;
    lab.allocatedHeight = height;
    lab.pending[0] = lab.pending[1] = false;
    lab.readback.resize(size_t(width) * height);
    lab.curve.resize(width);
    changed();
}
void selectionDefaults()
{
    static_cast<PreviewSettings&>(lab) = {};
    if (lab.category == 0 && lab.selected < int(lab.previews.size()))
    {
        const auto& preview = lab.previews[lab.selected];
        lab.integer = preview.integer;
        if (preview.dimensions == 1)
            lab.extent = { 1, 1, 1 };
        if (preview.function == "previewWaterNoise" || preview.function == "previewFoamPattern")
            lab.extent = { 8, 8, 8 };
        if (preview.function == "previewWaterPhase")
        {
            lab.origin[0] = -1;
            lab.extent[0] = 2;
        }
        if (preview.dimensions == 3)
        {
            lab.extent = { 10000, 4000, 10000 };
            lab.slice = 0;
        }
    }
    if (lab.category == 1 && lab.selected < int(resources.size()))
    {
        const auto& resource = resources[lab.selected];
        lab.integer = resource.buffer != nullptr;
        lab.extent = resource.buffer ? float3{ float(resource.layout.width), float(resource.layout.height), 1 } : float3{ 1, 1, 1 };
    }
    changed();
}
} // namespace
#endif

void shaderLabAddShader(TFRenderer* renderer, const TFShaderLoadDesc* description, TFShader** shader)
{
    addShader(renderer, description, shader);
#if defined(MOORING_SHADER_LAB)
    if (*shader)
        shaders.push_back({ *shader, description->mVert.pFileName ? description->mVert.pFileName : "",
                            description->mFrag.pFileName ? description->mFrag.pFileName : "",
                            description->mComp.pFileName ? description->mComp.pFileName : "" });
#endif
}
void shaderLabRemoveShader(TFRenderer* renderer, TFShader* shader)
{
    if (!shader)
        return;
#if defined(MOORING_SHADER_LAB)
    shaders.erase(std::remove_if(shaders.begin(), shaders.end(), [shader](const auto& item) { return item.handle == shader; }),
                  shaders.end());
#endif
    removeShader(renderer, shader);
}
void shaderLabAddPipeline(TFRenderer* renderer, const TFPipelineDesc* description, TFPipeline** pipeline)
{
    *pipeline = nullptr;
    addPipeline(renderer, description, pipeline);
#if defined(MOORING_SHADER_LAB)
    if (!*pipeline)
        return;
    PipelineInfo information;
    information.handle = *pipeline;
    information.name = description->pName ? description->pName : "Unnamed pipeline";
    TFShader* shader = description->mType == TF_PIPELINE_TYPE_GRAPHICS ? description->mGraphicsDesc.pShaderProgram
                                                                       : description->mComputeDesc.pShaderProgram;
    for (const auto& entry : shaders)
        if (entry.handle == shader)
        {
            information.vertex = entry.vertex;
            information.fragment = entry.fragment;
            information.compute = entry.compute;
        }
    if (description->mType == TF_PIPELINE_TYPE_GRAPHICS)
    {
        const auto& graphics = description->mGraphicsDesc;
        information.depthTest = graphics.pDepthState && graphics.pDepthState->mDepthTest;
        information.depthWrite = graphics.pDepthState && graphics.pDepthState->mDepthWrite;
        information.blend = graphics.pBlendState != nullptr;
    }
    information.bindings.reserve(64);
    pipelines.push_back(std::move(information));
#endif
}
void shaderLabRemovePipeline(TFRenderer* renderer, TFPipeline* pipeline)
{
    if (!pipeline)
        return;
#if defined(MOORING_SHADER_LAB)
    const std::string selectedName = lab.category == 2 && lab.selected < int(pipelines.size()) ? pipelines[lab.selected].name : "";
    pipelines.erase(std::remove_if(pipelines.begin(), pipelines.end(), [pipeline](const auto& entry) { return entry.handle == pipeline; }),
                    pipelines.end());
    if (!selectedName.empty())
    {
        const auto selected =
            std::find_if(pipelines.begin(), pipelines.end(), [&](const auto& entry) { return entry.name == selectedName; });
        lab.selected = selected == pipelines.end() ? 0 : int(selected - pipelines.begin());
    }
    if (boundPipeline == pipeline)
        boundPipeline = nullptr;
#endif
    removePipeline(renderer, pipeline);
}
void shaderLabUpdateDescriptorSet(TFRenderer* renderer, uint32_t index, TFDescriptorSet* set, uint32_t count, const TFDescriptorData* data)
{
    updateDescriptorSet(renderer, index, set, count, data);
#if defined(MOORING_SHADER_LAB)
    auto found = std::find_if(descriptors.begin(), descriptors.end(),
                              [set, index](const auto& entry) { return entry.handle == set && entry.index == index; });
    if (found == descriptors.end())
    {
        descriptors.push_back({ set, index, {} });
        found = descriptors.end() - 1;
        found->bindings.reserve(64);
    }
    for (unsigned position = 0; position < count; ++position)
    {
        const auto& update = data[position];
        if (update.mUseTextureDescriptors || !update.ppBuffers)
            continue;
        const TFDescriptorType type = set->pDescriptors[update.mIndex].mType;
        if (type == TF_DESCRIPTOR_TYPE_SAMPLER)
            continue;
        Binding binding;
        IF_VALIDATE_DESCRIPTOR(binding.name = set->pDescriptors[update.mIndex].pName;)
        std::memcpy(&binding.handle, update.ppBuffers, sizeof(binding.handle));
        binding.slot = update.mIndex;
        binding.set = set->mSetIndex;
        binding.index = index;
        binding.offset = update.pRanges ? update.pRanges[0].mOffset : 0;
        auto previous =
            std::find_if(found->bindings.begin(), found->bindings.end(), [&](const auto& entry) { return entry.slot == binding.slot; });
        if (previous == found->bindings.end())
            found->bindings.push_back(binding);
        else
            *previous = binding;
    }
#endif
}
void shaderLabRemoveDescriptorSet(TFRenderer* renderer, TFDescriptorSet* set)
{
#if defined(MOORING_SHADER_LAB)
    descriptors.erase(std::remove_if(descriptors.begin(), descriptors.end(), [set](const auto& entry) { return entry.handle == set; }),
                      descriptors.end());
#endif
    removeDescriptorSet(renderer, set);
}
void shaderLabBindPipeline(TFCmd* command, TFPipeline* pipeline)
{
    cmdBindPipeline(command, pipeline);
#if defined(MOORING_SHADER_LAB)
    boundPipeline = pipeline;
    for (auto& entry : pipelines)
        if (entry.handle == pipeline)
            entry.bindings.clear();
#endif
}
void shaderLabBindDescriptorSet(TFCmd* command, uint32_t index, TFDescriptorSet* set)
{
    cmdBindDescriptorSet(command, index, set);
#if defined(MOORING_SHADER_LAB)
    for (auto& pipeline : pipelines)
        if (pipeline.handle == boundPipeline)
            for (const auto& descriptor : descriptors)
                if (descriptor.handle == set && descriptor.index == index)
                    pipeline.bindings.insert(pipeline.bindings.end(), descriptor.bindings.begin(), descriptor.bindings.end());
#endif
}
void shaderLabRegisterBuffer(const void* owner, const char* name, TFBuffer* buffer, ShaderLabBufferLayout layout)
{
#if defined(MOORING_SHADER_LAB)
    auto found = std::find_if(resources.begin(), resources.end(),
                              [owner, name](const auto& entry) { return entry.owner == owner && entry.name == name; });
    if (found == resources.end())
    {
        resources.push_back({});
        found = resources.end() - 1;
        found->owner = owner;
        found->name = name;
    }
    found->buffer = buffer;
    found->texture = nullptr;
    found->layout = layout;
#endif
}
void shaderLabRegisterTexture(const void* owner, const char* name, TFTexture* texture, bool readable)
{
#if defined(MOORING_SHADER_LAB)
    auto found = std::find_if(resources.begin(), resources.end(),
                              [owner, name](const auto& entry) { return entry.owner == owner && entry.name == name; });
    if (found == resources.end())
    {
        resources.push_back({});
        found = resources.end() - 1;
        found->owner = owner;
        found->name = name;
    }
    found->texture = texture;
    found->buffer = nullptr;
    found->readable = readable;
#endif
}
void shaderLabForgetResources(const void* owner)
{
#if defined(MOORING_SHADER_LAB)
    resources.erase(std::remove_if(resources.begin(), resources.end(), [owner](const auto& entry) { return entry.owner == owner; }),
                    resources.end());
    changed();
#endif
}

void initShaderLab(TFRenderer* renderer, TFQueue* queue)
{
#if defined(MOORING_SHADER_LAB)
    lab.renderer = renderer;
    lab.queue = queue;
    TFDescriptorSetDesc        set = SRT_SET_DESC(ShaderLabResources, PerDraw, 2, 0);
    // FSL assigns direct buffer slots across both SRTs in declaration order.
    // Include WaterDraw when deriving PerDraw's slots, just as the shader does.
    const TFMetalDescriptorSet combinedSets[] = { *getSrtWaterDrawPtr(), *getSrtShaderLabResourcesPtr() };
    set.mIndex = 1;
    set.mSrtSetCount = TF_ARRAY_COUNT(combinedSets);
    set.pSrtSets = combinedSets;
    addDescriptorSet(renderer, &set, &lab.sets);
    for (auto*& uniform : lab.uniforms)
        uniform = makeBuffer(sizeof(ShaderLabParameters), TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER, TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU,
                             "Shader Lab controls");
    lab.dummyBuffer = makeBuffer(16, TF_DESCRIPTOR_TYPE_BUFFER, TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU, "Shader Lab empty buffer");
    for (unsigned dimensions = 2; dimensions <= 3; ++dimensions)
    {
        TFTextureDesc texture{};
        texture.mWidth = texture.mHeight = texture.mDepth = texture.mArraySize = texture.mMipLevels = 1;
        texture.mFormat = TinyImageFormat_R8G8B8A8_UNORM;
        texture.mSampleCount = TF_SAMPLE_COUNT_1;
        texture.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        texture.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        texture.mFlags = dimensions == 3 ? TF_TEXTURE_CREATION_FLAG_FORCE_3D : TF_TEXTURE_CREATION_FLAG_FORCE_2D;
        texture.pName = "Shader Lab empty texture";
        TFTextureLoadDesc load{};
        load.pDesc = &texture;
        load.ppTexture = dimensions == 3 ? &lab.dummyVolume : &lab.dummyTexture;
        addResource(&load, nullptr);
    }
    waitForAllResourceLoads();
    reloadShaderLab();
    ensureTarget();
    // Own a foreground child rather than killing or replacing a shared daemon.
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int  reservation = socket(AF_INET, SOCK_STREAM, 0);
    socklen_t  addressSize = sizeof(address);
    const bool reserved = reservation >= 0 && bind(reservation, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
                          getsockname(reservation, reinterpret_cast<sockaddr*>(&address), &addressSize) == 0;
    if (reservation >= 0)
        close(reservation);
    if (!reserved || !platformSetReloadPort(ntohs(address.sin_port)))
    {
        snprintf(lab.applied, sizeof lab.applied, "Cannot start the local shader reload host.");
        return;
    }
    lab.server = [[NSTask alloc] init];
    lab.server.executableURL = [NSURL fileURLWithPath:@MOORING_PYTHON];
    lab.server.arguments =
        @[ @MOORING_SOURCE_DIR "/Tools/Mooring/reload_shaders.py", @"--port", [NSString stringWithFormat:@"%u", ntohs(address.sin_port)] ];
    lab.server.currentDirectoryURL = [NSURL fileURLWithPath:@MOORING_SOURCE_DIR];
    NSMutableDictionary* environment = [[[NSProcessInfo processInfo] environment] mutableCopy];
    environment[@"MOORING_RELOAD_PARENT"] = [NSString stringWithFormat:@"%d", getpid()];
    lab.server.environment = environment;
    NSError* error = nil;
    if (![lab.server launchAndReturnError:&error])
        LOGF(eWARNING, "Shader reload host: %s", error.localizedDescription.UTF8String);
#endif
}
void exitShaderLab()
{
#if defined(MOORING_SHADER_LAB)
    if (lab.server.running)
        [lab.server terminate];
    lab.server = nil;
    if (!lab.renderer)
        return;
    for (auto& preview : lab.previews)
        releasePreview(preview);
    if (lab.resourcePipeline)
        removePipeline(lab.renderer, lab.resourcePipeline);
    if (lab.resourceShader)
        removeShader(lab.renderer, lab.resourceShader);
    if (lab.target)
        removeRenderTarget(lab.renderer, lab.target);
    for (auto* buffer : lab.uniforms)
        if (buffer)
            removeResource(buffer);
    for (auto* buffer : lab.samples)
        if (buffer)
            removeResource(buffer);
    if (lab.dummyBuffer)
        removeResource(lab.dummyBuffer);
    if (lab.dummyTexture)
        removeResource(lab.dummyTexture);
    if (lab.dummyVolume)
        removeResource(lab.dummyVolume);
    if (lab.sets)
        removeDescriptorSet(lab.renderer, lab.sets);
    lab = {};
#endif
}
bool reloadShaderLab()
{
#if defined(MOORING_SHADER_LAB)
    if (!lab.renderer)
        return true;
    NSDictionary* document = manifest();
    if (!document)
    {
        lab.reloadFailed = true;
        snprintf(lab.applied, sizeof lab.applied, "Preview registry is missing. Rebuild this Debug app.");
        return false;
    }
    std::vector<Preview> pending;
    const bool           firstLoad = lab.previews.empty();
    bool                 selectionRemoved = false;
    const std::string    selectedFunction =
        lab.category == 0 && lab.selected < int(lab.previews.size()) ? lab.previews[lab.selected].function : "";
    const std::string selectedProbe = lab.category == 3 && lab.selected < int(lab.probes.size()) ? lab.probes[lab.selected].name : "";
    bool              valid = true;
    for (NSDictionary* entry in document[@"previews"])
    {
        Preview preview;
        preview.name = string(entry, @"label");
        preview.binary = string(entry, @"binary");
        preview.source = string(entry, @"source");
        preview.function = string(entry, @"function");
        const std::string kind = string(entry, @"kind");
        preview.dimensions = kind == "1D" ? 1 : kind == "3D_SLICE" ? 3 : 2;
        preview.integer = [entry[@"integer"] boolValue];
        preview.line = [entry[@"line"] unsignedIntValue];
        valid &= loadPreview(preview);
        pending.push_back(std::move(preview));
    }
    Preview resource;
    resource.name = "Shader Lab resource";
    resource.binary = "shader_lab_resource.frag";
    valid &= loadPreview(resource);
    if (!valid)
    {
        lab.reloadFailed = true;
        for (auto& preview : pending)
            releasePreview(preview);
        releasePreview(resource);
        snprintf(lab.applied, sizeof lab.applied, "Preview pipeline failed. Previous previews retained.");
        return false;
    }
    for (auto& preview : lab.previews)
        releasePreview(preview);
    lab.previews = std::move(pending);
    if (lab.resourcePipeline)
        removePipeline(lab.renderer, lab.resourcePipeline);
    if (lab.resourceShader)
        removeShader(lab.renderer, lab.resourceShader);
    lab.resourcePipeline = resource.pipeline;
    lab.resourceShader = resource.shader;
    lab.probes.clear();
    for (NSDictionary* entry in document[@"probes"])
        lab.probes.push_back(
            { string(entry, @"label"), string(entry, @"source"), [entry[@"id"] unsignedIntValue], [entry[@"line"] unsignedIntValue] });
    if (!selectedFunction.empty())
    {
        const auto found =
            std::find_if(lab.previews.begin(), lab.previews.end(), [&](const auto& entry) { return entry.function == selectedFunction; });
        lab.selected = found == lab.previews.end() ? 0 : int(found - lab.previews.begin());
        selectionRemoved |= found == lab.previews.end();
    }
    if (!selectedProbe.empty())
    {
        const auto found =
            std::find_if(lab.probes.begin(), lab.probes.end(), [&](const auto& entry) { return entry.name == selectedProbe; });
        lab.selected = found == lab.probes.end() ? 0 : int(found - lab.probes.begin());
        selectionRemoved |= found == lab.probes.end();
    }
    if (firstLoad || selectionRemoved)
        selectionDefaults();
    else if (!lab.frozen)
        changed();
    snprintf(lab.applied, sizeof lab.applied, "%zu function previews, %zu scene probes loaded", lab.previews.size(), lab.probes.size());
    lab.reloadFailed = false;
    LOGF(eINFO, "Shader Lab: %s", lab.applied);
#endif
    return true;
}
void shaderLabRequestReload()
{
#if defined(MOORING_SHADER_LAB)
    platformRequestReload();
#endif
}
bool shaderLabAvailable()
{
#if defined(MOORING_SHADER_LAB)
    return lab.renderer != nullptr;
#else
    return false;
#endif
}
void updateShaderLab(float elapsed)
{
#if defined(MOORING_SHADER_LAB)
    lab.visible = false;
    lab.clock += elapsed;
    if (lab.animate && !lab.frozen)
        lab.time += elapsed;
    if (!lab.autoReload || lab.clock < lab.nextPoll)
        return;
    lab.nextPoll = lab.clock + .35f;
    @autoreleasepool
    {
        NSMutableString* signature = [NSMutableString string];
        NSString*        directory = @MOORING_SOURCE_DIR "/Mooring/Shaders";
        NSArray*         paths = [[[NSFileManager defaultManager] subpathsAtPath:directory] sortedArrayUsingSelector:@selector(compare:)];
        for (NSString* path in paths)
        {
            if (![path hasSuffix:@".fsl"] && ![path hasSuffix:@".srt.h"] && ![path hasSuffix:@".list"])
                continue;
            NSDictionary* attributes =
                [[NSFileManager defaultManager] attributesOfItemAtPath:[directory stringByAppendingPathComponent:path] error:nil];
            [signature
                appendFormat:@"%@:%.9f:%@;", path, [attributes[NSFileModificationDate] timeIntervalSince1970], attributes[NSFileSize]];
        }
        if (lab.watchedSignature && ![signature isEqualToString:lab.watchedSignature])
            lab.changeTime = lab.clock;
        lab.watchedSignature = [signature copy];
    }
    if (lab.changeTime >= 0 && lab.clock - lab.changeTime >= .35f && platformReloadStatus(nullptr, 0) != 1)
    {
        shaderLabRequestReload();
        lab.changeTime = -1;
    }
#endif
}
float4 shaderLabProbeDisplay()
{
#if defined(MOORING_SHADER_LAB)
    return { lab.minimum, lab.maximum, float(lab.component), 0 };
#else
    return { 0, 1, -1, 0 };
#endif
}

#if defined(MOORING_SHADER_LAB)
namespace
{
constexpr float4 HeadingColor{ .55f, .78f, 1, 1 };
constexpr float4 MutedColor{ .68f, .72f, .77f, 1 };
constexpr float4 ReadyColor{ .55f, .85f, .65f, 1 };
constexpr float4 WarningColor{ 1, .77f, .4f, 1 };
constexpr float4 ErrorColor{ 1, .48f, .48f, 1 };

int entryCount(int category)
{
    switch (category)
    {
    case 0:
        return int(lab.previews.size());
    case 1:
        return int(resources.size());
    case 2:
        return int(pipelines.size());
    default:
        return int(lab.probes.size());
    }
}
const std::string& entryName(int category, int index)
{
    switch (category)
    {
    case 0:
        return lab.previews[index].name;
    case 1:
        return resources[index].name;
    case 2:
        return pipelines[index].name;
    default:
        return lab.probes[index].name;
    }
}
const std::string& entryKey(int category, int index) { return category == 0 ? lab.previews[index].function : entryName(category, index); }
std::string        entryTitle(int category, int index)
{
    const auto& name = entryName(category, index);
    // Wrappers can include parameter hints in their labels. Keep those in the inspector.
    return name.substr(0, name.find(" ("));
}
bool containsIgnoringCase(const std::string& text, const char* query)
{
    return std::search(text.begin(), text.end(), query, query + std::strlen(query),
                       [](unsigned char left, unsigned char right) { return std::tolower(left) == std::tolower(right); }) != text.end();
}
void rememberPreview()
{
    if (lab.selected >= entryCount(lab.category))
        return;
    const auto& name = entryKey(lab.category, lab.selected);
    lab.lastEntry[lab.category] = name;
    for (auto& saved : lab.savedPreviews)
        if (saved.category == lab.category && saved.name == name)
        {
            saved.settings = lab;
            return;
        }
    lab.savedPreviews.push_back({ lab.category, name, lab });
}
void selectEntry(int category, int selected)
{
    rememberPreview();
    lab.category = category;
    lab.selected = std::clamp(selected, 0, std::max(0, entryCount(category) - 1));
    if (!entryCount(category))
        return;
    const auto& name = entryKey(category, lab.selected);
    lab.lastEntry[category] = name;
    const auto found = std::find_if(lab.savedPreviews.begin(), lab.savedPreviews.end(),
                                    [&](const auto& saved) { return saved.category == category && saved.name == name; });
    if (found == lab.savedPreviews.end())
        selectionDefaults();
    else
    {
        static_cast<PreviewSettings&>(lab) = found->settings;
        changed();
    }
}
void selectCategory(int category)
{
    int selected = 0;
    for (int index = 0; index < entryCount(category); ++index)
        if (entryKey(category, index) == lab.lastEntry[category])
            selected = index;
    selectEntry(category, selected);
}
unsigned previewDimensions()
{
    if (lab.category == 0)
        return lab.previews[lab.selected].dimensions;
    if (lab.category == 1 && resources[lab.selected].texture && resources[lab.selected].texture->mDepth > 1)
        return 3;
    return 2;
}
void heading(const char* title)
{
    uiLayoutAutoTextRows(1);
    uiColorLabel(title, TF_ALIGN_LEFT, HeadingColor);
}
void muted(const char* text)
{
    uiLayoutAutoTextRows(1);
    uiColorLabel(text, TF_ALIGN_LEFT, MutedColor);
}
bool integerField(const char* title, int& value, int minimum, int maximum)
{
    uiLayoutAutoTextRows(2);
    label(title);
    const int previous = value;
    uiPropertyInt(&value, minimum, maximum, 1);
    return previous != value;
}
void sourceLocation(const std::string& source, unsigned line, float width)
{
    char location[512];
    snprintf(location, sizeof location, "%s:%u", source.substr(source.find_last_of('/') + 1).c_str(), line);
    wrapped(location, width);
}

void drawEntryBrowser(WaterLook& look, float height, float width)
{
    uiLayoutAutoTextRows(1);
    const float top = uiLayoutPeek()[1];
    bstring     query = bfromarr(lab.search[lab.category]);
    uiTextbox("Search", &query, TF_WIDGET_EDIT_FILTER_ASCII);
    if (!query.slen)
        uiBeginWidgetDisable();
    if (button("Clear search"))
        lab.search[lab.category][0] = 0;
    if (!query.slen)
        uiEndWidgetDisable();
    char             count[80];
    std::vector<int> matches;
    for (int index = 0; index < entryCount(lab.category); ++index)
    {
        std::string searchable = entryName(lab.category, index);
        if (lab.category == 0)
            searchable += " " + lab.previews[index].function + " " + lab.previews[index].source;
        if (lab.category == 2)
            searchable += " " + pipelines[index].vertex + " " + pipelines[index].fragment + " " + pipelines[index].compute;
        if (!lab.search[lab.category][0] || containsIgnoringCase(searchable, lab.search[lab.category]))
            matches.push_back(index);
    }
    snprintf(count, sizeof count, "%zu of %d entries", matches.size(), entryCount(lab.category));
    muted(count);
    uiLayoutDynamicRows(std::max(40.0f, height - (uiLayoutPeek()[1] - top) - 16), 1);
    if (UI_GROUP_IS_VISIBLE(uiBeginWidgetGroup("Shader Lab entries", TF_UI_WINDOW_BORDER)))
    {
        uiLayoutAutoTextRows(1);
        for (int index : matches)
        {
            std::string title = entryTitle(lab.category, index);
            const float available = uiLayoutPeek()[2] - 24;
            if (uiGetTextWidth(title.c_str()) > available)
            {
                while (title.size() > 1 && uiGetTextWidth((title + "...").c_str()) > available)
                    title.pop_back();
                title += "...";
            }
            if (toolui::button(title.c_str(), index == lab.selected, true, entryName(lab.category, index).c_str()) && index != lab.selected)
            {
                selectEntry(lab.category, index);
                lab.returnPipeline.clear();
                if (look.debugView >= 1000)
                    look.debugView = 0;
            }
        }
        if (matches.empty())
            wrapped("No matches. Clear the search to see all entries.", width);
        uiEndWidgetGroup();
    }
}

bool fitOutputRange()
{
    if (lab.readbackGeneration != lab.generation)
        return false;
    float minimum = INFINITY, maximum = -INFINITY;
    for (const auto& sample : lab.readback)
        for (int channel = lab.component < 0 ? 0 : lab.component; channel < (lab.component < 0 ? 3 : lab.component + 1); ++channel)
            if (std::isfinite(sample[channel]))
            {
                minimum = std::min(minimum, sample[channel]);
                maximum = std::max(maximum, sample[channel]);
            }
    if (!std::isfinite(minimum))
        return false;
    if (minimum == maximum)
    {
        const float padding = std::max(.001f, std::fabs(minimum) * .05f);
        minimum -= padding;
        maximum += padding;
    }
    lab.minimum = minimum;
    lab.maximum = maximum;
    return true;
}

void drawDisplayControls(unsigned dimensions)
{
    heading("Display");
    const char* channels[] = { "RGB", "X / Red", "Y / Green", "Z / Blue", "W / Alpha" };
    const int   first = dimensions == 1 ? 1 : 0;
    int  component = UI_WIDGET_GET_SELECTED(uiDropdown(channels + first, 5 - first, std::max(0, lab.component + 1 - first))) + first - 1;
    bool edited = component != lab.component;
    lab.component = component;
    edited |= scalar("Minimum", lab.minimum, std::min(-10.0f, lab.minimum), std::max(10.0f, lab.maximum), .01f);
    edited |= scalar("Maximum", lab.maximum, std::min(-10.0f, lab.minimum), std::max(10.0f, lab.maximum), .01f);
    lab.maximum = std::max(lab.minimum + .0001f, lab.maximum);
    uiLayoutAutoTextRows(1);
    if (lab.category < 2)
    {
        const bool hasSamples = lab.readbackGeneration == lab.generation;
        if (!hasSamples && !lab.frozen)
            uiBeginWidgetDisable();
        if (button("Fit output range"))
            edited |= fitOutputRange();
        if (!hasSamples && !lab.frozen)
            uiEndWidgetDisable();
        edited |= UI_WIDGET_IS_CHANGED(uiCheckbox("Signed color ramp", &lab.signedColor));
    }
    if (edited)
        changed();
}

void drawResourceDetails(float width)
{
    const auto& resource = resources[lab.selected];
    char        text[512];
    heading(resource.buffer ? "Buffer layout" : "Texture layout");
    if (resource.buffer)
    {
        const auto& layout = resource.layout;
        const char* scalarType = layout.scalar == ShaderLabScalar::Float  ? "float32"
                                 : layout.scalar == ShaderLabScalar::Half ? "float16"
                                                                          : "uint32";
        snprintf(text, sizeof text, "%u x %u / %s x %u\n%u-byte elements / %.1f KiB", layout.width, layout.height, scalarType,
                 layout.components, layout.stride, double(resource.buffer->mSize) / 1024);
        wrapped(text, width);
        wrapped(layout.channels, width);
    }
    else if (resource.texture)
    {
        const auto* texture = resource.texture;
        snprintf(text, sizeof text, "%u x %u x %u / %u mips\n%s", texture->mWidth, texture->mHeight, texture->mDepth, texture->mMipLevels,
                 TinyImageFormat_Name(TinyImageFormat(texture->mFormat)));
        wrapped(text, width);
    }
}

void drawPipelineDetails(float width)
{
    const auto& pipeline = pipelines[lab.selected];
    char        text[256];
    heading(pipeline.compute.empty() ? "Graphics pipeline" : "Compute pipeline");
    if (!pipeline.vertex.empty())
    {
        muted("Vertex shader");
        wrapped(pipeline.vertex.c_str(), width);
    }
    if (!pipeline.fragment.empty())
    {
        muted("Fragment shader");
        wrapped(pipeline.fragment.c_str(), width);
    }
    if (!pipeline.compute.empty())
    {
        wrapped(pipeline.compute.c_str(), width);
        snprintf(text, sizeof text, "Threads: %lu x %lu x %lu", (unsigned long)pipeline.handle->mNumThreadsPerGroup.width,
                 (unsigned long)pipeline.handle->mNumThreadsPerGroup.height, (unsigned long)pipeline.handle->mNumThreadsPerGroup.depth);
        wrapped(text, width);
    }
    else
    {
        snprintf(text, sizeof text, "Depth test: %s\nDepth write: %s\nBlending: %s", pipeline.depthTest ? "on" : "off",
                 pipeline.depthWrite ? "on" : "off", pipeline.blend ? "on" : "off");
        wrapped(text, width);
    }
}

void drawInspector(float width)
{
    if (lab.category == 2)
    {
        drawPipelineDetails(width);
        return;
    }
    if (lab.category == 1 && !resources[lab.selected].readable)
    {
        drawResourceDetails(width);
        return;
    }
    const unsigned dimensions = previewDimensions();
    uiLayoutAutoTextRows(1);
    if (button("Reset this preview"))
        selectionDefaults();
    const bool frozen = lab.frozen && lab.category < 2;
    if (frozen)
        uiBeginWidgetDisable();
    drawDisplayControls(dimensions);
    if (lab.category == 3)
    {
        const auto& probe = lab.probes[lab.selected];
        heading("Source");
        sourceLocation(probe.source, probe.line, width);
        if (frozen)
            uiEndWidgetDisable();
        return;
    }
    bool edited = false;
    if (lab.category == 1)
    {
        const auto& resource = resources[lab.selected];
        heading("Subresource");
        int            previousMip = lab.mip, previousBand = lab.band;
        const unsigned mips = resource.buffer ? resource.layout.mips : resource.texture->mMipLevels;
        if (resource.buffer && resource.layout.bands > 1)
            integerField("Ocean band", lab.band, 0, int(resource.layout.bands) - 1);
        if (mips > 1)
            integerField("Mip level", lab.mip, 0, int(mips) - 1);
        else
            muted("Base level only");
        if (previousBand != lab.band || previousMip != lab.mip)
        {
            if (resource.buffer)
            {
                lab.origin = { 0, 0, 0 };
                lab.extent = { float(std::max(1u, resource.layout.width >> lab.mip)),
                               float(std::max(1u, resource.layout.height >> lab.mip)), 1 };
            }
            edited = true;
        }
    }
    if (dimensions == 3)
    {
        heading("Volume slice");
        const char* planes[] = { "XY plane / fixed Z", "XZ plane / fixed Y", "YZ plane / fixed X" };
        const int   plane = UI_WIDGET_GET_SELECTED(uiDropdown(planes, 3, lab.sliceAxis));
        edited |= plane != lab.sliceAxis;
        lab.sliceAxis = plane;
        const bool texture = lab.category == 1 && resources[lab.selected].texture;
        edited |= scalar("Slice", lab.slice, texture ? 0 : -10000, texture ? 1 : 10000, texture ? .01f : .1f);
    }
    heading("Canvas");
    const char* resolutions2D[] = { "128 x 128 samples", "256 x 256 samples", "512 x 512 samples" };
    const char* resolutions1D[] = { "128 samples", "256 samples", "512 samples" };
    const int   resolution = 128 << UI_WIDGET_GET_SELECTED(uiDropdown(dimensions == 1 ? resolutions1D : resolutions2D, 3,
                                                                    lab.resolution == 128   ? 0
                                                                      : lab.resolution == 256 ? 1
                                                                                              : 2));
    edited |= resolution != lab.resolution;
    lab.resolution = resolution;
    edited |= scalar("Zoom", lab.zoom, 1, 32, .25f);
    edited |= scalar("Pan X", lab.panX, 0, 1, .01f);
    if (dimensions != 1)
        edited |= scalar("Pan Y", lab.panY, 0, 1, .01f);
    uiLayoutAutoTextRows(1);
    if (button("Reset zoom and pan"))
    {
        lab.zoom = 1;
        lab.panX = lab.panY = 0;
        edited = true;
    }
    heading("Input domain");
    edited |= UI_WIDGET_IS_CHANGED(uiCheckbox("Integer coordinates", &lab.integer));
    const char* axes[] = { "X", "Y", "Z" };
    char        text[128];
    for (unsigned axis = 0; axis < dimensions; ++axis)
    {
        snprintf(text, sizeof text, "Origin %s", axes[axis]);
        edited |= scalar(text, lab.origin[axis], -10000, 10000, .1f);
        snprintf(text, sizeof text, "Span %s", axes[axis]);
        edited |= scalar(text, lab.extent[axis], .001f, dimensions == 3 ? 10000 : 512, .1f);
    }
    if (lab.category == 0)
    {
        heading("Time and parameters");
        edited |= UI_WIDGET_IS_CHANGED(uiCheckbox("Animate preview time", &lab.animate));
        edited |= scalar("Time (seconds)", lab.time, 0, std::max(120.0f, lab.time), .1f);
        const auto& preview = lab.previews[lab.selected];
        const auto  hint = preview.name.find(" (");
        if (hint != std::string::npos)
            wrapped(preview.name.substr(hint + 1).c_str(), width);
        const char* parameters[] = { "Parameter X", "Parameter Y", "Parameter Z", "Parameter W" };
        for (unsigned parameter = 0; parameter < 4; ++parameter)
            edited |= scalar(parameters[parameter], lab.parameters[parameter], -10, 10, .01f);
        heading("Source");
        wrapped(preview.function.c_str(), width);
        sourceLocation(preview.source, preview.line, width);
    }
    else
        drawResourceDetails(width);
    muted("Magenta marks NaN or infinity.");
    if (edited)
        changed();
    if (frozen)
        uiEndWidgetDisable();
}

void drawSampleValues(float width)
{
    char text[256];
    heading("Selected sample / raw GPU output");
    if (lab.readbackGeneration != lab.generation)
    {
        muted("Waiting for GPU results...");
        return;
    }
    const auto& value = lab.readback[size_t(lab.sampleY) * lab.allocatedWidth + lab.sampleX];
    uiLayoutAutoTextRows(2);
    const char* channels[] = { "X / R", "Y / G", "Z / B", "W / A" };
    for (unsigned channel = 0; channel < 4; ++channel)
    {
        snprintf(text, sizeof text, "%s: %.7g", channels[channel], value[channel]);
        label(text);
    }
    const auto& controls = lab.completed;
    const float horizontal = (lab.sampleX + .5f) / lab.allocatedWidth;
    const float vertical = (lab.sampleY + .5f) / lab.allocatedHeight;
    float3      coordinate{ controls.origin[0] + horizontal * controls.extent[0], controls.origin[1] + vertical * controls.extent[1],
                       controls.settings[2] };
    if (controls.extent[3] == 3 && controls.settings[1] == 1)
        coordinate = { coordinate[0], controls.settings[2], controls.origin[2] + vertical * controls.extent[2] };
    if (controls.extent[3] == 3 && controls.settings[1] == 2)
        coordinate = { controls.settings[2], controls.origin[1] + horizontal * controls.extent[1],
                       controls.origin[2] + vertical * controls.extent[2] };
    if (controls.settings[0])
        for (unsigned axis = 0; axis < 3; ++axis)
            coordinate[axis] = std::floor(coordinate[axis]);
    if (controls.extent[3] == 1)
        snprintf(text, sizeof text, "Input X: %.6g  |  Time: %.4g s", coordinate[0], controls.origin[3]);
    else if (controls.extent[3] == 2)
        snprintf(text, sizeof text, "Input: %.6g, %.6g  |  Time: %.4g s", coordinate[0], coordinate[1], controls.origin[3]);
    else
        snprintf(text, sizeof text, "Input: %.6g, %.6g, %.6g\nTime: %.4g s", coordinate[0], coordinate[1], coordinate[2],
                 controls.origin[3]);
    wrapped(text, width);
}

void drawCanvas(float width, float height)
{
    ensureTarget();
    const unsigned dimensions = previewDimensions();
    const float    top = uiLayoutPeek()[1];
    uiLayoutAutoTextRows(2);
    if (button(lab.frozen ? "Resume preview" : "Freeze preview"))
    {
        lab.frozen = !lab.frozen;
        lab.freezeRendered = false;
    }
    uiColorLabel(lab.frozen ? "Frozen" : "Live", TF_ALIGN_RIGHT, lab.frozen ? WarningColor : ReadyColor);
    const float rowHeight = uiLayoutPeek()[3] + 4;
    // Keep sample controls and values visible while the inspector scrolls independently.
    // Controls, heading, two value rows and input coordinates need five rows.
    // Curves add axis labels; 3D inputs put time on a separate line.
    const float sampleRows = dimensions == 2 ? 5 : 6;
    const float imageHeight = std::max(rowHeight * 2, height - (uiLayoutPeek()[1] - top) - rowHeight * sampleRows - 24);
    lab.sampleX = std::clamp(lab.sampleX, 0, int(lab.allocatedWidth) - 1);
    lab.sampleY = dimensions == 1 ? 0 : std::clamp(lab.sampleY, 0, int(lab.allocatedHeight) - 1);
    if (dimensions == 1)
    {
        const unsigned component = unsigned(std::max(0, lab.component));
        for (unsigned sample = 0; sample < lab.allocatedWidth; ++sample)
            lab.curve[sample] = lab.readbackGeneration == lab.generation ? lab.readback[sample][component] : 0;
        uiLayoutDynamicRows(imageHeight, 1);
        uiPlotLines(lab.curve.data(), lab.curve.size(), lab.minimum, lab.maximum, false);
        muted("Horizontal: input X / Vertical: output");
    }
    else
    {
        uiLayoutSpaceBegin(TF_LAYOUT_STATIC, imageHeight, 1);
        const float side = std::min(imageHeight, uiLayoutSpaceBounds()[2]);
        uiLayoutSpacePush(vec2(std::max(0.0f, (uiLayoutSpaceBounds()[2] - side) * .5f), 0), vec2(side, side));
        float2       picked{};
        const float2 selected{ (lab.sampleX + .5f) / lab.allocatedWidth, (lab.sampleY + .5f) / lab.allocatedHeight };
        if (uiDebugTexturePick(lab.target->pTexture, &picked, &selected))
        {
            lab.sampleX = std::clamp(int(picked[0] * lab.allocatedWidth), 0, int(lab.allocatedWidth) - 1);
            lab.sampleY = std::clamp(int(picked[1] * lab.allocatedHeight), 0, int(lab.allocatedHeight) - 1);
        }
        uiLayoutSpaceEnd();
    }
    uiLayoutAutoTextRows(dimensions == 1 ? 2 : 4);
    label("Column");
    uiPropertyInt(&lab.sampleX, 0, int(lab.allocatedWidth) - 1, 1);
    if (dimensions != 1)
    {
        label("Row");
        uiPropertyInt(&lab.sampleY, 0, int(lab.allocatedHeight) - 1, 1);
    }
    drawSampleValues(width);
}

void drawPipelineBindings(float width)
{
    const auto& pipeline = pipelines[lab.selected];
    heading("Bound resources");
    muted("Bindings from the most recent use.");
    if (pipeline.bindings.empty())
        wrapped("No bindings recorded yet. This pipeline may not have run in the current scene.", width);
    char text[512];
    for (const auto& binding : pipeline.bindings)
    {
        const auto resource = std::find_if(resources.begin(), resources.end(), [&](const auto& item)
                                           { return item.buffer == binding.handle || item.texture == binding.handle; });
        snprintf(text, sizeof text, "%s / set %u [%u], slot %u", binding.name.c_str(), binding.set, binding.index, binding.slot);
        wrapped(text, width);
        if (resource != resources.end())
        {
            uiLayoutAutoTextRows(1);
            if (button(resource->name.c_str()))
            {
                const auto pipelineName = pipeline.name;
                selectEntry(1, int(resource - resources.begin()));
                lab.returnPipeline = pipelineName;
                lab.search[1][0] = 0;
                return;
            }
        }
        else
            muted("Resource has no registered preview.");
    }
}

void drawSceneProbe(WaterLook& look, float width)
{
    const auto& probe = lab.probes[lab.selected];
    const bool  active = look.debugView == int(probe.id);
    heading(active ? "Probe is active in the scene" : "Preview at the water surface");
    wrapped("This probe replaces water shading with the selected value.", width);
    uiLayoutAutoTextRows(1);
    if (button(active ? "Hide lab to view scene" : "Show probe and hide lab"))
    {
        look.debugView = int(probe.id);
        lab.open = false;
    }
    if (button("Show probe with lab open"))
        look.debugView = int(probe.id);
    if (!active)
        uiBeginWidgetDisable();
    if (button("Restore normal shading"))
        look.debugView = 0;
    if (!active)
        uiEndWidgetDisable();
    heading("Reading the result");
    wrapped("A checkerboard marks fragments that do not reach this probe. Magenta marks NaN or infinity.", width);
    wrapped("Function previews evaluate an isolated input domain.", width);
}

void drawBuildLog(unsigned width, unsigned height)
{
    if (!toolui::beginWindow("Shader Lab / Build log", lab.showBuildLog, lab.buildLogWindow, width, height, 640, 400, 380, 220, false,
                             true))
        return;
    const float panelWidth = lab.buildLogWindow.size.x;
    wrapped(lab.applied, panelWidth);
    wrapped(lab.message, panelWidth);
    uiEndWidgetWindow();
}
} // namespace
#endif

void setShaderLabOpen(bool open)
{
#if defined(MOORING_SHADER_LAB)
    lab.open = open;
#endif
}
bool shaderLabIsOpen()
{
#if defined(MOORING_SHADER_LAB)
    return lab.open;
#else
    return false;
#endif
}
void drawShaderLab(WaterLook& look, unsigned width, unsigned height)
{
#if defined(MOORING_SHADER_LAB)
    if (look.debugView >= 1000 &&
        std::none_of(lab.probes.begin(), lab.probes.end(), [&](const auto& probe) { return probe.id == unsigned(look.debugView); }))
        look.debugView = 0;
    if (!lab.open)
    {
        lab.showBuildLog = false;
        lab.window.wasOpen = false;
        lab.buildLogWindow.wasOpen = false;
        return;
    }
    float dpi[2];
    getMonitorDpiScale(getActiveMonitorIdx(), dpi);
    if (toolui::beginWindow("Shader Lab", lab.open, lab.window, width, height, 900, 660, 740, 420, true))
    {
        const char* categories[] = { "Functions", "Resources", "Pipelines", "Scene probes" };
        uiLayoutAutoTextRows(4);
        for (int category = 0; category < 4; ++category)
        {
            if (toolui::button(categories[category], category == lab.category) && category != lab.category)
            {
                selectCategory(category);
                if (look.debugView >= 1000)
                    look.debugView = 0;
            }
        }
        const int reloadState = platformReloadStatus(lab.message, sizeof lab.message);
        uiLayoutAutoTextRows(3);
        if (reloadState == 1 || reloadState < 0)
            uiBeginWidgetDisable();
        if (button(reloadState == 1 ? "Compiling..." : "Reload shaders"))
            shaderLabRequestReload();
        if (reloadState == 1 || reloadState < 0)
            uiEndWidgetDisable();
        uiCheckbox("Auto reload on save", &lab.autoReload);
        if (button("Build log"))
            lab.showBuildLog = !lab.showBuildLog;
        uiLayoutAutoTextRows(1);
        const bool failed = reloadState == 2 || reloadState < 0 || lab.reloadFailed;
        char       status[256];
        if (failed)
            snprintf(status, sizeof status, "Reload failed / previous shaders retained. Open Build log for details.");
        else if (reloadState == 1)
            snprintf(status, sizeof status, "Compiling shaders / preview uses the previous build.");
        else
            snprintf(status, sizeof status, "Ready / %zu functions / %zu resources / %zu pipelines", lab.previews.size(), resources.size(),
                     pipelines.size());
        uiColorLabel(status, TF_ALIGN_LEFT, failed ? ErrorColor : reloadState == 1 ? WarningColor : ReadyColor);

        lab.selected = std::clamp(lab.selected, 0, std::max(0, entryCount(lab.category) - 1));
        const float bodyWidth = uiLayoutPeek()[2];
        const float bodyHeight = std::max(100.0f, lab.window.position.y + lab.window.size.y - uiLayoutPeek()[1] - 22 * dpi[1]);
        const float gap = 6 * dpi[0];
        const float browserWidth = std::min(190 * dpi[0], bodyWidth * .22f);
        const bool  compact = bodyWidth < 720 * dpi[0];
        const float inspectorWidth = compact ? 0 : std::min(270 * dpi[0], bodyWidth * .31f);
        const float canvasWidth = bodyWidth - browserWidth - gap - (compact ? 0 : inspectorWidth + gap);
        uiLayoutSpaceBegin(TF_LAYOUT_STATIC, bodyHeight, compact ? 2 : 3);
        uiLayoutSpacePush(vec2(0, 0), vec2(browserWidth, bodyHeight));
        if (UI_GROUP_IS_VISIBLE(uiBeginWidgetGroup("Shader Lab browser", TF_UI_WINDOW_NO_SCROLLBAR)))
        {
            drawEntryBrowser(look, bodyHeight, browserWidth);
            uiEndWidgetGroup();
        }
        // Controls run before the canvas: resizing its target must precede queuing the UI image.
        if (!compact)
        {
            uiLayoutSpacePush(vec2(bodyWidth - inspectorWidth, 0), vec2(inspectorWidth, bodyHeight));
            if (UI_GROUP_IS_VISIBLE(uiBeginWidgetGroup("Shader Lab inspector", TF_UI_WINDOW_BORDER)))
            {
                if (entryCount(lab.category))
                    drawInspector(inspectorWidth);
                uiEndWidgetGroup();
            }
        }
        uiLayoutSpacePush(vec2(browserWidth + gap, 0), vec2(canvasWidth, bodyHeight));
        if (UI_GROUP_IS_VISIBLE(uiBeginWidgetGroup("Shader Lab preview", TF_UI_WINDOW_BORDER)))
        {
            uiLayoutAutoTextRows(1);
            const float top = uiLayoutPeek()[1];
            if (!entryCount(lab.category))
                wrapped("No entries are registered. Add a preview wrapper and reload shaders.", canvasWidth);
            else
            {
                uiColorLabel(entryTitle(lab.category, lab.selected).c_str(), TF_ALIGN_LEFT, HeadingColor);
                if (compact && button(lab.controlsOnly ? "Back to preview" : "Show controls"))
                    lab.controlsOnly = !lab.controlsOnly;
                if (compact && lab.controlsOnly)
                    drawInspector(canvasWidth);
                else if (lab.category == 2)
                    drawPipelineBindings(canvasWidth);
                else if (lab.category == 3)
                    drawSceneProbe(look, canvasWidth);
                else
                {
                    if (lab.category == 1 && !lab.returnPipeline.empty() && button("Back to pipeline"))
                    {
                        for (int index = 0; index < int(pipelines.size()); ++index)
                            if (pipelines[index].name == lab.returnPipeline)
                            {
                                selectEntry(2, index);
                                break;
                            }
                    }
                    if (lab.category == 1 && !resources[lab.selected].readable)
                        wrapped(
                            "This attachment lives only in GPU tile memory or cannot be sampled. Its layout is available in the inspector.",
                            canvasWidth);
                    else if (lab.category < 2)
                    {
                        lab.visible = true;
                        drawCanvas(canvasWidth, bodyHeight - (uiLayoutPeek()[1] - top) - 12);
                    }
                }
            }
            uiEndWidgetGroup();
        }
        uiLayoutSpaceEnd();
        uiEndWidgetWindow();
    }
    drawBuildLog(width, height);
#endif
}

int runShaderLabQA()
{
#if defined(MOORING_SHADER_LAB)
    struct ResourceCase
    {
        const char* name;
        int         mip, band;
    };
    static const ResourceCase                               cases[] = { { "Water / Surface (current)", 0, 0 },
                                                                        { "Water / Surface (current)", 3, 3 },
                                                                        { "Water / Normals", 8, 2 },
                                                                        { "Whitewater / Current", 2, 1 },
                                                                        { "Whitewater / Local current", 0, 0 },
                                                                        { "Foam / Current", 0, 0 },
                                                                        { "Particles / Current", 0, 0 },
                                                                        { "Ocean / Twiddles", 0, 0 },
                                                                        { "Water / Grid indices 0", 0, 0 },
                                                                        { "Sky / Radiance", 2, 0 },
                                                                        { "Clouds / Volume noise", 0, 0 },
                                                                        { "Water HDR composite", 0, 0 } };
    static unsigned                                         step = 0, frames = 0;
    static bool                                             started = false, requestedReload = false;
    static float                                            reloadStarted = 0;
    static std::vector<std::pair<std::string, const void*>> originalResources;
    lab.visible = true;
    const unsigned count = unsigned(lab.previews.size()) + TF_ARRAY_COUNT(cases);
    if (step < count)
    {
        if (!started)
        {
            lab.category = step < lab.previews.size() ? 0 : 1;
            lab.selected = int(step);
            if (lab.category == 1)
            {
                const auto& test = cases[step - lab.previews.size()];
                const auto  found =
                    std::find_if(resources.begin(), resources.end(), [&](const auto& entry) { return entry.name == test.name; });
                if (found == resources.end())
                    return 0;
                lab.selected = int(found - resources.begin());
            }
            selectionDefaults();
            lab.resolution = 128;
            if (lab.category == 1)
            {
                const auto& test = cases[step - lab.previews.size()];
                const auto& resource = resources[lab.selected];
                lab.mip = test.mip;
                lab.band = test.band;
                if (resource.buffer)
                {
                    lab.mip = std::min(lab.mip, int(resource.layout.mips) - 1);
                    lab.extent = { float(std::max(1u, resource.layout.width >> lab.mip)),
                                   float(std::max(1u, resource.layout.height >> lab.mip)), 1 };
                }
            }
            ensureTarget();
            LOGF(eINFO, "Shader Lab QA sampling: %s",
                 lab.category == 0 ? lab.previews[lab.selected].name.c_str() : resources[lab.selected].name.c_str());
            frames = 0;
            started = true;
        }
        if (++frames < 8 || lab.readbackGeneration != lab.generation)
            return frames < 120 ? 0 : -1;
        for (const auto& value : lab.readback)
            for (unsigned component = 0; component < 4; ++component)
                if (!std::isfinite(value[component]))
                {
                    LOGF(eERROR, "Shader Lab QA found nonfinite GPU output at step %u", step);
                    return -1;
                }
        if (lab.category == 0 && lab.previews[lab.selected].function == "previewWaterHash")
        {
            LOGF(eINFO, "Shader Lab QA hash origin: %.9g %.9g %.9g %.9g", lab.readback[0][0], lab.readback[0][1], lab.readback[0][2],
                 lab.readback[0][3]);
            if (lab.readback[0][0] != 0 || lab.readback[0][3] != 1)
                return -1;
            const auto range = std::minmax_element(lab.readback.begin(), lab.readback.end(),
                                                   [](const auto& left, const auto& right) { return left[0] < right[0]; });
            LOGF(eINFO, "Shader Lab QA hash range: %.9g %.9g", (*range.first)[0], (*range.second)[0]);
            if ((*range.second)[0] < .9f || (*range.second)[0] >= 1)
                return -1;
        }
        LOGF(eINFO, "Shader Lab QA GPU values: %.7g %.7g %.7g %.7g", lab.readback[0][0], lab.readback[0][1], lab.readback[0][2],
             lab.readback[0][3]);
        ++step;
        started = false;
        return 0;
    }
    if (!requestedReload)
    {
        if (pipelines.empty() || resources.empty() || descriptors.empty())
            return -1;
        for (const auto& resource : resources)
            originalResources.push_back({ resource.name, resource.buffer ? static_cast<void*>(resource.buffer) : resource.texture });
        requestedReload = true;
        reloadStarted = lab.clock;
        shaderLabRequestReload();
        return 0;
    }
    int state = platformReloadStatus(nullptr, 0);
    if (state == 1 || lab.clock - reloadStarted < 2)
        return lab.clock - reloadStarted < 120 ? 0 : -1;
    if (state != 0)
        return -1;
    for (const auto& saved : originalResources)
    {
        const auto found = std::find_if(resources.begin(), resources.end(), [&](const auto& entry) { return entry.name == saved.first; });
        // Ping-pong handles can swap with the frame; stable render targets must survive reload.
        if (found == resources.end() || (found->texture && found->texture != saved.second))
            return -1;
    }
    return 1;
#else
    return -1;
#endif
}

void completeShaderLabFrame(unsigned frame)
{
#if defined(MOORING_SHADER_LAB)
    frame %= 2;
    if (!lab.pending[frame])
        return;
    lab.pending[frame] = false;
    if (lab.submittedGeneration[frame] != lab.generation)
        return;
    std::memcpy(static_cast<void*>(lab.readback.data()), lab.samples[frame]->pCpuMappedAddress, lab.readback.size() * sizeof(float4));
    lab.completed = lab.submitted[frame];
    lab.readbackGeneration = lab.generation;
#endif
}

void renderShaderLab(TFCmd* command, unsigned frame, OceanRenderer* ocean)
{
#if defined(MOORING_SHADER_LAB)
    if (!lab.visible || !lab.target || lab.category > 1 || (lab.frozen && lab.freezeRendered))
        return;
    if (lab.category == 0 && lab.selected >= int(lab.previews.size()))
        return;
    if (lab.category == 1 && (lab.selected >= int(resources.size()) || !resources[lab.selected].readable))
        return;
    frame %= 2;
    ShaderLabParameters parameters{};
    parameters.origin = { lab.origin[0], lab.origin[1], lab.origin[2], lab.time };
    parameters.extent = { lab.extent[0], lab.extent[1], lab.extent[2], 2 };
    parameters.settings = { float(lab.integer), float(lab.sliceAxis), lab.slice, 0 };
    parameters.display = { lab.minimum, lab.maximum, float(lab.component), float(lab.signedColor) };
    parameters.parameters = lab.parameters;
    parameters.outputSize = { lab.allocatedWidth, lab.allocatedHeight, 0, 0 };
    TFBuffer*   sourceBuffer = lab.dummyBuffer;
    TFTexture*  sourceTexture = lab.dummyTexture;
    TFTexture*  sourceVolume = lab.dummyVolume;
    TFPipeline* pipeline = lab.resourcePipeline;
    if (lab.category == 0)
    {
        const auto& preview = lab.previews[lab.selected];
        parameters.extent[3] = float(preview.dimensions);
        pipeline = preview.pipeline;
    }
    else
    {
        const auto& resource = resources[lab.selected];
        if (resource.buffer)
        {
            sourceBuffer = resource.buffer;
            const auto& layout = resource.layout;
            unsigned    level = std::min(unsigned(lab.mip), layout.mips - 1), band = std::min(unsigned(lab.band), layout.bands - 1);
            unsigned    width = std::max(1u, layout.width >> level), height = std::max(1u, layout.height >> level);
            uint64_t    preceding = 0;
            for (unsigned mip = 0; mip < level; ++mip)
                preceding += uint64_t(layout.bands) * std::max(1u, layout.width >> mip) * std::max(1u, layout.height >> mip);
            uint64_t first = (preceding + uint64_t(band) * width * height) * layout.stride / 4;
            if (layout.stride < 4 || first >= sourceBuffer->mSize / 4)
                return;
            unsigned count =
                unsigned(std::min<uint64_t>(uint64_t(width) * height, (sourceBuffer->mSize / 4 - first) / (layout.stride / 4)));
            parameters.settings[3] = 1;
            parameters.bufferLayout = { unsigned(first), width, layout.stride / 4, count };
            parameters.texture = { 0, 0,
                                   float(layout.scalar == ShaderLabScalar::Half   ? 1
                                         : layout.scalar == ShaderLabScalar::Uint ? 2
                                                                                  : 0),
                                   float(layout.components) };
        }
        else if (resource.texture)
        {
            bool volume = resource.texture->mDepth > 1;
            if (volume)
                sourceVolume = resource.texture;
            else
                sourceTexture = resource.texture;
            parameters.settings[3] = volume ? 3 : 2;
            parameters.extent[3] = volume ? 3 : 2;
            parameters.texture[0] = float(std::min(unsigned(lab.mip), unsigned(resource.texture->mMipLevels) - 1));
        }
    }
    if (!pipeline)
        return;
    // Zoom and pan operate in the selected input plane, including volume slices.
    const unsigned horizontalAxis = parameters.extent[3] == 3 && lab.sliceAxis == 2 ? 1 : 0;
    const unsigned verticalAxis = parameters.extent[3] == 3 && lab.sliceAxis != 0 ? 2 : 1;
    parameters.origin[horizontalAxis] += lab.panX * parameters.extent[horizontalAxis] * (1 - 1 / lab.zoom);
    parameters.origin[verticalAxis] += lab.panY * parameters.extent[verticalAxis] * (1 - 1 / lab.zoom);
    parameters.extent[horizontalAxis] /= lab.zoom;
    parameters.extent[verticalAxis] /= lab.zoom;
    TFBufferUpdateDesc update{ lab.uniforms[frame] };
    beginUpdateResource(&update);
    std::memcpy(update.pMappedData, &parameters, sizeof parameters);
    endUpdateResource(&update);
    TFDescriptorData data[5]{};
    data[0].mIndex = SRT_RES_IDX(ShaderLabResources, PerDraw, gShaderLab);
    data[0].ppBuffers = &lab.uniforms[frame];
    data[1].mIndex = SRT_RES_IDX(ShaderLabResources, PerDraw, gPreviewSamples);
    data[1].ppBuffers = &lab.samples[frame];
    data[2].mIndex = SRT_RES_IDX(ShaderLabResources, PerDraw, gPreviewBuffer);
    data[2].ppBuffers = &sourceBuffer;
    data[3].mIndex = SRT_RES_IDX(ShaderLabResources, PerDraw, gPreviewTexture);
    data[3].ppTextures = &sourceTexture;
    data[4].mIndex = SRT_RES_IDX(ShaderLabResources, PerDraw, gPreviewVolume);
    data[4].ppTextures = &sourceVolume;
    updateDescriptorSet(lab.renderer, frame, lab.sets, 5, data);
    cmdBindRenderTargets(command, nullptr);
    TFRenderTargetBarrier barrier{ lab.target, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET };
    cmdResourceBarrier(command, 0, nullptr, 0, nullptr, 1, &barrier);
    TFBindRenderTargetsDesc targets{};
    targets.mRenderTargetCount = 1;
    targets.mRenderTargets[0] = { lab.target, TF_LOAD_ACTION_CLEAR };
    cmdBindRenderTargets(command, &targets);
    cmdSetViewport(command, 0, 0, lab.allocatedWidth, lab.allocatedHeight, 0, 1);
    cmdSetScissor(command, 0, 0, lab.allocatedWidth, lab.allocatedHeight);
    cmdBindPipeline(command, pipeline);
    bindOceanShaderLabResources(ocean, command, frame);
    cmdBindDescriptorSet(command, frame, lab.sets);
    cmdDraw(command, 3, 0);
    cmdBindRenderTargets(command, nullptr);
    barrier.mCurrentState = TF_RESOURCE_STATE_RENDER_TARGET;
    barrier.mNewState = TF_RESOURCE_STATE_SHADER_RESOURCE;
    cmdResourceBarrier(command, 0, nullptr, 0, nullptr, 1, &barrier);
    lab.submitted[frame] = parameters;
    lab.submittedGeneration[frame] = lab.generation;
    lab.pending[frame] = true;
    lab.freezeRendered = true;
#endif
}
} // namespace mooring
