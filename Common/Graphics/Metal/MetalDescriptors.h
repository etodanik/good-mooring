// Apple compatibility layer for the explicit view API in Forge 5abcdd9.
// Included by MetalRenderer.mm after the native types and allocation hooks.

static void metalInitTextureDescriptor(const TFTextureDescriptorDesc* desc, TFTextureDescriptor* view)
{
    ASSERT(desc && desc->pTexture);
    const TFTexture* texture = desc->pTexture;
    TinyImageFormat  format = desc->mFormat == TinyImageFormat_UNDEFINED ? (TinyImageFormat)texture->mFormat : desc->mFormat;
    MTLPixelFormat   metalFormat = (MTLPixelFormat)TinyImageFormat_ToMTLPixelFormat(format);
    if (desc->mIsStencil)
    {
        metalFormat =
            texture->pTexture.pixelFormat == MTLPixelFormatDepth32Float_Stencil8 ? MTLPixelFormatX32_Stencil8 : MTLPixelFormatStencil8;
    }
    const uint32_t mipCount = desc->mMipLevelCount ? desc->mMipLevelCount : texture->mMipLevels - desc->mBaseMipLevel;
    const uint32_t layerCount = desc->mArrayLayerCount ? desc->mArrayLayerCount : texture->mArraySizeMinusOne + 1 - desc->mBaseArrayLayer;
    ASSERT(mipCount && desc->mBaseMipLevel + mipCount <= texture->mMipLevels);
    ASSERT(layerCount && desc->mBaseArrayLayer + layerCount <= texture->mArraySizeMinusOne + 1);
    MTLTextureType type = texture->pTexture.textureType;
    if ((desc->mDescriptors & TF_DESCRIPTOR_TYPE_RW_TEXTURE) && (type == MTLTextureTypeCube || type == MTLTextureTypeCubeArray))
        type = MTLTextureType2DArray;
    view->pSrvUav = [texture->pTexture newTextureViewWithPixelFormat:metalFormat
                                                         textureType:type
                                                              levels:NSMakeRange(desc->mBaseMipLevel, mipCount)
                                                              slices:NSMakeRange(desc->mBaseArrayLayer, layerCount)];
    view->mFormat = format;
    ASSERT(view->pSrvUav);
}

void addTextureDescriptor(TFRenderer* renderer, const TFTextureDescriptorDesc* desc, TFTextureDescriptor** output)
{
    UNREF_PARAM(renderer);
    auto* view = (TFTextureDescriptor*)tf_calloc_memalign(1, alignof(TFTextureDescriptor), sizeof(TFTextureDescriptor));
    metalInitTextureDescriptor(desc, view);
    *output = view;
}

void removeTextureDescriptor(TFRenderer* renderer, TFTextureDescriptor* view)
{
    UNREF_PARAM(renderer);
    view->pSrvUav = nil;
    tf_free(view);
}

static void metalInitRenderTargetDescriptor(const TFRenderTargetDescriptorDesc* desc, TFRenderTargetDescriptor* view)
{
    ASSERT(desc && desc->pRenderTarget);
    view->pRenderTarget = desc->pRenderTarget;
    view->pTexture = desc->pRenderTarget->pTexture->pTexture;
    view->mMipLevel = desc->mUseMipSlice ? desc->mMipSlice : 0;
    view->mSlice = desc->mUseArraySlice ? desc->mArraySlice : 0;
    view->mMipSlice = desc->mMipSlice;
    view->mArraySlice = desc->mArraySlice;
    view->mUseMipSlice = desc->mUseMipSlice;
    view->mUseArraySlice = desc->mUseArraySlice;
}

void addRenderTargetDescriptor(TFRenderer* renderer, const TFRenderTargetDescriptorDesc* desc, TFRenderTargetDescriptor** output)
{
    UNREF_PARAM(renderer);
    auto* view = (TFRenderTargetDescriptor*)tf_calloc_memalign(1, alignof(TFRenderTargetDescriptor), sizeof(TFRenderTargetDescriptor));
    metalInitRenderTargetDescriptor(desc, view);
    *output = view;
}

void removeRenderTargetDescriptor(TFRenderer* renderer, TFRenderTargetDescriptor* view)
{
    UNREF_PARAM(renderer);
    view->pTexture = nil;
    tf_free(view);
}
