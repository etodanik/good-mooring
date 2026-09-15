STRUCT(OceanParameters)
{
    DATA(float4, clockDepth, None);
    DATA(uint4, transform, None);
    DATA(float4, lengths, None);
    DATA(float4, current, None);
    DATA(float4, patch, None);
    DATA(float4, foamSettings, None);
    DATA(float4, breaking, None);
};
BEGIN_SRT(OceanCompute)
    BEGIN_SRT_SET(PerFrame)
        DECL_CBUFFER(PerFrame, CBUFFER(OceanParameters), gOcean)
        DECL_BUFFER(PerFrame, Buffer(float4), gSpectrum)
        DECL_BUFFER(PerFrame, Buffer(float4), gSource)
        DECL_RWBUFFER(PerFrame, RWBuffer(float4), gDestination)
        DECL_BUFFER(PerFrame, Buffer(float4), gHistory)
        DECL_RWBUFFER(PerFrame, RWBuffer(float4), gNormals)
        DECL_BUFFER(PerFrame, Buffer(min16float4), gWhitewaterHistory)
        DECL_RWBUFFER(PerFrame, RWBuffer(min16float4), gWhitewaterOutput)
        DECL_BUFFER(PerFrame, Buffer(float2), gTwiddles)
        DECL_BUFFER(PerFrame, Buffer(float4), gPackets)
    END_SRT_SET(PerFrame)
END_SRT(OceanCompute)
