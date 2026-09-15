// Particle kind is stored in metadata.x in the fixed GPU pool.
#define WATER_DROPLET 0
#define WATER_RAIN 1
#define WATER_FOAM 2
#define WATER_SHEET 3
#define WATER_MIST 4
#define WATER_FROTH 5
#define WATER_CREST_CELL_SIZE 4.0
#define WATER_PARTICLE_VERTICES 24

STRUCT(WaterParameters)
{
    DATA(float4x4, viewProjection, None);
    DATA(float4x4, shadowMatrix, None);
    DATA(float4, eye, None);
    DATA(float4, surface, None);
    DATA(float4, origin, None);
    DATA(float4, patch, None);
    DATA(float4, screen, None);
    DATA(float4, current, None);
    DATA(float4, weather, None);
    DATA(float4, effects, None);
    DATA(float4, history, None);
    DATA(float4, boatPosition, None);
    DATA(float4, boatForward, None);
    DATA(float4, boatVelocity, None);
    DATA(float4, wind, None);
    DATA(float4, detail, None);
    DATA(float4, quality, None);
    DATA(float4, optics, None);
    DATA(float4, foamSettings, None);
    DATA(float4, foamPatch, None);
    DATA(float4, sun, None);
    DATA(float4, hulls, None);
    DATA(float4, propellers[2], None);
    DATA(float4, trace, None);
    DATA(float4, seabed, None);
    DATA(float4, reflectionBounds, None);
    DATA(float4, shadowBounds, None);
    DATA(float4, foamAppearance, None);
    DATA(float4x4, lightProjection, None);
    DATA(float4, volume, None);
};
BEGIN_SRT(WaterDraw)
    BEGIN_SRT_SET(PerFrame)
        DECL_CBUFFER(PerFrame, CBUFFER(WaterParameters), gWater)
        DECL_BUFFER(PerFrame, Buffer(float4), gWaterSurface)
        DECL_BUFFER(PerFrame, Buffer(float4), gPreviousWaterSurface)
        DECL_BUFFER(PerFrame, Buffer(float4), gWaterNormals)
        DECL_BUFFER(PerFrame, Buffer(min16float4), gWhitewater)
        DECL_BUFFER(PerFrame, Buffer(min16float4), gLocalWhitewater)
        DECL_BUFFER(PerFrame, Buffer(min16float4), gLocalWhitewaterHistory)
        DECL_RWBUFFER(PerFrame, RWBuffer(min16float4), gLocalWhitewaterOutput)
        DECL_BUFFER(PerFrame, Buffer(float4), gLocalSurface)
        DECL_TEXTURE(PerFrame, Tex2D(float4), gOpaque)
        DECL_TEXTURE(PerFrame, Tex2D(float4), gReflection)
        DECL_TEXTURE(PerFrame, Tex2D(float), gShadow)
        DECL_TEXTURE(PerFrame, Tex2D(float), gWaterLight)
        DECL_TEXTURE(PerFrame, Tex2D(float4), gFoamTexture)
        DECL_TEXTURE(PerFrame, Tex3D(float4), gCloudNoiseTexture)
        DECL_RWTEXTURE(PerFrame, RWTex3D(float4), gCloudNoiseOutput)
        DECL_BUFFER(PerFrame, Buffer(float4), gFoamDraw)
        DECL_BUFFER(PerFrame, Buffer(float4), gSprayDraw)
        DECL_BUFFER(PerFrame, Buffer(float4), gFoamHistory)
        DECL_RWBUFFER(PerFrame, RWBuffer(float4), gFoamOutput)
        DECL_BUFFER(PerFrame, Buffer(float4), gSprayHistory)
        DECL_RWBUFFER(PerFrame, RWBuffer(float4), gSprayOutput)
        DECL_BUFFER(PerFrame, Buffer(float4), gCrestHistory)
        DECL_BUFFER(PerFrame, Buffer(float4), gCrestDraw)
        DECL_RWBUFFER(PerFrame, RWBuffer(float4), gCrestOutput)
        DECL_TEXTURE(PerFrame, Tex2D(float4), gSkyRadiance)
        DECL_RWTEXTURE(PerFrame, RWTex2D(float4), gSkyOutput)
    END_SRT_SET(PerFrame)
END_SRT(WaterDraw)
