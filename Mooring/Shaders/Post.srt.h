#include "WaterDebug.h.fsl"

STRUCT(PostConstants)
{
    DATA(float4, forward, None);
    DATA(float4, right, None);
    DATA(float4, up, None);
    DATA(float4, screen, None);
    DATA(float4, eye, None);
    DATA(float4, weather, None);
    DATA(float4, effects, None);
    DATA(float4x4, shadowMatrix, None);
    DATA(float4, quality, None);
    DATA(float4, sun, None);
    DATA(float4, bloom, None);
};
BEGIN_SRT(MarinaPost)
    BEGIN_SRT_SET(PerFrame)
        DECL_CBUFFER(PerFrame, CBUFFER(PostConstants), gPost)
        DECL_TEXTURE(PerFrame, Tex2D(float4), gSceneColor)
        DECL_TEXTURE(PerFrame, Tex2D(float4), gAirRays)
        DECL_TEXTURE(PerFrame, Tex2D(float4), gBloomNear)
        DECL_TEXTURE(PerFrame, Tex2D(float4), gBloomMiddle)
        DECL_TEXTURE(PerFrame, Tex2D(float4), gBloomFar)
        DECL_TEXTURE(PerFrame, Tex2D(float), gPostShadow)
        DECL_TEXTURE(PerFrame, Tex3D(float4), gCloudNoiseTexture)
        DECL_TEXTURE(PerFrame, Tex2D(float4), gSkyRadiance)
        DECL_SAMPLER(PerFrame, SamplerState, gSceneSampler)
    END_SRT_SET(PerFrame)
END_SRT(MarinaPost)
