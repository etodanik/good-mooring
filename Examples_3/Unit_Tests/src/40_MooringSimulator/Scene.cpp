#include "Scene.h"
#include "GraphicsMath.h"
#include "Simulation/Hydrodynamics.h"
#include "Water/OceanRenderer.h"
#include "Water/WaterProfile.h"
#include "Common_3/Graphics/Interfaces/IGraphics.h"
#include "Common_3/Resources/ResourceLoader/Interfaces/IResourceLoader.h"
#include "Common_3/Graphics/FSL/defaults.h"
#include "Shaders/Scene.srt.h"
#include "Shaders/Post.srt.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "Common_3/Utilities/Interfaces/IMemory.h"

namespace mooring {
struct Vertex { Vec3 position,normal,color; float material; };
struct Scene {
    TFRenderer* renderer; TFShader* shader; TFPipeline* pipeline; TFPipeline* shadowPipeline; TFDescriptorSet* descriptors;
    TFShader* postShader; TFPipeline* copyPipeline; TFPipeline* postPipeline; TFPipeline* airPipeline; TFDescriptorSet* postDescriptors; TFSampler* sampler;
    TFBuffer* vertices[2]; TFBuffer* cameras[2][3]; TFBuffer* postCameras[2][6];
    TFRenderTarget* depth; TFRenderTarget* opaque; TFRenderTarget* composite; TFRenderTarget* airRays;
    TFRenderTarget* reflection; TFRenderTarget* reflectionDepth; TFRenderTarget* shadow; TFRenderTarget* shadowDepth;
    TFRenderTarget* waterLight; TFRenderTarget* waterLightDepth;
    TFRenderTarget* bloom[3];
    Vertex mesh[8192]; unsigned count,skyHeight;
};
static void uploadScene(TFBuffer* buffer,const void* data,size_t size) {
    MTRACY_ZONE("uploadScene");
    TFBufferUpdateDesc update={buffer}; beginUpdateResource(&update); memcpy(update.pMappedData,data,size); endUpdateResource(&update);
}
static TFRenderTarget* sceneTarget(Scene* s,unsigned width,unsigned height,TinyImageFormat format,bool depth,const char* name,bool transient=false) {
    TFRenderTargetDesc desc={}; desc.mWidth=width; desc.mHeight=height; desc.mDepth=desc.mArraySize=1;
    desc.mSampleCount=TF_SAMPLE_COUNT_1; desc.mFormat=format; desc.pName=name;
    desc.mClearValue=depth?TFClearValue{1,0,0,0}:TFClearValue{0,0,0,0};
    desc.mStartState=depth?TF_RESOURCE_STATE_DEPTH_WRITE:TF_RESOURCE_STATE_SHADER_RESOURCE;
    if(transient) desc.mFlags=TF_TEXTURE_CREATION_FLAG_ON_TILE;
    if(!depth) desc.mDescriptors=TF_DESCRIPTOR_TYPE_TEXTURE;
    TFRenderTarget* target=nullptr; addRenderTarget(s->renderer,&desc,&target); return target;
}
static void transitionTarget(TFCmd* cmd,TFRenderTarget* target,bool render) {
    TFRenderTargetBarrier b={target,render?TF_RESOURCE_STATE_SHADER_RESOURCE:TF_RESOURCE_STATE_RENDER_TARGET,
                                  render?TF_RESOURCE_STATE_RENDER_TARGET:TF_RESOURCE_STATE_SHADER_RESOURCE};
    cmdResourceBarrier(cmd,0,nullptr,0,nullptr,1,&b);
}
static void bindSceneTarget(TFCmd* cmd,TFRenderTarget* target,TFRenderTarget* depth,TFLoadActionType action,TFLoadActionType depthAction=TF_LOAD_ACTION_CLEAR) {
    TFBindRenderTargetsDesc bind={}; bind.mRenderTargetCount=1; bind.mRenderTargets[0]={target,action};
    if(depth) bind.mDepthStencil={depth,depthAction};
    cmdBindRenderTargets(cmd,&bind);
    cmdSetViewport(cmd,0,0,target->mWidth,target->mHeight,0,1); cmdSetScissor(cmd,0,0,target->mWidth,target->mHeight);
}
static void triangle(Scene* s,Vec3 a,Vec3 b,Vec3 c,Vec3 color,float material=0) {
    ASSERT(s->count+3 <= 8192);
    auto normal=fromForge(f3Normalize(f3Cross(f3Sub(toForge(b),toForge(a)),f3Sub(toForge(c),toForge(a)))));
    s->mesh[s->count++]={a,normal,color,material}; s->mesh[s->count++]={b,normal,color,material}; s->mesh[s->count++]={c,normal,color,material};
}
static void box(Scene* s,Vec3 p,Vec3 size,Vec3 color,const Snapshot* boat=nullptr,float material=0) {
    Vec3 v[8];
    for(unsigned i=0;i<8;++i) {
        v[i]={p.x+(i&1?1:-1)*size.x*.5f,p.y+(i&2?1:-1)*size.y*.5f,p.z+(i&4?1:-1)*size.z*.5f};
        if(boat) v[i]=deckToWorld(*boat,v[i]);
    }
    constexpr unsigned faces[6][4]={{0,4,6,2},{1,3,7,5},{0,1,5,4},{2,6,7,3},{0,2,3,1},{4,5,7,6}};
    for(auto& f:faces) { triangle(s,v[f[0]],v[f[1]],v[f[2]],color,material); triangle(s,v[f[0]],v[f[2]],v[f[3]],color,material); }
}
static void hull(Scene* s,const Snapshot& boat,const VesselLayout& layout,unsigned index) {
    float offset=layout.hullSpacing>0?(index?.5f:-.5f)*layout.hullSpacing:0;
    for(unsigned i=0;i<HullTriangles;++i) {
        Vec3 points[3]; hullTriangle(layout,index,i,points);
        auto color=(points[0].y>.8f && points[1].y>.8f && points[2].y>.8f)?Vec3{.82f,.78f,.64f}:Vec3{.84f,.9f,.89f};
        triangle(s,deckToWorld(boat,points[0]),deckToWorld(boat,points[1]),deckToWorld(boat,points[2]),color);
    }
    float height=layout.draft-layout.hullDraft;
    box(s,{offset,-layout.hullDraft-height*.5f,0},{layout.hullSpacing>0?.28f:.2f,height,2},{.2f,.26f,.26f},&boat);
}
Scene* createScene(TFRenderer* r) {
    MTRACY_ZONE("createScene");
    auto* s=tf_new(Scene); s->renderer=r;
    TFBufferLoadDesc load={}; load.mDesc.mMemoryUsage=TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
    load.mDesc.mFlags=TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
    for(unsigned i=0;i<2;++i) {
        load.mDesc.mSize=sizeof s->mesh; load.mDesc.mDescriptors=TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        load.ppBuffer=&s->vertices[i]; addResource(&load,nullptr);
        load.mDesc.mDescriptors=TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        for(unsigned p=0;p<3;++p) { load.mDesc.mSize=sizeof(SceneConstants); load.ppBuffer=&s->cameras[i][p]; addResource(&load,nullptr); }
        for(unsigned p=0;p<TF_ARRAY_COUNT(s->postCameras[i]);++p) { load.mDesc.mSize=sizeof(PostConstants); load.ppBuffer=&s->postCameras[i][p]; addResource(&load,nullptr); }
    }
    waitForAllResourceLoads(); return s;
}
void loadScene(Scene* s,uint32_t format,unsigned width,unsigned height,const WaterLook& look) {
    MTRACY_ZONE("loadScene");
    TFShaderLoadDesc shader={}; shader.mVert.pFileName="marina.vert"; shader.mFrag.pFileName="marina.frag";
    addShader(s->renderer,&shader,&s->shader);
    shader.mVert.pFileName="marina_post.vert"; shader.mFrag.pFileName="marina_post.frag";
    addShader(s->renderer,&shader,&s->postShader);
    TFDescriptorSetDesc set=SRT_SET_DESC(MarinaScene,PerFrame,6,0); addDescriptorSet(s->renderer,&set,&s->descriptors);
    s->depth=sceneTarget(s,width,height,TinyImageFormat_D32_SFLOAT,true,"Marina depth");
    s->opaque=sceneTarget(s,width,height,TinyImageFormat_R16G16B16A16_SFLOAT,false,"Opaque colour and view depth");
    s->composite=sceneTarget(s,width,height,TinyImageFormat_R16G16B16A16_SFLOAT,false,"Water HDR composite");
    s->airRays=sceneTarget(s,(width+3)/4,(height+3)/4,TinyImageFormat_R16G16B16A16_SFLOAT,false,"Atmospheric rays and view depth");
    for(unsigned level=0;level<TF_ARRAY_COUNT(s->bloom);++level) {
        unsigned divisor=4u<<level;
        s->bloom[level]=sceneTarget(s,(width+divisor-1)/divisor,(height+divisor-1)/divisor,TinyImageFormat_R16G16B16A16_SFLOAT,false,"Bloom");
    }
    s->reflection=sceneTarget(s,(width+look.reflectionDivisor-1)/look.reflectionDivisor,(height+look.reflectionDivisor-1)/look.reflectionDivisor,TinyImageFormat_R16G16B16A16_SFLOAT,false,"Planar reflection");
    s->reflectionDepth=sceneTarget(s,(width+look.reflectionDivisor-1)/look.reflectionDivisor,(height+look.reflectionDivisor-1)/look.reflectionDivisor,TinyImageFormat_D32_SFLOAT,true,"Reflection depth",true);
    s->shadow=sceneTarget(s,look.shadowSize,look.shadowSize,TinyImageFormat_R32_SFLOAT,false,"Water shadow map");
    s->shadow->mClearValue.r=1;
    s->shadowDepth=sceneTarget(s,look.shadowSize,look.shadowSize,TinyImageFormat_D32_SFLOAT,true,"Shadow depth",true);
    s->waterLight=sceneTarget(s,look.waterLightSize,look.waterLightSize,TinyImageFormat_R32_SFLOAT,false,"Water light entry depth");
    s->waterLight->mClearValue.r=1;
    s->waterLightDepth=sceneTarget(s,look.waterLightSize,look.waterLightSize,TinyImageFormat_D32_SFLOAT,true,"Water light depth test",true);
    TFSamplerDesc sampler={}; sampler.mMinFilter=sampler.mMagFilter=TF_FILTER_LINEAR; sampler.mMipMapMode=TF_MIPMAP_MODE_NEAREST;
    sampler.mAddressU=sampler.mAddressV=sampler.mAddressW=TF_ADDRESS_MODE_CLAMP_TO_EDGE; addSampler(s->renderer,&sampler,&s->sampler);
    set=SRT_SET_DESC(MarinaPost,PerFrame,12,0); addDescriptorSet(s->renderer,&set,&s->postDescriptors);
    TFVertexLayout vertices={}; vertices.mBindingCount=1; vertices.mBindings[0].mStride=sizeof(Vertex); vertices.mAttribCount=4;
    for(unsigned i=0;i<3;++i) { vertices.mAttribs[i].mFormat=TinyImageFormat_R32G32B32_SFLOAT;
        vertices.mAttribs[i].mLocation=i; vertices.mAttribs[i].mOffset=i*sizeof(Vec3); }
    vertices.mAttribs[0].mSemantic=TF_SEMANTIC_POSITION; vertices.mAttribs[1].mSemantic=TF_SEMANTIC_NORMAL;
    vertices.mAttribs[2].mSemantic=TF_SEMANTIC_COLOR;
    vertices.mAttribs[3].mFormat=TinyImageFormat_R32_SFLOAT;
    vertices.mAttribs[3].mLocation=3; vertices.mAttribs[3].mOffset=offsetof(Vertex,material); vertices.mAttribs[3].mSemantic=TF_SEMANTIC_TEXCOORD0;
    TFDepthStateDesc ds={}; ds.mDepthTest=ds.mDepthWrite=true; ds.mDepthFunc=TF_CMP_LEQUAL;
    TFRasterizerStateDesc rs={}; rs.mCullMode=TF_CULL_MODE_NONE;
    TFPipelineDesc p={}; p.mType=TF_PIPELINE_TYPE_GRAPHICS;
    PIPELINE_LAYOUT_DESC(p,nullptr,SRT_LAYOUT_DESC(MarinaScene,PerFrame),nullptr,nullptr);
    auto& g=p.mGraphicsDesc; g.pShaderProgram=s->shader; g.pVertexLayout=&vertices; g.pDepthState=&ds; g.pRasterizerState=&rs;
    TinyImageFormat color=TinyImageFormat_R16G16B16A16_SFLOAT; g.pColorFormats=&color; g.mRenderTargetCount=1;
    g.mDepthStencilFormat=TinyImageFormat_D32_SFLOAT; g.mSampleCount=TF_SAMPLE_COUNT_1; g.mPrimitiveTopo=TF_PRIMITIVE_TOPO_TRI_LIST;
    p.pName="Opaque geometry"; addPipeline(s->renderer,&p,&s->pipeline);
    color=TinyImageFormat_R32_SFLOAT; p.pName="Scene shadow"; addPipeline(s->renderer,&p,&s->shadowPipeline);
    PIPELINE_LAYOUT_DESC(p,nullptr,SRT_LAYOUT_DESC(MarinaPost,PerFrame),nullptr,nullptr);
    g.pShaderProgram=s->postShader; g.pVertexLayout=nullptr; g.pDepthState=nullptr;
    color=TinyImageFormat_R16G16B16A16_SFLOAT; p.pName="HDR copy and bloom"; addPipeline(s->renderer,&p,&s->copyPipeline);
    g.mDepthStencilFormat=TinyImageFormat_UNDEFINED;
    p.pName="Atmospheric integration"; addPipeline(s->renderer,&p,&s->airPipeline);
    color=static_cast<TinyImageFormat>(format); p.pName="Tone mapping and composite"; addPipeline(s->renderer,&p,&s->postPipeline);
}
void unloadScene(Scene* s) {
    MTRACY_ZONE("unloadScene");
    removePipeline(s->renderer,s->pipeline); removePipeline(s->renderer,s->shadowPipeline);
    removePipeline(s->renderer,s->copyPipeline); removePipeline(s->renderer,s->postPipeline);
    removePipeline(s->renderer,s->airPipeline);
    removeDescriptorSet(s->renderer,s->descriptors); removeDescriptorSet(s->renderer,s->postDescriptors);
    removeShader(s->renderer,s->shader); removeShader(s->renderer,s->postShader); removeSampler(s->renderer,s->sampler);
    for(auto* target:s->bloom) removeRenderTarget(s->renderer,target);
    for(auto* target:{s->depth,s->opaque,s->composite,s->airRays,s->reflection,s->reflectionDepth,s->shadow,s->shadowDepth,s->waterLight,s->waterLightDepth}) removeRenderTarget(s->renderer,target);
}
void destroyScene(Scene* s) {
    MTRACY_ZONE("destroyScene");
    for(unsigned i=0;i<2;++i) {
        removeResource(s->vertices[i]); for(auto* b:s->cameras[i]) removeResource(b); for(auto* b:s->postCameras[i]) removeResource(b);
    }
    tf_delete(s);
}
void connectSceneWater(Scene* s,OceanRenderer* water) {
    MTRACY_ZONE("connectSceneWater");
    setOceanScene(water,s->opaque,s->reflection,s->shadow,s->waterLight);
    TFTexture* sky=oceanSkyTexture(water,s->skyHeight);
    TFTexture* noise=oceanCloudNoise(water);
    // Supply every binding together: partial updates discard Metal residency
    // declarations for the render targets already in an argument buffer.
    for(unsigned i=0;i<2;++i) for(unsigned p=0;p<6;++p) {
        TFDescriptorData d[10]={};
        d[0].mIndex=SRT_RES_IDX(MarinaPost,PerFrame,gPost); d[0].ppBuffers=&s->postCameras[i][p];
        d[1].mIndex=SRT_RES_IDX(MarinaPost,PerFrame,gSceneColor); d[1].ppTextures=p>=4?&s->bloom[p-4]->pTexture:p?&s->composite->pTexture:&s->opaque->pTexture;
        d[2].mIndex=SRT_RES_IDX(MarinaPost,PerFrame,gSceneSampler); d[2].ppSamplers=&s->sampler;
        d[3].mIndex=SRT_RES_IDX(MarinaPost,PerFrame,gPostShadow); d[3].ppTextures=&s->shadow->pTexture;
        d[4].mIndex=SRT_RES_IDX(MarinaPost,PerFrame,gAirRays);
        d[4].ppTextures=p==1?&s->airRays->pTexture:&s->opaque->pTexture;
        d[5].mIndex=SRT_RES_IDX(MarinaPost,PerFrame,gBloomNear); d[5].ppTextures=p==1?&s->bloom[0]->pTexture:&s->opaque->pTexture;
        d[6].mIndex=SRT_RES_IDX(MarinaPost,PerFrame,gBloomMiddle); d[6].ppTextures=p==1?&s->bloom[1]->pTexture:&s->opaque->pTexture;
        d[7].mIndex=SRT_RES_IDX(MarinaPost,PerFrame,gBloomFar); d[7].ppTextures=p==1?&s->bloom[2]->pTexture:&s->opaque->pTexture;
        d[8].mIndex=SRT_RES_IDX(MarinaPost,PerFrame,gSkyRadiance); d[8].ppTextures=&sky;
        d[9].mIndex=SRT_RES_IDX(MarinaPost,PerFrame,gCloudNoiseTexture); d[9].ppTextures=&noise;
        updateDescriptorSet(s->renderer,i*6+p,s->postDescriptors,TF_ARRAY_COUNT(d),d);
    }
    for(unsigned i=0;i<2;++i) for(unsigned p=0;p<3;++p) {
        TFDescriptorData d[2]={};
        d[0].mIndex=SRT_RES_IDX(MarinaScene,PerFrame,gCamera); d[0].ppBuffers=&s->cameras[i][p];
        d[1].mIndex=SRT_RES_IDX(MarinaScene,PerFrame,gCloudNoiseTexture); d[1].ppTextures=&noise;
        updateDescriptorSet(s->renderer,i*3+p,s->descriptors,TF_ARRAY_COUNT(d),d);
    }
}
void drawScene(Scene* s,TFCmd* cmd,TFRenderTarget* target,unsigned frame,const Camera& camera,const Snapshot& boat,const VesselLayout& layout,OceanRenderer* water,float level,float waterDepth,const WaterLook& look,const WaterProfile* profile) {
    MTRACY_ZONE("drawScene");
    s->count=0;
    box(s,{0,level-waterDepth-.5f,0},{800,1,800},{look.seabedColor[0],look.seabedColor[1],look.seabedColor[2]});
    const unsigned seabedVertices=s->count;
    // The shader filters plank joints by pixel footprint. Thin geometry
    // aliases into dark moiré bands at the low camera angles used for water.
    box(s,{10,.25f,0},{2.4f,1,30},{.33f,.28f,.21f},nullptr,1);
    for(int n=-2;n<=2;++n) { box(s,{8.95f,.83f,n*5.0f},{.2f,.15f,.65f},{.45f,.48f,.47f}); }
    bool cat=layout.hullSpacing>0;
    if(cat) { hull(s,boat,layout,0); hull(s,boat,layout,1); box(s,{0,.66f,-.5f},{6.0f,.14f,8.8f},{.78f,.8f,.74f},&boat); }
    else hull(s,boat,layout,0);
    box(s,{0,.85f,.4f},{2.15f,.45f,3.8f},{.93f,.93f,.84f},&boat);
    box(s,{0,1.22f,.65f},{1.8f,.3f,2.7f},{.08f,.19f,.22f},&boat);
    box(s,{0,1.42f,.65f},{1.9f,.13f,2.8f},{.91f,.92f,.84f},&boat);
    box(s,{0,3.1f,1.4f},{.12f,4.4f,.12f},{.64f,.68f,.65f},&boat);
    box(s,{0,4.45f,-.2f},{.09f,.1f,3.2f},{.56f,.6f,.58f},&boat);
    for(int side=-1;side<=1;side+=2) for(int n=0;n<6;++n) {
        box(s,{side*1.68f,.99f,-4.5f+n*1.4f},{.05f,.6f,.05f},{.52f,.58f,.59f},&boat);
        box(s,{side*1.68f,1.25f,-3.8f+n*1.4f},{.045f,.045f,1.4f},{.52f,.58f,.59f},&boat);
    }
    for(Station station:{Station::Helm,Station::Port,Station::Starboard,Station::Bow}) {
        auto p=stationPosition(station); p.y=.77f;
        box(s,p,{.6f,.08f,.6f},station==Station::Helm?Vec3{.9f,.52f,.1f}:Vec3{.1f,.62f,.62f},&boat);
    }
    box(s,{0,1.0f,-3.35f},{.25f,.7f,.25f},{.2f,.25f,.25f},&boat);
    auto person=boat.skipperDeck; person.y=.99f;
    box(s,person,{.37f,.58f,.3f},{.98f,.37f,.09f},&boat);
    person.y=1.41f; box(s,person,{.25f,.25f,.25f},{.73f,.5f,.32f},&boat);
    uploadScene(s->vertices[frame],s->mesh,s->count*sizeof(Vertex));
    SceneConstants cameras[3]={};
    float4 sun; look.sunlight(sun);
    cameras[0].viewProjection=cameras[1].viewProjection=cameraMatrix(camera);
    cameras[2].viewProjection=lightProjection(boat.position,sun.getXYZ(),s->shadow->mWidth,64,200);
    for(unsigned p=0;p<3;++p) {
        cameras[p].modeLevel[0]=float(p); cameras[p].modeLevel[1]=level; cameras[p].modeLevel[2]=boat.time;
        float4 effects; look.parameters(cameras[p].weather,effects); cameras[p].sun=sun;
        uploadScene(s->cameras[frame][p],&cameras[p],sizeof(SceneConstants));
    }
    PostConstants post={}; auto eye=cameraEye(camera); auto forward=f3Normalize(f3Sub(toForge(camera.focus),toForge(eye)));
    auto right=f3MulScalar(f3Normalize(f3Cross(make_float3(0,1,0),forward)),float(camera.width)/camera.height/2.41421356f);
    auto up=f3MulScalar(f3Normalize(f3Cross(forward,right)),1/2.41421356f);
    post.forward=float4(forward,float(s->skyHeight)); post.right=float4(right); post.up=float4(up); post.sun=sun;
    look.parameters(post.weather,post.effects); post.eye=float4(toForge(eye));
    float4 detail,optics,foam; look.fidelity(detail,post.quality,optics,foam);
    post.shadowMatrix=cameras[2].viewProjection; post.screen[3]=boat.time;
    post.screen[0]=float(camera.width); post.screen[1]=float(camera.height);
    for(unsigned p=0;p<6;++p) {
        post.screen[2]=float(p);
        auto* source=p>=4?s->bloom[p-4]:s->composite;
        post.bloom={look.bloom,look.bloomThreshold,1.0f/source->mWidth,1.0f/source->mHeight};
        uploadScene(s->postCameras[frame][p],&post,sizeof post);
    }
    WaterLook activeLook=look;
    if(look.debugView==37) activeLook.debugView=0;
    activeLook.shadowSize=int(s->shadow->mWidth);
    activeLook.waterLightSize=int(s->waterLight->mWidth);
    activeLook.reflectionDivisor=int((camera.width+s->reflection->mWidth-1)/s->reflection->mWidth);
    // Conservative bounds from the same triangles used by the reflection
    // capture. Open-water pixels can reject rays without any texture fetches.
    float2 low={1,1}, high={0,0};
    for(unsigned triangle=0;triangle<s->count;triangle+=3) {
        if(std::max({s->mesh[triangle].position.y,s->mesh[triangle+1].position.y,s->mesh[triangle+2].position.y})<level-.04f) continue;
        bool crossesEye=false;
        for(unsigned vertex=triangle;vertex<triangle+3;++vertex) {
            auto p=s->mesh[vertex].position;
            auto clip=f4x4Mulf4(cameras[0].viewProjection,make_float4(p.x,2*level-p.y,p.z,1));
            if(clip.w<.2f) { crossesEye=true; break; }
            float2 uv={clip.x/clip.w*.5f+.5f,.5f-clip.y/clip.w*.5f};
            low=f2MinPerElem(low,uv); high=f2MaxPerElem(high,uv);
        }
        if(crossesEye) { low={0,0}; high={1,1}; break; }
    }
    const float4 bounds={std::max(0.0f,low.x-.002f),std::max(0.0f,low.y-.002f),std::min(1.0f,high.x+.002f),std::min(1.0f,high.y+.002f)};
    float2 shadowLow={1,1}, shadowHigh={0,0};
    for(unsigned vertex=seabedVertices;vertex<s->count;++vertex) {
        auto clip=f4x4Mulf4(cameras[2].viewProjection,float4(toForge(s->mesh[vertex].position),1));
        float2 uv={clip.x*.5f+.5f,.5f-clip.y*.5f};
        shadowLow=f2MinPerElem(shadowLow,uv); shadowHigh=f2MaxPerElem(shadowHigh,uv);
    }
    // Include the complete PCF footprint around the projected caster bounds.
    const float margin=2.0f/s->shadow->mWidth;
    const float4 shadowBounds={shadowLow.x-margin,shadowLow.y-margin,shadowHigh.x+margin,shadowHigh.y+margin};
    prepareOceanEffects(water,cmd,camera,frame,level,cameras[2].viewProjection,activeLook,boat,layout,bounds,profile,shadowBounds);
    beginWaterPass(cmd,profile,WaterGeometry);
    TFRenderTarget* targets[]={s->opaque,s->reflection,s->shadow};
    TFRenderTarget* depths[]={s->depth,s->reflectionDepth,s->shadowDepth};
    uint32_t stride=sizeof(Vertex); uint64_t offset=0;
    for(unsigned p=0;p<3;++p) {
        transitionTarget(cmd,targets[p],true);
        bindSceneTarget(cmd,targets[p],depths[p],TF_LOAD_ACTION_CLEAR);
        cmdBindPipeline(cmd,p==2?s->shadowPipeline:s->pipeline); cmdBindDescriptorSet(cmd,frame*3+p,s->descriptors);
        cmdBindVertexBuffer(cmd,1,&s->vertices[frame],&stride,&offset); cmdDraw(cmd,s->count,0);
        transitionTarget(cmd,targets[p],false); cmdBindRenderTargets(cmd,nullptr);
    }
    transitionTarget(cmd,s->waterLight,true);
    bindSceneTarget(cmd,s->waterLight,s->waterLightDepth,TF_LOAD_ACTION_CLEAR);
    drawOceanLight(water,cmd,frame);
    transitionTarget(cmd,s->waterLight,false); cmdBindRenderTargets(cmd,nullptr);
    endWaterPass(cmd,profile,WaterGeometry);
    beginWaterPass(cmd,profile,WaterSurface);
    transitionTarget(cmd,s->composite,true);
    // Sky, water and spray share one pass. Starting a second pass without a
    // write-to-write dependency let the sky copy overwrite water tiles on Metal.
    bindSceneTarget(cmd,s->composite,s->depth,TF_LOAD_ACTION_DONTCARE,TF_LOAD_ACTION_LOAD);
    cmdBindPipeline(cmd,s->copyPipeline); cmdBindDescriptorSet(cmd,frame*6,s->postDescriptors); cmdDraw(cmd,3,0);
    drawOcean(water,cmd,frame);
    transitionTarget(cmd,s->composite,false); cmdBindRenderTargets(cmd,nullptr);
    endWaterPass(cmd,profile,WaterSurface);
    beginWaterPass(cmd,profile,WaterPost);
    if(look.shaftSteps>0 && look.shafts>0 && (look.debugView==0 || look.debugView==20)) {
        transitionTarget(cmd,s->airRays,true);
        bindSceneTarget(cmd,s->airRays,nullptr,TF_LOAD_ACTION_DONTCARE);
        cmdBindPipeline(cmd,s->airPipeline); cmdBindDescriptorSet(cmd,frame*6+2,s->postDescriptors); cmdDraw(cmd,3,0);
        transitionTarget(cmd,s->airRays,false); cmdBindRenderTargets(cmd,nullptr);
    }
    if(look.bloom>0 || look.debugView==37) for(unsigned level=0;level<TF_ARRAY_COUNT(s->bloom);++level) {
        transitionTarget(cmd,s->bloom[level],true);
        bindSceneTarget(cmd,s->bloom[level],nullptr,TF_LOAD_ACTION_DONTCARE);
        cmdBindPipeline(cmd,s->airPipeline); cmdBindDescriptorSet(cmd,frame*6+3+level,s->postDescriptors); cmdDraw(cmd,3,0);
        transitionTarget(cmd,s->bloom[level],false); cmdBindRenderTargets(cmd,nullptr);
    }
    bindSceneTarget(cmd,target,nullptr,TF_LOAD_ACTION_DONTCARE);
    cmdBindPipeline(cmd,s->postPipeline); cmdBindDescriptorSet(cmd,frame*6+1,s->postDescriptors); cmdDraw(cmd,3,0);
    TFRenderTargetBarrier uiBarrier={target,TF_RESOURCE_STATE_RENDER_TARGET,TF_RESOURCE_STATE_RENDER_TARGET};
    cmdResourceBarrier(cmd,0,nullptr,0,nullptr,1,&uiBarrier);
    cmdBindRenderTargets(cmd,nullptr);
    endWaterPass(cmd,profile,WaterPost);
}
}
