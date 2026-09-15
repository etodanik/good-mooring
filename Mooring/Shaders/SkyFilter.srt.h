STRUCT(SkyFilterParameters)
{
    DATA(uint4, dimensions, None);
};
BEGIN_SRT(SkyFilter)
    BEGIN_SRT_SET(PerFrame)
        DECL_CBUFFER(PerFrame, CBUFFER(SkyFilterParameters), gFilter)
        DECL_RWTEXTURE(PerFrame, RWTex2D(float4), gRadiance)
        DECL_RWTEXTURE(PerFrame, RWTex2D(float4), gFiltered)
    END_SRT_SET(PerFrame)
END_SRT(SkyFilter)
