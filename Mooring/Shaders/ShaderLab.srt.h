#ifndef MOORING_SHADER_LAB_RESOURCES
#define MOORING_SHADER_LAB_RESOURCES

STRUCT(ShaderLabParameters)
{
    DATA(float4, origin, None);
    DATA(float4, extent, None);
    // Integer sampling, slice axis, slice coordinate, resource kind.
    DATA(float4, settings, None);
    // Display minimum, maximum, component (-1 = RGB), signed colour ramp.
    DATA(float4, display, None);
    DATA(float4, parameters, None);
    DATA(uint4, outputSize, None);
    // First word, row stride in elements, element stride in words, element count.
    DATA(uint4, bufferLayout, None);
    // Mip, reserved, buffer scalar type (0 float, 1 half, 2 uint), components.
    DATA(float4, texture, None);
};
BEGIN_SRT(ShaderLabResources)
    BEGIN_SRT_SET(PerDraw)
        DECL_CBUFFER(PerDraw, CBUFFER(ShaderLabParameters), gShaderLab)
        DECL_RWBUFFER(PerDraw, RWBuffer(float4), gPreviewSamples)
        DECL_BUFFER(PerDraw, Buffer(uint), gPreviewBuffer)
        DECL_TEXTURE(PerDraw, Tex2D(float4), gPreviewTexture)
        DECL_TEXTURE(PerDraw, Tex3D(float4), gPreviewVolume)
    END_SRT_SET(PerDraw)
END_SRT(ShaderLabResources)
#endif
