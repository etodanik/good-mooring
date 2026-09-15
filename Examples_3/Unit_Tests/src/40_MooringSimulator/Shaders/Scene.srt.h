STRUCT(SceneConstants)
{
    DATA(float4x4, viewProjection, None);
    DATA(float4, modeLevel, None);
    DATA(float4, weather, None);
    DATA(float4, sun, None);
};
BEGIN_SRT(MarinaScene)
    BEGIN_SRT_SET(PerFrame)
        DECL_CBUFFER(PerFrame, CBUFFER(SceneConstants), gCamera)
        DECL_TEXTURE(PerFrame, Tex3D(float4), gCloudNoiseTexture)
    END_SRT_SET(PerFrame)
END_SRT(MarinaScene)
