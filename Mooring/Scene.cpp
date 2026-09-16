#include "Water/ShaderLab.h"
#include "Scene.h"
#include "GraphicsMath.h"
#include "Simulation/Hydrodynamics.h"
#include "Water/OceanRenderer.h"
#include "Water/WaterProfile.h"
#include "Tools/Mooring/TracyMetal.h"
#include "Common/Graphics/Interfaces/IGraphics.h"
#include "Common/Resources/ResourceLoader/Interfaces/IResourceLoader.h"
#include "Common/Graphics/FSL/defaults.h"
#include "Shaders/Scene.srt.h"
#include "Shaders/Post.srt.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "Common/Utilities/Interfaces/IMemory.h"

namespace mooring
{
struct Vertex
{
    Vec3  position, normal, color;
    float material;
};
struct ScenePrograms
{
    TFShader*   shader;
    TFPipeline* pipeline;
    TFPipeline* shadowPipeline;
    TFShader*   postShader;
    TFPipeline* copyPipeline;
    TFPipeline* postPipeline;
    TFPipeline* airPipeline;
};
struct Scene
{
    ScenePrograms    programs;
    TFRenderer*      renderer;
    TFDescriptorSet* descriptors;
    TFDescriptorSet* postDescriptors;
    TFSampler*       sampler;
    TFBuffer*        vertices[2];
    TFBuffer*        cameras[2][3];
    TFBuffer*        postCameras[2][6];
    TFRenderTarget*  depth;
    TFRenderTarget*  opaque;
    TFRenderTarget*  composite;
    TFRenderTarget*  airRays;
    TFRenderTarget*  reflection;
    TFRenderTarget*  reflectionDepth;
    TFRenderTarget*  shadow;
    TFRenderTarget*  shadowDepth;
    TFRenderTarget*  waterLight;
    TFRenderTarget*  waterLightDepth;
    TFRenderTarget*  bloom[3];
    Vertex           mesh[8192];
    unsigned         count, skyHeight;
};
static void uploadScene(TFBuffer* buffer, const void* data, size_t size)
{
    MTRACY_ZONE("uploadScene");
    TFBufferUpdateDesc update = { buffer };
    beginUpdateResource(&update);
    memcpy(update.pMappedData, data, size);
    endUpdateResource(&update);
}
static TFRenderTarget* sceneTarget(Scene* scene, unsigned width, unsigned height, TinyImageFormat format, bool depth, const char* name,
                                   bool transient = false)
{
    TFRenderTargetDesc desc = {};
    desc.mWidth = width;
    desc.mHeight = height;
    desc.mDepth = desc.mArraySize = 1;
    desc.mSampleCount = TF_SAMPLE_COUNT_1;
    desc.mFormat = format;
    desc.pName = name;
    desc.mClearValue = depth ? TFClearValue{ 1, 0, 0, 0 } : TFClearValue{ 0, 0, 0, 0 };
    desc.mStartState = depth ? TF_RESOURCE_STATE_DEPTH_WRITE : TF_RESOURCE_STATE_SHADER_RESOURCE;
    if (transient)
        desc.mFlags = TF_TEXTURE_CREATION_FLAG_ON_TILE;
    if (!depth)
        desc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
    TFRenderTarget* target = nullptr;
    addRenderTarget(scene->renderer, &desc, &target);
    shaderLabRegisterTexture(scene, name, target->pTexture, !depth && !transient);
    return target;
}
static void transitionTarget(TFCmd* cmd, TFRenderTarget* target, bool render)
{
    TFRenderTargetBarrier targetBarrier = { target, render ? TF_RESOURCE_STATE_SHADER_RESOURCE : TF_RESOURCE_STATE_RENDER_TARGET,
                                            render ? TF_RESOURCE_STATE_RENDER_TARGET : TF_RESOURCE_STATE_SHADER_RESOURCE };
    cmdResourceBarrier(cmd, 0, nullptr, 0, nullptr, 1, &targetBarrier);
}
static void bindSceneTarget(TFCmd* cmd, TFRenderTarget* target, TFRenderTarget* depth, TFLoadActionType action,
                            TFLoadActionType depthAction = TF_LOAD_ACTION_CLEAR)
{
    TFBindRenderTargetsDesc bind = {};
    bind.mRenderTargetCount = 1;
    bind.mRenderTargets[0] = { target, action };
    if (depth)
        bind.mDepthStencil = { depth, depthAction };
    cmdBindRenderTargets(cmd, &bind);
    cmdSetViewport(cmd, 0, 0, target->mWidth, target->mHeight, 0, 1);
    cmdSetScissor(cmd, 0, 0, target->mWidth, target->mHeight);
}
static void triangle(Scene* scene, Vec3 vertexA, Vec3 vertexB, Vec3 vertexC, Vec3 color, float material = 0)
{
    ASSERT(scene->count + 3 <= TF_ARRAY_COUNT(scene->mesh));
    auto normal = fromForge(f3Normalize(f3Cross(f3Sub(toForge(vertexB), toForge(vertexA)), f3Sub(toForge(vertexC), toForge(vertexA)))));
    scene->mesh[scene->count++] = { vertexA, normal, color, material };
    scene->mesh[scene->count++] = { vertexB, normal, color, material };
    scene->mesh[scene->count++] = { vertexC, normal, color, material };
}
static void box(Scene* scene, Vec3 center, Vec3 size, Vec3 color, const Snapshot* boat = nullptr, float material = 0)
{
    Vec3 corners[8];
    for (unsigned cornerIndex = 0; cornerIndex < 8; ++cornerIndex)
    {
        corners[cornerIndex] = { center.x + (cornerIndex & 1 ? 1 : -1) * size.x * .5f, center.y + (cornerIndex & 2 ? 1 : -1) * size.y * .5f,
                                 center.z + (cornerIndex & 4 ? 1 : -1) * size.z * .5f };
        if (boat)
            corners[cornerIndex] = deckToWorld(*boat, corners[cornerIndex]);
    }
    constexpr unsigned faces[6][4] = { { 0, 4, 6, 2 }, { 1, 3, 7, 5 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 2, 3, 1 }, { 4, 5, 7, 6 } };
    for (auto& face : faces)
    {
        triangle(scene, corners[face[0]], corners[face[1]], corners[face[2]], color, material);
        triangle(scene, corners[face[0]], corners[face[2]], corners[face[3]], color, material);
    }
}
static void hull(Scene* scene, const Snapshot& boat, const VesselLayout& layout, unsigned index)
{
    float offset = layout.hullSpacing > 0 ? (index ? .5f : -.5f) * layout.hullSpacing : 0;
    for (unsigned triangleIndex = 0; triangleIndex < HullTriangles; ++triangleIndex)
    {
        Vec3 points[3];
        hullTriangle(layout, index, triangleIndex, points);
        auto color = (points[0].y > .8f && points[1].y > .8f && points[2].y > .8f) ? Vec3{ .82f, .78f, .64f } : Vec3{ .84f, .9f, .89f };
        triangle(scene, deckToWorld(boat, points[0]), deckToWorld(boat, points[1]), deckToWorld(boat, points[2]), color);
    }
    float height = layout.draft - layout.hullDraft;
    box(scene, { offset, -layout.hullDraft - height * .5f, 0 }, { layout.hullSpacing > 0 ? .28f : .2f, height, 2 }, { .2f, .26f, .26f },
        &boat);
}
Scene* createScene(TFRenderer* renderer)
{
    MTRACY_ZONE("createScene");
    auto* scene = tf_new(Scene);
    scene->renderer = renderer;
    TFBufferLoadDesc load = {};
    load.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
    load.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
    for (unsigned frameIndex = 0; frameIndex < 2; ++frameIndex)
    {
        load.mDesc.mSize = sizeof scene->mesh;
        load.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        load.ppBuffer = &scene->vertices[frameIndex];
        addResource(&load, nullptr);
        load.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        for (unsigned passIndex = 0; passIndex < 3; ++passIndex)
        {
            load.mDesc.mSize = sizeof(SceneConstants);
            load.ppBuffer = &scene->cameras[frameIndex][passIndex];
            addResource(&load, nullptr);
        }
        for (unsigned passIndex = 0; passIndex < TF_ARRAY_COUNT(scene->postCameras[frameIndex]); ++passIndex)
        {
            load.mDesc.mSize = sizeof(PostConstants);
            load.ppBuffer = &scene->postCameras[frameIndex][passIndex];
            addResource(&load, nullptr);
        }
    }
    waitForAllResourceLoads();
    return scene;
}
static bool loadSceneShaders(TFRenderer* renderer, ScenePrograms& programs, uint32_t format)
{
    TFShaderLoadDesc shader = {};
    shader.mVert.pFileName = "marina.vert";
    shader.mFrag.pFileName = "marina.frag";
    shaderLabAddShader(renderer, &shader, &programs.shader);
    shader.mVert.pFileName = "marina_post.vert";
    shader.mFrag.pFileName = "marina_post.frag";
    shaderLabAddShader(renderer, &shader, &programs.postShader);
    TFVertexLayout vertices = {};
    vertices.mBindingCount = 1;
    vertices.mBindings[0].mStride = sizeof(Vertex);
    vertices.mAttribCount = 4;
    for (unsigned attributeIndex = 0; attributeIndex < 3; ++attributeIndex)
    {
        vertices.mAttribs[attributeIndex].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
        vertices.mAttribs[attributeIndex].mLocation = attributeIndex;
        vertices.mAttribs[attributeIndex].mOffset = attributeIndex * sizeof(Vec3);
    }
    vertices.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
    vertices.mAttribs[1].mSemantic = TF_SEMANTIC_NORMAL;
    vertices.mAttribs[2].mSemantic = TF_SEMANTIC_COLOR;
    vertices.mAttribs[3].mFormat = TinyImageFormat_R32_SFLOAT;
    vertices.mAttribs[3].mLocation = 3;
    vertices.mAttribs[3].mOffset = offsetof(Vertex, material);
    vertices.mAttribs[3].mSemantic = TF_SEMANTIC_TEXCOORD0;
    TFDepthStateDesc depthState = {};
    depthState.mDepthTest = depthState.mDepthWrite = true;
    depthState.mDepthFunc = TF_CMP_LEQUAL;
    TFRasterizerStateDesc rasterizerState = {};
    rasterizerState.mCullMode = TF_CULL_MODE_NONE;
    TFPipelineDesc pipelineDescription = {};
    pipelineDescription.mType = TF_PIPELINE_TYPE_GRAPHICS;
    PIPELINE_LAYOUT_DESC(pipelineDescription, nullptr, SRT_LAYOUT_DESC(MarinaScene, PerFrame), nullptr, nullptr);
    auto& graphics = pipelineDescription.mGraphicsDesc;
    graphics.pShaderProgram = programs.shader;
    graphics.pVertexLayout = &vertices;
    graphics.pDepthState = &depthState;
    graphics.pRasterizerState = &rasterizerState;
    TinyImageFormat color = TinyImageFormat_R16G16B16A16_SFLOAT;
    graphics.pColorFormats = &color;
    graphics.mRenderTargetCount = 1;
    graphics.mDepthStencilFormat = TinyImageFormat_D32_SFLOAT;
    graphics.mSampleCount = TF_SAMPLE_COUNT_1;
    graphics.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
    pipelineDescription.pName = "Opaque geometry";
    shaderLabAddPipeline(renderer, &pipelineDescription, &programs.pipeline);
    color = TinyImageFormat_R32_SFLOAT;
    pipelineDescription.pName = "Scene shadow";
    shaderLabAddPipeline(renderer, &pipelineDescription, &programs.shadowPipeline);
    PIPELINE_LAYOUT_DESC(pipelineDescription, nullptr, SRT_LAYOUT_DESC(MarinaPost, PerFrame), nullptr, nullptr);
    graphics.pShaderProgram = programs.postShader;
    graphics.pVertexLayout = nullptr;
    graphics.pDepthState = nullptr;
    color = TinyImageFormat_R16G16B16A16_SFLOAT;
    pipelineDescription.pName = "HDR copy";
    shaderLabAddPipeline(renderer, &pipelineDescription, &programs.copyPipeline);
    graphics.mDepthStencilFormat = TinyImageFormat_UNDEFINED;
    pipelineDescription.pName = "Atmosphere and bloom";
    shaderLabAddPipeline(renderer, &pipelineDescription, &programs.airPipeline);
    color = static_cast<TinyImageFormat>(format);
    pipelineDescription.pName = "Tone mapping and composite";
    shaderLabAddPipeline(renderer, &pipelineDescription, &programs.postPipeline);
    return programs.pipeline && programs.shadowPipeline && programs.copyPipeline && programs.airPipeline && programs.postPipeline;
}
void loadScene(Scene* scene, uint32_t format, unsigned width, unsigned height, const WaterLook& look)
{
    MTRACY_ZONE("loadScene");
    TFDescriptorSetDesc set = SRT_SET_DESC(MarinaScene, PerFrame, 6, 0);
    addDescriptorSet(scene->renderer, &set, &scene->descriptors);
    scene->depth = sceneTarget(scene, width, height, TinyImageFormat_D32_SFLOAT, true, "Marina depth");
    scene->opaque = sceneTarget(scene, width, height, TinyImageFormat_R16G16B16A16_SFLOAT, false, "Opaque colour and view depth");
    scene->composite = sceneTarget(scene, width, height, TinyImageFormat_R16G16B16A16_SFLOAT, false, "Water HDR composite");
    scene->airRays = sceneTarget(scene, (width + 3) / 4, (height + 3) / 4, TinyImageFormat_R16G16B16A16_SFLOAT, false,
                                 "Atmospheric rays and view depth");
    for (unsigned level = 0; level < TF_ARRAY_COUNT(scene->bloom); ++level)
    {
        unsigned    divisor = 4u << level;
        const char* names[] = { "Bloom / Near", "Bloom / Middle", "Bloom / Far" };
        scene->bloom[level] = sceneTarget(scene, (width + divisor - 1) / divisor, (height + divisor - 1) / divisor,
                                          TinyImageFormat_R16G16B16A16_SFLOAT, false, names[level]);
    }
    scene->reflection = sceneTarget(scene, (width + look.reflectionDivisor - 1) / look.reflectionDivisor,
                                    (height + look.reflectionDivisor - 1) / look.reflectionDivisor, TinyImageFormat_R16G16B16A16_SFLOAT,
                                    false, "Planar reflection");
    scene->reflectionDepth = sceneTarget(scene, (width + look.reflectionDivisor - 1) / look.reflectionDivisor,
                                         (height + look.reflectionDivisor - 1) / look.reflectionDivisor, TinyImageFormat_D32_SFLOAT, true,
                                         "Reflection depth", true);
    scene->shadow = sceneTarget(scene, look.shadowSize, look.shadowSize, TinyImageFormat_R32_SFLOAT, false, "Water shadow map");
    scene->shadow->mClearValue.r = 1;
    scene->shadowDepth = sceneTarget(scene, look.shadowSize, look.shadowSize, TinyImageFormat_D32_SFLOAT, true, "Shadow depth", true);
    scene->waterLight =
        sceneTarget(scene, look.waterLightSize, look.waterLightSize, TinyImageFormat_R32_SFLOAT, false, "Water light entry depth");
    scene->waterLight->mClearValue.r = 1;
    scene->waterLightDepth =
        sceneTarget(scene, look.waterLightSize, look.waterLightSize, TinyImageFormat_D32_SFLOAT, true, "Water light depth test", true);
    TFSamplerDesc sampler = {};
    sampler.mMinFilter = sampler.mMagFilter = TF_FILTER_LINEAR;
    sampler.mMipMapMode = TF_MIPMAP_MODE_NEAREST;
    sampler.mAddressU = sampler.mAddressV = sampler.mAddressW = TF_ADDRESS_MODE_CLAMP_TO_EDGE;
    addSampler(scene->renderer, &sampler, &scene->sampler);
    set = SRT_SET_DESC(MarinaPost, PerFrame, 12, 0);
    addDescriptorSet(scene->renderer, &set, &scene->postDescriptors);
    loadSceneShaders(scene->renderer, scene->programs, format);
}

static void unloadSceneShaders(TFRenderer* renderer, const ScenePrograms& programs)
{
    shaderLabRemovePipeline(renderer, programs.pipeline);
    shaderLabRemovePipeline(renderer, programs.shadowPipeline);
    shaderLabRemovePipeline(renderer, programs.copyPipeline);
    shaderLabRemovePipeline(renderer, programs.postPipeline);
    shaderLabRemovePipeline(renderer, programs.airPipeline);
    shaderLabRemoveShader(renderer, programs.shader);
    shaderLabRemoveShader(renderer, programs.postShader);
}
void unloadScene(Scene* scene)
{
    MTRACY_ZONE("unloadScene");
    shaderLabForgetResources(scene);
    unloadSceneShaders(scene->renderer, scene->programs);
    shaderLabRemoveDescriptorSet(scene->renderer, scene->descriptors);
    shaderLabRemoveDescriptorSet(scene->renderer, scene->postDescriptors);
    removeSampler(scene->renderer, scene->sampler);
    for (auto* target : scene->bloom)
        removeRenderTarget(scene->renderer, target);
    for (auto* target : { scene->depth, scene->opaque, scene->composite, scene->airRays, scene->reflection, scene->reflectionDepth,
                          scene->shadow, scene->shadowDepth, scene->waterLight, scene->waterLightDepth })
        removeRenderTarget(scene->renderer, target);
}
bool reloadSceneShaders(Scene* scene, uint32_t format)
{
    ScenePrograms pending{};
    const bool    valid = loadSceneShaders(scene->renderer, pending, format);
    if (valid)
        std::swap(scene->programs, pending);
    unloadSceneShaders(scene->renderer, pending);
    return valid;
}

void destroyScene(Scene* scene)
{
    MTRACY_ZONE("destroyScene");
    shaderLabForgetResources(scene);
    for (unsigned frameIndex = 0; frameIndex < 2; ++frameIndex)
    {
        removeResource(scene->vertices[frameIndex]);
        for (auto* buffer : scene->cameras[frameIndex])
            removeResource(buffer);
        for (auto* buffer : scene->postCameras[frameIndex])
            removeResource(buffer);
    }
    tf_delete(scene);
}
void connectSceneWater(Scene* scene, OceanRenderer* water)
{
    MTRACY_ZONE("connectSceneWater");
    setOceanScene(water, scene->opaque, scene->reflection, scene->shadow, scene->waterLight);
    TFTexture* sky = oceanSkyTexture(water, scene->skyHeight);
    TFTexture* noise = oceanCloudNoise(water);
    // Supply every binding together: partial updates discard Metal residency
    // declarations for the render targets already in an argument buffer.
    for (unsigned frameIndex = 0; frameIndex < 2; ++frameIndex)
        for (unsigned passIndex = 0; passIndex < 6; ++passIndex)
        {
            TFDescriptorData bindings[10] = {};
            bindings[0].mIndex = SRT_RES_IDX(MarinaPost, PerFrame, gPost);
            bindings[0].ppBuffers = &scene->postCameras[frameIndex][passIndex];
            bindings[1].mIndex = SRT_RES_IDX(MarinaPost, PerFrame, gSceneColor);
            bindings[1].ppTextures = passIndex >= 4 ? &scene->bloom[passIndex - 4]->pTexture
                                     : passIndex    ? &scene->composite->pTexture
                                                    : &scene->opaque->pTexture;
            bindings[2].mIndex = SRT_RES_IDX(MarinaPost, PerFrame, gSceneSampler);
            bindings[2].ppSamplers = &scene->sampler;
            bindings[3].mIndex = SRT_RES_IDX(MarinaPost, PerFrame, gPostShadow);
            bindings[3].ppTextures = &scene->shadow->pTexture;
            bindings[4].mIndex = SRT_RES_IDX(MarinaPost, PerFrame, gAirRays);
            bindings[4].ppTextures = passIndex == 1 ? &scene->airRays->pTexture : &scene->opaque->pTexture;
            bindings[5].mIndex = SRT_RES_IDX(MarinaPost, PerFrame, gBloomNear);
            bindings[5].ppTextures = passIndex == 1 ? &scene->bloom[0]->pTexture : &scene->opaque->pTexture;
            bindings[6].mIndex = SRT_RES_IDX(MarinaPost, PerFrame, gBloomMiddle);
            bindings[6].ppTextures = passIndex == 1 ? &scene->bloom[1]->pTexture : &scene->opaque->pTexture;
            bindings[7].mIndex = SRT_RES_IDX(MarinaPost, PerFrame, gBloomFar);
            bindings[7].ppTextures = passIndex == 1 ? &scene->bloom[2]->pTexture : &scene->opaque->pTexture;
            bindings[8].mIndex = SRT_RES_IDX(MarinaPost, PerFrame, gSkyRadiance);
            bindings[8].ppTextures = &sky;
            bindings[9].mIndex = SRT_RES_IDX(MarinaPost, PerFrame, gCloudNoiseTexture);
            bindings[9].ppTextures = &noise;
            shaderLabUpdateDescriptorSet(scene->renderer, frameIndex * 6 + passIndex, scene->postDescriptors, TF_ARRAY_COUNT(bindings),
                                         bindings);
        }
    for (unsigned frameIndex = 0; frameIndex < 2; ++frameIndex)
        for (unsigned passIndex = 0; passIndex < 3; ++passIndex)
        {
            TFDescriptorData bindings[2] = {};
            bindings[0].mIndex = SRT_RES_IDX(MarinaScene, PerFrame, gCamera);
            bindings[0].ppBuffers = &scene->cameras[frameIndex][passIndex];
            bindings[1].mIndex = SRT_RES_IDX(MarinaScene, PerFrame, gCloudNoiseTexture);
            bindings[1].ppTextures = &noise;
            shaderLabUpdateDescriptorSet(scene->renderer, frameIndex * 3 + passIndex, scene->descriptors, TF_ARRAY_COUNT(bindings),
                                         bindings);
        }
}
void drawScene(Scene* scene, TFCmd* cmd, TFRenderTarget* target, unsigned frame, const Camera& camera, const Snapshot& boat,
               const VesselLayout& layout, OceanRenderer* water, float level, float waterDepth, const WaterLook& look,
               const WaterProfile* profile)
{
    MTRACY_ZONE("drawScene");
    shaderLabRegisterBuffer(scene, "Scene / Vertices", scene->vertices[frame],
                            { unsigned(scene->vertices[frame]->mSize / sizeof(Vertex)), 1, 1, 1, sizeof(Vertex), 4, ShaderLabScalar::Float,
                              "Position XYZ, normal X (40-byte vertex stride)" });
    for (unsigned index = 0; index < 3; ++index)
    {
        char name[64];
        snprintf(name, sizeof name, "Scene / Camera %u", index);
        shaderLabRegisterBuffer(scene, name, scene->cameras[frame][index],
                                { unsigned(scene->cameras[frame][index]->mSize / 16), 1, 1, 1, 16, 4 });
    }
    for (unsigned index = 0; index < 6; ++index)
    {
        char name[64];
        snprintf(name, sizeof name, "Post / Constants %u", index);
        shaderLabRegisterBuffer(scene, name, scene->postCameras[frame][index],
                                { unsigned(scene->postCameras[frame][index]->mSize / 16), 1, 1, 1, 16, 4 });
    }
    scene->count = 0;
    box(scene, { 0, level - waterDepth - .5f, 0 }, { 800, 1, 800 }, { look.seabedColor[0], look.seabedColor[1], look.seabedColor[2] });
    const unsigned seabedVertices = scene->count;
    // The shader filters plank joints by pixel footprint. Thin geometry
    // aliases into dark moiré bands at the low camera angles used for water.
    box(scene, { 10, .25f, 0 }, { 2.4f, 1, 30 }, { .33f, .28f, .21f }, nullptr, 1);
    for (int cleatIndex = -2; cleatIndex <= 2; ++cleatIndex)
    {
        box(scene, { 8.95f, .83f, cleatIndex * 5.0f }, { .2f, .15f, .65f }, { .45f, .48f, .47f });
    }
    bool cat = layout.hullSpacing > 0;
    if (cat)
    {
        hull(scene, boat, layout, 0);
        hull(scene, boat, layout, 1);
        box(scene, { 0, .66f, -.5f }, { 6.0f, .14f, 8.8f }, { .78f, .8f, .74f }, &boat);
    }
    else
        hull(scene, boat, layout, 0);
    box(scene, { 0, .85f, .4f }, { 2.15f, .45f, 3.8f }, { .93f, .93f, .84f }, &boat);
    box(scene, { 0, 1.22f, .65f }, { 1.8f, .3f, 2.7f }, { .08f, .19f, .22f }, &boat);
    box(scene, { 0, 1.42f, .65f }, { 1.9f, .13f, 2.8f }, { .91f, .92f, .84f }, &boat);
    box(scene, { 0, 3.1f, 1.4f }, { .12f, 4.4f, .12f }, { .64f, .68f, .65f }, &boat);
    box(scene, { 0, 4.45f, -.2f }, { .09f, .1f, 3.2f }, { .56f, .6f, .58f }, &boat);
    for (int side = -1; side <= 1; side += 2)
        for (int railPostIndex = 0; railPostIndex < 6; ++railPostIndex)
        {
            box(scene, { side * 1.68f, .99f, -4.5f + railPostIndex * 1.4f }, { .05f, .6f, .05f }, { .52f, .58f, .59f }, &boat);
            box(scene, { side * 1.68f, 1.25f, -3.8f + railPostIndex * 1.4f }, { .045f, .045f, 1.4f }, { .52f, .58f, .59f }, &boat);
        }
    for (Station station : { Station::Helm, Station::Port, Station::Starboard, Station::Bow })
    {
        auto stationCenter = stationPosition(station);
        stationCenter.y = .77f;
        box(scene, stationCenter, { .6f, .08f, .6f }, station == Station::Helm ? Vec3{ .9f, .52f, .1f } : Vec3{ .1f, .62f, .62f }, &boat);
    }
    box(scene, { 0, 1.0f, -3.35f }, { .25f, .7f, .25f }, { .2f, .25f, .25f }, &boat);
    auto person = boat.skipperDeck;
    person.y = .99f;
    box(scene, person, { .37f, .58f, .3f }, { .98f, .37f, .09f }, &boat);
    person.y = 1.41f;
    box(scene, person, { .25f, .25f, .25f }, { .73f, .5f, .32f }, &boat);
    uploadScene(scene->vertices[frame], scene->mesh, scene->count * sizeof(Vertex));
    SceneConstants cameras[3] = {};
    float4         sun;
    look.sunlight(sun);
    cameras[0].viewProjection = cameras[1].viewProjection = cameraMatrix(camera);
    cameras[2].viewProjection = lightProjection(boat.position, sun.getXYZ(), scene->shadow->mWidth, 64, 200);
    for (unsigned passIndex = 0; passIndex < 3; ++passIndex)
    {
        cameras[passIndex].modeLevel[0] = float(passIndex);
        cameras[passIndex].modeLevel[1] = level;
        cameras[passIndex].modeLevel[2] = boat.time;
        float4 effects;
        look.parameters(cameras[passIndex].weather, effects);
        cameras[passIndex].sun = sun;
        uploadScene(scene->cameras[frame][passIndex], &cameras[passIndex], sizeof(SceneConstants));
    }
    PostConstants post = {};
    auto          eye = cameraEye(camera);
    auto          forward = f3Normalize(f3Sub(toForge(camera.focus), toForge(eye)));
    auto right = f3MulScalar(f3Normalize(f3Cross(make_float3(0, 1, 0), forward)), float(camera.width) / camera.height / 2.41421356f);
    auto up = f3MulScalar(f3Normalize(f3Cross(forward, right)), 1 / 2.41421356f);
    post.forward = float4(forward, float(scene->skyHeight));
    post.right = float4(right);
    post.up = float4(up);
    post.sun = sun;
    look.parameters(post.weather, post.effects);
    post.eye = float4(toForge(eye));
    float4 detail, optics, foam;
    look.fidelity(detail, post.quality, optics, foam);
    post.shadowMatrix = cameras[2].viewProjection;
    post.screen[3] = boat.time;
    post.screen[0] = float(camera.width);
    post.screen[1] = float(camera.height);
    for (unsigned passIndex = 0; passIndex < 6; ++passIndex)
    {
        post.screen[2] = float(passIndex);
        auto* source = passIndex >= 4 ? scene->bloom[passIndex - 4] : scene->composite;
        post.bloom = { look.bloom, look.bloomThreshold, 1.0f / source->mWidth, 1.0f / source->mHeight };
        uploadScene(scene->postCameras[frame][passIndex], &post, sizeof post);
    }
    WaterLook activeLook = look;
    if (look.debugView == 37)
        activeLook.debugView = 0;
    activeLook.shadowSize = int(scene->shadow->mWidth);
    activeLook.waterLightSize = int(scene->waterLight->mWidth);
    activeLook.reflectionDivisor = int((camera.width + scene->reflection->mWidth - 1) / scene->reflection->mWidth);
    // Conservative bounds from the same triangles used by the reflection
    // capture. Open-water pixels can reject rays without any texture fetches.
    float2 low = { 1, 1 }, high = { 0, 0 };
    for (unsigned triangle = 0; triangle < scene->count; triangle += 3)
    {
        if (std::max({ scene->mesh[triangle].position.y, scene->mesh[triangle + 1].position.y, scene->mesh[triangle + 2].position.y }) <
            level - .04f)
            continue;
        bool crossesEye = false;
        for (unsigned vertex = triangle; vertex < triangle + 3; ++vertex)
        {
            auto position = scene->mesh[vertex].position;
            auto clip = f4x4Mulf4(cameras[0].viewProjection, make_float4(position.x, 2 * level - position.y, position.z, 1));
            if (clip.w < .2f)
            {
                crossesEye = true;
                break;
            }
            float2 uv = { clip.x / clip.w * .5f + .5f, .5f - clip.y / clip.w * .5f };
            low = f2MinPerElem(low, uv);
            high = f2MaxPerElem(high, uv);
        }
        if (crossesEye)
        {
            low = { 0, 0 };
            high = { 1, 1 };
            break;
        }
    }
    const float4 bounds = { std::max(0.0f, low.x - .002f), std::max(0.0f, low.y - .002f), std::min(1.0f, high.x + .002f),
                            std::min(1.0f, high.y + .002f) };
    float2       shadowLow = { 1, 1 }, shadowHigh = { 0, 0 };
    for (unsigned vertex = seabedVertices; vertex < scene->count; ++vertex)
    {
        auto   clip = f4x4Mulf4(cameras[2].viewProjection, float4(toForge(scene->mesh[vertex].position), 1));
        float2 uv = { clip.x * .5f + .5f, .5f - clip.y * .5f };
        shadowLow = f2MinPerElem(shadowLow, uv);
        shadowHigh = f2MaxPerElem(shadowHigh, uv);
    }
    // Include the complete PCF footprint around the projected caster bounds.
    const float  margin = 2.0f / scene->shadow->mWidth;
    const float4 shadowBounds = { shadowLow.x - margin, shadowLow.y - margin, shadowHigh.x + margin, shadowHigh.y + margin };
    prepareOceanEffects(water, cmd, camera, frame, level, cameras[2].viewProjection, activeLook, boat, layout, bounds, profile,
                        shadowBounds);
    beginWaterPass(cmd, profile, WaterGeometry);
    TFRenderTarget* targets[] = { scene->opaque, scene->reflection, scene->shadow };
    TFRenderTarget* depths[] = { scene->depth, scene->reflectionDepth, scene->shadowDepth };
    uint32_t        stride = sizeof(Vertex);
    uint64_t        offset = 0;
    for (unsigned passIndex = 0; passIndex < 3; ++passIndex)
    {
        transitionTarget(cmd, targets[passIndex], true);
        bindSceneTarget(cmd, targets[passIndex], depths[passIndex], TF_LOAD_ACTION_CLEAR);
        shaderLabBindPipeline(cmd, passIndex == 2 ? scene->programs.shadowPipeline : scene->programs.pipeline);
        shaderLabBindDescriptorSet(cmd, frame * 3 + passIndex, scene->descriptors);
        cmdBindVertexBuffer(cmd, 1, &scene->vertices[frame], &stride, &offset);
        cmdDraw(cmd, scene->count, 0);
        transitionTarget(cmd, targets[passIndex], false);
        cmdBindRenderTargets(cmd, nullptr);
    }
    transitionTarget(cmd, scene->waterLight, true);
    bindSceneTarget(cmd, scene->waterLight, scene->waterLightDepth, TF_LOAD_ACTION_CLEAR);
    drawOceanLight(water, cmd, frame);
    transitionTarget(cmd, scene->waterLight, false);
    cmdBindRenderTargets(cmd, nullptr);
    endWaterPass(cmd, profile, WaterGeometry);
    beginWaterPass(cmd, profile, WaterSurface);
    transitionTarget(cmd, scene->composite, true);
    // Sky, water and spray share one pass. Starting a second pass without a
    // write-to-write dependency let the sky copy overwrite water tiles on Metal.
    bindSceneTarget(cmd, scene->composite, scene->depth, TF_LOAD_ACTION_DONTCARE, TF_LOAD_ACTION_LOAD);
    shaderLabBindPipeline(cmd, scene->programs.copyPipeline);
    shaderLabBindDescriptorSet(cmd, frame * 6, scene->postDescriptors);
    cmdDraw(cmd, 3, 0);
    drawOcean(water, cmd, frame);
    transitionTarget(cmd, scene->composite, false);
    cmdBindRenderTargets(cmd, nullptr);
    endWaterPass(cmd, profile, WaterSurface);
    beginWaterPass(cmd, profile, WaterPost);
    if (look.shaftSteps > 0 && look.shafts > 0 && (look.debugView == 0 || look.debugView == 20))
    {
        transitionTarget(cmd, scene->airRays, true);
        bindSceneTarget(cmd, scene->airRays, nullptr, TF_LOAD_ACTION_DONTCARE);
        shaderLabBindPipeline(cmd, scene->programs.airPipeline);
        shaderLabBindDescriptorSet(cmd, frame * 6 + 2, scene->postDescriptors);
        cmdDraw(cmd, 3, 0);
        mooringTracyMetalNameEncoder(cmd, "Post / Atmospheric integration");
        transitionTarget(cmd, scene->airRays, false);
        cmdBindRenderTargets(cmd, nullptr);
    }
    if (look.bloom > 0 || look.debugView == 37)
        for (unsigned bloomLevel = 0; bloomLevel < TF_ARRAY_COUNT(scene->bloom); ++bloomLevel)
        {
            transitionTarget(cmd, scene->bloom[bloomLevel], true);
            bindSceneTarget(cmd, scene->bloom[bloomLevel], nullptr, TF_LOAD_ACTION_DONTCARE);
            shaderLabBindPipeline(cmd, scene->programs.airPipeline);
            shaderLabBindDescriptorSet(cmd, frame * 6 + 3 + bloomLevel, scene->postDescriptors);
            cmdDraw(cmd, 3, 0);
            const char* names[] = { "Post / Bloom level 0", "Post / Bloom level 1", "Post / Bloom level 2" };
            mooringTracyMetalNameEncoder(cmd, names[bloomLevel]);
            transitionTarget(cmd, scene->bloom[bloomLevel], false);
            cmdBindRenderTargets(cmd, nullptr);
        }
    bindSceneTarget(cmd, target, nullptr, TF_LOAD_ACTION_DONTCARE);
    shaderLabBindPipeline(cmd, scene->programs.postPipeline);
    shaderLabBindDescriptorSet(cmd, frame * 6 + 1, scene->postDescriptors);
    cmdDraw(cmd, 3, 0);
    TFRenderTargetBarrier uiBarrier = { target, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_RENDER_TARGET };
    cmdResourceBarrier(cmd, 0, nullptr, 0, nullptr, 1, &uiBarrier);
    cmdBindRenderTargets(cmd, nullptr);
    endWaterPass(cmd, profile, WaterPost);
}
} // namespace mooring
