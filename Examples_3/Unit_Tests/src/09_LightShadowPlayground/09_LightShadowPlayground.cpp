/*
 * Copyright (c) 2017-2026 The Forge Interactive Inc.
 *
 * This file is part of The-Forge
 * (see https://github.com/ConfettiFX/The-Forge).
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#define ESM_MSAA_SAMPLES     1
#define SHADOWMAP_RES        2048u
#define ESM_SHADOWMAP_RES    SHADOWMAP_RES
#define VSM_SHADOWMAP_RES    SHADOWMAP_RES
#define NUM_SDF_MESHES       3
#define MAX_BLUR_KERNEL_SIZE 16
#define TEST_GPU_BREADCRUMBS 1
#define MSAA_STENCIL_MASK    1

#include "../../../../Common_3/Utilities/ThirdParty/OpenSource/Nothings/stb_ds.h"

// Hint
#define STB_DS_ARRAY(x)   x*
#define STB_HASH_ARRAY(x) x*

// Interfaces
#include "../../../../Common_3/Application/Interfaces/IApp.h"
#include "../../../../Common_3/Application/Interfaces/ICamera.h"
#include "../../../../Common_3/Application/Interfaces/IFont.h"
#include "../../../../Common_3/Application/Interfaces/IProfiler.h"
#include "../../../../Common_3/Application/Interfaces/IScreenshot.h"
#include "../../../../Common_3/Application/Interfaces/IUI.h"
#include "../../../../Common_3/Game/Interfaces/IScripting.h"
#include "../../../../Common_3/Graphics/Interfaces/IGraphics.h"
#include "../../../../Common_3/OS/Interfaces/IInput.h"
#include "../../../../Common_3/Renderer/Interfaces/IVisibilityBuffer.h"
#include "../../../../Common_3/Resources/ResourceLoader/Interfaces/IResourceLoader.h"
#include "../../../../Common_3/Utilities/Interfaces/IFileSystem.h"
#include "../../../../Common_3/Utilities/Interfaces/ILog.h"
#include "../../../../Common_3/Utilities/Interfaces/IMath.h"
#include "../../../../Common_3/Utilities/Interfaces/IThread.h"
#include "../../../../Common_3/Utilities/Interfaces/ITime.h"

#include "../../../../Common_3/Utilities/RingBuffer.h"
#include "../../../../Common_3/Utilities/Threading/ThreadSystem.h"

// Math
#include "../../../../Common_3/Utilities/Interfaces/IMath.h"

#include "../../../../Common_3/Utilities/Math/ShaderUtilities.h"

// clang-format off
#define NO_FSL_DEFINITIONS
#include "../../../../Common_3/Graphics/FSL/fsl_srt.h"
#include "Shaders/FSL/ShaderDefs.h.fsl"
#include "Shaders/FSL/VisibilityBufferResources.h.fsl"
#include "Shaders/FSL/ASMShaderDefs.h.fsl"
#include "Shaders/FSL/SDFConstants.h.fsl"
// clang-format on

#include "../../../Visibility_Buffer/src/SanMiguelSDF.h"

#include "../../../../Common_3/Utilities/Interfaces/IMemory.h"

// fsl
#include "../../../../Common_3/Graphics/FSL/defaults.h"
#include "./Shaders/FSL/Display.srt.h"
#include "./Shaders/FSL/GaussianBlur.srt.h"
#include "./Shaders/FSL/Global.srt.h"
#include "./Shaders/FSL/PackedQuads.srt.h"
#include "./Shaders/FSL/SDFMesh.srt.h"
#include "./Shaders/FSL/ScreenSpaceShadows.srt.h"
#include "./Shaders/FSL/TriangleFiltering.srt.h"
#include "./Shaders/FSL/UpdateRegion3DTexture.srt.h"
#include "./Shaders/FSL/VisibilityBuffer.srt.h"
#include "./Shaders/FSL/VisibilityBufferDepthPass.srt.h"

#define Epilson                     (1.e-4f)
#define MIN_BINDLESS_TEXTURES_COUNT 1024
#define SAN_MIGUEL_ORIGINAL_SCALE   50.0f
#define SAN_MIGUEL_ORIGINAL_OFFSETX -20.f

#define SAN_MIGUEL_OFFSETX          150.f
#define MESH_COUNT                  1
#define MESH_SCALE                  10.f
#define ASM_SUN_SPEED               0.05f
#define ASM_MAX_TILES_HORIZONTAL    (ASM_WORK_BUFFER_DEPTH_PASS_WIDTH / gs_ASMTileSize)
#define ASM_MAX_TILES_VERTICAL      (ASM_WORK_BUFFER_DEPTH_PASS_HEIGHT / gs_ASMTileSize)
#define ASM_MAX_TILES_PER_PASS      (ASM_MAX_TILES_HORIZONTAL * ASM_MAX_TILES_VERTICAL)
#ifndef VR_MULTIVIEW_COUNT
#define VR_MULTIVIEW_COUNT 1
#endif

#define FOREACH_SETTING(X)          \
    X(AddGeometryPassThrough, 0)    \
    X(BindlessSupported, 1)         \
    X(MSAASampleCount, 4)           \
    X(DisableScreenSpaceShadows, 0) \
    X(MaxMSAALevel, 4)

#define GENERATE_ENUM(x, y)   x,
#define GENERATE_STRING(x, y) #x,
#define GENERATE_STRUCT(x, y) uint32_t m##x;
#define GENERATE_VALUE(x, y)  y,
#define INIT_STRUCT(s)        s = { FOREACH_SETTING(GENERATE_VALUE) }

typedef enum ESettings
{
    FOREACH_SETTING(GENERATE_ENUM) Count
} ESettings;

const char* gSettingNames[] = { FOREACH_SETTING(GENERATE_STRING) };

// Useful for using names directly instead of subscripting an array
struct ConfigSettings
{
    FOREACH_SETTING(GENERATE_STRUCT)
} gGpuSettings;

// Kinda: 0.09f // thin: 0.15f // superthin: 0.5f // triplethin: 0.8f
MeshInfo opaqueMeshInfos[] = {
    { "SDF/twosided_superthin_innerfloor_01", MATERIAL_FLAG_TWO_SIDED, 0.5f },
    { "SDF/twosided_triplethin_hugewall_02", MATERIAL_FLAG_TWO_SIDED, 0.8f },
    { "SDF/twosided_triplethin_hugewall_01", MATERIAL_FLAG_TWO_SIDED, 0.8f },
    { "SDF/balcony_01", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/outerfloor_01", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/curvepillar_05", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/curvepillar_06", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/curvepillar_07", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/curvepillar_11", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/curvepillar_10", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/curvepillar_09", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/indoortable_03", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/outertable_01", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/outdoortable_03", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/outertable_02", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/outertable_04", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/outdoortable_07", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/curvepillar_02", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/outertable_15", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/outertable_14", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/outertable_13", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/outertable_12", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/outdoortable_08", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/outertable_10", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/outertable_06", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/outertable_05", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/outertable_09", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/outertable_11", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/uppertable_04", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/uppertable_03", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/uppertable_01", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/uppertable_02", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/opendoor_01", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/opendoor_02", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/indoortable_01", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/indoortable_05", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/indoortable_04", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/indoortable_02", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/curvepillar_01", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/waterpool_01", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/longpillar_01", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/longpillar_04", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/longpillar_05", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/longpillar_06", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/longpillar_10", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/longpillar_09", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/longpillar_08", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/longpillar_07", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/longpillar_02", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/longpillar_12", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/longpillar_03", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/twosided_thin_leavesbasket_01", MATERIAL_FLAG_TWO_SIDED, 0.15f },
    { "SDF/twosided_kinda_double_combinedoutdoorchairs_01", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_DOUBLE_VOXEL_SIZE, 0.09f },
    { "SDF/gargoyle_04", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/gargoyle_05", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/gargoyle_02", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/gargoyle_06", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/twosided_kinda_double_outdoorchairs_02", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_DOUBLE_VOXEL_SIZE, 0.09f },
    { "SDF/twosided_kinda_double_indoorchairs_02", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_DOUBLE_VOXEL_SIZE, 0.09f },
    { "SDF/twosided_thin_double_indoorchairs_03", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_DOUBLE_VOXEL_SIZE, 0.15f },
    { "SDF/twosided_thin_double_indoorchairs_04", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_DOUBLE_VOXEL_SIZE, 0.15f },
    { "SDF/twosided_kinda_double_indoorchairs_05", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_DOUBLE_VOXEL_SIZE, 0.09f },
    { "SDF/twosided_kinda_double_indoorchairs_06", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_DOUBLE_VOXEL_SIZE, 0.09f },
    { "SDF/double_upperchairs_01", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/longpillar_11", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/twosided_thin_leavesbasket_02", MATERIAL_FLAG_TWO_SIDED, 0.15f },
    { "SDF/twosided_kinda_double_outdoorchairs_01", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_DOUBLE_VOXEL_SIZE, 0.09f },
    { "SDF/twosided_kinda_double_outdoorchairs_04", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_DOUBLE_VOXEL_SIZE, 0.09f },
    { "SDF/double_upperchairs_02", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/double_upperchairs_03", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/double_upperchairs_04", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/doorwall_01", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/doorwall_02", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/underledge_01", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/underceil_01", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/underceil_02", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/twosided_kinda_double_indoorchairs_07", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_DOUBLE_VOXEL_SIZE, 0.09f },
    { "SDF/hugewallfront_01", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/twosided_kinda_metalLedges_02", MATERIAL_FLAG_TWO_SIDED, 0.09f },
    { "SDF/twosided_kinda_metalLedges_07", MATERIAL_FLAG_TWO_SIDED, 0.09f },
    { "SDF/twosided_kinda_metalLedges_01", MATERIAL_FLAG_TWO_SIDED, 0.09f },
    { "SDF/twosided_kinda_metalLedges_05", MATERIAL_FLAG_TWO_SIDED, 0.09f },
    { "SDF/twosided_kinda_metalLedges_03", MATERIAL_FLAG_TWO_SIDED, 0.09f },
    { "SDF/twosided_kinda_metalLedges_04", MATERIAL_FLAG_TWO_SIDED, 0.09f },
    { "SDF/twosided_kinda_metalLedges_06", MATERIAL_FLAG_TWO_SIDED, 0.09f },
    { "SDF/twosided_kinda_double_outdoorchairs_06", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_DOUBLE_VOXEL_SIZE, 0.09f },
    { "SDF/twosided_kinda_double_outdoorchairs_05", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_DOUBLE_VOXEL_SIZE, 0.09f },
    { "SDF/twosided_kinda_metalLedges_08", MATERIAL_FLAG_TWO_SIDED, 0.09f },
    { "SDF/twosided_kinda_metalLedges_09", MATERIAL_FLAG_TWO_SIDED, 0.09f },
    { "SDF/twosided_kinda_doorwall_03", MATERIAL_FLAG_TWO_SIDED, 0.09f },
    { "SDF/twosided_kinda_doorwall_04", MATERIAL_FLAG_TWO_SIDED, 0.09f },
    { "SDF/myinnerfloor_01", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/twosided_kinda_double_outdoorchairs_07", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_DOUBLE_VOXEL_SIZE, 0.09f },
    { "SDF/twosided_kinda_double_outdoorchairs_08", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_DOUBLE_VOXEL_SIZE, 0.09f },
    { "SDF/mywall_01", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/twosided_kinda_double_outdoorchairs_09", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_DOUBLE_VOXEL_SIZE, 0.09f },
    { "SDF/twosided_kinda_double_outdoorchairs_10", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_DOUBLE_VOXEL_SIZE, 0.09f },
    { "SDF/twosided_kinda_double_outdoorchairs_11", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_DOUBLE_VOXEL_SIZE, 0.09f },
    { "SDF/twosided_kinda_double_outdoorchairs_12", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_DOUBLE_VOXEL_SIZE, 0.09f },
    { "SDF/twosided_kinda_double_outdoorchairs_13", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_DOUBLE_VOXEL_SIZE, 0.09f },
    { "SDF/twosided_kinda_double_outdoorchairs_14", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_DOUBLE_VOXEL_SIZE, 0.09f },
    { "SDF/twosided_kinda_double_outdoorchairs_15", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_DOUBLE_VOXEL_SIZE, 0.09f },
    { "SDF/twosided_kinda_metalLedges_10", MATERIAL_FLAG_TWO_SIDED, 0.09f },
    { "SDF/curvepillar_12", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/curvepillar_08", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/twosided_superthin_innerhall_01", MATERIAL_FLAG_TWO_SIDED, 0.5f },
    { "SDF/doorwall_06", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/doorwall_05", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/double_doorwall_07", MATERIAL_FLAG_DOUBLE_VOXEL_SIZE, 0.0f },
    { "SDF/twosided_kinda_basketonly_01", MATERIAL_FLAG_TWO_SIDED, 0.09f },
    { "SDF/backmuros_01", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/shortceil_01", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/shortceil_02", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/shortceil_03", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/cap03", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/cap01", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/cap02", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/cap04", MATERIAL_FLAG_NONE, 0.0f }
};

MeshInfo flagsMeshInfos[] = {
    { "SDF/gargoyle_08", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/twosided_superthin_forgeflags_01", MATERIAL_FLAG_TWO_SIDED, 0.5f },
    { "SDF/twosided_superthin_forgeflags_02", MATERIAL_FLAG_TWO_SIDED, 0.5f },
    { "SDF/twosided_superthin_forgeflags_04", MATERIAL_FLAG_TWO_SIDED, 0.5f },
    { "SDF/twosided_superthin_forgeflags_03", MATERIAL_FLAG_TWO_SIDED, 0.5f },
    { "SDF/gargoyle_01", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/gargoyle_07", MATERIAL_FLAG_NONE, 0.0f },
    { "SDF/gargoyle_03", MATERIAL_FLAG_NONE, 0.0f },
};

MeshInfo alphaTestedMeshInfos[] = {
    // group 0
    { "SDF/twosided_thin_alphatested_smallLeaves04_beginstack0", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "flower0339_continuestack0", MATERIAL_FLAG_NONE, 0.0f },
    { "stem01_continuestack0", MATERIAL_FLAG_NONE, 0.0f },
    { "basket01_continuestack0", MATERIAL_FLAG_NONE, 0.0f },
    // group 1
    { "SDF/twosided_thin_alphatested_smallLeaves04_beginstack1", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "stem_continuestack1", MATERIAL_FLAG_NONE, 0.0f },
    { "smallLeaves019_alphatested_continuestack1", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "smallLeaves0377_alphatested_continuestack1", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "basket01_continuestack1", MATERIAL_FLAG_NONE, 0.0f },
    { "rose00_alphatested_continuestack1", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    // group 2
    { "SDF/twosided_thin_alphatested_smallLeaves00_beginstack2", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "alphatested_smallLeaves023_continuestack2", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "stem02_continuestack2", MATERIAL_FLAG_NONE, 0.0f },
    { "alphatested_flower0304_continuestack2", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "stem03_continuestack2", MATERIAL_FLAG_NONE, 0.0f },
    { "basket02_continuestack2", MATERIAL_FLAG_NONE, 0.0f },
    // group 3
    { "SDF/twosided_thin_alphatested_smallLeaves04_beginstack3", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "SDF/twosided_thin_alphatested_smallLeaves09_continuestack3", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "basket_continuestack3", MATERIAL_FLAG_NONE, 0.0f },
    // group 4
    { "SDF/twosided_thin_alphatested_smallLeaves022_beginstack4", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "basket_continuestack4", MATERIAL_FLAG_NONE, 0.0f },
    // group 5
    { "SDF/twosided_thin_alphatested_smallLeaves013_beginstack5", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "alphatested_smallLeaves012_continuestack5", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_smallLeaves020_continuestack5", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_SDF/twosided_thin_smallLeaves07_continuestack5", MATERIAL_FLAG_ALPHA_TESTED | MATERIAL_FLAG_TWO_SIDED, 0.15f },
    { "alphatested_SDF/twosided_thin_smallLeaves05_continuestack5", MATERIAL_FLAG_ALPHA_TESTED | MATERIAL_FLAG_TWO_SIDED, 0.15f },
    { "SDF/twosided_thin_alphatested_floor1_continuestack5", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "basket01_continuestack5", MATERIAL_FLAG_NONE, 0.0f },
    // group 6
    { "SDF/twosided_thin_alphatested_smallLeaves023_beginstack6", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "alphatested_smallLeaves0300_continuestack6", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_smallLeaves016_continuestack6", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_flower0341_continuestack6", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "basket01_continuestack6", MATERIAL_FLAG_NONE, 0.0f },
    // group 7
    { "SDF/twosided_thin_alphatested_smallLeaves014_beginstack7", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "alphatested_smallLeaves015_continuestack7", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "basket01_continuestack7", MATERIAL_FLAG_NONE, 0.0f },
    // group 8
    { "SDF/twosided_thin_alphatested_smallLeaves027_beginstack8", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "alphatested_flower0343_continuestack8", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_smallLeaves018_continuestack8", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "basket01_continuestack8", MATERIAL_FLAG_NONE, 0.0f },
    // group 9
    { "SDF/twosided_thin_alphatested_smallLeaves0380_beginstack9", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "alphatested_flower0338_continuestack9", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "bakset01_continuestack9", MATERIAL_FLAG_NONE, 0.0f },
    // group 10
    { "SDF/twosided_thin_alphatested_smallLeaves00_beginstack10", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "alphatested_flower0304_continuestack10", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "basket01_continuestack10", MATERIAL_FLAG_NONE, 0.0f },
    // group 11
    { "SDF/twosided_thin_double_alphatested__group146_beginstack11", MATERIAL_FLAG_ALL, 0.15f },
    { "alphatested_group147_continuestack11", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "branch_group145_continuestack11", MATERIAL_FLAG_NONE, 0.0f },
    // group 12
    { "SDF/twosided_superthin_alphatested_double_treeLeaves04_beginstack12", MATERIAL_FLAG_ALL, 0.5f },
    { "alphatested_treeLeaves00_continuestack12", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_treeLeaves02_continuestack12", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_treeLeaves05_continuestack12", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    // group 13
    { "SDF/twosided_superthin_alphatested_double_treeLeaves08_beginstack13", MATERIAL_FLAG_ALL, 0.5f },
    { "alphatested_treeLeaves05_continuestack13", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_treeLeaves07_continuestack13", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    // group 14
    { "SDF/twosided_superthin_alphatested_double_treeLeaves03_beginstack14", MATERIAL_FLAG_ALL, 0.5f },
    { "alphatested_treeLeaves01_continuestack14", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_treeLeaves06_continuestack14", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    // group 15
    { "SDF/twosided_thin_alphatested_smallLeaves02_beginstack15", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "alphatested_smallLeaves08_continuestack15", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "basket01_continuestack15", MATERIAL_FLAG_NONE, 0.0f },
    // group 16
    { "SDF/twosided_thin_double_alphatested_smallLeaves019_beginstack16", MATERIAL_FLAG_ALL, 0.15f },
    { "alphatested_flower0343_continuestack16", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_smallLeaves018_continuestack16", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_smallLeaves0377_continuestack16", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_rose00_continuestack16", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "basket01_continuestack16", MATERIAL_FLAG_NONE, 0.0f },
    // group 17
    { "SDF/twosided_thin_alphatested_smallLeaves0380_beginstack17", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "alphatested_flower0342_continuestack17", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_smallLeaves07_continuestack17", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_smallLeaves05_continuestack17", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_floor1_continuestack17", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_flower0338_continuestack17", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "basket01_continuestack17", MATERIAL_FLAG_NONE, 0.0f },
    // group 18
    { "SDF/twosided_thin_alphatested_smallLeaves06_beginstack18", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "alphatested_smallLeaves0378_continuestack18", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_flower0344_continuestack18", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_flower0340_continuestack18", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "basket01_continuestack18", MATERIAL_FLAG_NONE, 0.0f },
    // group 19
    { "SDF/twosided_thin_double_alphatested_smallLeaves00_beginstack19", MATERIAL_FLAG_ALL, 0.15f },
    { "alphatested_smallLeaves016_continuestack19", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_flower0341_continuestack19", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_smallLeaves017_continuestack19", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_smallLeaves021_continuestack19", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_flower0304_continuestack19", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "basket01_continuestack19", MATERIAL_FLAG_NONE, 0.0f },
    // group 20
    { "SDF/twosided_thin_alphatested_smallLeaves024_beginstack20", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "basket01_continuestack20", MATERIAL_FLAG_NONE, 0.0f },
    // group 21
    { "SDF/twosided_thin_alphatested_smallLeaves010_beginstack21", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "basket01_continuestack21", MATERIAL_FLAG_NONE, 0.0f },
    // group 22
    { "SDF/twosided_thin_alphatested_smallLeaves01_beginstack22", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "basket01_continuestack22", MATERIAL_FLAG_NONE, 0.0f },
    // group 23
    { "SDF/twosided_thin_alphatested_smallLeaves04_beginstack23", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "basket01_continuestack23", MATERIAL_FLAG_NONE, 0.0f },
    // group 24
    { "SDF/twosided_thin_alphatested_smallLeaves024_beginstack24", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "basket01_continuestack24", MATERIAL_FLAG_NONE, 0.0f },
    // group 25
    { "SDF/twosided_thin_alphatested_smallLeaves00_beginstack25", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "alphatested_flower0304_continuestack25", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "basket01_continuestack25", MATERIAL_FLAG_NONE, 0.0f },
    // group 26
    { "SDF/twosided_thin_alphatested_smallLeaves00_beginstack26", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "alphatested_smallLeaves023_continuestack26", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_flower0304_continuestack26", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "basket01_continuestack26", MATERIAL_FLAG_NONE, 0.0f },
    // group 27
    { "SDF/twosided_thin_double_alphatested_smallLeaves025_beginstack27", MATERIAL_FLAG_ALL, 0.15f },
    { "alphatested_smallLeaves011_continuestack27", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_smallLeaves021_continuestack27", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_smallLeaves017_continuestack27", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_smallLeaves08_continuestack27", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "basket01_continuestack27", MATERIAL_FLAG_NONE, 0.0f },
    // group 28
    { "SDF/twosided_thin_alphatested_smallLeaves0377_beginstack28", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "alphatested_rose00_continuestack28", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "basket01_continuestack28", MATERIAL_FLAG_NONE, 0.0f },
    // group 29
    { "SDF/twosided_thin_double_alphatested_smallLeaves00_beginstack29", MATERIAL_FLAG_ALL, 0.15f },
    { "alphatested_smallLeaves026_continuestack29", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_flower01_continuestack29", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_smallLeaves09_continuestack29", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_smallLeaves013_continuestack29", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_smallLeaves023_continuestack29", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "alphatested_flower0304_continuestack29", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "basket01_continuestack29", MATERIAL_FLAG_NONE, 0.0f },
    // group 30
    { "SDF/twosided_thin_alphatested_smallLeaves027_beginstack30", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "flower00_continuestack30", MATERIAL_FLAG_NONE, 0.0f },
    { "basket01_continuestack30", MATERIAL_FLAG_NONE, 0.0f },
    // group 31
    { "SDF/twosided_thin_alphatested_smallLeaves013_beginstack31", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "basket01_continuestack31", MATERIAL_FLAG_NONE, 0.0f },
    // group 32
    { "SDF/twosided_thin_alphatested_smallLeaves04_beginstack32", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "basket01_continuestack32", MATERIAL_FLAG_NONE, 0.0f },
    // group 33
    { "SDF/twosided_thin_alphatested_smallLeaves00_beginstack33", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "alphatested_flower0304_continuestack33", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "basket01_continuestack33", MATERIAL_FLAG_NONE, 0.0f },
    // group 34
    { "SDF/twosided_thin_alphatested_smallLeaves0381_beginstack34", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "basket01_continuestack34", MATERIAL_FLAG_NONE, 0.0f },
    // group 35
    { "SDF/twosided_thin_alphatested_smallLeaves0379_beginstack35", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "basket01_continuestack35", MATERIAL_FLAG_NONE, 0.0f },
    // group 36
    { "SDF/twosided_thin_alphatested_smallLeaves021_beginstack36", MATERIAL_FLAG_TWO_SIDED | MATERIAL_FLAG_ALPHA_TESTED, 0.15f },
    { "alphatested_smallLeaves017_continuestack36", MATERIAL_FLAG_ALPHA_TESTED, 0.0f },
    { "basket01_continuestack36", MATERIAL_FLAG_NONE, 0.0f }
};

uint32_t alphaTestedGroupSizes[] = { 4, 6, 6, 3, 2, 7, 5, 3, 4, 3, 3, 3, 4, 3, 3, 3, 6, 7, 5,
                                     7, 2, 2, 2, 2, 2, 3, 4, 6, 3, 8, 3, 2, 2, 3, 2, 2, 3 };

uint32_t alphaTestedMeshIndices[] = {
    30,  12,  31,  32,                      // group 0
    34,  35,  36,  37,  38,  39,            // group 1
    40,  41,  42,  43,  44,  45,            // group 2
    47,  50,  51,                           // group 3
    9,   52,                                // group 4
    53,  6,   25,  55,  57,  59,  60,       // group 5
    61,  14,  63,  65,  66,                 // group 6
    0,   7,   67,                           // group 7
    69,  71,  73,  75,                      // group 8
    77,  79,  80,                           // group 9
    81,  84,  86,                           // group 10
    2,   26,  28,                           // group 11
    33,  5,   16,  85,                      // group 12
    24,  20,  27,                           // group 13
    13,  17,  19,                           // group 14
    83,  82,  87,                           // group 15
    46,  70,  72,  88,  89,  90,            // group 16
    76,  1,   54,  56,  58,  78,  91,       // group 17
    11,  3,   18,  29,  92,                 // group 18
    95,  62,  64,  93,  94,  96,  97,       // group 19
    99,  100,                               // group 20
    23,  101,                               // group 21
    10,  102,                               // group 22
    104, 105,                               // group 23
    98,  106,                               // group 24
    107, 108, 109,                          // group 25
    111, 110, 112, 113,                     // group 26
    15,  21,  117, 118, 119, 120,           // group 27
    114, 115, 116,                          // group 28
    125, 4,   8,   49,  122, 123, 124, 126, // group 29
    68,  74,  127,                          // group 30
    121, 48,                                // group 31
    103, 129,                               // group 32
    128, 130, 131,                          // group 33
    22,  132,                               // group 34
    133, 134,                               // group 35
    135, 136, 137                           // group 36
};

// Define different geometry sets (opaque and alpha tested geometry)
const uint32_t gNumGeomSets = NUM_GEOMETRY_SETS;

enum ShadowType
{
    SHADOW_TYPE_ESM,            // Exponential Shadow Map
    SHADOW_TYPE_ASM,            // Adaptive Shadow Map, has Parallax Corrected Cache algorithm that approximate moving sun's shadow
    SHADOW_TYPE_MESH_BAKED_SDF, // Signed Distance field shadow for mesh using generated baked data
    SHADOW_TYPE_VSM,            // Variance Shadow Map
    SHADOW_TYPE_MSM,            // Moments Shadow Map
    SHADOW_TYPE_COUNT
};

enum BlurPassType
{
    BLUR_PASS_TYPE_HORIZONTAL,
    BLUR_PASS_TYPE_VERTICAL,
    BLUR_PASS_TYPE_COUNT
};

enum Projections
{
    MAIN_CAMERA, // primary view
    SHADOWMAP,
    PROJECTION_ASM,
    PROJECTION_COUNT
};

#define MSAA_LEVELS_COUNT    3U
#define MAX_SSS_WAVE_OFFSETS 8U

struct
{
    bool mHoldFilteredTriangles = false;
    bool mIsGeneratingSDF = false;

    TFSampleCount mMsaaLevel = TF_SAMPLE_COUNT_1;
    uint32_t      mMsaaIndex = (uint32_t)log2((uint32_t)mMsaaLevel);
    uint32_t      mMsaaIndexRequested = mMsaaIndex;
    TFSampleCount mMaxMsaaLevel = TF_SAMPLE_COUNT_4;
} gAppSettings;

typedef struct ObjectInfoStruct
{
    vec4   mColor;
    vec3   mTranslation;
    float3 mScale;
    mat4   mTranslationMat;
    mat4   mScaleMat;
} ObjectInfoStruct;

typedef struct MeshInfoStruct
{
    vec4   mColor;
    float3 mTranslation;
    float3 mOffsetTranslation;
    float3 mScale;
    mat4   mTranslationMat;
    mat4   mScaleMat;
    mat4   mWorldMat;
} MeshInfoStruct;

struct PerFrameData
{
    // Stores the camera/eye position in object space for cluster culling
    vec3     gEyeObjectSpace[NUM_CULLING_VIEWPORTS] = {};
    uint32_t gDrawCount[gNumGeomSets] = { 0 };
};

uint32_t gASMMaxTilesPerPass = 4;

typedef struct LightUniform
{
    mat4 mLightViewProj;
    vec4 mLightPosition;
    vec4 mLightColor = { 1, 0, 0, 1 };
    vec4 mLightUpVec;
    vec4 mTanLightAngleAndThresholdValue;
    vec4 mLightDir;
} LightUniform;

typedef struct MeshSDFConstants
{
    // TODO:
    // Missing center of the object's bbox & the radius of the object
    mat4  mWorldToVolumeMat[SDF_MAX_OBJECT_COUNT];
    vec4  mUVScaleAndVolumeScale[SDF_MAX_OBJECT_COUNT];
    // The local position of the extent in volume dimension space (aka TEXEL space of a 3d texture)
    vec4  mLocalPositionExtent[SDF_MAX_OBJECT_COUNT];
    vec4  mUVAddAndSelfShadowBias[SDF_MAX_OBJECT_COUNT];
    // Store min & max distance for x y, for z it stores the two sided world space mesh distance bias
    vec4  mSDFMAD[SDF_MAX_OBJECT_COUNT];
    uint4 mNumObjects;
} MeshSDFConstants;

struct VSMInputConstants
{
    float2 mBleedingReduction = float2(0.5f, 1.0f);
    float  mMinVariance = 0.00005f;
    float  mPad = 0.0f;
};

struct MSMInputConstants
{
    float2 padding;
    float  mRoundingErrorCorrection = 6.0e-5f;
    float  mBleedingReduction = 0.5f;
};

typedef struct CameraUniform
{
    TFCameraMatrix mView = {};
    TFCameraMatrix mProject = {};
    TFCameraMatrix mViewProject = {};
    TFCameraMatrix mInvView = {};
    TFCameraMatrix mInvProj = {};
    TFCameraMatrix mInvViewProject = {};
    TFCameraMatrix mInverseSkyViewProject = {};
    vec4           mCameraPos = {};
    float          mNear = {};
    float          mFar = {};
    float          mFarNearDiff = {};
    float          mFarNear = {};
    vec2           mTwoOverRes = {};
    vec2           mWindowSize = {};
    vec4           mDeviceZToWorldZ = {};

    VSMInputConstants mVSM = {};
    MSMInputConstants mMSM = {};
    LightUniform      mLight = {};

    uint32_t mShadowType = SHADOW_TYPE_ESM;
    uint32_t mSSSEnabled = 1;
    float    mEsmControl = 80.f;
} CameraUniform;

enum SSSDebugOutputMode
{
    DEBUG_OUTPUT_MODE_NONE,
    DEBUG_OUTPUT_MODE_EDGE_MASK,
    DEBUG_OUTPUT_MODE_THREAD_INDEX,
    DEBUG_OUTPUT_MODE_WAVE_INDEX,
    DEBUG_OUTPUT_MODES_COUNT
};

typedef struct SSSInputConstants
{
    float4   mLightCoordinate = float4(0.0f);
    float4   mScreenSize = float4(0.0f);
    float    mSurfaceThickness = 0.01f;
    float    mBilinearThreshold = 0.035f;
    float    mShadowContrast = 0.7f;
    uint32_t mIgnoreEdgePixels = 0;
    uint32_t mBilinearSamplingOffsetMode = 0;
    uint32_t mDebugOutputMode = 0;
    uint32_t mViewIndex = 0;
} SSSInputConstants;

struct BlurConstant
{
    uint32_t mBlurPassType; // Horizontal or Vertical pass
    uint32_t mFilterRadius;
};

struct gBlurWeights
{
    float mBlurWeights[MAX_BLUR_KERNEL_SIZE];
};

struct QuadDataUniform
{
    mat4 mModelMat;
};

struct ASMAtlasQuadsUniform
{
    vec4 mPosData;
    vec4 mMiscData;
    vec4 mTexCoordData;
};

struct ASMUniformBlock
{
    mat4 mIndexTexMat;
    mat4 mPrerenderIndexTexMat;
    vec4 mSearchVector;
    vec4 mPrerenderSearchVector;
    vec4 mWarpVector;
    vec4 mPrerenderWarpVector;
    // X is for IsPrerenderAvailable or not
    // Y is for whether we are using parallax corrected or not;
    // Z is for whether we are on the wrong side of a shadow/prerender swap
    // W is for penumbra size
    vec4 mMiscBool;
};

uint32_t gSDFNumObjects = 0;

typedef struct UpdateSDFVolumeTextureAtlasConstants
{
    ivec4 mSourceAtlasVolumeMinCoord;
    ivec4 mSourceDimensionSize;
    ivec4 mSourceAtlasVolumeMaxCoord;
} UpdateSDFVolumeTextureAtlasConstants;

struct ASMCpuSettings
{
    float mPenumbraSize = 15.f;
    float mParallaxStepDistance = 50.f;
    float mParallaxStepBias = 80.f;
    bool  mSunCanMove = false;
    bool  mEnableParallax = true;
    bool  mEnableCrossFade = true;
    bool  mShowDebugTextures = false;
};

static ThreadSystem gThreadSystem = NULL;

static float asmCurrentTime = 0.0f;

static bool sShouldExitSDFGeneration = false;
static bool sSDFGenerationFinished = false;
static bool gFloat2RWTextureSupported = true;

bool gUsingTextureAtlasFallback = false;
bool gUsingPrimitiveIDFallback = false;

// #NOTE: Two sets of resources (one in flight and one being used on CPU)
const uint32_t gDataBufferCount = 2;

#define LEFT_EYE_VIEW_INDEX  0
#define RIGHT_EYE_VIEW_INDEX 1

/************************************************************************/
// Render targets
/************************************************************************/
// RT used to have valid bindings when a shadow technique is disabled
TFRenderTarget* pDummyRenderTarget3D = NULL;
TFRenderTarget* pDummyRenderTarget = NULL;
TFRenderTarget* pRenderTargetDepth = NULL;
TFRenderTarget* pRenderTargetShadowMap = NULL;
TFRenderTarget* pRenderTargetVSM[2] = { NULL };
TFRenderTarget* pRenderTargetMSM[2] = { NULL };

TFRenderTarget* pRenderTargetASMColorPass = NULL;
TFRenderTarget* pRenderTargetASMDepthPass = NULL;

TFRenderTarget* pRenderTargetASMDepthAtlas = NULL;
TFRenderTarget* pRenderTargetASMDEMAtlas = NULL;

TFRenderTarget* pRenderTargetASMIndirection[gs_ASMMaxRefinement + 1] = { NULL };
TFRenderTarget* pRenderTargetASMPrerenderIndirection[gs_ASMMaxRefinement + 1] = { NULL };

TFRenderTarget* pRenderTargetASMLodClamp = NULL;
TFRenderTarget* pRenderTargetASMPrerenderLodClamp = NULL;

TFRenderTarget* pRenderTargetVBPass = NULL;
TFRenderTarget* pRenderTargetIntermediate = NULL;

TFRenderTarget* pRenderTargetMSAA = NULL;

TFRenderTarget* pRenderTargetMSAAEdges = NULL;

TFTexture* pTextureSkybox = NULL;
TFTexture* pTextureSSS = NULL;
TFBuffer*  pBufferSSS = NULL;
TFBuffer*  pBufferSSSWaveOffsets[MAX_SSS_WAVE_OFFSETS][gDataBufferCount] = { { NULL } };
TFBuffer*  pBufferGaussianBlurConstants[2][gDataBufferCount] = { { NULL } };

// We are rendering the scene (geometry, skybox, ...) at this resolution, UI at window resolution (mSettings.mWidth, mSettings.mHeight)
// Render scene at gSceneRes
// Render UI into backbuffer
static TFResolution gSceneRes;

/************************************************************************/

TFBuffer* pBufferSkyboxVertex = NULL;
TFBuffer* pBufferQuadVertex = NULL;

const float gQuadVertices[] = {
    // positions   // texCoords
    -1.0f, 1.0f,  0.f, 0.f, 0.0f, 0.0f, -1.0f, -1.0f, 0.f, 0.f, 0.0f, 1.0f, 1.0f,  -1.0f, 0.f, 0.f, 1.0f, 1.0f,

    1.0f,  -1.0f, 0.f, 0.f, 1.0f, 1.0f, 1.0f,  1.0f,  0.f, 0.f, 1.0f, 0.0f, -1.0f, 1.0f,  0.f, 0.f, 0.0f, 0.0f,
};

// Warning these indices are not good indices for cubes that want correct normals
// (a.k.a. all vertices are shared)
// const uint16_t gBoxIndices[36] = {
//	0, 1, 4, 4, 1, 5,    //y-
//	0, 4, 2, 2, 4, 6,    //x-
//	0, 2, 1, 1, 2, 3,    //z-
//	2, 6, 3, 3, 6, 7,    //y+
//	1, 3, 5, 5, 3, 7,    //x+
//	4, 5, 6, 6, 5, 7     //z+
//};

TFDescriptorSet* pDescriptorSetPersistent = NULL;
TFDescriptorSet* pDescriptorSetPerFrame = NULL;
TFDescriptorSet* pDescriptorSetTriangleFiltering = NULL;

TFDescriptorSet* pDescriptorSetBakedSDFMeshShadow = NULL;
TFDescriptorSet* pDescriptorSetUpdateRegion3DTexture = NULL;
#ifdef QUEST_VR
TFDescriptorSet* pDescriptorSetSDFShadowPerDraw = NULL;
#endif

TFDescriptorSet* pDescriptorSetGaussianBlurVSMPerDraw = NULL;
TFDescriptorSet* pDescriptorSetGaussianBlurMSMPerDraw = NULL;
TFDescriptorSet* pDescriptorSetGaussianBlurPerBatch = NULL;
TFDescriptorSet* pDescriptorSetDisplayPerDraw = NULL;
TFDescriptorSet* pDescriptorSetSSSPerBatch = NULL;
TFDescriptorSet* pDescriptorSetSSSPerDraw = NULL;

TFDescriptorSet* pDescriptorSetAtlasToColor[2] = { NULL };
TFDescriptorSet* pDescriptorSetColorToAtlas[2] = { NULL };
TFDescriptorSet* pDescriptorSetAsmAtlasQuads[2] = { NULL };
TFDescriptorSet* pDescriptorSetAsmCopyDEM[2] = { NULL };
TFDescriptorSet* pDescriptorSetAsmLodClamp = NULL;
TFDescriptorSet* pDescriptorSetAsmClearIndirection = NULL;
/************************************************************************/
// Clear buffers pipeline
/************************************************************************/
TFShader*        pShaderClearBuffers = NULL;
TFPipeline*      pPipelineClearBuffers = NULL;
/************************************************************************/
// Triangle filtering pipeline
/************************************************************************/
TFShader*        pShaderTriangleFiltering = NULL;
TFPipeline*      pPipelineTriangleFiltering = NULL;

/************************************************************************/
// Gaussian Blur pipelines
/************************************************************************/
TFShader*   pShaderBlurComp = NULL;
TFPipeline* pPipelineBlur = NULL;

/************************************************************************/
// Indirect vib buffer depth pass Shader Pack
/************************************************************************/
TFShader*   pShaderIndirectDepthPass = NULL;
TFShader*   pShaderIndirectVSMDepthPass = NULL;
TFShader*   pShaderIndirectMSMDepthPass = NULL;
TFPipeline* pPipelineIndirectDepthPass = NULL;

TFPipeline* pPipelineESMIndirectDepthPass = NULL;
TFPipeline* pPipelineVSMIndirectDepthPass = NULL;
TFPipeline* pPipelineMSMIndirectDepthPass = NULL;
/************************************************************************/
// Indirect vib buffer alpha depth pass Shader Pack
/************************************************************************/
TFShader*   pShaderIndirectAlphaDepthPass = NULL;
TFShader*   pShaderIndirectVSMAlphaDepthPass = NULL;
TFShader*   pShaderIndirectMSMAlphaDepthPass = NULL;
TFPipeline* pPipelineIndirectAlphaDepthPass = NULL;

TFPipeline* pPipelineESMIndirectAlphaDepthPass = NULL;
TFPipeline* pPipelineVSMIndirectAlphaDepthPass = NULL;
TFPipeline* pPipelineMSMIndirectAlphaDepthPass = NULL;
/************************************************************************/
// Screen Space Shadow Mapping
/************************************************************************/
TFShader*   pShaderSSS[MSAA_LEVELS_COUNT];
TFShader*   pShaderSSSClear;
TFPipeline* pPipelineSSS;
TFPipeline* pPipelineSSSClear;

#if TEST_GPU_BREADCRUMBS
TFShader*   pShaderSSSCrash = NULL;
TFPipeline* pPipelineSSSCrash = NULL;
#endif
/************************************************************************/
// ASM copy quads pass Shader Pack
/************************************************************************/
TFShader*        pShaderASMCopyDepthQuadPass = NULL;
TFPipeline*      pPipelineASMCopyDepthQuadPass = NULL;
TFDescriptorSet* pDescriptorSetASMDepthPass = NULL;
/************************************************************************/
// ASM fill indirection Shader Pack
/************************************************************************/
TFShader*        pShaderASMFillIndirection = NULL;
TFPipeline*      pPipelineASMFillIndirection = NULL;
#if defined(ORBIS) || defined(PROSPERO)
TFShader* pShaderASMFillIndirectionFP16 = NULL;
#endif
TFDescriptorSet* pDescriptorSetASMFillIndirection[2] = { NULL };
/************************************************************************/
// ASM fill lod clamp Pack
/************************************************************************/
// Reuse pShaderASMFillIndirection since they pretty much has the same shader
TFPipeline*      pPipelineASMFillLodClamp = NULL;
/************************************************************************/
// ASM Copy DEM Shader Pack
/************************************************************************/
TFShader*        pShaderASMCopyDEM = NULL;
TFPipeline*      pPipelineASMCopyDEM = NULL;
/************************************************************************/
// ASM generate DEM Shader Pack
/************************************************************************/
TFShader*        pShaderASMGenerateDEM = NULL;
TFPipeline*      pPipelineASMDEMAtlasToColor = NULL;
TFPipeline*      pPipelineASMDEMColorToAtlas = NULL;
/************************************************************************/
// VB pass pipeline
/************************************************************************/
TFShader*        pShaderVBBufferPass[gNumGeomSets] = {};
TFPipeline*      pPipelineVBBufferPass[gNumGeomSets] = {};
TFDescriptorSet* pDescriptorSetVBPass = NULL;
TFDescriptorSet* pDescriptorSetDepthVBPass = NULL;

#if TEST_GPU_BREADCRUMBS
TFShader*   pShaderVBBufferCrashPass = NULL;
TFPipeline* pPipelineVBBufferCrashPass = NULL;
#endif
/************************************************************************/
// VB shade pipeline
/************************************************************************/
TFShader*   pShaderVBShade[MSAA_LEVELS_COUNT] = { NULL };
TFPipeline* pPipelineVBShadeSrgb = NULL;

#if TEST_GPU_BREADCRUMBS
TFShader*   pShaderVBShadeCrash = NULL;
TFPipeline* pPipelineVBShadeSrgbCrash = NULL;
#endif
/************************************************************************/
// SDF draw update volume texture atlas pipeline
/************************************************************************/
TFShader*   pShaderUpdateSDFVolumeTextureAtlas = NULL;
TFPipeline* pPipelineUpdateSDFVolumeTextureAtlas = NULL;
/************************************************************************/
// SDF mesh visualization pipeline
/************************************************************************/
TFShader*   pShaderSDFMeshVisualization[MSAA_LEVELS_COUNT] = { NULL };
TFPipeline* pPipelineSDFMeshVisualization = NULL;
/************************************************************************/
// SDF baked mesh shadow pipeline
/************************************************************************/
TFShader*   pShaderSDFMeshShadow[MSAA_LEVELS_COUNT] = { NULL };
TFPipeline* pPipelineSDFMeshShadow = NULL;
/************************************************************************/
// SDF upsample shadow texture pipeline
/************************************************************************/
TFShader*   pShaderUpsampleSDFShadow[MSAA_LEVELS_COUNT] = { NULL };
TFPipeline* pPipelineUpsampleSDFShadow = NULL;
/************************************************************************/
// Resolve pipeline
/************************************************************************/
TFShader*   pShaderResolve[MSAA_LEVELS_COUNT] = { NULL };
TFPipeline* pPipelineResolve = nullptr;
/************************************************************************/
// Present pipeline
/************************************************************************/
TFShader*   pShaderPresentPass = NULL;
TFPipeline* pPipelinePresentPass = NULL;
/************************************************************************/
// Programmable MSAA resources
/************************************************************************/
TFShader*   pShaderDrawMSAAEdges[MSAA_LEVELS_COUNT - 1] = { nullptr };
TFPipeline* pPipelineDrawMSAAEdges = nullptr;
/************************************************************************/
// Samplers
/************************************************************************/
TFSampler*  pSamplerMiplessLinear = NULL;
TFSampler*  pSamplerComparisonShadow = NULL;
TFSampler*  pSamplerMiplessClampToBorderNear = NULL;
TFSampler*  pSamplerTrilinearAniso = NULL;
TFSampler*  pSamplerMiplessNear = NULL;
/************************************************************************/
// Constant buffers
/************************************************************************/
TFBuffer*   pBufferMeshTransforms[MESH_COUNT][gDataBufferCount] = { { NULL } };
TFBuffer*   pBufferMeshShadowProjectionTransforms[MESH_COUNT][ASM_MAX_TILES_PER_PASS][gDataBufferCount] = { { { NULL } } };

TFBuffer* pBufferBlurWeights = NULL;
TFBuffer* pBufferSSSUniform[VR_MULTIVIEW_COUNT][gDataBufferCount] = { { NULL } };
TFBuffer* pBufferCameraUniform[gDataBufferCount] = { NULL };

TFBuffer* pBufferASMAtlasQuadsUniform[gDataBufferCount] = { NULL };

TFBuffer* pBufferASMCopyDEMPackedQuadsUniform[gDataBufferCount] = { NULL };
TFBuffer* pBufferASMAtlasToColorPackedQuadsUniform[gDataBufferCount] = { NULL };
TFBuffer* pBufferASMColorToAtlasPackedQuadsUniform[gDataBufferCount] = { NULL };

TFBuffer* pBufferASMLodClampPackedQuadsUniform[gDataBufferCount] = { NULL };

TFBuffer* pBufferASMPackedIndirectionQuadsUniform[gs_ASMMaxRefinement + 1][gDataBufferCount] = { { NULL } };
TFBuffer* pBufferASMPackedPrerenderIndirectionQuadsUniform[gs_ASMMaxRefinement + 1][gDataBufferCount] = { { NULL } };
TFBuffer* pBufferASMClearIndirectionQuadsUniform[gDataBufferCount] = { NULL };

TFBuffer* pBufferASMDataUniform[gDataBufferCount] = { NULL };

TFBuffer* pBufferMeshConstants = NULL;
TFBuffer* pBufferBlurConstants = NULL;

TFBuffer* pBufferQuadUniform[gDataBufferCount] = { NULL };
TFBuffer* pBufferVBConstants[gDataBufferCount] = { NULL };

/************************************************************************/
// Constants for SDF Mesh
/************************************************************************/
TFBuffer* pBufferMeshSDFConstants[gDataBufferCount] = { NULL };
TFBuffer* pBufferUpdateSDFVolumeTextureAtlasConstants[gDataBufferCount] = { NULL };

TFBuffer* pBufferSDFVolumeData[gDataBufferCount] = {};
#ifdef QUEST_VR
TFBuffer* pBufferSDFPerDraw[gDataBufferCount * VR_MULTIVIEW_COUNT] = { NULL };
#endif
/************************************************************************/
// Textures/rendertargets for SDF Algorithm
/************************************************************************/

TFRenderTarget* pRenderTargetSDFMeshVisualization = NULL;
TFTexture*      pTextureSDFMeshShadow = NULL;

TFRenderTarget* pRenderTargetUpSampleSDFShadow = NULL;

TFTexture* pTextureSDFVolumeAtlas = NULL;

/************************************************************************/
// Bindless texture array
/************************************************************************/
size_t gAllTexturesCount = 0;

TFBuffer* pBufferSDFVolumeAtlas[gDataBufferCount] = { NULL };
/************************************************************************/
// Render control variables
/************************************************************************/
struct
{
    uint32_t mFilterWidth = 2U;
    float    mEsmControl = 225.f;
} gEsmCpuSettings;

SSSInputConstants gSSSUniformData;

struct
{
    // Only used for ESM shadow
    float mSourceAngle = 1.0f;

    // Adjust directional sunlight angle based on time. Values represent a 24 hour clock.
    float mTimeOfDay = 12.0f;
    float mTimeOfDaySunrise = 6.0f;
    float mTimeOfDaySunset = 18.0f;

    // The percentage of the full range of sun angles that the shadowmap will take.
    // For example, a range of 0.8f means that the shadowmap camera lerps from
    // 0.1 * PI radians at sunrise to 0.9 * PI radians at sunset.
    // This does not affect the light direction that BRDFs see, so highlights and contour shadows are unaffected.
    float mShadowRange = 0.985f;

    // The vector pointing to the sunrise direction is given by mat4::rotationY(mSunriseDirection) * vec4::zAxis().
    float mSunriseDirection = 0.58f;

    float mSunSpeed = 0.25f;
    // Only for SDF shadow now
    bool  mAutomaticSunMovement = false;
} gLightCpuSettings;

struct
{
    bool mDrawSDFMeshVisualization = false;
} gBakedSDFMeshSettings;

ASMCpuSettings gASMCpuSettings;
/************************************************************************/

bool gBufferUpdateSDFMeshConstantFlags[3] = { true, true, true };

// Constants
uint32_t gFrameIndex = 0;

ObjectUniform gMeshInfoUniformData[MESH_COUNT][gDataBufferCount];

PerFrameData gPerFrameData[gDataBufferCount] = {};

uint32_t            gBlurConstantsIndex = 0;
///
ObjectShadowUniform gMeshASMProjectionInfoUniformData[MESH_COUNT][gDataBufferCount];

ASMUniformBlock gAsmModelUniformBlockData = {};
bool            preRenderSwap = false;

CameraUniform gCameraUniformData;
BlurConstant  gBlurConstantsData;
gBlurWeights  gBlurWeightsUniform;
float         gGaussianBlurSigma[2] = { 1.0f, 1.0f };

// TODO remove this
QuadDataUniform gQuadUniformData;
MeshInfoStruct  gMeshInfoData[MESH_COUNT] = {};

PerFrameVBConstantsData gVBConstants[gDataBufferCount] = {};

vec3 gObjectsCenter = { SAN_MIGUEL_OFFSETX, 0, 0 };

TFICamera* pCamera = NULL;
TFICamera* pLightView = NULL;

VBMeshInstance*  pVBMeshInstances = NULL;
VBPreFilterStats gVBPreFilterStats[gDataBufferCount] = {};
TFGeometry*      pGeom = NULL;
TFGeometryData*  pGeomData = NULL;
size_t           gMeshCount = 0;
size_t           gTextureCount = 0;

/// UI
TFUIWindowDesc gGuiWindowDesc;
TFUIWindowDesc gUIASMDebugTexturesWindowDesc;
TFUIWindowDesc gLoadingGuiDesc;
TFUIWindowDesc gSSSGuiDesc;

TFFontDrawDesc  gFrameTimeDraw;
TFFont*         gFont;
ProfileToken    gCurrentGpuProfileTokens[SHADOW_TYPE_COUNT];
ProfileToken    gCurrentGpuProfileToken;
uint32_t        gSSSRootConstantIndex = 0;
// VR 2D layer transform (positioned at -1 along the Z axis, default rotation, default scale)
TFVR2DLayerDesc gVR2DLayer{ { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f }, 1.0f };

TFRenderer*       pRenderer = NULL;
VisibilityBuffer* pVisibilityBuffer = NULL;
TFPackage*        pPackage = NULL;

TFQueue*   pGraphicsQueue = NULL;
GpuCmdRing gGraphicsCmdRing = {};

TFSwapChain* pSwapChain = NULL;
TFSemaphore* pImageAcquiredSemaphore[gDataBufferCount] = { NULL };

uint32_t gCurrentShadowType = SHADOW_TYPE_ASM;

#if TEST_GPU_BREADCRUMBS
/// GPU Breadcrumbs
/* Markers to be used to pinpoint which command has caused GPU hang.
 * In this example, three markers get injected into the command list: before drawing the VB, before computing SSS,
 * before shading VB. Pressing one of the crash buttons will make the application hang at that point.
 * There are additonal markers that reset the buffer at the start of the frame and after VB shading.
 * When a crash is detected, the marker value is retrieved from the marker buffer that gives a clue
 * as to what render pass likely was executed last.
 * Markers aren't perfectly reliable and can be subject to GPU command reordering.
 * As an example of this, a crash in compute SSS pass will likely show VB shade pass
 * as the last rendering step, unless additional work is done between the two passes (enabling SDF mesh shadows).
 * Establishing clear dependencies between render passes can help make them more reliable.
 */

// Rendering steps where we insert markers
enum RenderingStep
{
    RENDERING_STEP_DRAW_VB_PASS = 0,
    RENDERING_STEP_SS_SHADOWS_PASS,
    RENDERING_STEP_SHADE_VB_PASS,
    RENDERING_STEP_COUNT
};

enum MarkerType
{
    MARKER_TASK_INDEX = 0,
    MARKER_FRAME_INDEX,
    MARKER_COUNT,
};
#define MARKER_OFFSET(type) ((type)*TF_GPU_MARKER_SIZE)

bool bHasCrashed = false;
bool bCrashedSteps[RENDERING_STEP_COUNT] = {};
char gSimulateCrashButtonLabels[RENDERING_STEP_COUNT][MAX_LABEL_STR_LENGTH] = {};

TFBuffer*      pMarkerBuffer = {};
const uint32_t gMarkerInitialValue = UINT32_MAX;
const char*    gMarkerNames[] = { "Draw Visibility Buffer", "Compute Screen Space Shadows", "Shade Visibility Buffer" };

uint32_t gUIRenderingStepIndices[RENDERING_STEP_COUNT];
COMPILE_ASSERT(TF_ARRAY_COUNT(gMarkerNames) == RENDERING_STEP_COUNT);
#endif

TFTexture* gNullTextureResource = NULL;

#ifdef METAL
bool gSupportTextureAtomics = false;
#else
bool gSupportTextureAtomics = true;
#endif
struct Triangle
{
    Triangle() = default;
    Triangle(const vec3& v0, const vec3& v1, const vec3& v2, const vec3& n0, const vec3& n1, const vec3& n2):
        mV0(v0), mV1(v1), mV2(v2), mN0(n0), mN1(n1), mN2(n2)
    {
        mE1 = mV1 - mV0;
        mE2 = mV2 - mV0;
    }

    void Init(const vec3& v0, const vec3& v1, const vec3& v2, const vec3& n0, const vec3& n1, const vec3& n2)
    {
        mV0 = v0;
        mV1 = v1;
        mV2 = v2;
        mN0 = n0;
        mN1 = n1;
        mN2 = n2;

        mE1 = mV1 - mV0;
        mE2 = mV2 - mV0;
    }

    // triangle vertices
    vec3 mV0;
    vec3 mV1;
    vec3 mV2;

    // triangle edges
    vec3 mE1;
    vec3 mE2;

    // vertices normal
    vec3 mN0;
    vec3 mN1;
    vec3 mN2;
};

struct Intersection
{
    Intersection(const vec3& hitted_pos, const vec3& hitted_normal, float t_intersection):
        mHittedPos(hitted_pos), mHittedNormal(hitted_normal), mIntersectedTriangle(NULL), mIntersection_TVal(t_intersection),
        mIsIntersected(false)
    {
    }
    Intersection(): mHittedPos(), mHittedNormal(), mIntersectedTriangle(NULL), mIntersection_TVal(FLT_MAX), mIsIntersected(false) {}

    vec3            mHittedPos;
    vec3            mHittedNormal;
    const Triangle* mIntersectedTriangle;
    float           mIntersection_TVal;
    bool            mIsIntersected;
};

void RayIntersectTriangle(const TFRay& ray, const Triangle& triangle, Intersection& outIntersection)
{
    vec3 P_V = cross(ray.direction, triangle.mE2);

    float P_dot_E1 = dot(P_V, triangle.mE1);

    if (P_dot_E1 == 0.f)
    {
        return;
    }

    vec3 S_V = ray.origin - triangle.mV0;

    float u_val = dot(P_V, S_V) / P_dot_E1;

    if (u_val < Epilson || u_val > 1.f)
    {
        return;
    }

    vec3 Q_V = cross(S_V, triangle.mE1);

    float v_val = dot(ray.direction, Q_V) / P_dot_E1;

    if (v_val < Epilson || (v_val + u_val) > 1.f)
    {
        return;
    }

    float t_val = dot(triangle.mE2, Q_V) / P_dot_E1;

    if (t_val < Epilson)
    {
        return;
    }

    if (t_val < outIntersection.mIntersection_TVal)
    {
        outIntersection.mIsIntersected = true;
        outIntersection.mIntersection_TVal = t_val;
        outIntersection.mHittedPos = rayEval(ray, t_val);
        outIntersection.mHittedNormal = normalize((1.f - u_val - v_val) * triangle.mN0 + u_val * triangle.mN1 + v_val * triangle.mN2);
        outIntersection.mIntersectedTriangle = &triangle;
    }
}

struct BVHAABBox
{
    TFAABB   mAABB;
    vec3     Center;
    Triangle mTriangle;

    int32_t InstanceID;
    float   SurfaceAreaLeft;
    float   SurfaceAreaRight;

    BVHAABBox()
    {
        mAABB.min = vec3(FLT_MAX);
        mAABB.max = vec3(-FLT_MAX);
        InstanceID = 0;
        SurfaceAreaLeft = 0.0f;
        SurfaceAreaRight = 0.0f;
    }

    void Expand(vec3& point)
    {
        mAABB.min = vec3(fminf(mAABB.min.x, point.x), fminf(mAABB.min.y, point.y), fminf(mAABB.min.z, point.z));

        mAABB.max = vec3(fmaxf(mAABB.max.x, point.x), fmaxf(mAABB.max.y, point.y), fmaxf(mAABB.max.z, point.z));

        Center = 0.5f * (mAABB.max + mAABB.min);
    }

    void Expand(BVHAABBox& aabox)
    {
        Expand(aabox.mAABB.min);
        Expand(aabox.mAABB.max);
    }
};

struct BVHNode
{
    float     SplitCost;
    BVHAABBox BoundingBox;
    BVHNode*  Left;
    BVHNode*  Right;
};

struct BVHTree
{
    STB_DS_ARRAY(BVHAABBox) mBBOXDataList = NULL;
    BVHNode* mRootNode = NULL;
    uint32_t mBVHNodeCount = 0;
    uint32_t mTransitionNodeCount = 0;
};

bool RayIntersectsBox(const vec3& origin, const vec3& rayDirInv, const vec3& BboxMin, const vec3& BboxMax)
{
    const vec3 nonInvT0 = (BboxMin - origin);
    const vec3 nonInvT1 = (BboxMax - origin);
    const vec3 t0 = vec3(nonInvT0.x * rayDirInv.x, nonInvT0.y * rayDirInv.y, nonInvT0.z * rayDirInv.z);
    const vec3 t1 = vec3(nonInvT1.x * rayDirInv.x, nonInvT1.y * rayDirInv.y, nonInvT1.z * rayDirInv.z);

    const vec3 tmax = vec3(fmaxf(t0.x, t1.x), fmaxf(t0.y, t1.y), fmaxf(t0.z, t1.z));
    const vec3 tmin = vec3(fminf(t0.x, t1.x), fminf(t0.y, t1.y), fminf(t0.z, t1.z));

    const float a1 = fminf(tmax.x, fminf(tmax.y, tmax.z));
    const float a0 = fmaxf(fmaxf(tmin.x, tmin.y), fmaxf(tmin.z, 0.0f));

    return a1 >= a0;
}

void BVHTreeIntersectRayAux(BVHNode* rootNode, BVHNode* node, const TFRay& ray, Intersection& outIntersection)
{
    if (!node)
    {
        return;
    }

    if (node->BoundingBox.InstanceID < 0.f)
    {
        bool intersects = RayIntersectsBox(ray.origin, Vector3(1.f / ray.direction.x, 1.f / ray.direction.y, 1.f / ray.direction.z),
                                           node->BoundingBox.mAABB.min, node->BoundingBox.mAABB.max);

        if (intersects)
        {
            BVHTreeIntersectRayAux(rootNode, node->Left, ray, outIntersection);
            BVHTreeIntersectRayAux(rootNode, node->Right, ray, outIntersection);
        }
    }
    else
    {
        RayIntersectTriangle(ray, node->BoundingBox.mTriangle, outIntersection);
    }
}

void AddMeshInstanceToBBOX16(BVHTree* pBvhTree, SDFMesh* pMesh, uint16_t* pMeshIndices, uint32_t groupNum, uint32_t meshGroupSize,
                             uint32_t idxFirstMeshInGroup, TFAABB* pAABBFirstMeshInGroup, const mat4& meshWorldMat)
{
    float3*   positions = (float3*)pMesh->pGeometryData->pCpuData->pAttributes[TF_SEMANTIC_POSITION];
    uint32_t* normals = (uint32_t*)pMesh->pGeometryData->pCpuData->pAttributes[TF_SEMANTIC_NORMAL];
    ASSERT(pMesh->pGeometryData->pCpuData->mVertexStrides[TF_SEMANTIC_NORMAL] == sizeof(uint32_t));

    // Handle individual submesh in current submesh group
    for (uint32_t meshNum = 0; meshNum < meshGroupSize; ++meshNum)
    {
        // If not true, the meshIndex is simply the groupNum since there are as many submesh groups as there are meshes
        uint32_t meshIndex = pMesh->pSubMeshesIndices ? pMesh->pSubMeshesIndices[idxFirstMeshInGroup + meshNum] : groupNum;

        uint32_t startIndex = pMesh->pGeometry->pDrawArgs[meshIndex].mStartIndex;
        uint32_t indexCount = pMesh->pGeometry->pDrawArgs[meshIndex].mIndexCount;

        for (uint32_t i = 0; i < indexCount; i += 3)
        {
            uint16_t idx0 = pMeshIndices[startIndex + i];
            uint16_t idx1 = pMeshIndices[startIndex + i + 1];
            uint16_t idx2 = pMeshIndices[startIndex + i + 2];

            vec3 pos0 = (meshWorldMat * vec4((positions[idx0]) * MESH_SCALE + float3(SAN_MIGUEL_OFFSETX, 0.f, 0.f), 1.0f)).getXYZ();
            vec3 pos1 = (meshWorldMat * vec4((positions[idx1]) * MESH_SCALE + float3(SAN_MIGUEL_OFFSETX, 0.f, 0.f), 1.0f)).getXYZ();
            vec3 pos2 = (meshWorldMat * vec4((positions[idx2]) * MESH_SCALE + float3(SAN_MIGUEL_OFFSETX, 0.f, 0.f), 1.0f)).getXYZ();

            // TODO: multiply normal by world mat
            vec3 n0 = (decodeDir(unpackUnorm2x16(normals[idx0])));
            vec3 n1 = (decodeDir(unpackUnorm2x16(normals[idx1])));
            vec3 n2 = (decodeDir(unpackUnorm2x16(normals[idx2])));

            adjustAABB(pAABBFirstMeshInGroup, pos0);
            adjustAABB(pAABBFirstMeshInGroup, pos1);
            adjustAABB(pAABBFirstMeshInGroup, pos2);

            BVHAABBox bvhAABBOX = {};
            bvhAABBOX.mTriangle.Init(pos0, pos1, pos2, n0, n1, n2);
            bvhAABBOX.Expand(pos0);
            bvhAABBOX.Expand(pos1);
            bvhAABBOX.Expand(pos2);
            bvhAABBOX.InstanceID = 0;
            arrpush(pBvhTree->mBBOXDataList, bvhAABBOX);
        }
    }
}

void AddMeshInstanceToBBOX32(BVHTree* pBvhTree, SDFMesh* pMesh, uint32_t* pMeshIndices, uint32_t groupNum, uint32_t meshGroupSize,
                             uint32_t idxFirstMeshInGroup, TFAABB* pAABBFirstMeshInGroup, const mat4& meshWorldMat)
{
    float3*   positions = (float3*)pMesh->pGeometryData->pCpuData->pAttributes[TF_SEMANTIC_POSITION];
    uint32_t* normals = (uint32_t*)pMesh->pGeometryData->pCpuData->pAttributes[TF_SEMANTIC_NORMAL];
    ASSERT(pMesh->pGeometryData->pCpuData->mVertexStrides[TF_SEMANTIC_NORMAL] == sizeof(uint32_t));

    // Handle individual submesh in current submesh group
    for (uint32_t meshNum = 0; meshNum < meshGroupSize; ++meshNum)
    {
        // If not true, the meshIndex is simply the groupNum since there are as many submesh groups as there are meshes
        uint32_t meshIndex = pMesh->pSubMeshesIndices ? pMesh->pSubMeshesIndices[idxFirstMeshInGroup + meshNum] : groupNum;

        uint32_t startIndex = pMesh->pGeometry->pDrawArgs[meshIndex].mStartIndex;
        uint32_t indexCount = pMesh->pGeometry->pDrawArgs[meshIndex].mIndexCount;

        for (uint32_t i = 0; i < indexCount; i += 3)
        {
            uint32_t idx0 = pMeshIndices[startIndex + i];
            uint32_t idx1 = pMeshIndices[startIndex + i + 1];
            uint32_t idx2 = pMeshIndices[startIndex + i + 2];

            vec3 pos0 = (meshWorldMat * vec4((positions[idx0]) * MESH_SCALE + float3(SAN_MIGUEL_OFFSETX, 0.f, 0.f), 1.0f)).getXYZ();
            vec3 pos1 = (meshWorldMat * vec4((positions[idx1]) * MESH_SCALE + float3(SAN_MIGUEL_OFFSETX, 0.f, 0.f), 1.0f)).getXYZ();
            vec3 pos2 = (meshWorldMat * vec4((positions[idx2]) * MESH_SCALE + float3(SAN_MIGUEL_OFFSETX, 0.f, 0.f), 1.0f)).getXYZ();

            // TODO: multiply normal by world mat
            vec3 n0 = (decodeDir(unpackUnorm2x16(normals[idx0])));
            vec3 n1 = (decodeDir(unpackUnorm2x16(normals[idx1])));
            vec3 n2 = (decodeDir(unpackUnorm2x16(normals[idx2])));

            adjustAABB(pAABBFirstMeshInGroup, pos0);
            adjustAABB(pAABBFirstMeshInGroup, pos1);
            adjustAABB(pAABBFirstMeshInGroup, pos2);

            BVHAABBox bvhAABBOX = {};
            bvhAABBOX.mTriangle.Init(pos0, pos1, pos2, n0, n1, n2);
            bvhAABBOX.Expand(pos0);
            bvhAABBOX.Expand(pos1);
            bvhAABBOX.Expand(pos2);
            bvhAABBOX.InstanceID = 0;
            arrpush(pBvhTree->mBBOXDataList, bvhAABBOX);
        }
    }
}

void SortAlongAxis(BVHTree* bvhTree, int32_t begin, int32_t end, int32_t axis)
{
    BVHAABBox* data = bvhTree->mBBOXDataList + begin;
    int32_t    count = end - begin + 1;

    if (axis == 0)
        std::qsort(data, count, sizeof(BVHAABBox),
                   [](const void* a, const void* b)
                   {
                       const BVHAABBox* arg1 = static_cast<const BVHAABBox*>(a);
                       const BVHAABBox* arg2 = static_cast<const BVHAABBox*>(b);

                       float midPointA = arg1->Center[0];
                       float midPointB = arg2->Center[0];

                       if (midPointA < midPointB)
                           return -1;
                       else if (midPointA > midPointB)
                           return 1;

                       return 0;
                   });
    else if (axis == 1)
        std::qsort(data, count, sizeof(BVHAABBox),
                   [](const void* a, const void* b)
                   {
                       const BVHAABBox* arg1 = static_cast<const BVHAABBox*>(a);
                       const BVHAABBox* arg2 = static_cast<const BVHAABBox*>(b);

                       float midPointA = arg1->Center[1];
                       float midPointB = arg2->Center[1];

                       if (midPointA < midPointB)
                           return -1;
                       else if (midPointA > midPointB)
                           return 1;

                       return 0;
                   });
    else
        std::qsort(data, count, sizeof(BVHAABBox),
                   [](const void* a, const void* b)
                   {
                       const BVHAABBox* arg1 = static_cast<const BVHAABBox*>(a);
                       const BVHAABBox* arg2 = static_cast<const BVHAABBox*>(b);

                       float midPointA = arg1->Center[2];
                       float midPointB = arg2->Center[2];

                       if (midPointA < midPointB)
                           return -1;
                       else if (midPointA > midPointB)
                           return 1;

                       return 0;
                   });
}

float CalculateSurfaceArea(const BVHAABBox& bbox)
{
    vec3 extents = bbox.mAABB.max - bbox.mAABB.min;
    return (extents[0] * extents[1] + extents[1] * extents[2] + extents[2] * extents[0]) * 2.f;
}

void FindBestSplit(BVHTree* bvhTree, int32_t begin, int32_t end, int32_t& split, int32_t& axis, float& splitCost)
{
    int32_t count = end - begin + 1;
    int32_t bestSplit = begin;
    // int32_t globalBestSplit = begin;
    splitCost = FLT_MAX;

    split = begin;
    axis = 0;

    for (int32_t i = 0; i < 3; i++)
    {
        SortAlongAxis(bvhTree, begin, end, i);

        BVHAABBox boundsLeft;
        BVHAABBox boundsRight;

        for (int32_t indexLeft = 0; indexLeft < count; ++indexLeft)
        {
            int32_t indexRight = count - indexLeft - 1;

            boundsLeft.Expand(bvhTree->mBBOXDataList[begin + indexLeft]);

            boundsRight.Expand(bvhTree->mBBOXDataList[begin + indexRight]);

            float surfaceAreaLeft = CalculateSurfaceArea(boundsLeft);
            float surfaceAreaRight = CalculateSurfaceArea(boundsRight);

            bvhTree->mBBOXDataList[begin + indexLeft].SurfaceAreaLeft = surfaceAreaLeft;
            bvhTree->mBBOXDataList[begin + indexRight].SurfaceAreaRight = surfaceAreaRight;
        }

        float bestCost = FLT_MAX;
        for (int32_t mid = begin + 1; mid <= end; ++mid)
        {
            float surfaceAreaLeft = bvhTree->mBBOXDataList[mid - 1].SurfaceAreaLeft;
            float surfaceAreaRight = bvhTree->mBBOXDataList[mid].SurfaceAreaRight;

            int32_t countLeft = mid - begin;
            int32_t countRight = end - mid;

            float costLeft = surfaceAreaLeft * (float)countLeft;
            float costRight = surfaceAreaRight * (float)countRight;

            float cost = costLeft + costRight;
            if (cost < bestCost)
            {
                bestSplit = mid;
                bestCost = cost;
            }
        }

        if (bestCost < splitCost)
        {
            split = bestSplit;
            splitCost = bestCost;
            axis = i;
        }
    }
}
void CalculateBounds(BVHTree* bvhTree, int32_t begin, int32_t end, vec3& outMinBounds, vec3& outMaxBounds)
{
    outMinBounds = vec3(FLT_MAX);
    outMaxBounds = vec3(-FLT_MAX);

    for (int32_t i = begin; i <= end; ++i)
    {
        const vec3& memberMinBounds = bvhTree->mBBOXDataList[i].mAABB.min;
        const vec3& memberMaxBounds = bvhTree->mBBOXDataList[i].mAABB.max;
        outMinBounds = vec3(fminf(memberMinBounds.x, outMinBounds.x), fminf(memberMinBounds.y, outMinBounds.y),
                            fminf(memberMinBounds.z, outMinBounds.z));

        outMaxBounds = vec3(fmaxf(memberMaxBounds.x, outMaxBounds.x), fmaxf(memberMaxBounds.y, outMaxBounds.y),
                            fmaxf(memberMaxBounds.z, outMaxBounds.z));
    }
}

BVHNode* CreateBVHNodeSHA(BVHTree* bvhTree, int32_t begin, int32_t end, float parentSplitCost)
{
    UNREF_PARAM(parentSplitCost);
    int32_t count = end - begin + 1;

    vec3 min;
    vec3 max;

    CalculateBounds(bvhTree, begin, end, min, max);

    BVHNode* node = (BVHNode*)tf_placement_new<BVHNode>(tf_calloc(1, sizeof(BVHNode)));

    ++bvhTree->mBVHNodeCount;

    node->BoundingBox.Expand(min);
    node->BoundingBox.Expand(max);

    if (count == 1)
    {
        // This is a leaf node
        node->Left = NULL;
        node->Right = NULL;

        node->BoundingBox.InstanceID = bvhTree->mBBOXDataList[begin].InstanceID;
        node->BoundingBox.mTriangle = bvhTree->mBBOXDataList[begin].mTriangle;
    }
    else
    {
        ++bvhTree->mTransitionNodeCount;

        int32_t split;
        int32_t axis;
        float   splitCost;

        // Find the best axis to sort along and where the split should be according to SAH
        FindBestSplit(bvhTree, begin, end, split, axis, splitCost);

        // Sort along that axis
        SortAlongAxis(bvhTree, begin, end, axis);

        // Create the two branches
        node->Left = CreateBVHNodeSHA(bvhTree, begin, split - 1, splitCost);
        node->Right = CreateBVHNodeSHA(bvhTree, split, end, splitCost);

        // Access the child with the largest probability of collision first.
        float surfaceAreaLeft = CalculateSurfaceArea(node->Left->BoundingBox);
        float surfaceAreaRight = CalculateSurfaceArea(node->Right->BoundingBox);

        if (surfaceAreaRight > surfaceAreaLeft)
        {
            BVHNode* temp = node->Right;
            node->Right = node->Left;
            node->Left = temp;
        }

        // This is an intermediate Node
        node->BoundingBox.InstanceID = -1;
    }

    return node;
}

void DeleteBVHTree(BVHNode* node)
{
    if (node)
    {
        if (node->Left)
        {
            DeleteBVHTree(node->Left);
        }

        if (node->Right)
        {
            DeleteBVHTree(node->Right);
        }

        node->~BVHNode();
        tf_free(node);
    }
}

struct SDFTextureLayoutNode
{
    // Node coord not in texel space but in raw volume dimension space
    ivec3 mNodeCoord;
    ivec3 mNodeSize;
    bool  mUsed;
};

struct SDFVolumeTextureAtlasLayout
{
    ~SDFVolumeTextureAtlasLayout() { arrfree(mNodes); }

    SDFVolumeTextureAtlasLayout(const ivec3& atlasLayoutSize): mAtlasLayoutSize(atlasLayoutSize)
    {
        mAllocationCoord = ivec3(-SDF_MAX_VOXEL_ONE_DIMENSION_X, 0, SDF_DOUBLE_MAX_VOXEL_ONE_DIMENSION_Z * 3);
        mDoubleAllocationCoord = ivec3(-SDF_DOUBLE_MAX_VOXEL_ONE_DIMENSION_X, 0, 0);
    }

    bool AddNewNode(const ivec3& volumeDimension, ivec3& outCoord)
    {
        if (volumeDimension.x <= SDF_MAX_VOXEL_ONE_DIMENSION_X && volumeDimension.y <= SDF_MAX_VOXEL_ONE_DIMENSION_Y &&
            volumeDimension.z <= SDF_MAX_VOXEL_ONE_DIMENSION_Z)
        {
            return AddNormalNode(volumeDimension, outCoord);
        }
        return AddDoubleNode(volumeDimension, outCoord);
    }

    bool AddNormalNode(const ivec3& volumeDimension, ivec3& outCoord)
    {
        if ((mAllocationCoord.x + (SDF_MAX_VOXEL_ONE_DIMENSION_X * 2)) <= mAtlasLayoutSize.x)
        {
            mAllocationCoord.x = (mAllocationCoord.x + SDF_MAX_VOXEL_ONE_DIMENSION_X);
            arrpush(mNodes, SDFTextureLayoutNode(SDFTextureLayoutNode{ mAllocationCoord, volumeDimension, false }));
        }
        else if ((mAllocationCoord.y + (SDF_MAX_VOXEL_ONE_DIMENSION_Y * 2)) <= mAtlasLayoutSize.y)
        {
            mAllocationCoord.x = (0);
            mAllocationCoord.y = (mAllocationCoord.y + SDF_MAX_VOXEL_ONE_DIMENSION_Y);
            arrpush(mNodes, SDFTextureLayoutNode(SDFTextureLayoutNode{ mAllocationCoord, volumeDimension, false }));
        }
        else if ((mAllocationCoord.z + (SDF_MAX_VOXEL_ONE_DIMENSION_Z * 2)) <= mAtlasLayoutSize.z)
        {
            mAllocationCoord.x = (0);
            mAllocationCoord.y = (0);
            mAllocationCoord.z = (mAllocationCoord.z + SDF_MAX_VOXEL_ONE_DIMENSION_Z);
            arrpush(mNodes, SDFTextureLayoutNode(SDFTextureLayoutNode{ mAllocationCoord, volumeDimension, false }));
        }
        else
        {
            return false;
        }
        outCoord = mAllocationCoord;
        return true;
    }
    bool AddDoubleNode(const ivec3& volumeDimension, ivec3& outCoord)
    {
        if ((mDoubleAllocationCoord.x + (SDF_DOUBLE_MAX_VOXEL_ONE_DIMENSION_X * 2)) <= mAtlasLayoutSize.x)
        {
            mDoubleAllocationCoord.x = (mDoubleAllocationCoord.x + SDF_DOUBLE_MAX_VOXEL_ONE_DIMENSION_X);
            arrpush(mNodes, SDFTextureLayoutNode(SDFTextureLayoutNode{ mDoubleAllocationCoord, volumeDimension }));
        }
        else if ((mDoubleAllocationCoord.y + (SDF_DOUBLE_MAX_VOXEL_ONE_DIMENSION_Y * 2)) <= mAtlasLayoutSize.y)
        {
            mDoubleAllocationCoord.x = (0);
            mDoubleAllocationCoord.y = (mDoubleAllocationCoord.y + SDF_DOUBLE_MAX_VOXEL_ONE_DIMENSION_Y);
            arrpush(mNodes, SDFTextureLayoutNode(SDFTextureLayoutNode{ mDoubleAllocationCoord, volumeDimension }));
        }
        else if ((mDoubleAllocationCoord.z + (SDF_DOUBLE_MAX_VOXEL_ONE_DIMENSION_Z * 2)) <= mAtlasLayoutSize.z)
        {
            mDoubleAllocationCoord.x = (0);
            mDoubleAllocationCoord.y = (0);
            mDoubleAllocationCoord.z = (mDoubleAllocationCoord.z + SDF_DOUBLE_MAX_VOXEL_ONE_DIMENSION_Z);
            arrpush(mNodes, SDFTextureLayoutNode(SDFTextureLayoutNode{ mDoubleAllocationCoord, volumeDimension }));
        }
        else
        {
            return false;
        }
        outCoord = mDoubleAllocationCoord;
        return true;
    }

    STB_DS_ARRAY(SDFTextureLayoutNode) mNodes = NULL;

    ivec3 mAtlasLayoutSize;
    ivec3 mAllocationCoord;
    ivec3 mDoubleAllocationCoord;
};

struct SDFVolumeTextureNode
{
    SDFVolumeTextureNode(SDFVolumeData* sdfVolumeData /*, SDFMesh* mainMesh, SDFMeshInstance* meshInstance*/):
        mAtlasAllocationCoord(-1, -1, -1), mSDFVolumeData(sdfVolumeData),
        /*mMainMesh(mainMesh),
        mMeshInstance(meshInstance),*/
        mHasBeenAdded(false)
    {
    }

    ivec3          mAtlasAllocationCoord;
    SDFVolumeData* mSDFVolumeData;
    /*SDFMesh* mMainMesh;
    SDFMeshInstance* mMeshInstance;*/

    // The coordinate of this node inside the volume texture atlases not in texel space
    bool mHasBeenAdded;
};

struct SDFVolumeData
{
    SDFVolumeData(SDFMesh* mainMesh):
        mSDFVolumeSize(0), mLocalBoundingBox(), mDistMinMax(FLT_MAX, FLT_MIN), mIsTwoSided(false), mTwoSidedWorldSpaceBias(0.f),
        mSDFVolumeTextureNode(this)
    {
        UNREF_PARAM(mainMesh);
    }

    SDFVolumeData():
        mSDFVolumeSize(0), mLocalBoundingBox(), mDistMinMax(FLT_MAX, FLT_MIN), mIsTwoSided(false), mTwoSidedWorldSpaceBias(0.f),
        mSDFVolumeTextureNode(this)
    {
    }

    ~SDFVolumeData()
    {
        arrfree(mSDFVolumeList);
        mSDFVolumeList = NULL;
    }

    //
    STB_DS_ARRAY(float) mSDFVolumeList = NULL;
    //
    // Size of the distance volume
    ivec3  mSDFVolumeSize;
    //
    // Local Space of the Bounding Box volume
    TFAABB mLocalBoundingBox;

    // Stores the min & the maximum distances found in the volume
    // in the space of the world voxel volume
    // x stores the minimum while y stores the maximum
    vec2  mDistMinMax;
    //
    bool  mIsTwoSided;
    //
    float mTwoSidedWorldSpaceBias;

    SDFVolumeTextureNode mSDFVolumeTextureNode;
};

struct gSDFVolumeTextureAtlas
{
    gSDFVolumeTextureAtlas(const ivec3& atlasSize): mSDFVolumeAtlasLayout(atlasSize) {}

    ~gSDFVolumeTextureAtlas()
    {
        mNextNodeIndex = 0;
        arrfree(mPendingNodeQueue);
        mPendingNodeQueue = NULL;
    }

    void AddVolumeTextureNode(SDFVolumeTextureNode* volumeTextureNode)
    {
        // ivec3 atlasCoord = volumeTextureNode->mAtlasAllocationCoord;

        if (volumeTextureNode->mHasBeenAdded)
        {
            return;
        }

        mSDFVolumeAtlasLayout.AddNewNode(volumeTextureNode->mSDFVolumeData->mSDFVolumeSize, volumeTextureNode->mAtlasAllocationCoord);

        if (mNextNodeIndex > 0)
        {
            ASSERT(mPendingNodeQueue);
            memmove(mPendingNodeQueue, mPendingNodeQueue + mNextNodeIndex, mNextNodeIndex * sizeof(*mPendingNodeQueue)); //-V595
            arrsetlen(mPendingNodeQueue, arrlenu(mPendingNodeQueue) - mNextNodeIndex);
            mNextNodeIndex = 0;
        }

        arrpush(mPendingNodeQueue, volumeTextureNode);
        volumeTextureNode->mHasBeenAdded = true;
    }

    SDFVolumeTextureNode* ProcessQueuedNode()
    {
        if (mNextNodeIndex >= arrlenu(mPendingNodeQueue))
        {
            return NULL;
        }
        return mPendingNodeQueue[mNextNodeIndex++];
    }

    SDFVolumeTextureNode* GetLastProcessedNode()
    {
        if (mNextNodeIndex == 0)
        {
            return NULL;
        }
        return mPendingNodeQueue[mNextNodeIndex - 1];
    }

    SDFVolumeTextureAtlasLayout mSDFVolumeAtlasLayout;

    size_t mNextNodeIndex = 0;
    STB_DS_ARRAY(SDFVolumeTextureNode*) mPendingNodeQueue = NULL;
};

void GenerateSampleDirections(int32_t thetaSteps, int32_t phiSteps, STB_DS_ARRAY(vec3) & outDirectionsList, int32_t finalThetaModifier)
{
    for (int32_t theta = 0; theta < thetaSteps; ++theta)
    {
        for (int32_t phi = 0; phi < phiSteps; ++phi)
        {
            float random1 = randomFloat01();
            float random2 = randomFloat01();

            float thetaFrac = (theta + random1) / (float)thetaSteps;
            float phiFrac = (phi + random2) / (float)phiSteps;

            float rVal = sqrtf(1.0f - thetaFrac * thetaFrac);

            const float finalPhi = 2.0f * (float)PI * phiFrac;

            arrpush(outDirectionsList, vec3(cosf(finalPhi) * rVal, sinf(finalPhi) * rVal, thetaFrac * finalThetaModifier));
        }
    }
}

struct CalculateMeshSDFTask
{
    STB_DS_ARRAY(vec3) * mDirectionsList;
    STB_DS_ARRAY(Triangle) * mMeshTrianglesList;
    const TFAABB* mSDFVolumeBounds;
    const ivec3*  mSDFVolumeDimension;
    int32_t       mZIndex;
    float         mSDFVolumeMaxDist;
    BVHTree*      mBVHTree;
    STB_DS_ARRAY(float) * mSDFVolumeList;
    bool mIsTwoSided;
};

void DoCalculateMeshSDFTask(void* dataPtr, uintptr_t index)
{
    UNREF_PARAM(index);
    CalculateMeshSDFTask* task = (CalculateMeshSDFTask*)(dataPtr);

    const TFAABB& sdfVolumeBounds = *task->mSDFVolumeBounds;
    const ivec3&  sdfVolumeDimension = *task->mSDFVolumeDimension;
    int32_t       zIndex = task->mZIndex;
    float         sdfVolumeMaxDist = task->mSDFVolumeMaxDist;

    STB_DS_ARRAY(vec3)& directionsList = *task->mDirectionsList;
    // const SDFVolumeData::TriangeList& meshTrianglesList = *task->mMeshTrianglesList;

    STB_DS_ARRAY(float)& sdfVolumeList = *task->mSDFVolumeList;

    BVHTree* bvhTree = task->mBVHTree;

    // vec3 floatSDFVolumeDimension = vec3((float)sdfVolumeDimension.x, (float)sdfVolumeDimension.y,
    // (float)sdfVolumeDimension.z);

    vec3 sdfVolumeBoundsSize = calculateAABBSize(&sdfVolumeBounds);

    vec3 sdfVoxelSize(sdfVolumeBoundsSize.x / sdfVolumeDimension.x, sdfVolumeBoundsSize.y / sdfVolumeDimension.y,
                      sdfVolumeBoundsSize.z / sdfVolumeDimension.z);

    float voxelDiameterSquared = dot(sdfVoxelSize, sdfVoxelSize);

    for (int32_t yIndex = 0; yIndex < sdfVolumeDimension.y; ++yIndex)
    {
        for (int32_t xIndex = 0; xIndex < sdfVolumeDimension.x; ++xIndex)
        {
            vec3 offsettedIndex = vec3((float)(xIndex) + 0.5f, float(yIndex) + 0.5f, float(zIndex) + 0.5f);

            vec3 voxelPos = vec3(offsettedIndex.x * sdfVoxelSize.x, offsettedIndex.y * sdfVoxelSize.y, offsettedIndex.z * sdfVoxelSize.z) +
                            sdfVolumeBounds.min;

            int32_t outIndex = (zIndex * sdfVolumeDimension.y * sdfVolumeDimension.x + yIndex * sdfVolumeDimension.x + xIndex);

            float   minDistance = sdfVolumeMaxDist;
            int32_t hit = 0;
            int32_t hitBack = 0;

            for (uint32_t sampleIndex = 0; sampleIndex < arrlen(directionsList); ++sampleIndex)
            {
                vec3 rayDir = directionsList[sampleIndex];
                // vec3 endPos = voxelPos + rayDir * sdfVolumeMaxDist;

                TFRay newRay = rayMake(voxelPos, rayDir);

                bool intersectWithBbox =
                    RayIntersectsBox(newRay.origin, Vector3(1.f / newRay.direction.x, 1.f / newRay.direction.y, 1.f / newRay.direction.z),
                                     sdfVolumeBounds.min, sdfVolumeBounds.max);

                // If we pass the cheap bbox testing
                if (intersectWithBbox)
                {
                    Intersection meshTriangleIntersect;
                    // Optimized version
                    BVHTreeIntersectRayAux(bvhTree->mRootNode, bvhTree->mRootNode, newRay, meshTriangleIntersect);
                    if (meshTriangleIntersect.mIsIntersected)
                    {
                        ++hit;
                        const vec3& hitNormal = meshTriangleIntersect.mHittedNormal;
                        if (dot(rayDir, hitNormal) > 0 && !task->mIsTwoSided)
                        {
                            ++hitBack;
                        }

                        const vec3 finalEndPos = rayEval(newRay, meshTriangleIntersect.mIntersection_TVal);

                        float newDist = length(newRay.origin - finalEndPos);

                        if (newDist < minDistance)
                        {
                            minDistance = newDist;
                        }
                    }
                }
            }

            //

            float unsignedDist = minDistance;

            // If 50% hit backface, we consider the voxel sdf value to be inside the mesh
            minDistance *= (hit == 0 || hitBack < ((float)arrlen(directionsList) * 0.5f)) ? 1 : -1;

            // If we are very close to the surface and 95% of our rays hit backfaces, the sdf value is inside the mesh
            if ((unsignedDist * unsignedDist) < voxelDiameterSquared && hitBack > 0.95f * hit)
            {
                minDistance = -unsignedDist;
            }

            minDistance = fminf(minDistance, sdfVolumeMaxDist);
            // float maxExtent = fmax(fmax(sdfVolumeBounds.GetExtent().x,
            // sdfVolumeBounds.GetExtent().y), sdfVolumeBounds.GetExtent().z);
            vec3  sdfVolumeBoundsExtent = calculateAABBExtent(&sdfVolumeBounds);
            float maxExtent = maxElem(sdfVolumeBoundsExtent);

            float volumeSpaceDist = minDistance / maxExtent;

            sdfVolumeList[outIndex] = volumeSpaceDist;
        }
    }
}

bool GenerateVolumeDataFromFile(SDFVolumeData** ppOutVolumeData, MeshInfo* pMeshInfo)
{
    TFFileStream newBakedFile = {};
    if (!fsOpenStreamFromPath(TF_RD_OTHER_FILES, pMeshInfo->name, TF_FM_READ, &newBakedFile))
    {
        return false;
    }

    *ppOutVolumeData = tf_new(SDFVolumeData);
    SDFVolumeData& outVolumeData = **ppOutVolumeData;

    int32_t x, y, z;
    fsReadFromStream(&newBakedFile, &x, sizeof(int32_t));
    fsReadFromStream(&newBakedFile, &y, sizeof(int32_t));
    fsReadFromStream(&newBakedFile, &z, sizeof(int32_t));
    outVolumeData.mSDFVolumeSize.x = (x);
    outVolumeData.mSDFVolumeSize.y = (y);
    outVolumeData.mSDFVolumeSize.z = (z);

    uint32_t finalSDFVolumeDataCount = outVolumeData.mSDFVolumeSize.x * outVolumeData.mSDFVolumeSize.y * outVolumeData.mSDFVolumeSize.z;

    arrsetlen(outVolumeData.mSDFVolumeList, finalSDFVolumeDataCount);

    fsReadFromStream(&newBakedFile, outVolumeData.mSDFVolumeList, finalSDFVolumeDataCount * sizeof(float));
    float3 min;
    float3 max;
    fsReadFromStream(&newBakedFile, &min, sizeof(float3));
    fsReadFromStream(&newBakedFile, &max, sizeof(float3));
    outVolumeData.mLocalBoundingBox.min = (min);
    outVolumeData.mLocalBoundingBox.max = (max);
    fsReadFromStream(&newBakedFile, &outVolumeData.mIsTwoSided, sizeof(bool));
    // outVolumeData.mTwoSidedWorldSpaceBias = newBakedFile.ReadFloat();
    outVolumeData.mTwoSidedWorldSpaceBias = pMeshInfo->twoSidedWorldSpaceBias;
    /*
    only uses the minimum & maximum of SDF if we ever want to quantized the SDF data
    for (int32_t index = 0; index < outVolumeData.mSDFVolumeList.size(); ++index)
    {
        const float volumeSpaceDist = outVolumeData.mSDFVolumeList[index];
        outVolumeData.mDistMinMax.x = (fmin(volumeSpaceDist, outVolumeData.mDistMinMax.x));
        outVolumeData.mDistMinMax.y = (fmax(volumeSpaceDist, outVolumeData.mDistMinMax.y));
    }*/
    fsCloseStream(&newBakedFile);

    LOGF(LogLevel::eINFO, "SDF binary data for %s found & parsed", pMeshInfo->name);
    return true;
}

void GenerateVolumeDataFromMesh(SDFVolumeData** ppOutVolumeData, SDFMesh* pMainMesh, MeshInfo* pMeshGroupInfo, uint32_t groupNum,
                                uint32_t meshGroupSize, uint32_t idxFirstMeshInGroup, float sdfResolutionScale)
{
    if (sShouldExitSDFGeneration)
        return;

    if (GenerateVolumeDataFromFile(ppOutVolumeData, pMeshGroupInfo))
        return;

    LOGF(LogLevel::eINFO, "Generating SDF binary data for %s", pMeshGroupInfo->name);

    *ppOutVolumeData = tf_new(SDFVolumeData);

    SDFVolumeData& outVolumeData = **ppOutVolumeData;

    // For now assume all triangles are valid and useable
    ivec3 maxNumVoxelsOneDimension;
    ivec3 minNumVoxelsOneDimension;

    if (pMeshGroupInfo->materialFlags & MATERIAL_FLAG_DOUBLE_VOXEL_SIZE)
    {
        maxNumVoxelsOneDimension =
            ivec3(SDF_DOUBLE_MAX_VOXEL_ONE_DIMENSION_X, SDF_DOUBLE_MAX_VOXEL_ONE_DIMENSION_Y, SDF_DOUBLE_MAX_VOXEL_ONE_DIMENSION_Z);

        minNumVoxelsOneDimension =
            ivec3(SDF_DOUBLE_MAX_VOXEL_ONE_DIMENSION_X, SDF_DOUBLE_MAX_VOXEL_ONE_DIMENSION_Y, SDF_DOUBLE_MAX_VOXEL_ONE_DIMENSION_Z);
    }
    else
    {
        maxNumVoxelsOneDimension = ivec3(SDF_MAX_VOXEL_ONE_DIMENSION_X, SDF_MAX_VOXEL_ONE_DIMENSION_Y, SDF_MAX_VOXEL_ONE_DIMENSION_Z);
        minNumVoxelsOneDimension = ivec3(SDF_MIN_VOXEL_ONE_DIMENSION_X, SDF_MIN_VOXEL_ONE_DIMENSION_Y, SDF_MIN_VOXEL_ONE_DIMENSION_Z);
    }

    const float voxelDensity = 1.0f;

    const float numVoxelPerLocalSpaceUnit = voxelDensity * sdfResolutionScale;

    TFAABB subMeshBBox(vec3(FLT_MAX), vec3(-FLT_MAX));

    BVHTree bvhTree;

    if (pMainMesh->pGeometry->mIndexType == TF_INDEX_TYPE_UINT16)
    {
        AddMeshInstanceToBBOX16(&bvhTree, pMainMesh, (uint16_t*)pMainMesh->pGeometryData->pCpuData->pIndices, groupNum, meshGroupSize,
                                idxFirstMeshInGroup, &subMeshBBox, (mat4)mat4::identity());
    }
    else
    {
        AddMeshInstanceToBBOX32(&bvhTree, pMainMesh, (uint32_t*)pMainMesh->pGeometryData->pCpuData->pIndices, groupNum, meshGroupSize,
                                idxFirstMeshInGroup, &subMeshBBox, (mat4)mat4::identity());
    }

    bvhTree.mRootNode = CreateBVHNodeSHA(&bvhTree, 0, (int32_t)arrlen(bvhTree.mBBOXDataList) - 1, FLT_MAX);

    vec3 subMeshExtent = 0.5f * (subMeshBBox.max - subMeshBBox.min);

    float maxExtentSize = maxElem(subMeshExtent);

    vec3 minNewExtent(0.2f * maxExtentSize);
    vec3 standardExtentSize = 4.f * subMeshExtent;
    vec3 dynamicNewExtent(standardExtentSize.x / minNumVoxelsOneDimension.x, standardExtentSize.y / minNumVoxelsOneDimension.y,
                          standardExtentSize.z / minNumVoxelsOneDimension.z);

    vec3 finalNewExtent = subMeshExtent + vec3(fmaxf(minNewExtent.x, dynamicNewExtent.x), fmaxf(minNewExtent.y, dynamicNewExtent.y),
                                               fmaxf(minNewExtent.z, dynamicNewExtent.z));

    vec3 subMeshBBoxCenter = (subMeshBBox.max + subMeshBBox.min) * 0.5f;

    TFAABB newSDFVolumeBound;
    newSDFVolumeBound.min = subMeshBBoxCenter - finalNewExtent;
    newSDFVolumeBound.max = subMeshBBoxCenter + finalNewExtent;

    vec3 newSDFVolumeBoundSize = newSDFVolumeBound.max - newSDFVolumeBound.min;
    vec3 newSDFVolumeBoundExtent = 0.5f * (newSDFVolumeBound.max - newSDFVolumeBound.min);

    float newSDFVolumeMaxDistance = length(newSDFVolumeBoundExtent);
    vec3  dynamicDimension = vec3(newSDFVolumeBoundSize.x * numVoxelPerLocalSpaceUnit, newSDFVolumeBoundSize.y * numVoxelPerLocalSpaceUnit,
                                 newSDFVolumeBoundSize.z * numVoxelPerLocalSpaceUnit);

    ivec3 finalSDFVolumeDimension(clamp((int32_t)(dynamicDimension.x), minNumVoxelsOneDimension.x, maxNumVoxelsOneDimension.x),
                                  clamp((int32_t)(dynamicDimension.y), minNumVoxelsOneDimension.y, maxNumVoxelsOneDimension.y),
                                  clamp((int32_t)(dynamicDimension.z), minNumVoxelsOneDimension.z, maxNumVoxelsOneDimension.z));

    uint32_t finalSDFVolumeDataCount = finalSDFVolumeDimension.x * finalSDFVolumeDimension.y * finalSDFVolumeDimension.z;

    arrsetlen(outVolumeData.mSDFVolumeList, finalSDFVolumeDataCount);

    // Here we begin our stratified sampling calculation
    const uint32_t numVoxelDistanceSample = SDF_STRATIFIED_DIRECTIONS_NUM;

    STB_DS_ARRAY(vec3) sampleDirectionsList = NULL;

    int32_t thetaStep = (int32_t)floor((sqrt((float)numVoxelDistanceSample / (PI * 2.f))));
    int32_t phiStep = (int32_t)floor((float)thetaStep * PI);

    // sampleDirectionsList.reserve(thetaStep * phiStep * 2);

    GenerateSampleDirections(thetaStep, phiStep, sampleDirectionsList, 1);
    GenerateSampleDirections(thetaStep, phiStep, sampleDirectionsList, -1);

    bool twoSided = pMeshGroupInfo->materialFlags & MATERIAL_FLAG_TWO_SIDED;

    CalculateMeshSDFTask calculateMeshSDFTask = {};
    calculateMeshSDFTask.mDirectionsList = &sampleDirectionsList;
    calculateMeshSDFTask.mSDFVolumeBounds = &newSDFVolumeBound;
    calculateMeshSDFTask.mSDFVolumeDimension = &finalSDFVolumeDimension;
    calculateMeshSDFTask.mSDFVolumeMaxDist = newSDFVolumeMaxDistance;
    calculateMeshSDFTask.mSDFVolumeList = &outVolumeData.mSDFVolumeList;
    calculateMeshSDFTask.mBVHTree = &bvhTree;
    calculateMeshSDFTask.mIsTwoSided = twoSided;

    for (int32_t zIndex = 0; zIndex < finalSDFVolumeDimension.z; ++zIndex)
    {
        if (sShouldExitSDFGeneration)
            break;

        calculateMeshSDFTask.mZIndex = zIndex;
        DoCalculateMeshSDFTask(&calculateMeshSDFTask, 0);
    }

    DeleteBVHTree(bvhTree.mRootNode);
    arrfree(bvhTree.mBBOXDataList);
    bvhTree.mBBOXDataList = NULL;

    if (sShouldExitSDFGeneration)
        return;

    TFFileStream portDataFile = {};
    fsOpenStreamFromPath(TF_RD_OTHER_FILES, pMeshGroupInfo->name, TF_FM_WRITE, &portDataFile);
    int32_t x = finalSDFVolumeDimension.x;
    int32_t y = finalSDFVolumeDimension.y;
    int32_t z = finalSDFVolumeDimension.z;
    fsWriteToStream(&portDataFile, &x, sizeof(int32_t));
    fsWriteToStream(&portDataFile, &y, sizeof(int32_t));
    fsWriteToStream(&portDataFile, &z, sizeof(int32_t));
    fsWriteToStream(&portDataFile, outVolumeData.mSDFVolumeList, finalSDFVolumeDataCount * sizeof(float));
    float3 min = (newSDFVolumeBound.min);
    float3 max = (newSDFVolumeBound.max);
    fsWriteToStream(&portDataFile, &min, sizeof(float3));
    fsWriteToStream(&portDataFile, &max, sizeof(float3));
    fsWriteToStream(&portDataFile, &twoSided, sizeof(bool));
    // portDataFile.WriteFloat(twoSidedWorldSpaceBias);
    fsCloseStream(&portDataFile);

    float minVolumeDist = 1.0f;
    float maxVolumeDist = -1.0f;

    // We can probably move the calculation of the minimum & maximum distance of the SDF value
    // into the CalculateMeshSDFValue function
    for (size_t index = 0; index < arrlenu(outVolumeData.mSDFVolumeList); ++index)
    {
        const float volumeSpaceDist = outVolumeData.mSDFVolumeList[index];
        minVolumeDist = fminf(volumeSpaceDist, minVolumeDist);
        maxVolumeDist = fmaxf(volumeSpaceDist, maxVolumeDist);
    }

    // TODO, not every mesh is going to be closed do the check sometime in the future
    outVolumeData.mIsTwoSided = twoSided;
    outVolumeData.mSDFVolumeSize = finalSDFVolumeDimension;
    outVolumeData.mLocalBoundingBox = newSDFVolumeBound;
    outVolumeData.mDistMinMax = vec2(minVolumeDist, maxVolumeDist);
    outVolumeData.mTwoSidedWorldSpaceBias = pMeshGroupInfo->twoSidedWorldSpaceBias;
}

static SDFMesh SDFMeshes[NUM_SDF_MESHES] = {};
static STB_DS_ARRAY(SDFVolumeData*) gSDFVolumeInstances = NULL;

gSDFVolumeTextureAtlas* pSDFVolumeTextureAtlas = NULL;

UpdateSDFVolumeTextureAtlasConstants gUpdateSDFVolumeTextureAtlasConstants = {};
MeshSDFConstants                     gMeshSDFConstants = {};

static void generateMissingSDFTask(void* user, uint64_t)
{
    uint32_t idxFirstMeshInGroup = 0;

    SDFMesh* sdfMesh = (SDFMesh*)user;
    for (uint32_t groupNum = 0; groupNum < sdfMesh->numSubMeshesGroups; ++groupNum)
    {
        MeshInfo& meshInfo = sdfMesh->pSubMeshesInfo[idxFirstMeshInGroup];
        uint32_t  meshGroupSize = sdfMesh->pSubMeshesGroupsSizes ? sdfMesh->pSubMeshesGroupsSizes[groupNum] : 1;

        // Generate what isn't already generated
        if (!meshInfo.sdfGenerated)
        {
            SDFVolumeData* volumeData = NULL;

            GenerateVolumeDataFromMesh(&volumeData, sdfMesh, &meshInfo, groupNum, meshGroupSize, idxFirstMeshInGroup, 1.f);

            if (volumeData)
            {
                arrpush(gSDFVolumeInstances, volumeData);
                meshInfo.sdfGenerated = true;
                ++sdfMesh->numGeneratedSDFMeshes;
            }
        }

        idxFirstMeshInGroup += meshGroupSize;
    }
    gAppSettings.mIsGeneratingSDF = false;
}

static size_t sSDFProgressValue = 0;
static size_t sSDFProgressMaxValue = 0;

/*--------------------ASM main logic-------------*/

#define SQR(a) ((a) * (a))

static const float maxWarpAngleCos = 0.994f;
static uint32_t    asmFrameCounter;
float              gLightDirUpdateThreshold = 0.999f;
float              gLightDirUpdateAngle = 0;

class QuadTreeNode;
class ASMFrustum;
class ASMTileCache;

vec3 Project(const vec3& v, float w, const mat4& projMat)
{
    vec4 newV = projMat * vec4(v, w);
    return vec3(newV.x / newV.w, newV.y / newV.w, newV.z / newV.w);
}

float GetRefinementDistanceSq(const TFAABB& BBox, const vec2& refinementPos)
{
    vec3 bboxCenter = calculateAABBCenter(&BBox);
    return lengthSqr(vec2(bboxCenter.x, bboxCenter.y) - refinementPos);
}

float Get3DRefinementDistanceSq(const TFAABB& BBox, const vec2& refinementPos)
{
    vec3 bboxCenter = calculateAABBCenter(&BBox);
    return lengthSqr((bboxCenter)-vec3(refinementPos.x, refinementPos.y, 0.f));
}

struct ASMProjectionData
{
    mat4           mViewMat;
    mat4           mInvViewMat;
    TFCameraMatrix mProjMat;
    TFCameraMatrix mInvProjMat;
    TFCameraMatrix mViewProjMat;
    TFCameraMatrix mInvViewProjMat;
};

class ConvexHull2D
{
public:
    static const uint32_t MAX_VERTICES = 5;
    vec2                  m_vertices[MAX_VERTICES] = { vec2(0.f), vec2(0.f), vec2(0.f), vec2(0.f), vec2(0.f) };
    int32_t               m_size;

    ConvexHull2D() { Reset(); }

    ConvexHull2D(int32_t numVertices, const vec2* pVertices) { m_size = FindConvexHull(numVertices, pVertices, m_vertices); }

    void Reset() { m_size = 0; }

    const vec2 FindFrustumConvexHull(const ASMProjectionData& projection, float frustumZMaxOverride, const mat4& viewProj)
    {
        static const uint32_t numVertices = 5;
        vec3                  frustumPos = projection.mInvViewMat[3].getXYZ();

        vec3 projectedFrustumPos = Project(frustumPos, 1.f, viewProj);
        vec2 vertices[numVertices];
        vertices[0] = vec2(projectedFrustumPos.x, projectedFrustumPos.y);

        mat4 projMat = projection.mProjMat.mMatrices[MONO_CAMERA_VIEW_INDEX];

        float hz = Project(vec3(0, 0, frustumZMaxOverride), 1.f, projMat).z;

        const vec3 frustumCorners[] = {
            vec3(-1.0f, -1.0f, hz),
            vec3(+1.0f, -1.0f, hz),
            vec3(+1.0f, +1.0f, hz),
            vec3(-1.0f, +1.0f, hz),
        };

        mat4 tm = viewProj * projection.mInvViewProjMat.mMatrices[MONO_CAMERA_VIEW_INDEX];
        for (uint32_t i = 1; i < numVertices; ++i)
        {
            vec3 indxProjectedFrustumPos = Project(frustumCorners[i - 1], 1.f, tm);
            vertices[i] = vec2(indxProjectedFrustumPos.x, indxProjectedFrustumPos.y);
        }

        m_size = FindConvexHull(numVertices, vertices, m_vertices);

        return vertices[0];
    }

    bool Intersects(const TFAABB& BBox) const
    {
        if (m_size == 0)
        {
            return false;
        }

        static const vec2 normals[] = {
            vec2(1, 0),
            vec2(0, 1),
            vec2(-1, 0),
            vec2(0, -1),
        };

        vec2 vb1[MAX_VERTICES * 2];
        vec2 vb2[MAX_VERTICES * 2];

        const vec2* v = m_vertices;
        int32_t     n = m_size;

        int32_t j, index[2];
        float   d[2];
        for (int32_t i = 0; i < 4; ++i)
        {
            float pw = -dot(vec3(normals[i].x, normals[i].y, 0.f), i < 2 ? BBox.min : BBox.max);
            index[1] = n - 1;
            d[1] = dot(normals[i], v[index[1]]) + pw;
            for (j = 0; j < n; j++)
            {
                index[0] = index[1];
                index[1] = j;
                d[0] = d[1];
                d[1] = dot(normals[i], v[index[1]]) + pw;
                if (d[1] > 0 && d[0] < 0)
                    break;
            }
            if (j < n)
            {
                int32_t k = 0;
                vec2*   tmp = v == vb1 ? vb2 : vb1;

                vec3 lerpedVal =
                    lerp(vec3(v[index[1]].x, v[index[1]].y, 0.f), vec3(v[index[0]].x, v[index[0]].y, 0.f), d[1] / (d[1] - d[0]));
                tmp[k++] = vec2(lerpedVal.x, lerpedVal.y);
                do
                {
                    index[0] = index[1];
                    index[1] = (index[1] + 1) % n;
                    d[0] = d[1];
                    d[1] = dot(normals[i], v[index[1]]) + pw;
                    tmp[k++] = v[index[0]];
                } while (d[1] > 0);

                vec3 lerpedVal2 =
                    lerp(vec3(v[index[1]].x, v[index[1]].y, 0.f), vec3(v[index[0]].x, v[index[0]].y, 0.f), d[1] / (d[1] - d[0]));

                tmp[k++] = vec2(lerpedVal2.x, lerpedVal2.y);
                n = k;
                v = tmp;
            }
            else
            {
                if (d[1] < 0)
                    return false;
            }
        }
        return n > 0;
    }

    static int32_t FindConvexHull(int32_t numVertices, const vec2* pVertices, vec2* pHull)
    {
        //_ASSERT(numVertices <= MAX_VERTICES);
        const float eps = 1e-5f;
        const float epsSq = eps * eps;
        int32_t     leftmostIndex = 0;
        for (int32_t i = 1; i < numVertices; ++i)
        {
            float f = pVertices[leftmostIndex].x - pVertices[i].x;
            if (fabsf(f) < epsSq)
            {
                if (pVertices[leftmostIndex].y > pVertices[i].y)
                    leftmostIndex = i;
            }
            else if (f > 0)
            {
                leftmostIndex = i;
            }
        }
        vec2    dir0(0, -1);
        int32_t hullSize = 0;
        int32_t index0 = leftmostIndex;
        do
        {
            float   maxCos = -FLT_MAX;
            int32_t index1 = -1;
            vec2    dir1(0.0f);
            for (int32_t j = 1; j < numVertices; ++j)
            {
                int32_t k = (index0 + j) % numVertices;
                vec2    v = pVertices[k] - pVertices[index0];

                float l = lengthSqr(v);
                if (l > epsSq)
                {
                    vec2  d = normalize(v);
                    float f = dot(d, dir0);
                    if (maxCos < f)
                    {
                        maxCos = f;
                        index1 = k;
                        dir1 = d;
                    }
                }
            }
            if (index1 < 0 || hullSize >= numVertices)
            {
                //_ASSERT(!"epic fail");
                return 0;
            }
            pHull[hullSize++] = pVertices[index1];
            index0 = index1;
            dir0 = dir1;
        } while (lengthSqr(pVertices[index0] - pVertices[leftmostIndex]) > epsSq);
        return hullSize;
    }
};

struct ASMRendererContext
{
    TFRenderer* m_pRenderer;
    TFCmd*      m_pCmd;
};

struct ASMRenderTargets
{
    TFRenderTarget*  m_pRenderTargetASMLodClamp;
    TFRenderTarget*  m_pRenderTargetASMPrerenderLodClamp;
    TFRenderTarget** m_pASMPrerenderIndirectionMips;
    TFRenderTarget** m_pASMIndirectionMips;
};

struct ASMSShadowMapPrepareRenderContext
{
    const vec3* m_worldCenter;
};

struct ASMSShadowMapRenderContext
{
    ASMRendererContext* m_pRendererContext;
    ASMProjectionData*  m_pASMProjectionData;
};

struct IndirectionRenderData
{
    TFBuffer*   pBufferASMPackedIndirectionQuadsUniform[gs_ASMMaxRefinement + 1];
    TFBuffer*   pBufferASMClearIndirectionQuadsUniform;
    TFPipeline* m_pGraphicsPipeline;
};

struct ASMTickData
{
    IndirectionRenderData mIndirectionRenderData;
    IndirectionRenderData mPrerenderIndirectionRenderData;
};

ASMTickData gASMTickData;

class CIntrusiveUnorderedSetItemHandle
{
public:
    CIntrusiveUnorderedSetItemHandle(): mIndex(-1) {}
    bool IsInserted() const { return mIndex >= 0; }

protected:
    int32_t mIndex;
};

template<class CItem, CIntrusiveUnorderedSetItemHandle& (CItem::*GetHandle)()>
class CIntrusiveUnorderedPtrSet
{
    struct SHandleSetter: public CIntrusiveUnorderedSetItemHandle
    {
        void    Set(int32_t i) { mIndex = i; }
        void    Set_Size_t(size_t i) { mIndex = static_cast<int32_t>(i); }
        int32_t Get() const { return mIndex; }
    };

public:
    ~CIntrusiveUnorderedPtrSet()
    {
        arrfree(container);
        container = NULL;
    }

    void Add(CItem* pItem, bool mayBeAlreadyInserted = false)
    {
        UNREF_PARAM(mayBeAlreadyInserted);
        SHandleSetter& handle = static_cast<SHandleSetter&>((pItem->*GetHandle)());
        if (!handle.IsInserted())
        {
            handle.Set_Size_t(arrlenu(container));
            arrpush(container, pItem);
        }
    }
    void Remove(CItem* pItem, bool mayBeNotInserted = false)
    {
        UNREF_PARAM(mayBeNotInserted);
        SHandleSetter& handle = static_cast<SHandleSetter&>((pItem->*GetHandle)());
        if (handle.IsInserted())
        {
            CItem* pLastItem = container[arrlen(container) - 1];
            (pLastItem->*GetHandle)() = handle;
            container[handle.Get()] = pLastItem;
            (void)arrpop(container);
            handle.Set(-1);
        }
    }

    size_t size() const { return arrlenu(container); }

    CItem*& operator[](size_t i) { return container[i]; }

    CItem* operator[](size_t i) const { return container[i]; }

    bool empty() const { return size() == 0; }

    STB_DS_ARRAY(CItem*) container = NULL;
};

template<class CItem, CIntrusiveUnorderedSetItemHandle& (CItem::*GetHandle)()>
class CVectorBasedIntrusiveUnorderedPtrSet: public CIntrusiveUnorderedPtrSet<CItem, GetHandle>
{
};

struct SQuad
{
    vec4 m_pos;

    static const SQuad Get(int32_t dstRectW, int32_t dstRectH, int32_t dstX, int32_t dstY, int32_t dstW, int32_t dstH)
    {
        SQuad q;

        q.m_pos.x = (float(dstRectW) / float(dstW));
        q.m_pos.y = (float(dstRectH) / float(dstH));
        q.m_pos.z = (q.m_pos.x + 2.0f * float(dstX) / float(dstW) - 1.0f);
        q.m_pos.w = (1.0f - q.m_pos.y - 2.0f * float(dstY) / float(dstH));

        return q;
    }
};

struct SFillQuad: public SQuad
{
    vec4 m_misc;

    static const SFillQuad Get(const vec4& miscParams, int32_t dstRectW, int32_t dstRectH, int32_t dstX, int32_t dstY, int32_t dstW,
                               int32_t dstH)
    {
        SFillQuad q;

        static_cast<SQuad&>(q) = SQuad::Get(dstRectW, dstRectH, dstX, dstY, dstW, dstH);

        q.m_misc = miscParams;

        return q;
    }
};

struct SCopyQuad: public SFillQuad
{
    vec4 m_texCoord;

    static const SCopyQuad Get(const vec4& miscParams, int32_t dstRectW, int32_t dstRectH, int32_t dstX, int32_t dstY, int32_t dstW,
                               int32_t dstH, int32_t srcRectW, int32_t srcRectH, int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH)
    {
        SCopyQuad q;

        static_cast<SFillQuad&>(q) = SFillQuad::Get(miscParams, dstRectW, dstRectH, dstX, dstY, dstW, dstH);

        // Align with pixel center @ (0.5, 0.5).
        q.m_pos.z = (q.m_pos.z + 1.0f / float(dstW));
        q.m_pos.w = (q.m_pos.w - 1.0f / float(dstH));

        q.m_texCoord.x = (float(srcRectW) / float(srcW));
        q.m_texCoord.y = (float(srcRectH) / float(srcH));
        q.m_texCoord.z = ((float(srcX) + 0.5f) / float(srcW));
        q.m_texCoord.w = ((float(srcY) + 0.5f) / float(srcH));

        return q;
    }
};

class ASMTileCacheEntry
{
public:
    struct SViewport
    {
        int32_t x, y, w, h;
    } m_viewport;
    uint8_t m_refinement;

    TFAABB        m_BBox;
    QuadTreeNode* m_pOwner;
    ASMFrustum*   m_pFrustum;
    uint32_t      m_lastFrameUsed;
    uint32_t      m_frustumID;
    bool          m_isLayer;
    float         m_fadeInFactor;

    ASMProjectionData mRenderProjectionData;

    ASMTileCacheEntry(ASMTileCache* pCache, int32_t x, int32_t y);
    ~ASMTileCacheEntry();

    template<ASMTileCacheEntry*& (QuadTreeNode::*TileAccessor)(), bool isLayer>
    void Allocate(QuadTreeNode* pOwner, ASMFrustum* pFrustum);

    void Free();
    void Invalidate()
    {
        m_BBox = TFAABB();
        m_refinement = gs_ASMMaxRefinement;
        m_lastFrameUsed = asmFrameCounter - 0x7fFFffFF;
        m_frustumID = 0;
    }
    void MarkReady();
    void MarkNotReady();

    bool          IsReady() const { return m_readyTilesPos.IsInserted(); }
    ASMTileCache* GetCache() const { return m_pCache; }
    bool          IsAllocated() const { return m_pOwner != nullptr; }
    bool          IsBeingUpdated() const { return m_updateQueuePos.IsInserted(); }
    bool          IsQueuedForRendering() const { return m_renderQueuePos.IsInserted(); }

    static const ivec4 GetRect(SViewport vp, int32_t border)
    {
        return ivec4(vp.x - border, vp.y - border, vp.x + vp.w + border, vp.y + vp.h + border);
    }

protected:
    ASMTileCache* m_pCache;

    CIntrusiveUnorderedSetItemHandle m_tilesPos;
    CIntrusiveUnorderedSetItemHandle m_freeTilesPos;
    CIntrusiveUnorderedSetItemHandle m_renderQueuePos;
    CIntrusiveUnorderedSetItemHandle m_readyTilesPos;
    CIntrusiveUnorderedSetItemHandle m_demQueuePos;
    CIntrusiveUnorderedSetItemHandle m_renderBatchPos;
    CIntrusiveUnorderedSetItemHandle m_updateQueuePos;

    CIntrusiveUnorderedSetItemHandle& GetTilesPos() { return m_tilesPos; }
    CIntrusiveUnorderedSetItemHandle& GetFreeTilesPos() { return m_freeTilesPos; }
    CIntrusiveUnorderedSetItemHandle& GetRenderQueuePos() { return m_renderQueuePos; }
    CIntrusiveUnorderedSetItemHandle& GetReadyTilesPos() { return m_readyTilesPos; }
    CIntrusiveUnorderedSetItemHandle& GetDemQueuePos() { return m_demQueuePos; }
    CIntrusiveUnorderedSetItemHandle& GetRenderBatchPos() { return m_renderBatchPos; }
    CIntrusiveUnorderedSetItemHandle& GetUpdateQueuePos() { return m_updateQueuePos; }

    void PrepareRender(ASMSShadowMapPrepareRenderContext context);

    friend class ASMTileCache;
};

class ASMTileCache
{
public:
    ASMTileCache():
        m_cacheHits(0), m_tileAllocs(0), m_numTilesRendered(0), m_numTilesUpdated(0), m_depthAtlasWidth(0), m_depthAtlasHeight(0),
        m_demAtlasWidth(0), m_demAtlasHeight(0), mDEMFirstTimeRender(true), mDepthFirstTimeRender(true)
    {
        m_depthAtlasWidth = gs_ASMDepthAtlasTextureWidth;
        m_depthAtlasHeight = gs_ASMDepthAtlasTextureHeight;

        m_demAtlasWidth = gs_ASMDEMAtlasTextureWidth;
        m_demAtlasHeight = gs_ASMDEMAtlasTextureHeight;

        int32_t gridWidth = m_depthAtlasWidth / gs_ASMTileSize;
        int32_t gridHeight = m_depthAtlasHeight / gs_ASMTileSize;

        for (int32_t i = 0; i < gridHeight; ++i)
        {
            for (int32_t j = 0; j < gridWidth; ++j)
            {
                tf_new(ASMTileCacheEntry, this, j * gs_ASMTileSize, i * gs_ASMTileSize);
            }
        }
    }

    ~ASMTileCache()
    {
        for (size_t i = m_tiles.size(); i > 0; --i)
        {
            tf_delete(m_tiles[i - 1]);
        }
    }

    void Tick(float deltaTime)
    {
        for (uint32_t i = 0; i < m_readyTiles.size(); ++i)
        {
            ASMTileCacheEntry* pTile = m_readyTiles[i];
            pTile->m_fadeInFactor = max(0.0f, pTile->m_fadeInFactor - deltaTime);
        }
    }

    static float CalcDepthBias(const mat4& orthoProjMat, const vec3& kernelSize, int32_t viewportWidth, int32_t viewportHeight,
                               int32_t depthBitsPerPixel)
    {
        vec3  texelSizeWS(fabsf(2.0f / (orthoProjMat[0].x * float(viewportWidth))),
                         fabsf(2.0f / (orthoProjMat[1].y * float(viewportHeight))),
                         fabsf(1.0f / (orthoProjMat[2].z * float(1 << depthBitsPerPixel))));
        vec3  kernelSizeWS = vec3(texelSizeWS.x * kernelSize.x, texelSizeWS.y * kernelSize.y, texelSizeWS.z * kernelSize.z);
        float kernelSizeMax = fmaxf(fmaxf(kernelSizeWS.x, kernelSizeWS.y), kernelSizeWS.z);
        return kernelSizeMax * fabsf(orthoProjMat[2].z);
    }

    template<ASMTileCacheEntry*& (QuadTreeNode::*TileAccessor)(), bool isLayer>
    ASMTileCacheEntry* Allocate(QuadTreeNode* pNode, ASMFrustum* pFrustum);

    int32_t AddTileFromRenderQueueToRenderBatch(ASMFrustum* pFrustum, int32_t maxRefinement, bool isLayer)
    {
        return AddTileToRenderBatch(m_renderQueue, pFrustum, maxRefinement, isLayer);
    }

    int32_t AddTileFromUpdateQueueToRenderBatch(ASMFrustum* pFrustum, int32_t maxRefinement, bool isLayer)
    {
        return AddTileToRenderBatch(m_updateQueue, pFrustum, maxRefinement, isLayer);
    }

    bool PrepareRenderTilesBatch(ASMSShadowMapPrepareRenderContext context)
    {
        for (uint32_t i = 0; i < m_renderBatch.size(); ++i)
        {
            ASMTileCacheEntry* pTile = m_renderBatch[i];
            pTile->PrepareRender(context);
        }
        return !m_renderBatch.empty();
    }

    void RenderTilesBatch(TFRenderTarget* workBufferDepth, TFRenderTarget* workBufferColor, ASMSShadowMapRenderContext& context)
    {
        if (!m_renderBatch.empty())
        {
            RenderTiles(static_cast<uint32_t>(m_renderBatch.size()), &m_renderBatch[0], workBufferDepth, workBufferColor, context, true);
        }

        for (size_t i = m_renderBatch.size(); i > 0; --i)
        {
            ASMTileCacheEntry* pTile = m_renderBatch[i - 1];
            m_renderBatch.Remove(pTile);

            if (!pTile->IsReady())
            {
                pTile->MarkReady();
                ++m_numTilesRendered;
            }
            else
            {
                ++m_numTilesRendered;
            }
        }
    }

    void CreateDEM(TFRenderTarget* demWorkBufferColor, ASMSShadowMapRenderContext context, bool createDemForLayerRendering)
    {
        if (m_demQueue.empty())
        {
            return;
        }

        ASMRendererContext* rendererContext = context.m_pRendererContext;
        TFCmd*              pCurCmd = rendererContext->m_pCmd;

        uint32_t workBufferWidth = demWorkBufferColor->mWidth;
        uint32_t workBufferHeight = demWorkBufferColor->mHeight;
        uint32_t numTilesW = workBufferWidth / gs_ASMDEMTileSize;
        uint32_t numTilesH = workBufferHeight / gs_ASMDEMTileSize;
        uint32_t maxTilesPerPass = numTilesW * numTilesH;

        SCopyQuad* atlasToBulkQuads = (SCopyQuad*)tf_malloc((sizeof(SCopyQuad) * 2 + sizeof(ASMTileCacheEntry*)) * maxTilesPerPass);

        SCopyQuad*          bulkToAtlasQuads = atlasToBulkQuads + maxTilesPerPass;
        ASMTileCacheEntry** tilesToUpdate = reinterpret_cast<ASMTileCacheEntry**>(bulkToAtlasQuads + maxTilesPerPass); //-V1032

        while (true)
        {
            uint32_t numTiles = 0;
            for (uint32_t i = 0; i < m_demQueue.size() && numTiles < maxTilesPerPass; ++i)
            {
                ASMTileCacheEntry* pTile = m_demQueue[i];

                bool isDemForLayerRendering = pTile->m_refinement > 0 && !pTile->m_isLayer;
                if (isDemForLayerRendering == createDemForLayerRendering)
                {
                    static const uint32_t rectSize = gs_ASMDEMTileSize - 2;

                    uint32_t workX = (numTiles % numTilesW) * gs_ASMDEMTileSize;
                    uint32_t workY = (numTiles / numTilesW) * gs_ASMDEMTileSize;

                    uint32_t atlasX = ((pTile->m_viewport.x - gs_ASMTileBorderTexels) >> gs_ASMDEMDownsampleLevel) + 1;
                    uint32_t atlasY = ((pTile->m_viewport.y - gs_ASMTileBorderTexels) >> gs_ASMDEMDownsampleLevel) + 1;

                    if (createDemForLayerRendering)
                    {
                    }
                    else
                    {
                        atlasToBulkQuads[numTiles] =
                            SCopyQuad::Get(vec4(1.0f / float(m_demAtlasWidth), 1.0f / float(m_demAtlasHeight), 0.0f, 0.0f), rectSize,
                                           rectSize, workX + 1, workY + 1, workBufferWidth, workBufferHeight, rectSize, rectSize, atlasX,
                                           atlasY, m_demAtlasWidth, m_demAtlasHeight);

                        // float zTest = atlasToBulkQuads[numTiles].m_misc.z;
                    }

                    bulkToAtlasQuads[numTiles] = SCopyQuad::Get(
                        vec4(1.0f / float(workBufferWidth), 1.0f / float(workBufferHeight), 0.0f, 0.0f), rectSize, rectSize, atlasX, atlasY,
                        m_demAtlasWidth, m_demAtlasHeight, rectSize, rectSize, workX + 1, workY + 1, workBufferWidth, workBufferHeight);

                    tilesToUpdate[numTiles++] = pTile;
                }
            }

            if (numTiles == 0)
            {
                break;
            }

#if defined(GFX_RESOURCE_INIT_NON_ZERO)
            // On Xbox the texture is initialized with garbage value, so texture that isn't cleared every frame
            // needs to be cleared at the begininng for xbox.
            if (mDEMFirstTimeRender)
            {
                TFBindRenderTargetsDesc clearDEMBindDesc = {};
                clearDEMBindDesc.mRenderTargetCount = 1;
                clearDEMBindDesc.mRenderTargets[0] = { pRenderTargetASMDEMAtlas, TF_LOAD_ACTION_CLEAR };
                cmdBindRenderTargets(pCurCmd, &clearDEMBindDesc);
                cmdBindRenderTargets(pCurCmd, NULL);
                mDEMFirstTimeRender = false;
            }
#endif

            cmdBeginGpuTimestampQuery(pCurCmd, gCurrentGpuProfileToken, "DEM Atlas To Color");

            TFRenderTargetBarrier asmAtlasToColorBarrier[] = {
                { demWorkBufferColor, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET },
                { pRenderTargetASMDEMAtlas, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_SHADER_RESOURCE }
            };

            cmdResourceBarrier(pCurCmd, 0, NULL, 0, NULL, 2, asmAtlasToColorBarrier);

            TFBindRenderTargetsDesc atlasToColorBindDesc = {};
            atlasToColorBindDesc.mRenderTargetCount = 1;
            atlasToColorBindDesc.mRenderTargets[0] = { demWorkBufferColor, TF_LOAD_ACTION_CLEAR };
            cmdBindRenderTargets(pCurCmd, &atlasToColorBindDesc);
            cmdSetViewport(pCurCmd, 0.0f, 0.0f, (float)demWorkBufferColor->mWidth, (float)demWorkBufferColor->mHeight, 0.0f, 1.0f);
            cmdSetScissor(pCurCmd, 0, 0, demWorkBufferColor->mWidth, demWorkBufferColor->mHeight);

            // GenerateDEMAtlasToColorRenderData& atlasToColorRenderData = gASMTickData.mDEMAtlasToColorRenderData;

            cmdBindPipeline(pCurCmd, pPipelineASMDEMAtlasToColor);

            TFBufferUpdateDesc atlasToColorUpdateDesc = {};
            atlasToColorUpdateDesc.pBuffer = pBufferASMAtlasToColorPackedQuadsUniform[gFrameIndex];
            beginUpdateResource(&atlasToColorUpdateDesc);
            memcpy(atlasToColorUpdateDesc.pMappedData, atlasToBulkQuads, sizeof(vec4) * 3 * numTiles);
            endUpdateResource(&atlasToColorUpdateDesc);

            cmdBindDescriptorSet(pCurCmd, 0, pDescriptorSetAtlasToColor[0]);
            cmdBindDescriptorSet(pCurCmd, gFrameIndex, pDescriptorSetAtlasToColor[1]);

            cmdDraw(pCurCmd, numTiles * 6, 0);

            cmdBindRenderTargets(pCurCmd, NULL);

            cmdEndGpuTimestampQuery(pCurCmd, gCurrentGpuProfileToken);

            cmdBeginGpuTimestampQuery(pCurCmd, gCurrentGpuProfileToken, "DEM Color To Atlas");

            TFRenderTargetBarrier asmColorToAtlasBarriers[] = {
                { demWorkBufferColor, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_SHADER_RESOURCE },
                { pRenderTargetASMDEMAtlas, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET }
            };

            cmdResourceBarrier(pCurCmd, 0, NULL, 0, NULL, 2, asmColorToAtlasBarriers);

            TFBindRenderTargetsDesc asmColorToAtlasBindDesc = {};
            asmColorToAtlasBindDesc.mRenderTargetCount = 1;
            asmColorToAtlasBindDesc.mRenderTargets[0] = { pRenderTargetASMDEMAtlas, TF_LOAD_ACTION_DONTCARE };
            cmdBindRenderTargets(pCurCmd, &asmColorToAtlasBindDesc);
            cmdSetViewport(pCurCmd, 0.0f, 0.0f, (float)pRenderTargetASMDEMAtlas->mWidth, (float)pRenderTargetASMDEMAtlas->mHeight, 0.0f,
                           1.0f);
            cmdSetScissor(pCurCmd, 0, 0, pRenderTargetASMDEMAtlas->mWidth, pRenderTargetASMDEMAtlas->mHeight);

            cmdBindPipeline(pCurCmd, pPipelineASMDEMColorToAtlas);

            TFBufferUpdateDesc colorToAtlasBufferUbDesc = {};
            colorToAtlasBufferUbDesc.pBuffer = pBufferASMColorToAtlasPackedQuadsUniform[gFrameIndex];
            beginUpdateResource(&colorToAtlasBufferUbDesc);
            memcpy(colorToAtlasBufferUbDesc.pMappedData, bulkToAtlasQuads, sizeof(vec4) * 3 * numTiles);
            endUpdateResource(&colorToAtlasBufferUbDesc);

            cmdBindDescriptorSet(pCurCmd, 0, pDescriptorSetColorToAtlas[0]);
            cmdBindDescriptorSet(pCurCmd, gFrameIndex, pDescriptorSetColorToAtlas[1]);

            cmdDraw(pCurCmd, numTiles * 6, 0);

            for (uint32_t i = 0; i < numTiles; ++i)
            {
                m_demQueue.Remove(tilesToUpdate[i]);
            }

            cmdBindRenderTargets(pCurCmd, NULL);

            cmdEndGpuTimestampQuery(pCurCmd, gCurrentGpuProfileToken);
        }

        tf_free(atlasToBulkQuads);
    }

    const ASMProjectionData* GetFirstRenderBatchProjection() const
    {
        if (m_renderBatch.empty())
        {
            return NULL;
        }
        return &m_renderBatch[0]->mRenderProjectionData;
    }

    ASMProjectionData** GetRenderBatchProjection(size_t& size)
    {
        if (m_renderBatch.empty())
        {
            size = 0;
            return NULL;
        }
        size = m_renderBatch.size();
        ASMProjectionData** data = (ASMProjectionData**)tf_malloc(sizeof(ASMProjectionData*) * size);
        for (size_t i = 0; i < size; i++)
        {
            data[i] = &m_renderBatch[i]->mRenderProjectionData;
        }
        return data;
    }

    void RenderIndirectModelSceneTile(const vec2& viewPortLoc, const vec2& viewPortSize, const ASMProjectionData& renderProjectionData,
                                      bool isLayer, ASMSShadowMapRenderContext& renderContext, uint32_t tileIndex, uint32_t geomSet)
    {
        UNREF_PARAM(isLayer);
        TFCmd* pCurCmd = renderContext.m_pRendererContext->m_pCmd;

        cmdSetViewport(pCurCmd, static_cast<float>(viewPortLoc.x), static_cast<float>(viewPortLoc.y), static_cast<float>(viewPortSize.x),
                       static_cast<float>(viewPortSize.y), 0.f, 1.f);

        cmdSetScissor(pCurCmd, (uint32_t)viewPortLoc.x, (uint32_t)viewPortLoc.y, (uint32_t)viewPortSize.x, (uint32_t)viewPortSize.y);

        for (int32_t i = 0; i < MESH_COUNT; ++i)
        {
            gMeshASMProjectionInfoUniformData[0][gFrameIndex].mWorldViewProjMat =
                renderProjectionData.mViewProjMat.mMatrices[MONO_CAMERA_VIEW_INDEX] * gMeshInfoData[i].mWorldMat;
            gMeshASMProjectionInfoUniformData[0][gFrameIndex].mViewID = VIEW_SHADOW + tileIndex;
        }

        TFBufferUpdateDesc updateDesc = { pBufferMeshShadowProjectionTransforms[0][tileIndex][gFrameIndex] };
        beginUpdateResource(&updateDesc);
        memcpy(updateDesc.pMappedData, &gMeshASMProjectionInfoUniformData[0][gFrameIndex],
               sizeof(gMeshASMProjectionInfoUniformData[0][gFrameIndex]));
        endUpdateResource(&updateDesc);

        TFPipeline* pPipelines[] = { pPipelineIndirectDepthPass, pPipelineIndirectAlphaDepthPass };
        COMPILE_ASSERT(TF_ARRAY_COUNT(pPipelines) == NUM_GEOMETRY_SETS);

        cmdBindIndexBuffer(pCurCmd, pVisibilityBuffer->ppFilteredIndexBuffer[VIEW_SHADOW + tileIndex], TF_INDEX_TYPE_UINT32, 0);

        cmdBindPipeline(pCurCmd, pPipelines[geomSet]);

        cmdBindDescriptorSet(pCurCmd, gFrameIndex + tileIndex * gDataBufferCount, pDescriptorSetASMDepthPass);

        uint64_t  indirectBufferByteOffset = GET_INDIRECT_DRAW_ELEM_INDEX(VIEW_SHADOW + tileIndex, geomSet, 0) * sizeof(uint32_t);
        TFBuffer* pIndirectDrawBuffer = pVisibilityBuffer->ppIndirectDrawArgBuffer[0];
        cmdExecuteIndirect(pCurCmd, TF_INDIRECT_DRAW_INDEX, 1, pIndirectDrawBuffer, indirectBufferByteOffset, NULL, 0);
    }

    bool NothingToRender() const { return m_renderBatch.empty(); }
    bool IsFadeInFinished(const ASMFrustum* pFrustum) const;

    CVectorBasedIntrusiveUnorderedPtrSet<ASMTileCacheEntry, &ASMTileCacheEntry::GetTilesPos>       m_tiles;
    CVectorBasedIntrusiveUnorderedPtrSet<ASMTileCacheEntry, &ASMTileCacheEntry::GetFreeTilesPos>   m_freeTiles;
    CVectorBasedIntrusiveUnorderedPtrSet<ASMTileCacheEntry, &ASMTileCacheEntry::GetRenderQueuePos> m_renderQueue;
    CVectorBasedIntrusiveUnorderedPtrSet<ASMTileCacheEntry, &ASMTileCacheEntry::GetReadyTilesPos>  m_readyTiles;
    CVectorBasedIntrusiveUnorderedPtrSet<ASMTileCacheEntry, &ASMTileCacheEntry::GetDemQueuePos>    m_demQueue;
    CVectorBasedIntrusiveUnorderedPtrSet<ASMTileCacheEntry, &ASMTileCacheEntry::GetRenderBatchPos> m_renderBatch;
    CVectorBasedIntrusiveUnorderedPtrSet<ASMTileCacheEntry, &ASMTileCacheEntry::GetUpdateQueuePos> m_updateQueue;

    uint32_t m_cacheHits;
    uint32_t m_tileAllocs;
    uint32_t m_numTilesRendered;
    uint32_t m_numTilesUpdated;
    uint32_t m_depthAtlasWidth;
    uint32_t m_depthAtlasHeight;
    uint32_t m_demAtlasWidth;
    uint32_t m_demAtlasHeight;

    template<class T>
    int32_t AddTileToRenderBatch(T& tilesQueue, ASMFrustum* pFrustum, int32_t maxRefinement, bool isLayer);

    template<class T>
    bool DoneRendering(T& tilesQueue, ASMFrustum* pFrustum, bool isLayer);

    bool DoneRendering(ASMFrustum* pFrustum, bool isLayer) { return DoneRendering(m_renderQueue, pFrustum, isLayer); }

    void RenderTiles(uint32_t numTiles, ASMTileCacheEntry** tiles, TFRenderTarget* workBufferDepth, TFRenderTarget* workBufferColor,
                     ASMSShadowMapRenderContext& context, bool allowDEM);

    void StartDEM(ASMTileCacheEntry* pTile, SCopyQuad& copyDEMQuad)
    {
        m_demQueue.Add(pTile, true);

        int32_t demAtlasX = (pTile->m_viewport.x - gs_ASMTileBorderTexels) >> gs_ASMDEMDownsampleLevel;
        int32_t demAtlasY = (pTile->m_viewport.y - gs_ASMTileBorderTexels) >> gs_ASMDEMDownsampleLevel;

        copyDEMQuad = SCopyQuad::Get(vec4(0.f), gs_ASMDEMTileSize, gs_ASMDEMTileSize, demAtlasX, demAtlasY, m_demAtlasWidth,
                                     m_demAtlasHeight, gs_ASMTileSize, gs_ASMTileSize, pTile->m_viewport.x - gs_ASMTileBorderTexels,
                                     pTile->m_viewport.y - gs_ASMTileBorderTexels, m_depthAtlasWidth, m_depthAtlasHeight);
    }

    friend class ASMTileCacheEntry;

    bool mDEMFirstTimeRender;
    bool mDepthFirstTimeRender;
};

void ASMTileCacheEntry::MarkReady()
{
    m_pCache->m_readyTiles.Add(this);
    m_fadeInFactor = 0.5f;
}
void ASMTileCacheEntry::MarkNotReady() { m_pCache->m_readyTiles.Remove(this); }

ASMTileCacheEntry::ASMTileCacheEntry(ASMTileCache* pCache, int32_t x, int32_t y):
    m_pOwner(NULL), m_pFrustum(NULL), m_lastFrameUsed(0), m_frustumID(0), m_isLayer(false), m_fadeInFactor(0.f), m_pCache(pCache)
{
    m_viewport.x = x + gs_ASMTileBorderTexels;
    m_viewport.w = gs_ASMBorderlessTileSize;
    m_viewport.y = y + gs_ASMTileBorderTexels;
    m_viewport.h = gs_ASMBorderlessTileSize;

    Invalidate();

    m_pCache->m_tiles.Add(this);
    m_pCache->m_freeTiles.Add(this);
}
ASMTileCacheEntry::~ASMTileCacheEntry()
{
    if (IsAllocated())
    {
        Free();
    }

    m_pCache->m_tiles.Remove(this);
    m_pCache->m_freeTiles.Remove(this);
}

class ASMQuadTree
{
public:
    ~ASMQuadTree()
    {
        Reset();
        arrfree(mRoots);
        mRoots = NULL;
    }

    void Reset()
    {
        while (arrlenu(mRoots) > 0)
            tf_delete(mRoots[arrlenu(mRoots) - 1]);
    }

    QuadTreeNode* FindRoot(const TFAABB& bbox);

    STB_DS_ARRAY(QuadTreeNode*) mRoots = NULL;
};

class QuadTreeNode
{
public:
    TFAABB mBBox;

    QuadTreeNode*      m_pParent;
    QuadTreeNode*      mChildren[4];
    ASMTileCacheEntry* m_pTile;
    ASMTileCacheEntry* m_pLayerTile;

private:
    ASMQuadTree* m_pQuadTree;
    int32_t      mRootNodesIndex;

public:
    uint32_t mLastFrameVerified;
    uint8_t  mRefinement;
    uint8_t  mNumChildren;

    QuadTreeNode(ASMQuadTree* pQuadTree, QuadTreeNode* pParent):
        m_pParent(pParent), m_pTile(NULL), m_pLayerTile(NULL), m_pQuadTree(pQuadTree), mLastFrameVerified(0), mNumChildren(0)
    {
        memset(mChildren, 0, sizeof(mChildren));

        if (m_pParent != NULL)
        {
            mRefinement = m_pParent->mRefinement + 1;
            mRootNodesIndex = -1;
        }
        else
        {
            mRefinement = 0;
            mRootNodesIndex = (int32_t)arrlen(m_pQuadTree->mRoots);
            arrpush(m_pQuadTree->mRoots, this);
        }
    }
    ~QuadTreeNode()
    {
        if (m_pTile)
        {
            m_pTile->Free();
        }

        if (m_pLayerTile)
        {
            m_pLayerTile->Free();
        }

        for (int32_t i = 0; i < 4; ++i)
        {
            if (mChildren[i])
            {
                tf_delete(mChildren[i]);
            }
        }

        if (m_pParent)
        {
            for (int32_t i = 0; i < 4; ++i)
            {
                if (m_pParent->mChildren[i] == this)
                {
                    m_pParent->mChildren[i] = NULL;
                    --m_pParent->mNumChildren;
                    break;
                }
            }
        }
        else
        {
            QuadTreeNode* pLast = m_pQuadTree->mRoots[arrlen(m_pQuadTree->mRoots) - 1];
            pLast->mRootNodesIndex = mRootNodesIndex;
            m_pQuadTree->mRoots[mRootNodesIndex] = pLast;
            (void)arrpop(m_pQuadTree->mRoots);
        }
    }

    const TFAABB GetChildBBox(int32_t childIndex)
    {
        static const vec3 quadrantOffsets[] = {
            vec3(0.0f, 0.0f, 0.0f),
            vec3(1.0f, 0.0f, 0.0f),
            vec3(1.0f, 1.0f, 0.0f),
            vec3(0.0f, 1.0f, 0.0f),
        };
        vec3        halfSize = 0.5f * (mBBox.max - mBBox.min);
        const vec3& curQuadIndxOffset = quadrantOffsets[childIndex];
        vec3        bboxMin =
            mBBox.min + vec3(curQuadIndxOffset.x * halfSize.x, curQuadIndxOffset.y * halfSize.y, curQuadIndxOffset.z * halfSize.z);

        vec3 bboxMax = bboxMin + halfSize;
        return TFAABB(bboxMin, bboxMax);
    }
    QuadTreeNode* AddChild(int32_t childIndex)
    {
        if (!mChildren[childIndex])
        {
            mChildren[childIndex] = tf_new(QuadTreeNode, m_pQuadTree, this);
            ++mNumChildren;
        }
        return mChildren[childIndex];
    }

    ASMTileCacheEntry*& GetTile() { return m_pTile; }
    ASMTileCacheEntry*& GetLayerTile() { return m_pLayerTile; }

    ASMTileCacheEntry* GetTile(bool isLayer) const { return isLayer ? m_pLayerTile : m_pTile; }
};

QuadTreeNode* ASMQuadTree::FindRoot(const TFAABB& bbox)
{
    for (size_t i = 0; i < arrlenu(mRoots); ++i)
    {
        if (!(mRoots[i]->mBBox.min != bbox.min || mRoots[i]->mBBox.max != bbox.max))
        {
            return mRoots[i];
        }
    }
    return NULL;
}

void ASMTileCacheEntry::Free()
{
    if (m_renderQueuePos.IsInserted() || m_renderBatchPos.IsInserted() || m_updateQueuePos.IsInserted() || m_demQueuePos.IsInserted())
    {
        m_pCache->m_renderQueue.Remove(this, true);
        m_pCache->m_renderBatch.Remove(this, true);
        m_pCache->m_updateQueue.Remove(this, true);
        m_pCache->m_demQueue.Remove(this, true);
        m_pCache->m_readyTiles.Remove(this, true);
        Invalidate();
    }
    else
    {
        MarkNotReady();
        m_lastFrameUsed = asmFrameCounter;
    }
    m_pCache->m_freeTiles.Add(this);
    (m_isLayer ? m_pOwner->m_pLayerTile : m_pOwner->m_pTile) = nullptr;
    m_pOwner = NULL;
    m_pFrustum = NULL;
}

static bool IsTileAcceptableForIndexing(const ASMTileCacheEntry* pTile) { return pTile && pTile->IsReady(); }
static bool IsNodeAcceptableForIndexing(const QuadTreeNode* pNode) { return IsTileAcceptableForIndexing(pNode->m_pTile); }

class ASMFrustum
{
public:
    struct Config
    {
        float   m_largestTileWorldSize;
        float   m_shadowDistance;
        int32_t m_maxRefinement;
        int32_t m_minRefinementForLayer;
        int32_t m_indexSize;
        bool    m_forceImpostors;
        float   m_refinementDistanceSq[gs_ASMMaxRefinement + 2];
        float   m_minExtentLS;
    };

    ASMFrustum(const Config& cfg, bool useMRF, bool isPreRender):
        mIsPrerender(isPreRender), m_cfg(cfg), m_lodClampTexture(NULL), m_layerIndirectionTexture(NULL)
#if defined(GFX_RESOURCE_INIT_NON_ZERO)
        ,
        mFirstTimeRender(true)
#endif
    {
        m_demMinRefinement[0] = useMRF ? (UseLayers() ? 1 : 0) : -1;
        m_demMinRefinement[1] = m_cfg.m_minRefinementForLayer;
        m_indirectionTextureSize = (1 << m_cfg.m_maxRefinement) * m_cfg.m_indexSize;
        Reset();
    }

    ~ASMFrustum()
    {
        arrfree(m_indexedNodes);
        m_indexedNodes = NULL;

        arrfree(m_quads);
        m_quads = NULL;

        arrfree(m_lodClampQuads);
        m_lodClampQuads = NULL;
    }

    bool     mIsPrerender;
    uint32_t m_ID;
    vec3     m_lightDir;
    mat4     m_lightRotMat;
    mat4     m_invLightRotMat;
    vec2     m_refinementPoint;

    vec3 m_receiverWarpVector;
    vec3 m_blockerSearchVector;
    bool m_disableWarping;

    ConvexHull2D m_frustumHull;
    ConvexHull2D m_largerHull;
    ConvexHull2D m_prevLargerHull;

    ASMQuadTree m_quadTree;

    Config  m_cfg;
    int32_t m_indirectionTextureSize;
    int32_t m_demMinRefinement[2];

    TFRenderTarget** m_indirectionTexturesMips = NULL;
    TFRenderTarget*  m_lodClampTexture;

    mat4 m_indexTexMat;
    mat4 m_indexViewMat;

    void Load(const ASMRenderTargets& renderTargets, bool isPreRender)
    {
        if (!isPreRender)
        {
            m_indirectionTexturesMips = renderTargets.m_pASMIndirectionMips;
            m_lodClampTexture = renderTargets.m_pRenderTargetASMLodClamp;
        }
        else
        {
            m_indirectionTexturesMips = renderTargets.m_pASMPrerenderIndirectionMips;
            m_lodClampTexture = renderTargets.m_pRenderTargetASMPrerenderLodClamp;
        }
    }

    bool IsLightDirDifferent(const vec3& lightDir) const { return dot(m_lightDir, lightDir) < gLightDirUpdateThreshold; }
    void Set(TFICamera* lightCamera, const vec3& lightDir)
    {
        Reset();
        m_lightDir = lightDir;

        lightCamera->moveTo(vec3(0.f));
        lightCamera->lookAt(-m_lightDir);

        m_lightRotMat = lightCamera->getViewMatrix().mMatrices[MONO_CAMERA_VIEW_INDEX];
        m_invLightRotMat = inverse(m_lightRotMat);

        static uint32_t s_IDGen = 1;
        m_ID = s_IDGen;
        s_IDGen += 2;
    }
    void Reset()
    {
        m_quadTree.Reset();
        m_ID = 0;
        m_lightDir = vec3(0.0);
        m_lightRotMat = mat4::identity();
        m_invLightRotMat = mat4::identity();

        m_indexTexMat = mat4::identity();
        m_indexViewMat = mat4::identity();

        m_refinementPoint = vec2(0.0);

        m_frustumHull.Reset();
        m_largerHull.Reset();
        m_prevLargerHull.Reset();

        m_receiverWarpVector = vec3(0.0);
        m_blockerSearchVector = vec3(0.0);
        m_disableWarping = false;

        ResetIndirectionTextureData();
    }

    void CreateTiles(ASMTileCache* pCache, const ASMProjectionData& mainProjection)
    {
        if (!IsValid() || IsLightBelowHorizon())
        {
            return;
        }

        m_refinementPoint = m_frustumHull.FindFrustumConvexHull(mainProjection, m_cfg.m_shadowDistance, m_lightRotMat);

        m_prevLargerHull = m_largerHull;

        m_largerHull.FindFrustumConvexHull(mainProjection, 1.01f * m_cfg.m_shadowDistance, m_lightRotMat);

        for (size_t i = arrlenu(m_quadTree.mRoots); i > 0; --i)
        {
            RemoveNonIntersectingNodes(m_quadTree.mRoots[i - 1]);
        }

        TFAABB hullBBox(vec3(FLT_MAX), vec3(-FLT_MAX));

        for (int32_t i = 0; i < m_frustumHull.m_size; ++i)
        {
            adjustAABB(&hullBBox, vec3(m_frustumHull.m_vertices[i].x, m_frustumHull.m_vertices[i].y, 0.f));
        }

        adjustAABB(&hullBBox, vec3(m_refinementPoint.x, m_refinementPoint.y, 0.f) + vec3(m_cfg.m_minExtentLS, m_cfg.m_minExtentLS, 0.f));
        adjustAABB(&hullBBox, vec3(m_refinementPoint.x, m_refinementPoint.y, 0.f) - vec3(m_cfg.m_minExtentLS, m_cfg.m_minExtentLS, 0.f));

        alignAABB(&hullBBox, m_cfg.m_largestTileWorldSize);

        TFAABB nodeBBox(vec3(0.f), vec3(0.f));
        for (float minY = hullBBox.min.y; minY < hullBBox.max.y; minY += m_cfg.m_largestTileWorldSize) //-V1034
        {
            for (float minX = hullBBox.min.x; minX < hullBBox.max.x; minX += m_cfg.m_largestTileWorldSize) //-V1034
            {
                nodeBBox.min.y = (minY);
                nodeBBox.min.x = (minX);
                nodeBBox.max = nodeBBox.min + vec3(m_cfg.m_largestTileWorldSize, m_cfg.m_largestTileWorldSize, 0.0f);
                if (ShouldNodeExist(nodeBBox, 0))
                {
                    QuadTreeNode* pNode = m_quadTree.FindRoot(nodeBBox);
                    if (pNode == nullptr)
                    {
                        QuadTreeNode* temp = (QuadTreeNode*)tf_malloc(sizeof(QuadTreeNode));
                        pNode = tf_placement_new<QuadTreeNode>(temp, &m_quadTree, (QuadTreeNode*)0);
                        pNode->mBBox = nodeBBox;
                    }

                    RefineNode<ASMFrustum, &ASMFrustum::RefineAgainstFrustum>(pNode, m_cfg.m_maxRefinement, *this);
                }
            }
        }

        for (QuadTreeNode** it = m_quadTree.mRoots; it != m_quadTree.mRoots + arrlen(m_quadTree.mRoots); ++it)
        {
            AllocateTiles(pCache, *it);
        }
    }

    void BuildTextures(ASMSShadowMapRenderContext context, bool isPreRender)
    {
        FindIndexedNodes();
        ResetIndirectionTextureData();
        FillIndirectionTextureData(false);
        UpdateIndirectionTexture(NULL, context, isPreRender, isPreRender);

        if (isPreRender)
        {
            FillLODClampTextureData();
            UpdateLODClampTexture(m_lodClampTexture, context);
        }

        if (UseLayers())
        {
            ResetIndirectionTextureData();
            FillIndirectionTextureData(true);
            UpdateIndirectionTexture(m_layerIndirectionTexture, context, isPreRender, isPreRender);
        }
    }
    bool    IsValid() const { return m_ID != 0; }
    bool    UseLayers() const { return m_cfg.m_minRefinementForLayer <= m_cfg.m_maxRefinement; }
    int32_t GetDEMMinRefinement(bool isLayer) const { return m_demMinRefinement[isLayer]; }
    bool    IsLightBelowHorizon() const { return false; } // IsValid() && m_lightDir.y < 0; }

    const TFRenderTarget* GetLODClampTexture() const { return m_lodClampTexture; }
    const TFRenderTarget* GetLayerIndirectionTexture() const { return m_layerIndirectionTexture; }

    const ASMProjectionData CalcCamera(const vec3& cameraPos, const TFAABB& BBoxLS, const vec2& viewportScaleFactor, bool reverseZ = true,
                                       bool customCamera = false) const
    {
        UNREF_PARAM(customCamera);
        // Use the existing m_lightRotMat instead of lookAtRH for light view matrix
        // to avoid degenerate cross product when light is vertical
        mat4 viewMat = m_lightRotMat;
        viewMat.setTranslation((viewMat * vec4(-cameraPos, 1.0f)).getXYZ());
        ASMProjectionData renderProjection;
        renderProjection.mViewMat = viewMat;

        vec3 bboxSize = calculateAABBSize(&BBoxLS);

        float hw = 0.5f * bboxSize.x * viewportScaleFactor.x;
        float hh = 0.5f * bboxSize.y * viewportScaleFactor.y;

        float farPlane = gs_ASMTileFarPlane;

        if (reverseZ)
        {
            renderProjection.mProjMat = camMatOrthographic(-hw, hw, -hh, hh, farPlane, 0);
        }
        else
        {
            renderProjection.mProjMat = camMatOrthographic(-hw, hw, -hh, hh, 0, farPlane);
        }

        renderProjection.mInvViewMat = inverse(viewMat);
        renderProjection.mInvProjMat = camMatInverse(&renderProjection.mProjMat);
        renderProjection.mViewProjMat = camMatMulMat4(&renderProjection.mProjMat, &viewMat);
        renderProjection.mInvViewProjMat = mat4MulCamMat(&renderProjection.mInvViewMat, &renderProjection.mInvProjMat);

        return renderProjection;
    }

    const ASMProjectionData CalcCamera(const TFAABB& BBoxLS, const vec3& worldCenter, const vec2& viewportScaleFactor,
                                       bool customCamera = false) const
    {
        // Defines the bounds used for ASM light camera.
        vec3 aabbMin = worldCenter - vec3(gs_ASMCameraBoundsSize);
        vec3 aabbMax = worldCenter + vec3(gs_ASMCameraBoundsSize);

        float minZ = FLT_MAX;
        for (int32_t i = 0; i < 8; ++i)
        {
            vec3 aabbCorner(i & 1 ? aabbMin.x : aabbMax.x, i & 2 ? aabbMin.y : aabbMax.y, i & 4 ? aabbMin.z : aabbMax.z);
            minZ = fminf(minZ, -dot(aabbCorner, m_lightDir));
        }

        vec3 cameraPos = (m_invLightRotMat * vec4(calculateAABBCenter(&BBoxLS), 1.f)).getXYZ() - minZ * (float3)m_lightDir;

        static const vec3 boundsN[] = {
            vec3(-1.0f, 0.0f, 0.0f), vec3(0.0f, -1.0f, 0.0f), vec3(0.0f, 0.0f, -1.0f),
            vec3(1.0f, 0.0f, 0.0f),  vec3(0.0f, 1.0f, 0.0f),  vec3(0.0f, 0.0f, 1.0f),
        };

        float boundsD[] = {
            aabbMax.x, aabbMax.y, aabbMax.z, -aabbMin.x, -aabbMin.y, -aabbMin.z,
        };

        float minF = 0;
        for (uint32_t i = 0; i < 6; ++i)
        {
            float f1 = dot(boundsN[i], cameraPos) + boundsD[i];
            float f2 = dot(boundsN[i], m_lightDir);
            if (f1 <= 0 && f2 < 0)
            {
                minF = max(minF, f1 / f2);
            }
        }

        return CalcCamera(cameraPos - minF * m_lightDir, BBoxLS, viewportScaleFactor, true, customCamera);
    }

    void UpdateWarpVector(const ASMCpuSettings& asmCpuSettings, const vec3& lightDir, bool disableWarping)
    {
        if (!IsValid())
        {
            return;
        }
        m_disableWarping |= disableWarping;
        if (m_disableWarping)
        {
            return;
        }

        if (dot(m_lightDir, lightDir) < maxWarpAngleCos)
        {
            return;
        }

        vec3 shadowDir = m_lightDir;
        vec3 dir = lightDir;

        float warpBias = 1.0f + length(dir - shadowDir);
        m_receiverWarpVector = warpBias * dir - shadowDir;

        vec3 warpDirVS = (m_indexViewMat * vec4(m_receiverWarpVector, 0.f)).getXYZ();

        float stepDistance = asmCpuSettings.mParallaxStepDistance;
        float stepBias = asmCpuSettings.mParallaxStepBias;
        m_blockerSearchVector = vec3(stepDistance * warpDirVS.x / gs_ASMDEMAtlasTextureWidth,
                                     stepDistance * warpDirVS.y / gs_ASMDEMAtlasTextureHeight, -stepBias / gs_ASMTileFarPlane);
    }

    void GetIndirectionTextureData(ASMTileCacheEntry* pTile, vec4& packedData, ivec4& dstCoord)
    {
        float invAtlasWidth = 1.f / float(gs_ASMDepthAtlasTextureWidth);
        float invAtlasHeight = 1.f / float(gs_ASMDepthAtlasTextureHeight);

        vec3 tileMin(0.f, 1.f, 0.f);
        vec3 tileMax(1.f, 0.f, 0.f);

        vec3 renderProjPos = pTile->mRenderProjectionData.mInvViewMat[3].getXYZ();

        vec3 indexMin = ProjectToTS(pTile->m_BBox.min, m_indexBBox, renderProjPos - m_indexCameraPos);
        vec3 indexMax = ProjectToTS(pTile->m_BBox.max, m_indexBBox, renderProjPos - m_indexCameraPos);

        int32_t x0 = static_cast<int32_t>(indexMin.x * static_cast<float>(m_indirectionTextureSize) + 0.25f);
        int32_t y0 = static_cast<int32_t>(indexMax.y * static_cast<float>(m_indirectionTextureSize) + 0.25f);

        int32_t x1 = static_cast<int32_t>(indexMax.x * static_cast<float>(m_indirectionTextureSize) - 0.25f);
        int32_t y1 = static_cast<int32_t>(indexMin.y * static_cast<float>(m_indirectionTextureSize) - 0.25f);

        // const int32_t mipMask = (1 << (m_cfg.m_maxRefinement - pTile->m_refinement)) - 1;

        // Compute affine transform (scale and offset) from index normalized cube to tile normalized cube.
        vec3 scale1((tileMax.x - tileMin.x) / (indexMax.x - indexMin.x), (tileMax.y - tileMin.y) / (indexMax.y - indexMin.y), 1.0f);
        vec3 offset1 = tileMin - vec3(indexMin.x * scale1.x, indexMin.y * scale1.y, indexMin.z * scale1.z);

        // Compute affine transform (scale and offset) from tile normalized cube to shadowmap atlas.
        vec3 scale2(float(pTile->m_viewport.w) * invAtlasWidth, float(pTile->m_viewport.h) * invAtlasHeight, 1.0f);
        vec3 offset2((float(pTile->m_viewport.x) + 0.5f) * invAtlasWidth, (float(pTile->m_viewport.y) + 0.5f) * invAtlasHeight, 0.0f);

        // Compute combined affine transform from index normalized cube to shadowmap atlas.
        // vec3 scale = vec3(scale1.x * scale2.x, scale1.y * scale2.y, scale1.z * scale2.z);
        vec3 offset = vec3(offset1.x * scale2.x, offset1.y * scale2.y, offset1.z * scale2.z) + offset2;

        // Assemble data for indirection texture:
        //   packedData.xyz contains transform from view frustum of index texture to view frustum of individual tile
        //   packedData.w contains packed data: integer part is refinement-dependent factor for texcoords computation,
        //      fractional part is bias for smooth tile transition unpacked via getFadeInConstant() in shader,
        //      sign indicates if the tile is a layer tile or just a regular tile.
        packedData.x = (offset.x);
        packedData.y = (offset.y);
        packedData.z = (offset.z);
        packedData.w = (float((1 << pTile->m_refinement) * gs_ASMBorderlessTileSize * m_cfg.m_indexSize));

        dstCoord = ivec4(x0, y0, x1, y1);
    }

private:
    TFRenderTarget* m_layerIndirectionTexture;

    STB_DS_ARRAY(SFillQuad) m_quads = NULL;
    uint32_t m_quadsCnt[gs_ASMMaxRefinement + 1];

    STB_DS_ARRAY(SFillQuad) m_lodClampQuads = NULL;

    STB_DS_ARRAY(QuadTreeNode*) m_indexedNodes = NULL;
    TFAABB m_indexBBox;
    vec3   m_indexCameraPos;

    static bool RefineAgainstFrustum(const TFAABB& childbbox, const QuadTreeNode* pParent, const ASMFrustum& frustum)
    {
        return frustum.ShouldNodeExist(childbbox, pParent->mRefinement + 1);
    }

    template<class T, bool (*isRefinable)(const TFAABB&, const QuadTreeNode*, const T&)>
    static void RefineNode(QuadTreeNode* pParent, int32_t maxRefinement, const T& userData)
    {
        if (pParent->mRefinement < maxRefinement)
        {
            for (int32_t i = 0; i < 4; ++i)
            {
                if (pParent->mChildren[i])
                {
                    RefineNode<T, isRefinable>(pParent->mChildren[i], maxRefinement, userData);
                }
                else
                {
                    // Here we check if any of the nodes requires new
                    // child node or not
                    TFAABB childBBox = pParent->GetChildBBox(i);
                    if (isRefinable(childBBox, pParent, userData))
                    {
                        QuadTreeNode* pNode = pParent->AddChild(i);
                        pNode->mBBox = childBBox;
                        RefineNode<T, isRefinable>(pNode, maxRefinement, userData);
                    }
                }
            }
        }
    }

    void AllocateTiles(ASMTileCache* pCache, QuadTreeNode* pNode)
    {
        for (int32_t i = 0; i < 4; ++i)
        {
            if (pNode->mChildren[i])
            {
                AllocateTiles(pCache, pNode->mChildren[i]);
            }
        }

        if (!pNode->m_pTile)
        {
            pCache->Allocate<&QuadTreeNode::GetTile, false>(pNode, this);
        }
    }
    void RemoveNonIntersectingNodes(QuadTreeNode* pNode)
    {
        for (int32_t i = 0; i < 4; ++i)
        {
            if (pNode->mChildren[i])
            {
                RemoveNonIntersectingNodes(pNode->mChildren[i]);
            }
        }

        if (pNode->mLastFrameVerified != asmFrameCounter)
        {
            pNode->mLastFrameVerified = asmFrameCounter;

            if (ShouldNodeExist(pNode->mBBox, pNode->mRefinement))
            {
                if (pNode->m_pParent)
                {
                    pNode->m_pParent->mLastFrameVerified = asmFrameCounter;
                }
                return;
            }
            tf_delete(pNode);
        }
    }

    static STB_DS_ARRAY(QuadTreeNode*) SortNodes(const vec2& refinementPoint, const vec2& sortRegionMaxSize, float tileSize,
                                                 uint32_t nodeCount, QuadTreeNode** nodes, TFAABB& sortedBBox)
    {
        struct SortStruct
        {
            QuadTreeNode* m_pNode;
            float         mKey;
        };

        SortStruct* nodesToSort = (SortStruct*)tf_malloc(sizeof(*nodesToSort) * nodeCount);

        vec2  distMax = sortRegionMaxSize + vec2(tileSize, tileSize);
        float distMaxSq = dot(distMax, distMax);

        uint32_t numNodesToSort = 0;

        for (uint32_t i = 0; i < nodeCount; ++i)
        {
            QuadTreeNode* pNode = nodes[i];
            if (IsNodeAcceptableForIndexing(pNode))
            {
                const TFAABB& bbox = pNode->mBBox;
                vec3          bboxCenter = calculateAABBCenter(&bbox);
                vec3          bboxSize = calculateAABBSize(&bbox);
                float         dx = fmaxf(fabsf(refinementPoint.x - bboxCenter.x) - bboxSize.x * 0.5f, 0.0f);
                float         dy = fmaxf(fabsf(refinementPoint.y - bboxCenter.y) - bboxSize.y * 0.5f, 0.0f);

                float distSq = dx * dx + dy * dy;
                if (distSq < distMaxSq)
                {
                    SortStruct& ss = nodesToSort[numNodesToSort++];
                    ss.mKey = fabsf(bbox.min.x - refinementPoint.x);
                    ss.mKey = fmaxf(fabsf(bbox.min.y - refinementPoint.y), ss.mKey);
                    ss.mKey = fmaxf(fabsf(bbox.max.x - refinementPoint.x), ss.mKey);
                    ss.mKey = fmaxf(fabsf(bbox.max.y - refinementPoint.y), ss.mKey);
                    ss.m_pNode = pNode;
                }
            }
        }

        std::qsort(nodesToSort, numNodesToSort, sizeof(SortStruct),
                   [](const void* a, const void* b)
                   {
                       const SortStruct* left = (SortStruct*)(a);
                       const SortStruct* right = (SortStruct*)(b);

                       if (left->mKey < right->mKey)
                       {
                           return -1;
                       }
                       else if (left->mKey > right->mKey)
                       {
                           return 1;
                       }

                       return 0;
                   });

        sortedBBox = TFAABB(vec3(refinementPoint.x, refinementPoint.y, 0.f), vec3(refinementPoint.x, refinementPoint.y, 0.f));
        alignAABB(&sortedBBox, tileSize);

        STB_DS_ARRAY(QuadTreeNode*) sortedNodes = NULL;

        for (uint32_t i = 0; i < numNodesToSort; ++i)
        {
            SortStruct&   ss = nodesToSort[i];
            const TFAABB& nodeBBox = ss.m_pNode->mBBox;
            vec3          testMin(min(sortedBBox.min.x, nodeBBox.min.x), min(sortedBBox.min.y, nodeBBox.min.y), 0.f);
            vec3          testMax(max(sortedBBox.max.x, nodeBBox.max.x), max(sortedBBox.max.y, nodeBBox.max.y), 0.f);

            if ((testMax.x - testMin.x) > sortRegionMaxSize.x || (testMax.y - testMin.y) > sortRegionMaxSize.y)
            {
                if (ss.mKey > distMax.x)
                {
                    break;
                }
            }
            else
            {
                sortedBBox = TFAABB(testMin, testMax);
                arrpush(sortedNodes, ss.m_pNode);
            }
        }

        tf_free(nodesToSort);
        nodesToSort = NULL;

        return sortedNodes;
    }

    void FindIndexedNodes()
    {
        if (!IsValid())
        {
            return;
        }

        float sortRegionSizeMax = static_cast<float>(m_cfg.m_indexSize) * m_cfg.m_largestTileWorldSize;

        arrfree(m_indexedNodes);
        m_indexedNodes = SortNodes(m_refinementPoint, vec2(sortRegionSizeMax), m_cfg.m_largestTileWorldSize,
                                   (uint32_t)arrlen(m_quadTree.mRoots), m_quadTree.mRoots, m_indexBBox);

        m_indexBBox = TFAABB(m_indexBBox.min, m_indexBBox.min + vec3(sortRegionSizeMax, sortRegionSizeMax, 0.f));

        size_t nodeCount = arrlenu(m_indexedNodes);
        if (nodeCount)
        {
            float offset = -FLT_MAX;

            for (uint32_t i = 0; i < nodeCount; ++i)
            {
                QuadTreeNode* indexedNode = m_indexedNodes[i];
                offset = fmaxf(offset, dot(m_lightDir, indexedNode->m_pTile->mRenderProjectionData.mInvViewMat[3].getXYZ()));
            }
            m_indexCameraPos = (m_invLightRotMat * vec4(calculateAABBCenter(&m_indexBBox), 1.f)).getXYZ() + offset * (float3)m_lightDir;

            ASMProjectionData renderProjection = CalcCamera(m_indexCameraPos, m_indexBBox, vec2(1.0f), true);
            m_indexViewMat = renderProjection.mViewMat;

            static const mat4 screenToTexCoordMatrix = mat4::translation(vec3(0.5f, 0.5f, 0.f)) * mat4::scale(vec3(0.5f, -0.5f, 1.f));
            m_indexTexMat = screenToTexCoordMatrix * renderProjection.mViewProjMat.mMatrices[MONO_CAMERA_VIEW_INDEX];
        }
    }
    void FillIndirectionTextureData(bool processLayers)
    {
        if (!IsValid())
        {
            return;
        }

        size_t nodeCount = arrlenu(m_indexedNodes);
        if (nodeCount == 0)
        {
            return;
        }

        size_t   numIndexedNodes = nodeCount;
        uint32_t i = 0;

        for (int32_t z = m_cfg.m_maxRefinement; z >= 0; --z)
        {
            size_t numNodes = arrlenu(m_indexedNodes);
            for (; i < numNodes; ++i)
            {
                QuadTreeNode*      pNode = m_indexedNodes[i];
                ASMTileCacheEntry* pTile = pNode->m_pTile;
                if (processLayers)
                {
                    if (!IsTileAcceptableForIndexing(pNode->m_pLayerTile))
                    {
                    }
                    else
                    {
                        pTile = pNode->m_pLayerTile;
                    }
                }

                vec4  packedData(0.0f);
                ivec4 destCoord;
                GetIndirectionTextureData(pTile, packedData, destCoord);

                packedData.w = (packedData.w + pTile->m_fadeInFactor);

                arrpush(m_quads, SFillQuad::Get(packedData, destCoord.z - destCoord.x + 1, destCoord.w - destCoord.y + 1, destCoord.x,
                                                destCoord.y, m_indirectionTextureSize, m_indirectionTextureSize));

                ++m_quadsCnt[z];

                for (int32_t j = 0; j < 4; ++j)
                {
                    QuadTreeNode* pChild = pNode->mChildren[j];
                    if (pChild && IsNodeAcceptableForIndexing(pChild))
                    {
                        arrpush(m_indexedNodes, pChild);
                    }
                }
            }
        }
        arrsetlen(m_indexedNodes, numIndexedNodes);
    }
    void ResetIndirectionTextureData()
    {
        memset(m_quadsCnt, 0, sizeof(m_quadsCnt));
        arrfree(m_quads);
        m_quads = NULL;

        arrsetlen(m_lodClampQuads, 1);
        m_lodClampQuads[0] = SFillQuad::Get(vec4(1.f), m_indirectionTextureSize, m_indirectionTextureSize, 0, 0, m_indirectionTextureSize,
                                            m_indirectionTextureSize);
    }

    const vec3 ProjectToTS(const vec3& pointLS, const TFAABB& bboxLS, const vec3& cameraOffset)
    {
        vec3 bboxLSSize = calculateAABBSize(&bboxLS);
        return vec3((pointLS.x - bboxLS.min.x) / bboxLSSize.x, 1.0f - (pointLS.y - bboxLS.min.y) / bboxLSSize.y,
                    -dot(m_lightDir, (m_invLightRotMat * vec4(pointLS, 1.0)).getXYZ() + (float3)cameraOffset) / gs_ASMTileFarPlane);
    }

    bool ShouldNodeExist(const TFAABB& bbox, uint8_t refinement) const
    {
        return Get3DRefinementDistanceSq(bbox, m_refinementPoint) < fabsf(m_cfg.m_refinementDistanceSq[refinement])
                   ? (m_cfg.m_refinementDistanceSq[refinement] < 0 || m_frustumHull.Intersects(bbox))
                   : false;
    }

    void FillLODClampTextureData()
    {
        size_t nodeCount = arrlenu(m_indexedNodes);
        if (!IsValid() || nodeCount == 0)
        {
            return;
        }

        size_t   numIndexedNodes = nodeCount;
        uint32_t i = 0;

        for (int32_t z = m_cfg.m_maxRefinement; z >= 0; --z)
        {
            float clampValue = static_cast<float>(z) / static_cast<float>(gs_ASMMaxRefinement);

            size_t numNodes = arrlenu(m_indexedNodes);

            for (; i < numNodes; ++i)
            {
                QuadTreeNode*      pNode = m_indexedNodes[i];
                ASMTileCacheEntry* pTile = pNode->m_pTile;

                if (z < m_cfg.m_maxRefinement)
                {
                    vec4  packedData;
                    ivec4 destCoord;
                    GetIndirectionTextureData(pTile, packedData, destCoord);

                    arrpush(m_lodClampQuads, SFillQuad::Get(vec4(clampValue), destCoord.z - destCoord.x + 1, destCoord.w - destCoord.y + 1,
                                                            destCoord.x, destCoord.y, m_indirectionTextureSize, m_indirectionTextureSize));
                }

                for (int32_t j = 0; j < 4; ++j)
                {
                    QuadTreeNode* pChild = pNode->mChildren[j];
                    if (pChild && pChild->m_pTile)
                    {
                        arrpush(m_indexedNodes, pChild);
                    }
                }
            }
        }
        arrsetlen(m_indexedNodes, numIndexedNodes);
    }
    void UpdateIndirectionTexture(TFRenderTarget* indirectionTexture, ASMSShadowMapRenderContext context, bool disableHierarchy,
                                  bool isPreRender)
    {
        UNREF_PARAM(indirectionTexture);
        UNREF_PARAM(isPreRender);
        ASMRendererContext* curRendererContext = context.m_pRendererContext;

        IndirectionRenderData* finalIndirectionRenderData =
            disableHierarchy ? (&gASMTickData.mPrerenderIndirectionRenderData) : (&gASMTickData.mIndirectionRenderData);

        IndirectionRenderData& indirectionRenderData = *finalIndirectionRenderData;

        SFillQuad clearQuad = SFillQuad::Get(vec4(0.f), m_indirectionTextureSize, m_indirectionTextureSize, 0, 0, m_indirectionTextureSize,
                                             m_indirectionTextureSize);

        PackedAtlasQuads   packedClearAtlasQuads = { { clearQuad.m_pos, clearQuad.m_misc } };
        TFBufferUpdateDesc clearUpdateUbDesc = { indirectionRenderData.pBufferASMClearIndirectionQuadsUniform };
        beginUpdateResource(&clearUpdateUbDesc);
        memcpy(clearUpdateUbDesc.pMappedData, &packedClearAtlasQuads, sizeof(packedClearAtlasQuads));
        endUpdateResource(&clearUpdateUbDesc);

        uint32_t firstQuad = 0;
        uint32_t numQuads = 0;

        TFBindRenderTargetsDesc bindRenderTargets = {};
        bindRenderTargets.mRenderTargetCount = 1;
#if defined(GFX_RESOURCE_INIT_NON_ZERO)
        if (mFirstTimeRender)
        {
            bindRenderTargets.mRenderTargets[0].mLoadAction = TF_LOAD_ACTION_CLEAR;
            mFirstTimeRender = false;
        }
#endif

        for (int32_t mip = m_cfg.m_maxRefinement; mip >= 0; --mip)
        {
            numQuads += m_quadsCnt[mip];

            bindRenderTargets.mRenderTargets[0].pRenderTarget = m_indirectionTexturesMips[mip];
            cmdBindRenderTargets(curRendererContext->m_pCmd, &bindRenderTargets);
            cmdSetViewport(curRendererContext->m_pCmd, 0.0f, 0.0f, (float)m_indirectionTexturesMips[mip]->mWidth,
                           (float)m_indirectionTexturesMips[mip]->mHeight, 0.0f, 1.0f);
            cmdSetScissor(curRendererContext->m_pCmd, 0, 0, m_indirectionTexturesMips[mip]->mWidth,
                          m_indirectionTexturesMips[mip]->mHeight);

            //------------------Clear ASM indirection quad
            cmdBindPipeline(curRendererContext->m_pCmd, indirectionRenderData.m_pGraphicsPipeline);
            cmdBindDescriptorSet(curRendererContext->m_pCmd, gFrameIndex, pDescriptorSetAsmClearIndirection);
            cmdDraw(curRendererContext->m_pCmd, 6, 0);

            if (numQuads > 0)
            {
                TFBufferUpdateDesc updateIndirectionUBDesc = { indirectionRenderData.pBufferASMPackedIndirectionQuadsUniform[mip] };
                beginUpdateResource(&updateIndirectionUBDesc);
                memcpy(updateIndirectionUBDesc.pMappedData, &m_quads[firstQuad], sizeof(vec4) * 2 * numQuads);
                endUpdateResource(&updateIndirectionUBDesc);

                cmdBindDescriptorSet(curRendererContext->m_pCmd, gFrameIndex * (gs_ASMMaxRefinement + 1) + mip,
                                     pDescriptorSetASMFillIndirection[disableHierarchy ? 1 : 0]);
                cmdDraw(curRendererContext->m_pCmd, 6 * numQuads, 0);
            }

            if (disableHierarchy)
            {
                firstQuad += numQuads;
                numQuads = 0;
            }

            cmdBindRenderTargets(curRendererContext->m_pCmd, NULL);
        }
    }
    void UpdateLODClampTexture(TFRenderTarget* lodClampTexture, ASMSShadowMapRenderContext context)
    {
        ASMRendererContext* rendererContext = context.m_pRendererContext;
        TFCmd*              pCurCmd = rendererContext->m_pCmd;

        TFRenderTargetBarrier lodBarrier[] = { { lodClampTexture, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET } };

        cmdResourceBarrier(pCurCmd, 0, NULL, 0, NULL, 1, lodBarrier);

        TFBindRenderTargetsDesc bindRenderTargets = {};
        bindRenderTargets.mRenderTargetCount = 1;
        bindRenderTargets.mRenderTargets[0] = { lodClampTexture, TF_LOAD_ACTION_DONTCARE };
        cmdBindRenderTargets(pCurCmd, &bindRenderTargets);
        cmdSetViewport(pCurCmd, 0.0f, 0.0f, (float)lodClampTexture->mWidth, (float)lodClampTexture->mHeight, 0.0f, 1.0f);
        cmdSetScissor(pCurCmd, 0, 0, lodClampTexture->mWidth, lodClampTexture->mHeight);

        TFBufferUpdateDesc updateBufferDesc = { pBufferASMLodClampPackedQuadsUniform[gFrameIndex] };
        beginUpdateResource(&updateBufferDesc);
        memcpy(updateBufferDesc.pMappedData, &m_lodClampQuads[0], sizeof(SFillQuad) * arrlenu(m_lodClampQuads));
        endUpdateResource(&updateBufferDesc);

        cmdBindPipeline(pCurCmd, pPipelineASMFillLodClamp);
        cmdBindDescriptorSet(pCurCmd, gFrameIndex, pDescriptorSetAsmLodClamp);
        cmdDraw(pCurCmd, (uint32_t)arrlen(m_lodClampQuads) * 6, 0);

        cmdBindRenderTargets(pCurCmd, NULL);
    }

private:
#if defined(GFX_RESOURCE_INIT_NON_ZERO)
    bool mFirstTimeRender;
#endif
};

void ASMTileCache::RenderTiles(uint32_t numTiles, ASMTileCacheEntry** tiles, TFRenderTarget* workBufferDepth,
                               TFRenderTarget* workBufferColor, ASMSShadowMapRenderContext& context, bool allowDEM)
{
    UNREF_PARAM(workBufferColor);
    if (!numTiles)
        return;

    ASMRendererContext* curRendererContext = context.m_pRendererContext;

    TFCmd* pCurCmd = curRendererContext->m_pCmd;
    // TFRenderer* pRenderer = curRendererContext->m_pRenderer;

    uint32_t workBufferWidth = workBufferDepth->mWidth;
    uint32_t workBufferHeight = workBufferDepth->mHeight;
    uint32_t numTilesW = workBufferWidth / gs_ASMTileSize;
    uint32_t numTilesH = workBufferHeight / gs_ASMTileSize;
    uint32_t maxTilesPerPass = numTilesW * numTilesH;

    // Basically this code changes pixel center from DX10 to DX9 (DX9 pixel center is integer while DX10 is (0.5, 0.5)
    mat4 pixelCenterOffsetMatrix =
        mat4::translation(vec3(1.f / static_cast<float>(workBufferWidth), -1.f / static_cast<float>(workBufferHeight), 0.f));

    SCopyQuad* copyDepthQuads = (SCopyQuad*)tf_malloc(sizeof(SCopyQuad) * (maxTilesPerPass + numTiles));
    SCopyQuad* copyDEMQuads = copyDepthQuads + maxTilesPerPass;

    // float invAtlasWidth = 1.0f / float(m_depthAtlasWidth);
    // float invAtlasHeight = 1.0f / float(m_depthAtlasHeight);

    uint32_t numCopyDEMQuads = 0;
    for (uint32_t i = 0; i < numTiles;)
    {
        uint32_t tilesToRender = min(maxTilesPerPass, numTiles - i);

        if (tilesToRender > 0)
        {
            TFRenderTargetBarrier textureBarriers[] = { { workBufferDepth, TF_RESOURCE_STATE_SHADER_RESOURCE,
                                                          TF_RESOURCE_STATE_DEPTH_WRITE } };
            cmdResourceBarrier(pCurCmd, 0, NULL, 0, NULL, 1, textureBarriers);

            TFBindRenderTargetsDesc bindRenderTargets = {};
            bindRenderTargets.mDepthStencil = { workBufferDepth, TF_LOAD_ACTION_CLEAR };
            cmdBindRenderTargets(pCurCmd, &bindRenderTargets);
            cmdSetViewport(pCurCmd, 0.0f, 0.0f, (float)workBufferDepth->mWidth, (float)workBufferDepth->mHeight, 0.0f, 1.0f);
            cmdSetScissor(pCurCmd, 0, 0, workBufferDepth->mWidth, workBufferDepth->mHeight);
        }

        for (uint32_t geomSet = 0; geomSet < NUM_GEOMETRY_SETS; geomSet++)
        {
            for (uint32_t j = 0; j < tilesToRender; ++j)
            {
                ASMTileCacheEntry* pTile = tiles[i + j];

                //_ASSERT( !pTile->m_isLayer || pTile->m_pOwner->m_pTile->IsReady() );

                ASMProjectionData renderProjection;
                renderProjection.mViewMat = pTile->mRenderProjectionData.mViewMat;
                renderProjection.mProjMat = mat4MulCamMat(&pixelCenterOffsetMatrix, &pTile->mRenderProjectionData.mProjMat);

                renderProjection.mViewProjMat = camMatMulMat4(&renderProjection.mProjMat, &renderProjection.mViewMat);

                if (pTile->m_isLayer)
                {
                }
                else
                {
                    vec2 viewPortLoc(static_cast<float>((j % numTilesW) * gs_ASMTileSize),
                                     static_cast<float>((j / numTilesW) * gs_ASMTileSize));
                    vec2 viewPortSize(gs_ASMTileSize);
                    RenderIndirectModelSceneTile(viewPortLoc, viewPortSize, renderProjection, false, context, j, geomSet);
                }
            }
        }

        for (uint32_t j = 0; j < tilesToRender; j++)
        {
            ASMTileCacheEntry* pTile = tiles[i + j];

            vec2 viewPortLoc(static_cast<float>((j % numTilesW) * gs_ASMTileSize), static_cast<float>((j / numTilesW) * gs_ASMTileSize));

            copyDepthQuads[j] = SCopyQuad::Get(
                vec4(0, 0, 0, 0), gs_ASMTileSize, gs_ASMTileSize, pTile->m_viewport.x - gs_ASMTileBorderTexels,
                pTile->m_viewport.y - gs_ASMTileBorderTexels, m_depthAtlasWidth, m_depthAtlasHeight, gs_ASMTileSize, gs_ASMTileSize,
                static_cast<uint32_t>(viewPortLoc.x), static_cast<uint32_t>(viewPortLoc.y), workBufferWidth, workBufferHeight);

            bool generateDEM = pTile->m_refinement <= pTile->m_pFrustum->GetDEMMinRefinement(pTile->m_isLayer);
            if (generateDEM && (allowDEM || pTile->IsReady()))
            {
                StartDEM(pTile, copyDEMQuads[numCopyDEMQuads++]);
            }
        }

        cmdBindRenderTargets(curRendererContext->m_pCmd, NULL);

        TFRenderTargetBarrier copyDepthBarrier[] = {
            { pRenderTargetASMDepthAtlas, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET },
            { workBufferDepth, TF_RESOURCE_STATE_DEPTH_WRITE, TF_RESOURCE_STATE_SHADER_RESOURCE }
        };

        cmdResourceBarrier(pCurCmd, 0, NULL, 0, NULL, 2, copyDepthBarrier);

        if (tilesToRender > 0)
        {
            cmdBeginGpuTimestampQuery(pCurCmd, gCurrentGpuProfileToken, "Copying ASM Depth Quads");

            TFBufferUpdateDesc updateUbDesc = { pBufferASMAtlasQuadsUniform[gFrameIndex] };
            beginUpdateResource(&updateUbDesc);
            memcpy(updateUbDesc.pMappedData, &copyDepthQuads[0], sizeof(SCopyQuad) * tilesToRender);
            endUpdateResource(&updateUbDesc);

            TFBindRenderTargetsDesc copyDepthQuadBindDesc = {};
            copyDepthQuadBindDesc.mRenderTargetCount = 1;
            copyDepthQuadBindDesc.mRenderTargets[0] = { pRenderTargetASMDepthAtlas, TF_LOAD_ACTION_LOAD };
#if defined(GFX_RESOURCE_INIT_NON_ZERO)
            if (mDepthFirstTimeRender)
            {
                copyDepthQuadBindDesc.mRenderTargets[0].mLoadAction = TF_LOAD_ACTION_CLEAR;
                mDepthFirstTimeRender = false;
            }
#endif
            cmdBindRenderTargets(pCurCmd, &copyDepthQuadBindDesc);
            cmdSetViewport(pCurCmd, 0.0f, 0.0f, (float)pRenderTargetASMDepthAtlas->mWidth, (float)pRenderTargetASMDepthAtlas->mHeight, 0.0f,
                           1.0f);
            cmdSetScissor(pCurCmd, 0, 0, pRenderTargetASMDepthAtlas->mWidth, pRenderTargetASMDepthAtlas->mHeight);

            cmdBindPipeline(pCurCmd, pPipelineASMCopyDepthQuadPass);
            cmdBindDescriptorSet(pCurCmd, 0, pDescriptorSetAsmAtlasQuads[0]);
            cmdBindDescriptorSet(pCurCmd, gFrameIndex, pDescriptorSetAsmAtlasQuads[1]);
            cmdDraw(pCurCmd, 6 * tilesToRender, 0);

            cmdBindRenderTargets(curRendererContext->m_pCmd, NULL);
            cmdEndGpuTimestampQuery(pCurCmd, gCurrentGpuProfileToken);
        }

        i += tilesToRender;

        TFRenderTargetBarrier asmCopyDEMBarrier[] = { { pRenderTargetASMDepthAtlas, TF_RESOURCE_STATE_RENDER_TARGET,
                                                        TF_RESOURCE_STATE_SHADER_RESOURCE } };
        cmdResourceBarrier(pCurCmd, 0, NULL, 0, NULL, 1, asmCopyDEMBarrier);
    }

    if (numCopyDEMQuads > 0)
    {
        TFBindRenderTargetsDesc copyDEMQuadBindDesc = {};
        copyDEMQuadBindDesc.mRenderTargetCount = 1;
        copyDEMQuadBindDesc.mRenderTargets[0] = { pRenderTargetASMDEMAtlas, TF_LOAD_ACTION_LOAD };
        cmdBindRenderTargets(pCurCmd, &copyDEMQuadBindDesc);
        cmdSetViewport(pCurCmd, 0.0f, 0.0f, (float)pRenderTargetASMDEMAtlas->mWidth, (float)pRenderTargetASMDEMAtlas->mHeight, 0.0f, 1.0f);
        cmdSetScissor(pCurCmd, 0, 0, pRenderTargetASMDEMAtlas->mWidth, pRenderTargetASMDEMAtlas->mHeight);

        TFBufferUpdateDesc copyDEMQuadUpdateUbDesc = { pBufferASMCopyDEMPackedQuadsUniform[gFrameIndex] };
        beginUpdateResource(&copyDEMQuadUpdateUbDesc);
        memcpy(copyDEMQuadUpdateUbDesc.pMappedData, &copyDEMQuads[0], sizeof(SCopyQuad) * numCopyDEMQuads);
        endUpdateResource(&copyDEMQuadUpdateUbDesc);

        cmdBindPipeline(pCurCmd, pPipelineASMCopyDEM);
        cmdBindDescriptorSet(pCurCmd, 0, pDescriptorSetAsmCopyDEM[0]);
        cmdBindDescriptorSet(pCurCmd, gFrameIndex, pDescriptorSetAsmCopyDEM[1]);
        cmdDraw(pCurCmd, numCopyDEMQuads * 6, 0);

        cmdBindRenderTargets(curRendererContext->m_pCmd, NULL);
    }

    tf_free(copyDepthQuads);
}

void ASMTileCacheEntry::PrepareRender(ASMSShadowMapPrepareRenderContext context)
{
    mRenderProjectionData = m_pFrustum->CalcCamera(m_BBox, *context.m_worldCenter,
                                                   vec2((static_cast<float>(gs_ASMTileSize) / static_cast<float>(m_viewport.w))));
}

class ASM
{
public:
    ASMTileCache* m_cache;
    ASMFrustum*   m_longRangeShadows;
    ASMFrustum*   m_longRangePreRender;

public:
    friend class ASMTileCacheEntry;
    friend class ASMTileCache;
    friend class ASMFrustum;

    friend class ASMFrustum;
    friend class ASMTileCacheEntry;

public:
    ASM(): m_preRenderDone(false)
    {
        m_preRenderStarted = false;
        m_cache = tf_new(ASMTileCache);
        static const ASMFrustum::Config longRangeCfg = {
            gs_ASMLargestTileWorldSize,
            gs_ASMDistanceMax,
            gs_ASMMaxRefinement,
            INT_MAX,
            gsASMIndexSize,
            true,
            { SQR(gs_ASMDistanceMax), SQR(120.0f), SQR(60.0f), SQR(30.0f),
              SQR(10.0f) } // The threshold distances for each refinement level.
        };
        m_longRangeShadows = tf_new(ASMFrustum, longRangeCfg, true, false);
        m_longRangePreRender = tf_new(ASMFrustum, longRangeCfg, true, true);
        Reset();
    }
    ~ASM()
    {
        tf_delete(m_cache);
        tf_delete(m_longRangeShadows);
        tf_delete(m_longRangePreRender);
    }

    void Load(const ASMRenderTargets& renderTargets)
    {
        m_longRangePreRender->Load(renderTargets, true);
        m_longRangeShadows->Load(renderTargets, false);
    }

    bool PrepareRender(const ASMProjectionData& mainViewProjection, bool disablePreRender)
    {
        m_longRangeShadows->CreateTiles(m_cache, mainViewProjection);
        m_longRangePreRender->CreateTiles(m_cache, mainViewProjection);

        uint32_t maxTilesPerFrame = gASMMaxTilesPerPass;
        uint32_t tilesToRender = 0;

        if (m_cache->NothingToRender())
        {
            for (uint32_t i = 0; i <= gs_ASMMaxRefinement && tilesToRender < maxTilesPerFrame; ++i)
            {
                while (tilesToRender < maxTilesPerFrame)
                {
                    bool success = m_cache->AddTileFromRenderQueueToRenderBatch(m_longRangeShadows, i, false) != -1;
                    if (!success)
                    {
                        break;
                    }
                    tilesToRender++;
                }

                while (tilesToRender < maxTilesPerFrame && m_longRangePreRender->IsValid() && !disablePreRender)
                {
                    bool success = m_cache->AddTileFromRenderQueueToRenderBatch(m_longRangePreRender, i, false) != -1;
                    if (!success)
                    {
                        break;
                    }
                    tilesToRender++;
                    m_preRenderStarted = true;
                }
            }

            if (m_longRangePreRender->IsValid() && m_preRenderStarted && m_cache->DoneRendering(m_longRangePreRender, false))
            {
                m_preRenderDone = true;
            }
        }

        vec3                              mainViewCameraPosition = mainViewProjection.mInvViewMat[3].getXYZ();
        ASMSShadowMapPrepareRenderContext context = { &mainViewCameraPosition };
        return m_cache->PrepareRenderTilesBatch(context);
    }

    void Render(TFRenderTarget* pDepthTarget, TFRenderTarget* pRenderTargetColor, ASMRendererContext& renderContext,
                ASMProjectionData* projectionRender)
    {
        ASMSShadowMapRenderContext context = { &renderContext, projectionRender };

        if (!m_cache->NothingToRender())
        {
            m_cache->RenderTilesBatch(pDepthTarget, pRenderTargetColor, context);
        }

        m_cache->CreateDEM(pRenderTargetColor, context, false);
        m_cache->CreateDEM(pRenderTargetColor, context, true);

        m_longRangeShadows->BuildTextures(context, false);

        if (m_longRangePreRender->IsValid())
        {
            m_longRangePreRender->BuildTextures(context, true);
        }
    }

    void Reset()
    {
        m_longRangeShadows->Reset();
        m_longRangePreRender->Reset();
        m_preRenderDone = false;
        m_preRenderStarted = false;
    }

    void Tick(const ASMCpuSettings& asmCpuSettings, TFICamera* lightCamera, const vec3& lightDir, const vec3& halfwayLightDir,
              uint32_t currentTime, uint32_t dt, bool disableWarping, bool forceUpdate, uint32_t updateDeltaTime)
    {
        UNREF_PARAM(currentTime);
        UNREF_PARAM(updateDeltaTime);
        // mTickData = tickData;

        vec3 sunDir = lightDir;
        gLightDirUpdateThreshold = (float)cos(gLightDirUpdateAngle * PI / 180.0f);
        // vec3 sunDir = GetLightDirection(currentTime);

        float deltaTime = float(dt) * ASM_SUN_SPEED;

        bool isUpdated = false;
        if (!m_longRangeShadows->IsValid())
        {
            m_longRangeShadows->Set(lightCamera, sunDir);
            isUpdated = true;
        }
        else if (forceUpdate)
        {
            m_longRangePreRender->Reset();

            m_longRangePreRender->Set(lightCamera, sunDir);
            m_preRenderDone = false;
            m_preRenderStarted = false;

            isUpdated = true;
        }
        else if (!m_longRangePreRender->IsValid())
        {
            vec3 nextSunDir = halfwayLightDir;
            isUpdated = m_longRangeShadows->IsLightDirDifferent(nextSunDir);

            if (isUpdated)
            {
                m_longRangePreRender->Set(lightCamera, nextSunDir);
                m_preRenderDone = false;
                m_preRenderStarted = false;
            }
        }
        m_longRangeShadows->UpdateWarpVector(asmCpuSettings, sunDir, disableWarping);
        m_longRangePreRender->UpdateWarpVector(asmCpuSettings, sunDir, disableWarping);

        if (m_preRenderDone)
        {
            m_cache->Tick(deltaTime);
        }

        if (m_longRangePreRender->IsValid() && m_preRenderDone && m_cache->IsFadeInFinished(m_longRangePreRender))
        {
            ASMFrustum* asmf = m_longRangeShadows;
            m_longRangeShadows = m_longRangePreRender;
            m_longRangePreRender = asmf;

            preRenderSwap = !preRenderSwap;

            m_longRangePreRender->Reset();
            m_preRenderDone = false;
            m_preRenderStarted = false;
        }

        ++asmFrameCounter;
    }

    bool NothingToRender() const { return m_cache->NothingToRender(); }

    bool PreRenderAvailable() const { return m_longRangePreRender->IsValid(); }

    bool PreRenderDone() const { return m_preRenderDone; }

protected:
    bool m_preRenderDone;
    bool m_preRenderStarted;
};

bool ASMTileCache::IsFadeInFinished(const ASMFrustum* pFrustum) const
{
    for (uint32_t i = 0; i < m_readyTiles.size(); ++i)
    {
        ASMTileCacheEntry* pTile = m_readyTiles[i];
        if (pTile->m_frustumID == pFrustum->m_ID && pTile->m_fadeInFactor > 0)
        {
            return false;
        }
    }
    return true;
}

template<class T>
int32_t ASMTileCache::AddTileToRenderBatch(T& tilesQueue, ASMFrustum* pFrustum, int32_t maxRefinement, bool isLayer)
{
    if (!pFrustum->IsValid())
    {
        return -1;
    }

    ASMTileCacheEntry* pTileToRender = nullptr;
    float              minDistSq = FLT_MAX;
    uint8_t            refinement = UCHAR_MAX;
    for (uint32_t i = 0; i < tilesQueue.size(); ++i)
    {
        ASMTileCacheEntry* pTile = tilesQueue[i];
        if (pFrustum == pTile->m_pFrustum && isLayer == pTile->m_isLayer && (!pTile->m_isLayer || pTile->m_pOwner->m_pTile->IsReady()))
        {
            float distSq = GetRefinementDistanceSq(pTile->m_BBox, pFrustum->m_refinementPoint);
            if (pTile->m_refinement < refinement || (refinement == pTile->m_refinement && distSq < minDistSq))
            {
                refinement = pTile->m_refinement;
                minDistSq = distSq;
                pTileToRender = pTile;
            }
        }
    }

    if (pTileToRender == nullptr || pTileToRender->m_refinement > maxRefinement)
    {
        return -1;
    }

    tilesQueue.Remove(pTileToRender);
    m_renderBatch.Add(pTileToRender);
    return pTileToRender->m_refinement;
}

template<class T>
bool ASMTileCache::DoneRendering(T& tilesQueue, ASMFrustum* pFrustum, bool isLayer)
{
    if (!pFrustum->IsValid())
    {
        return false;
    }

    for (uint32_t i = 0; i < tilesQueue.size(); ++i)
    {
        ASMTileCacheEntry* pTile = tilesQueue[i];
        if (pFrustum == pTile->m_pFrustum && isLayer == pTile->m_isLayer && (!pTile->m_isLayer || pTile->m_pOwner->m_pTile->IsReady()))
        {
            return false;
        }
    }
    return true;
}

template<ASMTileCacheEntry*& (QuadTreeNode::*TileAccessor)(), bool isLayer>
void ASMTileCacheEntry::Allocate(QuadTreeNode* pOwner, ASMFrustum* pFrustum)
{
    m_pCache->m_freeTiles.Remove(this);
    m_pOwner = pOwner;
    m_pFrustum = pFrustum;
    m_refinement = pOwner->mRefinement;

    (pOwner->*TileAccessor)() = this;

    if (m_frustumID == pFrustum->m_ID && !(m_BBox.min != pOwner->mBBox.min || m_BBox.max != pOwner->mBBox.max) && m_isLayer == isLayer)
    {
        MarkReady();
    }
    else
    {
        m_frustumID = pFrustum->m_ID;
        m_BBox = pOwner->mBBox;
        m_isLayer = isLayer;
        m_pCache->m_renderQueue.Add(this);
    }
}
template<ASMTileCacheEntry*& (QuadTreeNode::*TileAccessor)(), bool isLayer>
ASMTileCacheEntry* ASMTileCache::Allocate(QuadTreeNode* pNode, ASMFrustum* pFrustum)
{
    ASMTileCacheEntry* pTileToAlloc = NULL;

    if (m_freeTiles.empty())
    {
        uint8_t minRefinement = pNode->mRefinement;
        float   minDistSq = GetRefinementDistanceSq(pNode->mBBox, pFrustum->m_refinementPoint);

        // Try to free visually less important tile (the one further from viewer or deeper in hierarchy)
        for (uint32_t i = 0; i < m_tiles.size(); ++i)
        {
            ASMTileCacheEntry* pTile = m_tiles[i];

            if (pTile->m_refinement < minRefinement)
            {
                continue;
            }

            float distSq = GetRefinementDistanceSq(pTile->m_BBox, pTile->m_pFrustum->m_refinementPoint);

            if (pTile->m_refinement == minRefinement)
            {
                if ((distSq == minDistSq && !pTile->m_isLayer) || distSq < minDistSq)
                {
                    continue;
                }
            }

            pTileToAlloc = pTile;
            minRefinement = pTile->m_refinement;
            minDistSq = distSq;
        }

        if (!pTileToAlloc)
        {
            return NULL;
        }
        pTileToAlloc->Free();
    }

    for (uint32_t i = 0; i < m_freeTiles.size(); ++i)
    {
        ASMTileCacheEntry* pTile = m_freeTiles[i];

        if (pTile->m_frustumID == pFrustum->m_ID && !(pTile->m_BBox.min != pNode->mBBox.min || pTile->m_BBox.max != pNode->mBBox.max) &&
            pTile->m_isLayer == isLayer)
        {
            pTileToAlloc = pTile;
            ++m_cacheHits;
            break;
        }
    }

    if (!pTileToAlloc)
    {
        uint8_t  refinement = 0;
        uint32_t LRUdt = 0;

        for (uint32_t i = 0; i < m_freeTiles.size(); ++i)
        {
            ASMTileCacheEntry* pTile = m_freeTiles[i];
            if (pTile->m_refinement < refinement)
            {
                continue;
            }
            uint32_t dt = asmFrameCounter - pTile->m_lastFrameUsed;
            if (pTile->m_refinement == refinement && dt < LRUdt)
            {
                continue;
            }
            pTileToAlloc = pTile;
            refinement = pTile->m_refinement;
            LRUdt = dt;
        }

        if (pTileToAlloc)
        {
            pTileToAlloc->Invalidate();
        }
    }

    if (pTileToAlloc)
    {
        pTileToAlloc->Allocate<TileAccessor, isLayer>(pNode, pFrustum);
        ++m_tileAllocs;
    }
    return pTileToAlloc;
}

ASM* pASM;

void SetupASMDebugTextures()
{
    float  scale = 0.15f;
    float2 screenSize = { (float)pRenderTargetVBPass->mWidth, (float)pRenderTargetVBPass->mHeight };
    float2 texSize = screenSize * scale;

    gUIASMDebugTexturesWindowDesc = {};
    gUIASMDebugTexturesWindowDesc.mStartPos = vec2(0.0f, screenSize.y - texSize.y - 50.f);
    gUIASMDebugTexturesWindowDesc.mStartSize = vec2(600.0f, 550.0f);
    gUIASMDebugTexturesWindowDesc.pWindowTitle = "ASM Debug Textures Info";
    gUIASMDebugTexturesWindowDesc.mFlags = gGuiWindowDesc.mFlags & ~TF_UI_WINDOW_INIT_HEIGHT_FIT;
}

static void createScene()
{
    /************************************************************************/
    // Initialize Models
    /************************************************************************/
    gMeshInfoData[0].mColor = vec4(1.f);
    gMeshInfoData[0].mScale = float3(MESH_SCALE);
    gMeshInfoData[0].mScaleMat = mat4::scale((gMeshInfoData[0].mScale));
    float finalXTranslation = SAN_MIGUEL_OFFSETX;
    gMeshInfoData[0].mTranslation = float3(finalXTranslation, 0.f, 0.f);
    gMeshInfoData[0].mOffsetTranslation = float3(0.0f, 0.f, 0.f);
    gMeshInfoData[0].mTranslationMat = mat4::translation((gMeshInfoData[0].mTranslation));
}
//////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////
struct GuiController
{
    static void luaRegisterGui();
    static void updateDynamicUI();

    static ShadowType currentlyShadowType;
};
ShadowType GuiController::currentlyShadowType;
//////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////

const char* gTestScripts[] = { "Test_ESM.lua", "Test_ASM.lua", "Test_SDF.lua", "Test_VSM.lua", "Test_MSM.lua" };
uint32_t    gCurrentScriptIndex = 0;
void        RunScript(void* pUserData)
{
    UNREF_PARAM(pUserData);
    TFLuaScriptDesc runDesc = {};
    runDesc.pScriptFileName = gTestScripts[gCurrentScriptIndex];
    luaQueueScriptToRun(&runDesc);
}

//////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////
class LightShadowPlayground: public IApp
{
public:
    LightShadowPlayground() //-V832
    {
#ifdef TARGET_IOS
        mSettings.mContentScaleFactor = 1.f;
#endif
    }

    struct SDFLoadData
    {
        SDFMesh* pSDFMesh = NULL;
        uint32_t startIdx = 0;
    } sdfLoadData[NUM_SDF_MESHES];

    static void refreshASM(void* pUserData)
    {
        UNREF_PARAM(pUserData);
        pASM->Reset();
    }

    static void resetLightDir(void* pUserData)
    {
        UNREF_PARAM(pUserData);
        asmCurrentTime = 0.f;
        refreshASM(pUserData);
    }

    static void loadSDFDataTask(void* user, uint64_t)
    {
        SDFLoadData* data = (SDFLoadData*)user;
        loadBakedSDFData(data->pSDFMesh, data->startIdx, ENABLE_SDF_MESH_GENERATION, gSDFVolumeInstances, &GenerateVolumeDataFromFile);
    }

    void loadSDFMeshes()
    {
        static const char* sdfModels[3] = { "SanMiguel_3/SanMiguel_Opaque.bin", "SanMiguel_3/SanMiguel_AlphaTested.bin",
                                            "SanMiguel_3/SanMiguel_Flags.bin" };

        SDFMeshes[0].pSubMeshesInfo = opaqueMeshInfos;
        SDFMeshes[0].numSubMeshesGroups = sizeof(opaqueMeshInfos) / sizeof(MeshInfo);

        SDFMeshes[1].pSubMeshesInfo = alphaTestedMeshInfos;
        SDFMeshes[1].pSubMeshesGroupsSizes = alphaTestedGroupSizes;
        SDFMeshes[1].pSubMeshesIndices = alphaTestedMeshIndices;
        SDFMeshes[1].numSubMeshesGroups = sizeof(alphaTestedGroupSizes) / sizeof(uint32_t);

        SDFMeshes[2].pSubMeshesInfo = flagsMeshInfos;
        SDFMeshes[2].numSubMeshesGroups = sizeof(flagsMeshInfos) / sizeof(MeshInfo);

        TFVertexLayout vertexLayout = {};
        vertexLayout.mAttribCount = 3;
        vertexLayout.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
        vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
        vertexLayout.mAttribs[0].mLocation = 0;
        vertexLayout.mAttribs[0].mOffset = 0;
        vertexLayout.mAttribs[0].mBinding = 0;
        vertexLayout.mAttribs[1].mSemantic = TF_SEMANTIC_NORMAL;
        vertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32_UINT;
        vertexLayout.mAttribs[1].mLocation = 1;
        vertexLayout.mAttribs[1].mOffset = 3 * sizeof(float);
        vertexLayout.mAttribs[2].mBinding = 0;
        vertexLayout.mAttribs[2].mSemantic = TF_SEMANTIC_TEXCOORD0;
        vertexLayout.mAttribs[2].mFormat = TinyImageFormat_R32_UINT;
        vertexLayout.mAttribs[2].mLocation = 2;
        vertexLayout.mAttribs[2].mOffset = 3 * sizeof(float) + sizeof(uint32_t);
        vertexLayout.mAttribs[2].mBinding = 0;

        TFGeometryLoadDesc geomLoadDesc = {};
        geomLoadDesc.pVertexLayout = &vertexLayout;

        for (uint32_t i = 0; i < NUM_SDF_MESHES; ++i)
        {
            geomLoadDesc.ppGeometry = &SDFMeshes[i].pGeometry;
            geomLoadDesc.ppGeometryData = &SDFMeshes[i].pGeometryData;
            geomLoadDesc.pFileName = sdfModels[i];
            // Required in case SDF Generation is required
            geomLoadDesc.mFlags = TF_GEOMETRY_LOAD_FLAG_KEEP_CPU_COPY;
            addResource(&geomLoadDesc, NULL);
        }
    }

    void loadSDFDataFromFileAsync()
    {
        uint32_t start = 0;
        for (uint32_t i = 0; i < NUM_SDF_MESHES; ++i)
        {
            sdfLoadData[i].pSDFMesh = &SDFMeshes[i];
            sdfLoadData[i].startIdx = start;
            start += SDFMeshes[i].numSubMeshesGroups;
        }
        arrsetlen(gSDFVolumeInstances, start);
        memset(gSDFVolumeInstances, 0, start * sizeof(*gSDFVolumeInstances));
        threadSystemAddTaskGroup(gThreadSystem, loadSDFDataTask, NUM_SDF_MESHES, sdfLoadData);
    }

    static void checkForMissingSDFDataAsync(void*)
    {
        if (gAppSettings.mIsGeneratingSDF)
        {
            LOGF(LogLevel::eINFO, "Generating missing SDF has been executed...");
            return;
        }
        gAppSettings.mIsGeneratingSDF = true;
        threadSystemAddTaskGroup(gThreadSystem, generateMissingSDFTask, NUM_SDF_MESHES, SDFMeshes);
    }

    static void calculateCurSDFMeshesProgress()
    {
        sSDFProgressValue = 0;
        for (int32_t i = 0; i < NUM_SDF_MESHES; ++i)
        {
            sSDFProgressValue += SDFMeshes[i].numGeneratedSDFMeshes;
        }
    }

    static uint32_t getMaxSDFMeshesProgress()
    {
        uint32_t max = 0;
        for (int32_t i = 0; i < NUM_SDF_MESHES; ++i)
        {
            max += SDFMeshes[i].numSubMeshesGroups;
        }
        return max;
    }

    static void initSDFVolumeTextureAtlasData()
    {
        for (size_t i = 0; i < arrlenu(gSDFVolumeInstances); ++i)
        {
            if (!gSDFVolumeInstances[i])
            {
                LOGF(LogLevel::eINFO, "SDF volume data index %zu in Init_SDF_Volume_Texture_Atlas_Data NULL", i);
                continue;
            }
            pSDFVolumeTextureAtlas->AddVolumeTextureNode(&gSDFVolumeInstances[i]->mSDFVolumeTextureNode);
        }
    }

    static float gaussian(float x, float m, float sigma)
    {
        x = abs(x - m) / sigma;
        x *= x;
        return expf(-0.5f * x);
    }

    bool Init() override
    {
        bool threadSystemInitialized = threadSystemInit(&gThreadSystem, &gThreadSystemInitDescDefault);
        ASSERT(threadSystemInitialized);

        INIT_STRUCT(gGpuSettings);

        TFExtendedSettings extendedSettings = {};
        extendedSettings.mNumSettings = ESettings::Count;
        extendedSettings.pSettings = (uint32_t*)&gGpuSettings;
        extendedSettings.ppSettingNames = gSettingNames;

        TFRendererDesc settings;
        memset(&settings, 0, sizeof(settings));
        settings.pExtendedSettings = &extendedSettings;
        initGPUConfig(settings.pExtendedSettings);
        initRenderer(GetName(), &settings, &pRenderer);
        // Check for init success
        if (!pRenderer)
        {
            ShowUnsupportedMessage(getUnsupportedGPUMsg());
            return false;
        }

        if (pRenderer->pGpu->mMaxBoundTextures < MIN_BINDLESS_TEXTURES_COUNT)
        {
            gUsingTextureAtlasFallback = true;
        }

        if (pRenderer->pGpu->mPrimitiveIdSupported == false)
        {
            gUsingPrimitiveIDFallback = true;
        }

        setGPUConfig(pRenderer->pContext->mGpus, pRenderer->pContext->mGpuCount, (uint32_t)(pRenderer->pGpu - pRenderer->pContext->mGpus),
                     settings.pExtendedSettings);

        mSettings.mFrameMaxCount = gDataBufferCount;

        // On Mac with AMD Gpu, doesn't allow ReadWrite on texture formats with 2 component then use float4 format
        gFloat2RWTextureSupported = (pRenderer->pGpu->mFormatCaps[TinyImageFormat_R16G16_UNORM] & TF_FORMAT_CAP_READ_WRITE) &&
                                    (pRenderer->pGpu->mFormatCaps[TinyImageFormat_R16G16_SNORM] & TF_FORMAT_CAP_READ_WRITE) &&
                                    (pRenderer->pGpu->mFormatCaps[TinyImageFormat_R16G16_SFLOAT] & TF_FORMAT_CAP_READ_WRITE) &&
                                    (pRenderer->pGpu->mFormatCaps[TinyImageFormat_R32G32_SFLOAT] & TF_FORMAT_CAP_READ_WRITE);

        gAppSettings.mMaxMsaaLevel = (TFSampleCount)gGpuSettings.mMaxMSAALevel;
        gAppSettings.mMsaaLevel =
            (TFSampleCount)clamp(gGpuSettings.mMSAASampleCount, (uint32_t)TF_SAMPLE_COUNT_1, (uint32_t)gAppSettings.mMaxMsaaLevel);
        gAppSettings.mMsaaIndex = (uint32_t)log2((uint32_t)gAppSettings.mMsaaLevel);
        gAppSettings.mMsaaIndexRequested = gAppSettings.mMsaaIndex;
        gCameraUniformData.mSSSEnabled = gGpuSettings.mDisableScreenSpaceShadows == 0;

        TFQueueDesc queueDesc = {};
        queueDesc.mType = TF_QUEUE_TYPE_GRAPHICS;
        queueDesc.mFlag = TF_QUEUE_FLAG_INIT_MICROPROFILE;
        initQueue(pRenderer, &queueDesc, &pGraphicsQueue);

        // Create the command pool and the command lists used to store GPU commands.
        // One TFCmd list per back buffer image is stored for triple buffering.
        GpuCmdRingDesc cmdRingDesc = {};
        cmdRingDesc.pQueue = pGraphicsQueue;
        cmdRingDesc.mPoolCount = gDataBufferCount;
        cmdRingDesc.mCmdPerPoolCount = 1;
        cmdRingDesc.mAddSyncPrimitives = true;
        initGpuCmdRing(pRenderer, &cmdRingDesc, &gGraphicsCmdRing);

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            initSemaphore(pRenderer, &pImageAcquiredSemaphore[i]);
        }

        initResourceLoaderInterface(pRenderer);

        TFRootSignatureDesc rootDesc = {};
        INIT_RS_DESC(rootDesc, "default.rootsig", "compute.rootsig");
        initRootSignature(pRenderer, &rootDesc);

        // Load fonts
        TFFontSystemDesc fontSystemDesc{};
        fontSystemDesc.pRenderer = pRenderer;
        fontSystemDesc.mFrameMaxCount = mSettings.mFrameMaxCount;
        fontSystemDesc.pFrameIdx = &mSettings.mFrameIdx;
        initFontSystem(&fontSystemDesc);

        TFFontDesc fontDesc = {};
        fontDesc.pFontPath = "Selawk/selawk.msdf";
        fontDesc.pFontName = "Selawk";
        fontDesc.mFlags |= TF_FONT_ASCII;
        fontDesc.mFlags |= TF_FONT_ATLAS_AUTO_RESOLUTION_ON_INIT;
        gFont = addFont(&fontDesc);
        if (gFont == NULL)
            return false;

        // Initialize Forge User Interface Rendering
        TFUserInterfaceDesc uiRenderDesc = {};
        uiRenderDesc.pRenderer = pRenderer;
        uiRenderDesc.mFrameMaxCount = mSettings.mFrameMaxCount;
        uiRenderDesc.pFrameIdx = &mSettings.mFrameIdx;
        uiRenderDesc.pFont = gFont;
        initUserInterface(&uiRenderDesc);

        const uint32_t  numScripts = sizeof(gTestScripts) / sizeof(gTestScripts[0]);
        TFLuaScriptDesc scriptDescs[numScripts] = {};
        for (uint32_t i = 0; i < numScripts; ++i)
        {
            scriptDescs[i].pScriptFileName = gTestScripts[i];

            if (strstr(gTestScripts[i], "SDF"))
                scriptDescs[i].pWaitCondition = &sSDFGenerationFinished;
        }
        luaDefineScripts(scriptDescs, numScripts);

        // Initialize micro profiler and its UI.
        TFProfilerDesc profiler = {};
        profiler.pRenderer = pRenderer;
        initProfiler(&profiler);

        /************************************************************************/
        // Geometry data for the scene
        /************************************************************************/
        TFSyncToken token = {};

        uint64_t         quadDataSize = sizeof(gQuadVertices);
        TFBufferLoadDesc quadVbDesc = {};
        quadVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        quadVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        quadVbDesc.mDesc.mStartState = TF_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        quadVbDesc.mDesc.mSize = quadDataSize;
        quadVbDesc.pData = gQuadVertices;
        quadVbDesc.ppBuffer = &pBufferQuadVertex;
        addResource(&quadVbDesc, &token);

        /************************************************************************/
        // Setup constant buffer data
        /************************************************************************/
        TFBufferLoadDesc vbConstantUBDesc = {};
        vbConstantUBDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        vbConstantUBDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        vbConstantUBDesc.mDesc.mSize = sizeof(PerFrameVBConstantsData);
        vbConstantUBDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        vbConstantUBDesc.mDesc.pName = "gPerFrameVBConstants Buffer Desc";
        vbConstantUBDesc.pData = NULL;

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            vbConstantUBDesc.ppBuffer = &pBufferVBConstants[i];
            addResource(&vbConstantUBDesc, NULL);
        }

        TFBufferLoadDesc ubDesc = {};
        ubDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        ubDesc.mDesc.mSize = sizeof(ObjectUniform);
        ubDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        ubDesc.pData = NULL;

        for (uint32_t j = 0; j < MESH_COUNT; ++j)
        {
            for (uint32_t i = 0; i < gDataBufferCount; ++i)
            {
                ubDesc.ppBuffer = &pBufferMeshTransforms[j][i];
                addResource(&ubDesc, NULL);
            }
        }

        for (uint32_t k = 0; k < ASM_MAX_TILES_PER_PASS; k++)
        {
            for (uint32_t j = 0; j < MESH_COUNT; ++j)
            {
                for (uint32_t i = 0; i < gDataBufferCount; ++i)
                {
                    ubDesc.ppBuffer = &pBufferMeshShadowProjectionTransforms[j][k][i];
                    addResource(&ubDesc, NULL);
                }
            }
        }

        TFBufferLoadDesc ubBlurDesc = {};
        ubBlurDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubBlurDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        ubBlurDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        ubBlurDesc.mDesc.mSize = sizeof(BlurConstant);
        ubBlurDesc.pData = &gBlurConstantsData;
        ubBlurDesc.ppBuffer = &pBufferBlurConstants;
        addResource(&ubBlurDesc, &token);

        TFBufferLoadDesc ubSSSDesc = {};
        ubSSSDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubSSSDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        ubSSSDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        ubSSSDesc.mDesc.mSize = sizeof(SSSInputConstants);
        ubSSSDesc.pData = &gSSSUniformData;
        for (uint32_t viewIndex = 0; viewIndex < VR_MULTIVIEW_COUNT; viewIndex++)
        {
            for (uint32_t i = 0; i < gDataBufferCount; i++)
            {
                ubSSSDesc.ppBuffer = &pBufferSSSUniform[viewIndex][i];
                addResource(&ubSSSDesc, NULL);
            }
        }

        TFBufferLoadDesc quadUbDesc = {};
        quadUbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        quadUbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        quadUbDesc.mDesc.mSize = sizeof(QuadDataUniform);
        quadUbDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        quadUbDesc.pData = NULL;
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            quadUbDesc.ppBuffer = &pBufferQuadUniform[i];
            addResource(&quadUbDesc, NULL);
        }

        TFBufferLoadDesc asmAtlasQuadsUbDesc = {};
        asmAtlasQuadsUbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        asmAtlasQuadsUbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        asmAtlasQuadsUbDesc.mDesc.mSize = sizeof(ASMAtlasQuadsUniform);
        asmAtlasQuadsUbDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        asmAtlasQuadsUbDesc.pData = NULL;
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            asmAtlasQuadsUbDesc.mDesc.mSize = sizeof(PackedAtlasQuads);

            asmAtlasQuadsUbDesc.ppBuffer = &pBufferASMClearIndirectionQuadsUniform[i];
            addResource(&asmAtlasQuadsUbDesc, NULL);
        }

        TFBufferLoadDesc asmPackedAtlasQuadsUbDesc = {};
        asmPackedAtlasQuadsUbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        asmPackedAtlasQuadsUbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        asmPackedAtlasQuadsUbDesc.mDesc.mSize = sizeof(PackedAtlasQuads) * 2;
        asmPackedAtlasQuadsUbDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        for (uint32_t i = 0; i < gs_ASMMaxRefinement + 1; ++i)
        {
            for (uint32_t k = 0; k < gDataBufferCount; ++k)
            {
                asmPackedAtlasQuadsUbDesc.ppBuffer = &pBufferASMPackedIndirectionQuadsUniform[i][k];
                addResource(&asmPackedAtlasQuadsUbDesc, NULL);

                asmPackedAtlasQuadsUbDesc.ppBuffer = &pBufferASMPackedPrerenderIndirectionQuadsUniform[i][k];
                addResource(&asmPackedAtlasQuadsUbDesc, NULL);
            }
        }

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            asmPackedAtlasQuadsUbDesc.ppBuffer = &pBufferASMCopyDEMPackedQuadsUniform[i];
            addResource(&asmPackedAtlasQuadsUbDesc, NULL);

            asmPackedAtlasQuadsUbDesc.ppBuffer = &pBufferASMAtlasQuadsUniform[i];
            addResource(&asmPackedAtlasQuadsUbDesc, NULL);

            asmPackedAtlasQuadsUbDesc.ppBuffer = &pBufferASMColorToAtlasPackedQuadsUniform[i];
            addResource(&asmPackedAtlasQuadsUbDesc, NULL);

            asmPackedAtlasQuadsUbDesc.ppBuffer = &pBufferASMAtlasToColorPackedQuadsUniform[i];
            addResource(&asmPackedAtlasQuadsUbDesc, NULL);

            asmPackedAtlasQuadsUbDesc.ppBuffer = &pBufferASMLodClampPackedQuadsUniform[i];
            addResource(&asmPackedAtlasQuadsUbDesc, NULL);
        }

        TFBufferLoadDesc asmDataUbDesc = {};
        asmDataUbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        asmDataUbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        asmDataUbDesc.mDesc.mSize = sizeof(ASMUniformBlock);
        asmDataUbDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            asmDataUbDesc.ppBuffer = &pBufferASMDataUniform[i];
            addResource(&asmDataUbDesc, NULL);
        }

        TFBufferLoadDesc camUniDesc = {};
        camUniDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        camUniDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        camUniDesc.mDesc.mSize = sizeof(CameraUniform);
        camUniDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        camUniDesc.pData = &gCameraUniformData;
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            camUniDesc.ppBuffer = &pBufferCameraUniform[i];
            addResource(&camUniDesc, &token);
        }

        TFBufferLoadDesc meshSDFUniformDesc = {};
        meshSDFUniformDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        meshSDFUniformDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        meshSDFUniformDesc.mDesc.mSize = sizeof(MeshSDFConstants);
        meshSDFUniformDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        meshSDFUniformDesc.pData = NULL;

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            meshSDFUniformDesc.ppBuffer = &pBufferMeshSDFConstants[i];
            addResource(&meshSDFUniformDesc, NULL);
        }

#ifdef QUEST_VR
        TFBufferLoadDesc perDrawSDFData = {};
        perDrawSDFData.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        perDrawSDFData.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        perDrawSDFData.mDesc.mSize = sizeof(SDFPerDrawData);
        perDrawSDFData.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        SDFPerDrawData perDrawData[gDataBufferCount * VR_MULTIVIEW_COUNT] = { {} };
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            for (uint32_t viewIndex = 0; viewIndex < VR_MULTIVIEW_COUNT; ++viewIndex)
            {
                const uint32_t bufferIndex = i * VR_MULTIVIEW_COUNT + viewIndex;
                perDrawData[bufferIndex] = { viewIndex };
                perDrawSDFData.pData = &perDrawData[bufferIndex];
                perDrawSDFData.ppBuffer = &pBufferSDFPerDraw[bufferIndex];
                addResource(&perDrawSDFData, &token);
            }
        }
#endif

        TFBufferLoadDesc updateSDFVolumeTextureAtlasUniformDesc = {};
        updateSDFVolumeTextureAtlasUniformDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        updateSDFVolumeTextureAtlasUniformDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        updateSDFVolumeTextureAtlasUniformDesc.mDesc.mSize = sizeof(UpdateSDFVolumeTextureAtlasConstants);
        updateSDFVolumeTextureAtlasUniformDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        updateSDFVolumeTextureAtlasUniformDesc.pData = NULL;

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            updateSDFVolumeTextureAtlasUniformDesc.ppBuffer = &pBufferUpdateSDFVolumeTextureAtlasConstants[i];
            addResource(&updateSDFVolumeTextureAtlasUniformDesc, NULL);
        }

        TFBufferLoadDesc sssWaveOffsetsUniformDesc = {};
        sssWaveOffsetsUniformDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        sssWaveOffsetsUniformDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        sssWaveOffsetsUniformDesc.mDesc.mSize = sizeof(uint2);
        sssWaveOffsetsUniformDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        sssWaveOffsetsUniformDesc.pData = NULL;
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            for (uint32_t j = 0; j < MAX_SSS_WAVE_OFFSETS; ++j)
            {
                sssWaveOffsetsUniformDesc.ppBuffer = &pBufferSSSWaveOffsets[j][i];
                addResource(&sssWaveOffsetsUniformDesc, NULL);
            }
        }

        TFBufferLoadDesc gaussianBlurConstantsUniformDesc = {};
        gaussianBlurConstantsUniformDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        gaussianBlurConstantsUniformDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        gaussianBlurConstantsUniformDesc.mDesc.mSize = sizeof(BlurConstant);
        gaussianBlurConstantsUniformDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        gaussianBlurConstantsUniformDesc.pData = NULL;
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            for (uint32_t j = 0; j < 2; ++j)
            {
                gaussianBlurConstantsUniformDesc.ppBuffer = &pBufferGaussianBlurConstants[j][i];
                addResource(&gaussianBlurConstantsUniformDesc, NULL);
            }
        }

#if TEST_GPU_BREADCRUMBS
        // Initialize breadcrumb buffer to write markers in it.
        if (pRenderer->pGpu->mGpuMarkers)
        {
            initMarkers();
        }
#endif

        /************************************************************************/
        // Add GPU profiler
        /************************************************************************/
        for (uint32_t i = 0; i < SHADOW_TYPE_COUNT; ++i)
            gCurrentGpuProfileTokens[i] = initGpuProfiler(pRenderer, pGraphicsQueue, "Graphics");
        gCurrentGpuProfileToken = gCurrentShadowType;
        /************************************************************************/
        // Add samplers
        /************************************************************************/

        TFSamplerDesc samplerTrilinearAnisoDesc = {};
        samplerTrilinearAnisoDesc.mAddressU = TF_ADDRESS_MODE_REPEAT;
        samplerTrilinearAnisoDesc.mAddressV = TF_ADDRESS_MODE_REPEAT;
        samplerTrilinearAnisoDesc.mAddressW = TF_ADDRESS_MODE_REPEAT;
        samplerTrilinearAnisoDesc.mMinFilter = TF_FILTER_LINEAR;
        samplerTrilinearAnisoDesc.mMagFilter = TF_FILTER_LINEAR;
        samplerTrilinearAnisoDesc.mMipMapMode = TF_MIPMAP_MODE_LINEAR;
        samplerTrilinearAnisoDesc.mMipLodBias = 0.0f;
        samplerTrilinearAnisoDesc.mMaxAnisotropy = 8.0f;
        addSampler(pRenderer, &samplerTrilinearAnisoDesc, &pSamplerTrilinearAniso);

        TFSamplerDesc miplessNearSamplerDesc = {};
        miplessNearSamplerDesc.mMinFilter = TF_FILTER_NEAREST;
        miplessNearSamplerDesc.mMagFilter = TF_FILTER_NEAREST;
        miplessNearSamplerDesc.mMipMapMode = TF_MIPMAP_MODE_NEAREST;
        miplessNearSamplerDesc.mAddressU = TF_ADDRESS_MODE_CLAMP_TO_EDGE;
        miplessNearSamplerDesc.mAddressV = TF_ADDRESS_MODE_CLAMP_TO_EDGE;
        miplessNearSamplerDesc.mAddressW = TF_ADDRESS_MODE_CLAMP_TO_EDGE;
        miplessNearSamplerDesc.mMipLodBias = 0.f;
        miplessNearSamplerDesc.mMaxAnisotropy = 0.f;
        addSampler(pRenderer, &miplessNearSamplerDesc, &pSamplerMiplessNear);

        TFSamplerDesc miplessLinearSamplerDesc = {};
        miplessLinearSamplerDesc.mMinFilter = TF_FILTER_LINEAR;
        miplessLinearSamplerDesc.mMagFilter = TF_FILTER_LINEAR;
        miplessLinearSamplerDesc.mMipMapMode = TF_MIPMAP_MODE_LINEAR;
        miplessLinearSamplerDesc.mAddressU = TF_ADDRESS_MODE_CLAMP_TO_EDGE;
        miplessLinearSamplerDesc.mAddressV = TF_ADDRESS_MODE_CLAMP_TO_EDGE;
        miplessLinearSamplerDesc.mAddressW = TF_ADDRESS_MODE_CLAMP_TO_EDGE;
        miplessLinearSamplerDesc.mMipLodBias = 0.f;
        miplessLinearSamplerDesc.mMaxAnisotropy = 0.f;
        addSampler(pRenderer, &miplessLinearSamplerDesc, &pSamplerMiplessLinear);
        miplessLinearSamplerDesc.mCompareFunc = TFCompareMode::TF_CMP_LEQUAL;
        miplessLinearSamplerDesc.mAddressU = TF_ADDRESS_MODE_CLAMP_TO_BORDER;
        miplessLinearSamplerDesc.mAddressV = TF_ADDRESS_MODE_CLAMP_TO_BORDER;
        miplessLinearSamplerDesc.mAddressW = TF_ADDRESS_MODE_CLAMP_TO_BORDER;
        addSampler(pRenderer, &miplessLinearSamplerDesc, &pSamplerComparisonShadow);
        TFSamplerDesc miplessClampToBorderNearSamplerDesc = {};
        miplessClampToBorderNearSamplerDesc.mMinFilter = TF_FILTER_NEAREST;
        miplessClampToBorderNearSamplerDesc.mMagFilter = TF_FILTER_NEAREST;
        miplessClampToBorderNearSamplerDesc.mMipMapMode = TF_MIPMAP_MODE_NEAREST;
        miplessClampToBorderNearSamplerDesc.mAddressU = TF_ADDRESS_MODE_CLAMP_TO_BORDER;
        miplessClampToBorderNearSamplerDesc.mAddressV = TF_ADDRESS_MODE_CLAMP_TO_BORDER;
        miplessClampToBorderNearSamplerDesc.mAddressW = TF_ADDRESS_MODE_CLAMP_TO_EDGE;
        miplessClampToBorderNearSamplerDesc.mMaxAnisotropy = 0.f;
        miplessClampToBorderNearSamplerDesc.mMipLodBias = 0.f;
        addSampler(pRenderer, &miplessClampToBorderNearSamplerDesc, &pSamplerMiplessClampToBorderNear);

        threadSystemAddTasks(gThreadSystem, loadSkybox, 1, 0, NULL);

        {
            TFTextureDesc texDesc = {};
            texDesc.mArraySize = 1;
            texDesc.mDepth = 1;
            texDesc.mFormat = TinyImageFormat_B8G8R8A8_SRGB;
            texDesc.mWidth = 1;
            texDesc.mHeight = 2;
            texDesc.mMipLevels = 1;
            texDesc.mSampleCount = TF_SAMPLE_COUNT_1;
            texDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
            texDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
            texDesc.pName = "null_texture";

            TFTextureLoadDesc loadDesc = {};
            loadDesc.pDesc = &texDesc;
            loadDesc.ppTexture = &gNullTextureResource;
            addResource(&loadDesc, NULL);
        }

        TFPackageLoadDesc packageLoadDesc = {};
        if (gUsingTextureAtlasFallback)
        {
            packageLoadDesc.packageName = "SanMiguelPak.fallback.buny";
        }
        else
        {
            packageLoadDesc.packageName = "SanMiguelPak.buny";
        }
        packageLoadDesc.loadGeoData = false;
        packageLoadDesc.loadTexData = true;
        packageLoadDesc.pGeometryBuffer = NULL;
        packageLoadDesc.pGeometryBufferLayout = NULL;
        packageLoadDesc.geometryLoadFlags = TF_GEOMETRY_LOAD_FLAG_KEEP_CPU_COPY; // To compute CPU clusters
        packageLoadDesc.pOnGeometryLoaded = NULL;
        packageLoadDesc.pOnGeometryLoadedUserData = NULL;
        packageLoadDesc.createAtlas = gUsingTextureAtlasFallback;
        packageLoadDesc.ppOutPackage = &pPackage;
        bool pakLoaded = addResourcesFromPackage(&packageLoadDesc, gNullTextureResource);
        if (!pakLoaded)
        {
            LOGF(LogLevel::eERROR, "Failed to load package.");
            return false;
        }

        threadSystemWaitIdle(gThreadSystem);

        loadSDFMeshes();
        loadSDFDataFromFileAsync();

        gMeshCount = pPackage->pTextureMetadata->mMeshCount;
        gTextureCount = pPackage->pTextureMetadata->mTextureCount;

        pVBMeshInstances = (VBMeshInstance*)tf_calloc(gMeshCount, sizeof(VBMeshInstance));
        const uint32_t      sceneGeoIndex = 0;
        TFGeometryLoadDesc* pLoadDesc = &pPackage->pGeoData[sceneGeoIndex];
        pLoadDesc->ppGeometry = &pGeom;
        pLoadDesc->ppGeometryData = &pGeomData;
        TFSyncToken waitToken = {};
        addResource(pLoadDesc, &waitToken);
        waitForToken(&waitToken);

        /************************************************************************/
        uint32_t visibilityBufferFilteredIndexCount[NUM_GEOMETRY_SETS] = {};

        MeshConstants* meshConstants = (MeshConstants*)tf_malloc(gMeshCount * sizeof(MeshConstants));
        // Calculate mesh constants and filter containers
        for (uint32_t i = 0; i < gMeshCount; ++i)
        {
            uint16_t matFlag = pPackage->pTextureMetadata->pMaterialProps[i].mFlags;
            uint32_t geomSet = matFlag & (MATERIAL_FLAG_ALPHA_TESTED | MATERIAL_FLAG_TRANSPARENT) ? GEOMSET_ALPHA_CUTOUT : GEOMSET_OPAQUE;
            visibilityBufferFilteredIndexCount[geomSet] += (pGeom->pDrawArgs + i)->mIndexCount;
            pVBMeshInstances[i].mGeometrySet = geomSet;
            pVBMeshInstances[i].mMeshIndex = i;
            pVBMeshInstances[i].mTriangleCount = (pGeom->pDrawArgs + i)->mIndexCount / 3;
            pVBMeshInstances[i].mInstanceIndex = INSTANCE_INDEX_NONE;

            meshConstants[i].indexOffset = pGeom->pDrawArgs[i].mStartIndex;
            meshConstants[i].vertexOffset = pGeom->pDrawArgs[i].mVertexOffset;
            meshConstants[i].materialID_flags = ((matFlag & FLAG_MASK) << FLAG_LOW_BIT) | ((i & MATERIAL_ID_MASK) << MATERIAL_ID_LOW_BIT);
            meshConstants[i].specEmissiveStrength = pPackage->pTextureMetadata->pMaterialProps[i].mSpecEmissiveStrength;
            meshConstants[i].baseColor = pPackage->pTextureMetadata->pMaterialProps[i].mBaseColor;
        }

        removeResource(pGeomData);
        pGeomData = NULL;

        VisibilityBufferDesc vbDesc = {};
        vbDesc.mNumFrames = gDataBufferCount;
        vbDesc.mNumBuffers = 1; // We don't use Async Compute for triangle filtering, 1 buffer is enough
        vbDesc.mNumGeometrySets = NUM_GEOMETRY_SETS;
        vbDesc.pMaxIndexCountPerGeomSet = visibilityBufferFilteredIndexCount;
        vbDesc.mNumViews = NUM_CULLING_VIEWPORTS;
        vbDesc.mComputeThreads = VB_COMPUTE_THREADS;
        initVisibilityBuffer(pRenderer, &vbDesc, &pVisibilityBuffer);

        TFBufferLoadDesc meshConstantDesc = {};
        meshConstantDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER;
        meshConstantDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        meshConstantDesc.mDesc.mElementCount = (uint32_t)gMeshCount;
        meshConstantDesc.mDesc.mStructStride = sizeof(MeshConstants);
        meshConstantDesc.mDesc.mSize = meshConstantDesc.mDesc.mElementCount * meshConstantDesc.mDesc.mStructStride;
        meshConstantDesc.mDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        meshConstantDesc.ppBuffer = &pBufferMeshConstants;
        meshConstantDesc.pData = meshConstants;
        meshConstantDesc.mDesc.pName = "Mesh Constant desc";
        addResource(&meshConstantDesc, &token);

        UpdateVBMeshFilterGroupsDesc updateVBMeshFilterGroupsDesc = {};
        updateVBMeshFilterGroupsDesc.mNumMeshInstance = (uint32_t)gMeshCount;
        updateVBMeshFilterGroupsDesc.pVBMeshInstances = pVBMeshInstances;
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            updateVBMeshFilterGroupsDesc.mFrameIndex = i;
            gVBPreFilterStats[i] = updateVBMeshFilterGroups(pVisibilityBuffer, &updateVBMeshFilterGroupsDesc);
        }

        for (uint32_t frameIdx = 0; frameIdx < gDataBufferCount; ++frameIdx)
        {
            gPerFrameData[frameIdx].gDrawCount[GEOMSET_OPAQUE] = gVBPreFilterStats[frameIdx].mGeomsetMaxDrawCounts[GEOMSET_OPAQUE];
            gPerFrameData[frameIdx].gDrawCount[GEOMSET_ALPHA_CUTOUT] =
                gVBPreFilterStats[frameIdx].mGeomsetMaxDrawCounts[GEOMSET_ALPHA_CUTOUT];
        }

        /************************************************************************/
        ////////////////////////////////////////////////
        waitForToken(&token);

        /************************************************************************/
        // Initialize Resources
        /************************************************************************/
        gCameraUniformData.mEsmControl = gEsmCpuSettings.mEsmControl;

        createScene();

        /************************************************************************/
        // Initialize ASM's render data
        /************************************************************************/
        pASM = tf_new(ASM);
        gLightDirUpdateAngle = (float)acos(gLightDirUpdateThreshold) * 180.0f / PI;

        pSDFVolumeTextureAtlas = tf_new(
            gSDFVolumeTextureAtlas, ivec3(SDF_VOLUME_TEXTURE_ATLAS_WIDTH, SDF_VOLUME_TEXTURE_ATLAS_HEIGHT, SDF_VOLUME_TEXTURE_ATLAS_DEPTH));

        initSDFVolumeTextureAtlasData();

        uint32_t volumeBufferElementCount =
            SDF_DOUBLE_MAX_VOXEL_ONE_DIMENSION_X * SDF_DOUBLE_MAX_VOXEL_ONE_DIMENSION_Y * SDF_DOUBLE_MAX_VOXEL_ONE_DIMENSION_Z;

        TFBufferLoadDesc sdfMeshVolumeDataUniformDesc = {};
        sdfMeshVolumeDataUniformDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER;
        sdfMeshVolumeDataUniformDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        sdfMeshVolumeDataUniformDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_NONE;
        sdfMeshVolumeDataUniformDesc.mDesc.mStartState = TF_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        sdfMeshVolumeDataUniformDesc.mDesc.mStructStride = sizeof(float);
        sdfMeshVolumeDataUniformDesc.mDesc.mElementCount = volumeBufferElementCount;
        sdfMeshVolumeDataUniformDesc.mDesc.mSize =
            sdfMeshVolumeDataUniformDesc.mDesc.mStructStride * sdfMeshVolumeDataUniformDesc.mDesc.mElementCount;

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            sdfMeshVolumeDataUniformDesc.ppBuffer = &pBufferSDFVolumeData[i];
            addResource(&sdfMeshVolumeDataUniformDesc, NULL);
        }
        /************************************************************************/
        // SDF volume atlas Texture
        /************************************************************************/
        if (gCurrentShadowType == SHADOW_TYPE_MESH_BAKED_SDF)
            addSdfVolumeTextureAtlas();

        TFCameraMotionParameters cmp{ 146.0f, 300.0f, 140.0f };
        vec3                     camPos = vec3(120.f + SAN_MIGUEL_OFFSETX, 98.f, 14.f);
        vec3                     lookAt = camPos + vec3(-1.0f - 0.0f, 0.1f, 0.0f);

        pLightView = initGuiCamera(camPos, lookAt);
        pCamera = initFpsCamera(camPos, lookAt);
        pCamera->setMotionParameters(cmp);

        AddCustomInputBindings();

        waitForAllResourceLoads();

        gAllTexturesCount = gMeshCount * TEXTURES_PER_MESH;

        tf_free(meshConstants);
        initScreenshotCapturer(pRenderer, pGraphicsQueue, GetName());
        gFrameIndex = 0;

        return true;
    }

    void Exit() override
    {
        exitScreenshotCapturer();
        threadSystemWaitIdle(gThreadSystem);

        threadSystemExit(&gThreadSystem, &gThreadSystemExitDescDefault);

        exitCamera(pCamera);
        exitCamera(pLightView);

        for (uint32_t i = 0; i < TF_ARRAY_COUNT(SDFMeshes); ++i)
        {
            removeResource(SDFMeshes[i].pGeometry);
            removeResource(SDFMeshes[i].pGeometryData);
            SDFMeshes[i] = {};
        }

        for (uint32_t i = 0; i < SHADOW_TYPE_COUNT; ++i)
            exitGpuProfiler(gCurrentGpuProfileTokens[i]);

        exitProfiler();

#if TEST_GPU_BREADCRUMBS
        if (pRenderer->pGpu->mGpuMarkers)
        {
            exitMarkers();
        }
#endif
        removeResource(gNullTextureResource);
        removeResource(pGeom);

        tf_delete(pASM);
        tf_delete(pSDFVolumeTextureAtlas);

        if (pTextureSDFVolumeAtlas)
        {
            removeResource(pTextureSDFVolumeAtlas);
            pTextureSDFVolumeAtlas = NULL;
        }

        removeResourcesFromPackage(pPackage);

        exitUserInterface();

        removeFont(gFont);
        exitFontSystem();

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            removeResource(pBufferCameraUniform[i]);

            removeResource(pBufferVBConstants[i]);

            removeResource(pBufferASMAtlasQuadsUniform[i]);
            removeResource(pBufferASMAtlasToColorPackedQuadsUniform[i]);
            removeResource(pBufferASMClearIndirectionQuadsUniform[i]);
            removeResource(pBufferASMColorToAtlasPackedQuadsUniform[i]);
            removeResource(pBufferASMCopyDEMPackedQuadsUniform[i]);
            removeResource(pBufferASMDataUniform[i]);
            removeResource(pBufferASMLodClampPackedQuadsUniform[i]);
            removeResource(pBufferQuadUniform[i]);

            for (uint32_t j = 0; j < 2; ++j)
            {
                removeResource(pBufferGaussianBlurConstants[j][i]);
            }

            for (uint32_t j = 0; j < MAX_SSS_WAVE_OFFSETS; ++j)
            {
                removeResource(pBufferSSSWaveOffsets[j][i]);
            }

            for (int32_t k = 0; k < MESH_COUNT; ++k)
            {
                removeResource(pBufferMeshTransforms[k][i]);
            }

            for (int32_t k = 0; k < MESH_COUNT; ++k)
            {
                for (int j = 0; j < ASM_MAX_TILES_PER_PASS; j++)
                {
                    removeResource(pBufferMeshShadowProjectionTransforms[k][j][i]);
                }
            }

            for (uint32_t k = 0; k <= gs_ASMMaxRefinement; ++k)
            {
                removeResource(pBufferASMPackedIndirectionQuadsUniform[k][i]);
                removeResource(pBufferASMPackedPrerenderIndirectionQuadsUniform[k][i]);
            }

            removeResource(pBufferMeshSDFConstants[i]);
            removeResource(pBufferUpdateSDFVolumeTextureAtlasConstants[i]);
            removeResource(pBufferSDFVolumeData[i]);
#ifdef QUEST_VR
            for (uint32_t viewIndex = 0; viewIndex < VR_MULTIVIEW_COUNT; ++viewIndex)
            {
                removeResource(pBufferSDFPerDraw[i * VR_MULTIVIEW_COUNT + viewIndex]);
            }
#endif
        }
        removeResource(pBufferBlurConstants);
        removeResource(pBufferBlurWeights);

        for (uint32_t i = 0; i < gDataBufferCount; i++)
        {
            for (uint32_t viewIndex = 0; viewIndex < VR_MULTIVIEW_COUNT; viewIndex++)
            {
                removeResource(pBufferSSSUniform[viewIndex][i]);
            }
        }
        removeResource(pBufferMeshConstants);
        removeResource(pBufferQuadVertex);
        removeResource(pBufferSkyboxVertex);

        tf_free(pVBMeshInstances);

        for (size_t i = 0; i < arrlenu(gSDFVolumeInstances); ++i)
        {
            if (gSDFVolumeInstances[i])
            {
                tf_delete(gSDFVolumeInstances[i]);
            }
        }

        arrfree(gSDFVolumeInstances);
        gSDFVolumeInstances = NULL;

        removeSampler(pRenderer, pSamplerMiplessNear);
        removeSampler(pRenderer, pSamplerTrilinearAniso);
        removeSampler(pRenderer, pSamplerComparisonShadow);
        removeSampler(pRenderer, pSamplerMiplessLinear);
        removeSampler(pRenderer, pSamplerMiplessClampToBorderNear);

        removeResource(pTextureSkybox);

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            exitSemaphore(pRenderer, pImageAcquiredSemaphore[i]);
        }
        exitGpuCmdRing(pRenderer, &gGraphicsCmdRing);

        exitVisibilityBuffer(pVisibilityBuffer);
        exitRootSignature(pRenderer);
        exitResourceLoaderInterface(pRenderer);
        exitQueue(pRenderer, pGraphicsQueue);

        exitRenderer(pRenderer);
        exitGPUConfig();

        pRenderer = NULL;
    }

    bool Load(TFReloadDesc* pReloadDesc) override
    {
        UNREF_PARAM(pReloadDesc);

        gSceneRes = getGPUCfgSceneResolution(mSettings.mWidth, mSettings.mHeight);

        addShaders();
        addDescriptorSets();

        loadProfilerUI(mSettings.mWidth, mSettings.mHeight);

        calculateCurSDFMeshesProgress();
        sSDFProgressMaxValue = LightShadowPlayground::getMaxSDFMeshesProgress();

        gGuiWindowDesc = {};
        gGuiWindowDesc.mStartPos = vec2(mSettings.mWidth * 0.01f, mSettings.mHeight * 0.2f);
        gGuiWindowDesc.mStartSize = vec2(600.0f, 550.0f);
        gGuiWindowDesc.pWindowTitle = GetName();
        gGuiWindowDesc.mFlags = TF_UI_WINDOW_TITLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_MINIMIZABLE |
                                TF_UI_WINDOW_BORDER | TF_UI_WINDOW_INIT_HEIGHT_FIT;

        gLoadingGuiDesc = {};
        gLoadingGuiDesc.mStartPos = vec2(mSettings.mWidth * 0.15f, mSettings.mHeight * 0.4f);
        gLoadingGuiDesc.mStartSize = vec2(600.0f, 550.0f);
        gLoadingGuiDesc.pWindowTitle = "Generating SDF";
        gLoadingGuiDesc.mFlags = gGuiWindowDesc.mFlags;

        gSSSGuiDesc = {};
        gSSSGuiDesc.mStartPos = vec2(mSettings.mWidth * 0.01f, mSettings.mHeight * 0.6f);
        gSSSGuiDesc.mStartSize = vec2(600.0f, 550.0f);
        gSSSGuiDesc.pWindowTitle = "Screen Space Shadows";
        gSSSGuiDesc.mFlags = gGuiWindowDesc.mFlags;

        GuiController::luaRegisterGui();

        if (gAppSettings.mMsaaIndex != gAppSettings.mMsaaIndexRequested)
        {
            gAppSettings.mMsaaIndex = gAppSettings.mMsaaIndexRequested;
            gAppSettings.mMsaaLevel = (TFSampleCount)(1 << gAppSettings.mMsaaIndex);

            while (gAppSettings.mMsaaIndex > 0)
            {
                bool isValidLevel = (pRenderer->pGpu->mFrameBufferSamplesCount & gAppSettings.mMsaaLevel) != 0;
                isValidLevel &= gAppSettings.mMsaaLevel <= gAppSettings.mMaxMsaaLevel;
                if (!isValidLevel)
                {
                    gAppSettings.mMsaaIndex--;
                    gAppSettings.mMsaaLevel = (TFSampleCount)(gAppSettings.mMsaaLevel / 2);
                }
                else
                {
                    break;
                }
            }
        }

        if (!addSwapChain())
            return false;

        addRenderTargets();
        addASMRenderTargets();

        bool prev = gASMCpuSettings.mShowDebugTextures;
        gASMCpuSettings.mShowDebugTextures = true;
        SetupASMDebugTextures();
        gASMCpuSettings.mShowDebugTextures = prev;

        addPipelines();

        waitForAllResourceLoads();

        updateDescriptorSets();

        TFUserInterfaceLoadDesc uiLoad = {};
        uiLoad.mColorFormat = pSwapChain->ppRenderTargets[0]->mFormat;
        uiLoad.mHeight = mSettings.mHeight;
        uiLoad.mWidth = mSettings.mWidth;
        uiLoad.mVR2DLayer.mPosition = float3(gVR2DLayer.m2DLayerPosition.x, gVR2DLayer.m2DLayerPosition.y, gVR2DLayer.m2DLayerPosition.z);
        uiLoad.mVR2DLayer.mScale = gVR2DLayer.m2DLayerScale;
        loadUserInterface(&uiLoad);

        TFFontSystemLoadDesc fontLoad = {};
        fontLoad.mColorFormat = pSwapChain->ppRenderTargets[0]->mFormat;
        fontLoad.mHeight = mSettings.mHeight;
        fontLoad.mWidth = mSettings.mWidth;
        loadFontSystem(&fontLoad);

        return true;
    }

    void Unload(TFReloadDesc* pReloadDesc) override
    {
        UNREF_PARAM(pReloadDesc);
        waitQueueIdle(pGraphicsQueue);

        unloadFontSystem();
        unloadUserInterface();

        removePipelines();

        removeSwapChain(pRenderer, pSwapChain);

        unloadProfilerUI();

        TF_ESRAM_RESET_ALLOCS(pRenderer);
        removeRenderTargets();

        removeDescriptorSets();
        removeShaders();

        sShouldExitSDFGeneration = true;
    }

    void Update(float deltaTime) override
    {
        if (!uiIsFocused())
        {
            pCamera->onMove({ inputGetValue(0, CUSTOM_MOVE_X), inputGetValue(0, CUSTOM_MOVE_Y) });
            pCamera->onRotate({ inputGetValue(0, CUSTOM_LOOK_X), inputGetValue(0, CUSTOM_LOOK_Y) });
            pCamera->onMoveY(inputGetValue(0, CUSTOM_MOVE_UP));
            if (inputGetValue(0, CUSTOM_RESET_VIEW))
            {
                pCamera->resetView();
            }
            if (inputGetValue(0, CUSTOM_TOGGLE_FULLSCREEN))
            {
                toggleFullscreen(pWindow);
            }
            if (inputGetValue(0, CUSTOM_DUMP_PROFILE))
            {
                dumpProfileData(GetName());
            }
            if (inputGetValue(0, CUSTOM_EXIT))
            {
                requestShutdown();
            }
        }

        if (gASMCpuSettings.mSunCanMove && gCurrentShadowType == SHADOW_TYPE_ASM)
        {
            gLightCpuSettings.mTimeOfDay += deltaTime * ASM_SUN_SPEED;
            if (gLightCpuSettings.mTimeOfDay >= gLightCpuSettings.mTimeOfDaySunset)
            {
                gLightCpuSettings.mTimeOfDay = gLightCpuSettings.mTimeOfDaySunrise;
            }
        }

        if (gLightCpuSettings.mAutomaticSunMovement && gCurrentShadowType == SHADOW_TYPE_MESH_BAKED_SDF)
        {
            gLightCpuSettings.mTimeOfDay += deltaTime * gLightCpuSettings.mSunSpeed;
            if (gLightCpuSettings.mTimeOfDay >= gLightCpuSettings.mTimeOfDaySunset)
            {
                gLightCpuSettings.mTimeOfDay = gLightCpuSettings.mTimeOfDaySunrise;
            }
        }

        pCamera->update(deltaTime);

        GuiController::updateDynamicUI();

        if (gCurrentShadowType != gCameraUniformData.mShadowType)
        {
            waitQueueIdle(pGraphicsQueue);

            switch (gCurrentShadowType)
            {
            case SHADOW_TYPE_MSM:
                removeMsmRenderTargets();
                break;
            case SHADOW_TYPE_VSM:
                removeVsmRenderTargets();
                break;
            case SHADOW_TYPE_MESH_BAKED_SDF:
                removeSdfVolumeTextureAtlas();
                break;
            }

            switch (gCameraUniformData.mShadowType)
            {
            case SHADOW_TYPE_MSM:
                addMsmRenderTargets();
                break;
            case SHADOW_TYPE_VSM:
                addVsmRenderTargets();
                break;
            case SHADOW_TYPE_MESH_BAKED_SDF:
                addSdfVolumeTextureAtlas();
                break;
            default:
                break;
            }
            updateDescriptorSets();
        }

        gCurrentShadowType = gCameraUniformData.mShadowType;

        calculateCurSDFMeshesProgress();

        if (gCurrentShadowType == SHADOW_TYPE_MESH_BAKED_SDF)
        {
            initSDFVolumeTextureAtlasData();
        }

        sSDFGenerationFinished = sSDFProgressValue == sSDFProgressMaxValue;

        /************************************************************************/
        // Scene Update
        /************************************************************************/
        static float currentTime = 0.0f;

        currentTime += deltaTime * 1000.0f;

        if (gCurrentShadowType == SHADOW_TYPE_ASM && gASMCpuSettings.mSunCanMove)
        {
            asmCurrentTime += deltaTime * 1000.0f;
        }

        if (gCurrentShadowType == SHADOW_TYPE_ESM)
        {
            gCameraUniformData.mEsmControl = gEsmCpuSettings.mEsmControl;
        }

        Point3 lightSourcePos(10.f, 000.0f, 10.f);
        lightSourcePos[0] += (20.f);
        lightSourcePos[0] += (SAN_MIGUEL_OFFSETX);

        //
        /************************************************************************/
        // ASM Update - for shadow map
        /************************************************************************/

        if (gCurrentShadowType == SHADOW_TYPE_ASM)
        {
            float timeNormalized = (gLightCpuSettings.mTimeOfDay - gLightCpuSettings.mTimeOfDaySunrise) /
                                   (gLightCpuSettings.mTimeOfDaySunset - gLightCpuSettings.mTimeOfDaySunrise);

            // Direction from shadowmap camera to its pivot position
            float shadowPadding = (1.0f - gLightCpuSettings.mShadowRange) / 2.0f;
            float shadowRotationTime = lerp(shadowPadding, 1.0f - shadowPadding, timeNormalized);
            float sunRotationShadow = PI * shadowRotationTime;
            mat4  rotationShadow = mat4::rotationYX(gLightCpuSettings.mSunriseDirection, sunRotationShadow);
            vec3  newShadowDir = -(rotationShadow * vec4::zAxis()).getXYZ();

            mat4 newRotationShadow = mat4::rotationYX(gLightCpuSettings.mSunriseDirection + (PI / 2.0f), sunRotationShadow);
            vec3 shadowDirDest = -(newRotationShadow * vec4::zAxis()).getXYZ();

            float f = float((static_cast<uint32_t>(asmCurrentTime) >> 5) & 0xfff) / 8096.0f;
            vec3  asmShadowDir = normalize(lerp(newShadowDir, shadowDirDest, fabsf(f * 2.f)));

            uint32_t newDelta = static_cast<uint32_t>(deltaTime * 1000.f);
            uint32_t updateDeltaTime = 4500;
            uint32_t halfWayTime = static_cast<uint32_t>(asmCurrentTime) + (updateDeltaTime >> 1);

            float f_half = float((static_cast<uint32_t>(halfWayTime) >> 5) & 0xfff) / 8096.0f;
            vec3  halfWayShadowDir = normalize(lerp(newShadowDir, shadowDirDest, fabsf(f_half * 2.f)));

            pASM->Tick(gASMCpuSettings, pLightView, asmShadowDir, halfWayShadowDir, static_cast<uint32_t>(currentTime), newDelta, false,
                       false, updateDeltaTime);
        }

        if (gGaussianBlurSigma[1] != gGaussianBlurSigma[0])
        {
            gGaussianBlurSigma[1] = gGaussianBlurSigma[0];
            for (int i = 0; i < MAX_BLUR_KERNEL_SIZE; i++)
            {
                gBlurWeightsUniform.mBlurWeights[i] = gaussian((float)i, 0.0f, gGaussianBlurSigma[0]);
            }
        }

        updateUniformData();
    }

    void Draw() override
    {
        gFrameIndex = mSettings.mFrameIdx;

        if ((bool)pSwapChain->mEnableVsync != mSettings.mVSyncEnabled)
        {
            waitQueueIdle(pGraphicsQueue);
            ::toggleVSync(pRenderer, &pSwapChain);
        }

        uint32_t swapchainImageIndex;
        acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore[gFrameIndex], NULL, &swapchainImageIndex);

        TFRenderTarget*   pRenderTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];
        GpuCmdRingElement elem = getNextGpuCmdRingElement(&gGraphicsCmdRing, true, 1);

        // Stall if CPU is running "gDataBufferCount" frames ahead of GPU
        TFFenceStatus fenceStatus;
        getFenceStatus(pRenderer, elem.pFence, &fenceStatus);
        if (fenceStatus == TF_FENCE_STATUS_INCOMPLETE)
            waitForFences(pRenderer, 1, &elem.pFence);

#if TEST_GPU_BREADCRUMBS
        if (pRenderer->pGpu->mGpuMarkers)
        {
            // Check breadcrumb markers
            bool crashed = checkMarkers();
            if (crashed)
            {
                requestShutdown();
                return;
            }
        }
#endif

        resetCmdPool(pRenderer, elem.pCmdPool);

        if (gCurrentShadowType == SHADOW_TYPE_ASM)
        {
            updateASM();
        }
        else if (gCurrentShadowType == SHADOW_TYPE_ESM || gCurrentShadowType == SHADOW_TYPE_VSM || gCurrentShadowType == SHADOW_TYPE_MSM)
        {
            gPerFrameData[gFrameIndex].gEyeObjectSpace[VIEW_SHADOW] =
                (inverse(gCameraUniformData.mLight.mLightViewProj) * vec4(0.f, 0.f, 0.f, 1.f)).getXYZ();

            for (uint32_t viewIndex = 0; viewIndex < VR_MULTIVIEW_COUNT; ++viewIndex)
            {
                gVBConstants[gFrameIndex].transform[VIEW_SHADOW].mvp.mMatrices[viewIndex] =
                    gCameraUniformData.mLight.mLightViewProj * gMeshInfoData[0].mWorldMat;
            }
#ifdef QUEST_VR
            gVBConstants[gFrameIndex].cullingMVP[VIEW_SHADOW] =
                gVBConstants[gFrameIndex].transform[VIEW_SHADOW].mvp.mMatrices[MONO_CAMERA_VIEW_INDEX];
#endif
            gVBConstants[gFrameIndex].cullingViewports[VIEW_SHADOW].sampleCount = 1;

            vec2 windowSize = vec2(SHADOWMAP_RES, SHADOWMAP_RES);
            gVBConstants[gFrameIndex].cullingViewports[VIEW_SHADOW].windowSize = (windowSize);
            gVBConstants[gFrameIndex].numViewports = 2;
        }
        else if (gCurrentShadowType == SHADOW_TYPE_MESH_BAKED_SDF)
        {
            gPerFrameData[gFrameIndex].gEyeObjectSpace[VIEW_SHADOW] = gPerFrameData[gFrameIndex].gEyeObjectSpace[VIEW_CAMERA];

            gVBConstants[gFrameIndex].transform[VIEW_SHADOW].mvp = gVBConstants[gFrameIndex].transform[VIEW_CAMERA].mvp;
            gVBConstants[gFrameIndex].cullingViewports[VIEW_SHADOW] = gVBConstants[gFrameIndex].cullingViewports[VIEW_CAMERA];
#ifdef QUEST_VR
            gVBConstants[gFrameIndex].cullingMVP[VIEW_SHADOW] = gVBConstants[gFrameIndex].cullingMVP[VIEW_CAMERA];
#endif
            gVBConstants[gFrameIndex].numViewports = 2;
        }

        /************************************************************************/
        // Update uniform buffers
        /************************************************************************/
        for (uint32_t j = 0; j < MESH_COUNT; ++j)
        {
            TFBufferUpdateDesc viewProjCbv = { pBufferMeshTransforms[j][gFrameIndex] };
            beginUpdateResource(&viewProjCbv);
            memcpy(viewProjCbv.pMappedData, &gMeshInfoUniformData[j][gFrameIndex], sizeof(gMeshInfoUniformData[j][gFrameIndex]));
            endUpdateResource(&viewProjCbv);
        }

        TFBufferUpdateDesc cameraCbv = { pBufferCameraUniform[gFrameIndex] };
        beginUpdateResource(&cameraCbv);
        memcpy(cameraCbv.pMappedData, &gCameraUniformData, sizeof(gCameraUniformData));
        endUpdateResource(&cameraCbv);

        TFBufferUpdateDesc quadUniformCbv = { pBufferQuadUniform[gFrameIndex] };
        beginUpdateResource(&quadUniformCbv);
        memcpy(quadUniformCbv.pMappedData, &gQuadUniformData, sizeof(gQuadUniformData));
        endUpdateResource(&quadUniformCbv);

        TFBufferUpdateDesc updateVisibilityBufferConstantDesc = { pBufferVBConstants[gFrameIndex] };
        beginUpdateResource(&updateVisibilityBufferConstantDesc);
        memcpy(updateVisibilityBufferConstantDesc.pMappedData, &gVBConstants[gFrameIndex], sizeof(gVBConstants[gFrameIndex]));
        endUpdateResource(&updateVisibilityBufferConstantDesc);

        /************************************************************************/
        // Rendering
        /************************************************************************/
        // Get command list to store rendering commands for this frame
        {
            TFCmd* cmd = elem.pCmds[0];
            beginCmd(cmd);
            cmdBindDescriptorSet(cmd, 0, pDescriptorSetPersistent);
            cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorSetPerFrame);

#if TEST_GPU_BREADCRUMBS
            if (pRenderer->pGpu->mGpuMarkers)
            {
                // Reset task marker and update frame marker
                TFMarkerDesc marker = {};
                marker.pBuffer = pMarkerBuffer;

                marker.mOffset = MARKER_OFFSET(MARKER_FRAME_INDEX);
                marker.mValue = gFrameIndex;
                cmdWriteMarker(cmd, &marker);

                marker.mOffset = MARKER_OFFSET(MARKER_TASK_INDEX);
                marker.mValue = gMarkerInitialValue;
                cmdWriteMarker(cmd, &marker);
            }
#endif

            gCurrentGpuProfileToken = gCurrentGpuProfileTokens[gCurrentShadowType];

            cmdBeginGpuFrameProfile(cmd, gCurrentGpuProfileToken);

            // Triangle Filtering Pass
            if (!gAppSettings.mHoldFilteredTriangles)
            {
                TriangleFilteringPassDesc triangleFilteringDesc = {};
                triangleFilteringDesc.pPipelineClearBuffers = pPipelineClearBuffers;
                triangleFilteringDesc.pPipelineTriangleFiltering = pPipelineTriangleFiltering;

                triangleFilteringDesc.pDescriptorSetTriangleFilteringPerBatch = pDescriptorSetTriangleFiltering;

                triangleFilteringDesc.mFrameIndex = gFrameIndex;
                triangleFilteringDesc.mBuffersIndex = 0; // We don't use Async Compute for triangle filtering, we just have 1 buffer
                triangleFilteringDesc.mGpuProfileToken = gCurrentGpuProfileToken;
                triangleFilteringDesc.mVBPreFilterStats = gVBPreFilterStats[gFrameIndex];
                cmdVBTriangleFilteringPass(pVisibilityBuffer, cmd, &triangleFilteringDesc);
            }

            TFRenderTargetBarrier barriers[19] = {};
            uint32_t              barrierCount = 0;

            // Resource Transition
            {
                const uint32_t  numBarriers = NUM_CULLING_VIEWPORTS + 2;
                TFBufferBarrier triangleFilteringOutputBarriers[numBarriers] = {};
                uint32_t        triangleFilteringOutputBarrierCount = 0;
                triangleFilteringOutputBarriers[triangleFilteringOutputBarrierCount++] = { pVisibilityBuffer->ppIndirectDrawArgBuffer[0],
                                                                                           TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                                                           TF_RESOURCE_STATE_INDIRECT_ARGUMENT |
                                                                                               TF_RESOURCE_STATE_SHADER_RESOURCE };
                triangleFilteringOutputBarriers[triangleFilteringOutputBarrierCount++] = {
                    pVisibilityBuffer->ppIndirectDataBuffer[gFrameIndex], TF_RESOURCE_STATE_UNORDERED_ACCESS,
                    TF_RESOURCE_STATE_SHADER_RESOURCE
                };
                for (uint32_t i = 0; i < NUM_CULLING_VIEWPORTS; ++i)
                {
                    triangleFilteringOutputBarriers[triangleFilteringOutputBarrierCount++] = { pVisibilityBuffer->ppFilteredIndexBuffer[i],
                                                                                               TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                                                               TF_RESOURCE_STATE_INDEX_BUFFER |
                                                                                                   TF_RESOURCE_STATE_SHADER_RESOURCE };
                }

                barriers[barrierCount++] = { pRenderTargetShadowMap, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_DEPTH_WRITE };

                if (gCurrentShadowType == SHADOW_TYPE_ASM)
                {
                    barriers[barrierCount++] = { pRenderTargetASMDEMAtlas, TF_RESOURCE_STATE_SHADER_RESOURCE,
                                                 TF_RESOURCE_STATE_RENDER_TARGET };
                    for (int32_t i = 0; i <= gs_ASMMaxRefinement; ++i)
                    {
                        barriers[barrierCount++] = { pASM->m_longRangeShadows->m_indirectionTexturesMips[i],
                                                     TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET };
                        if (pASM->m_longRangePreRender->IsValid())
                        {
                            barriers[barrierCount++] = { pASM->m_longRangePreRender->m_indirectionTexturesMips[i],
                                                         TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET };
                        }
                    }
                }

                cmdResourceBarrier(cmd, triangleFilteringOutputBarrierCount, triangleFilteringOutputBarriers, 0, NULL, barrierCount,
                                   barriers);
                barrierCount = 0;
            }

            if (gCurrentShadowType == SHADOW_TYPE_ASM)
            {
                // ASM Shadow Pass
                {
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Draw ASM");
                    drawASMPass(cmd);
                    cmdBindRenderTargets(cmd, NULL);
                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }
            }
            else if (gCurrentShadowType == SHADOW_TYPE_ESM)
            {
                // Shadow Map Pass
                {
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Shadow Map Pass");

                    // Buffer Update
                    {
                        TFBufferUpdateDesc bufferUpdate = { pBufferMeshShadowProjectionTransforms[0][0][gFrameIndex] };
                        beginUpdateResource(&bufferUpdate);
                        memcpy(bufferUpdate.pMappedData, &gMeshASMProjectionInfoUniformData[0][gFrameIndex],
                               sizeof(gMeshASMProjectionInfoUniformData[0][gFrameIndex]));
                        endUpdateResource(&bufferUpdate);
                    }
                    // Bind Render Targets
                    {
                        TFBindRenderTargetsDesc bindRenderTargets = {};
                        bindRenderTargets.mRenderTargetCount = 0;
                        bindRenderTargets.mDepthStencil = { pRenderTargetShadowMap, TF_LOAD_ACTION_CLEAR };
                        cmdBindRenderTargets(cmd, &bindRenderTargets);
                        cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTargetShadowMap->mWidth, (float)pRenderTargetShadowMap->mHeight, 0.0f,
                                       1.0f);
                        cmdSetScissor(cmd, 0, 0, pRenderTargetShadowMap->mWidth, pRenderTargetShadowMap->mHeight);
                    }
                    // Draw
                    {
                        cmdBindIndexBuffer(cmd, pVisibilityBuffer->ppFilteredIndexBuffer[VIEW_SHADOW], TF_INDEX_TYPE_UINT32, 0);

                        TFPipeline* pPipelines[] = { pPipelineESMIndirectDepthPass, pPipelineESMIndirectAlphaDepthPass };
                        COMPILE_ASSERT(TF_ARRAY_COUNT(pPipelines) == NUM_GEOMETRY_SETS);

                        for (uint32_t i = 0; i < NUM_GEOMETRY_SETS; ++i)
                        {
                            cmdBindPipeline(cmd, pPipelines[i]);
                            cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorSetDepthVBPass);
                            uint64_t  indirectBufferByteOffset = GET_INDIRECT_DRAW_ELEM_INDEX(VIEW_SHADOW, i, 0) * sizeof(uint32_t);
                            TFBuffer* pIndirectDrawBuffer = pVisibilityBuffer->ppIndirectDrawArgBuffer[0];
                            cmdExecuteIndirect(cmd, TF_INDIRECT_DRAW_INDEX, 1, pIndirectDrawBuffer, indirectBufferByteOffset, NULL, 0);
                        }
                    }

                    cmdBindRenderTargets(cmd, NULL);
                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }
            }
            else if (gCurrentShadowType == SHADOW_TYPE_VSM || gCurrentShadowType == SHADOW_TYPE_MSM)
            {
                // Shadow Map Pass
                {
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Shadow Map Pass");

                    TFRenderTarget* pRenderTargetShad = gCurrentShadowType == SHADOW_TYPE_VSM ? pRenderTargetVSM[0] : pRenderTargetMSM[0];
                    TFPipeline*     pPipelineDepthPass =
                        gCurrentShadowType == SHADOW_TYPE_VSM ? pPipelineVSMIndirectDepthPass : pPipelineMSMIndirectDepthPass;
                    TFPipeline* pPipelineAlphaDepthPass =
                        gCurrentShadowType == SHADOW_TYPE_VSM ? pPipelineVSMIndirectAlphaDepthPass : pPipelineMSMIndirectAlphaDepthPass;

                    // Buffer Update
                    {
                        TFBufferUpdateDesc bufferUpdate = { pBufferMeshShadowProjectionTransforms[0][0][gFrameIndex] };
                        beginUpdateResource(&bufferUpdate);
                        memcpy(bufferUpdate.pMappedData, &gMeshASMProjectionInfoUniformData[0][gFrameIndex],
                               sizeof(gMeshASMProjectionInfoUniformData[0][gFrameIndex]));
                        endUpdateResource(&bufferUpdate);
                    }
                    // Resource Transition
                    {
                        TFRenderTargetBarrier barrier = { pRenderTargetShad, TF_RESOURCE_STATE_SHADER_RESOURCE,
                                                          TF_RESOURCE_STATE_RENDER_TARGET };
                        cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, &barrier);
                    }
                    // Bind Render Targets
                    {
                        TFBindRenderTargetsDesc bindRenderTargets = {};
                        bindRenderTargets.mRenderTargetCount = 1;
                        bindRenderTargets.mRenderTargets[0] = { pRenderTargetShad, TF_LOAD_ACTION_CLEAR };
                        bindRenderTargets.mDepthStencil = { pRenderTargetShadowMap, TF_LOAD_ACTION_CLEAR };
                        cmdBindRenderTargets(cmd, &bindRenderTargets);
                        cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTargetShadowMap->mWidth, (float)pRenderTargetShadowMap->mHeight, 0.0f,
                                       1.0f);
                        cmdSetScissor(cmd, 0, 0, pRenderTargetShadowMap->mWidth, pRenderTargetShadowMap->mHeight);
                    }
                    // Draw
                    {
                        cmdBindIndexBuffer(cmd, pVisibilityBuffer->ppFilteredIndexBuffer[VIEW_SHADOW], TF_INDEX_TYPE_UINT32, 0);

                        TFPipeline* pPipelines[] = { pPipelineDepthPass, pPipelineAlphaDepthPass };
                        COMPILE_ASSERT(TF_ARRAY_COUNT(pPipelines) == NUM_GEOMETRY_SETS);

                        for (uint32_t i = 0; i < NUM_GEOMETRY_SETS; ++i)
                        {
                            cmdBindPipeline(cmd, pPipelines[i]);
                            cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorSetDepthVBPass);
                            uint64_t  indirectBufferByteOffset = GET_INDIRECT_DRAW_ELEM_INDEX(VIEW_SHADOW, i, 0) * sizeof(uint32_t);
                            TFBuffer* pIndirectDrawBuffer = pVisibilityBuffer->ppIndirectDrawArgBuffer[0];
                            cmdExecuteIndirect(cmd, TF_INDIRECT_DRAW_INDEX, 1, pIndirectDrawBuffer, indirectBufferByteOffset, NULL, 0);
                        }
                        cmdBindRenderTargets(cmd, NULL);
                    }
                    // Resource Transition
                    {
                        TFRenderTargetBarrier barrier = { pRenderTargetShad, TF_RESOURCE_STATE_RENDER_TARGET,
                                                          TF_RESOURCE_STATE_SHADER_RESOURCE };
                        cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, &barrier);
                    }

                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }
                // Shadow Map Blur Pass
                {
                    TFRenderTarget** pRenderTargets = (gCurrentShadowType == SHADOW_TYPE_VSM ? pRenderTargetVSM : pRenderTargetMSM);
                    TFDescriptorSet* pDescriptorSet =
                        gCurrentShadowType == SHADOW_TYPE_VSM ? pDescriptorSetGaussianBlurVSMPerDraw : pDescriptorSetGaussianBlurMSMPerDraw;

                    const uint32_t threadGroupSizeX = (pRenderTargets[0]->mWidth + 15) / 16;
                    const uint32_t threadGroupSizeY = (pRenderTargets[0]->mHeight + 15) / 16;

                    // Horizontal pass
                    {
                        cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Horizontal Blur Pass");

                        // Buffer Update
                        {
                            TFBufferUpdateDesc bufferUpdate = { pBufferBlurWeights };
                            beginUpdateResource(&bufferUpdate);
                            memcpy(bufferUpdate.pMappedData, &gBlurWeightsUniform, sizeof(gBlurWeightsUniform));
                            endUpdateResource(&bufferUpdate);
                        }
                        // Resource Transition
                        {
                            TFRenderTargetBarrier rt[2];
                            rt[0] = { pRenderTargets[0], TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_UNORDERED_ACCESS };
                            rt[1] = { pRenderTargets[1], TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_UNORDERED_ACCESS };
                            cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 2, rt);
                        }
                        // Dispatch
                        {
                            gBlurConstantsData.mBlurPassType = BLUR_PASS_TYPE_HORIZONTAL;

                            cmdBindPipeline(cmd, pPipelineBlur);
                            cmdBindDescriptorSet(cmd, 0, pDescriptorSet);

                            TFBufferUpdateDesc blurConstantsUpdateHorizontal = { pBufferGaussianBlurConstants[gFrameIndex][0] };
                            beginUpdateResource(&blurConstantsUpdateHorizontal);
                            memcpy(blurConstantsUpdateHorizontal.pMappedData, &gBlurConstantsData, sizeof(gBlurConstantsData));
                            endUpdateResource(&blurConstantsUpdateHorizontal);

                            cmdBindDescriptorSet(cmd, gFrameIndex * 2, pDescriptorSetGaussianBlurPerBatch);
                            cmdDispatch(cmd, threadGroupSizeX, threadGroupSizeY, 1);
                        }

                        cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                    }
                    // Vertical pass
                    {
                        cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Vertical Blur Pass");

                        // Resource Transition
                        {
                            TFRenderTargetBarrier rt[2];
                            rt[0] = { pRenderTargets[0], TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS };
                            rt[1] = { pRenderTargets[1], TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS };
                            cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 2, rt);
                        }
                        // Dispatch
                        {
                            gBlurConstantsData.mBlurPassType = BLUR_PASS_TYPE_VERTICAL;

                            TFBufferUpdateDesc blurConstantsUpdateVetical = { pBufferGaussianBlurConstants[gFrameIndex][1] };
                            beginUpdateResource(&blurConstantsUpdateVetical);
                            memcpy(blurConstantsUpdateVetical.pMappedData, &gBlurConstantsData, sizeof(gBlurConstantsData));
                            endUpdateResource(&blurConstantsUpdateVetical);
                            cmdBindDescriptorSet(cmd, gFrameIndex * 2 + 1, pDescriptorSetGaussianBlurPerBatch);
                            cmdDispatch(cmd, threadGroupSizeX, threadGroupSizeY, 1);
                        }
                        // Resource Transition
                        {
                            TFRenderTargetBarrier rt[2];
                            rt[0] = { pRenderTargets[0], TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_SHADER_RESOURCE };
                            rt[1] = { pRenderTargets[1], TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_SHADER_RESOURCE };
                            cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 2, rt);
                        }

                        cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                    }
                }
            }

            // Resource Transition
            {
                barriers[barrierCount++] = { pRenderTargetShadowMap, TF_RESOURCE_STATE_DEPTH_WRITE, TF_RESOURCE_STATE_SHADER_RESOURCE };
                // Draw To Screen
                barriers[barrierCount++] = { pRenderTargetIntermediate, TF_RESOURCE_STATE_SHADER_RESOURCE,
                                             TF_RESOURCE_STATE_RENDER_TARGET };

                if (gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1)
                {
                    barriers[barrierCount++] = { pRenderTargetMSAA, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                 TF_RESOURCE_STATE_RENDER_TARGET };
                }

                if (gCurrentShadowType == SHADOW_TYPE_ASM)
                {
                    for (int32_t i = 0; i <= gs_ASMMaxRefinement; ++i)
                    {
                        barriers[barrierCount++] = { pASM->m_longRangeShadows->m_indirectionTexturesMips[i],
                                                     TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_SHADER_RESOURCE };
                        if (pASM->m_longRangePreRender->IsValid())
                        {
                            barriers[barrierCount++] = { pASM->m_longRangePreRender->m_indirectionTexturesMips[i],
                                                         TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_SHADER_RESOURCE };
                        }
                    }

                    if (pASM->m_longRangePreRender->IsValid())
                    {
                        barriers[barrierCount++] = { pASM->m_longRangePreRender->m_lodClampTexture, TF_RESOURCE_STATE_RENDER_TARGET,
                                                     TF_RESOURCE_STATE_SHADER_RESOURCE };
                    }
                    barriers[barrierCount++] = { pRenderTargetASMDEMAtlas, TF_RESOURCE_STATE_RENDER_TARGET,
                                                 TF_RESOURCE_STATE_SHADER_RESOURCE };
                }

                cmdResourceBarrier(cmd, 0, NULL, 0, NULL, barrierCount, barriers);
            }

            // SDF Update
            if (gCurrentShadowType == SHADOW_TYPE_MESH_BAKED_SDF)
            {
                SDFVolumeTextureNode* volumeTextureNode = pSDFVolumeTextureAtlas->ProcessQueuedNode();

                // Resource Transition
                {
                    TFTextureBarrier uavBarriers[] = { { pTextureSDFVolumeAtlas, TF_RESOURCE_STATE_SHADER_RESOURCE,
                                                         TF_RESOURCE_STATE_UNORDERED_ACCESS } };
                    cmdResourceBarrier(cmd, 0, NULL, 1, uavBarriers, 0, NULL);
                }
                // Update Buffers
                {
                    TFBufferUpdateDesc sdfMeshConstantsUniformCbv = { pBufferMeshSDFConstants[gFrameIndex] };
                    beginUpdateResource(&sdfMeshConstantsUniformCbv);
                    memcpy(sdfMeshConstantsUniformCbv.pMappedData, &gMeshSDFConstants, sizeof(gMeshSDFConstants));
                    endUpdateResource(&sdfMeshConstantsUniformCbv);
                }

                if (volumeTextureNode)
                {
                    // SDF Volume Texture Atlas Update Pass
                    {
                        cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "SDF Volume Texture Atlas Update Pass");

                        // Update Buffers
                        {
                            TFBufferUpdateDesc updateDesc = { pBufferSDFVolumeData[gFrameIndex] };
                            updateDesc.mCurrentState = TF_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                            beginUpdateResource(&updateDesc);
                            memcpy(updateDesc.pMappedData, volumeTextureNode->mSDFVolumeData->mSDFVolumeList,
                                   arrlen(volumeTextureNode->mSDFVolumeData->mSDFVolumeList) * sizeof(float));
                            endUpdateResource(&updateDesc);

                            gUpdateSDFVolumeTextureAtlasConstants.mSourceAtlasVolumeMinCoord =
                                ivec4(volumeTextureNode->mAtlasAllocationCoord);
                            gUpdateSDFVolumeTextureAtlasConstants.mSourceDimensionSize =
                                ivec4(volumeTextureNode->mSDFVolumeData->mSDFVolumeSize);
                            gUpdateSDFVolumeTextureAtlasConstants.mSourceAtlasVolumeMaxCoord = ivec4(
                                volumeTextureNode->mAtlasAllocationCoord + (volumeTextureNode->mSDFVolumeData->mSDFVolumeSize - ivec3(1)));

                            TFBufferUpdateDesc meshSDFConstantUpdate = { pBufferUpdateSDFVolumeTextureAtlasConstants[gFrameIndex] };

                            beginUpdateResource(&meshSDFConstantUpdate);
                            memcpy(meshSDFConstantUpdate.pMappedData, &gUpdateSDFVolumeTextureAtlasConstants,
                                   sizeof(gUpdateSDFVolumeTextureAtlasConstants));
                            endUpdateResource(&meshSDFConstantUpdate);
                        }
                        // Dispatch
                        {
                            cmdBindPipeline(cmd, pPipelineUpdateSDFVolumeTextureAtlas);
                            cmdBindDescriptorSet(cmd, 0, pDescriptorSetUpdateRegion3DTexture);

                            uint32_t* threadGroup = pShaderUpdateSDFVolumeTextureAtlas->mNumThreadsPerGroup;

                            cmdDispatch(cmd, SDF_VOLUME_TEXTURE_ATLAS_WIDTH / threadGroup[0],
                                        SDF_VOLUME_TEXTURE_ATLAS_HEIGHT / threadGroup[1], SDF_VOLUME_TEXTURE_ATLAS_DEPTH / threadGroup[2]);
                        }

                        cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                    }

                    updateMeshSDFConstants();

                    gBufferUpdateSDFMeshConstantFlags[0] = true;
                    gBufferUpdateSDFMeshConstantFlags[1] = true;
                    gBufferUpdateSDFMeshConstantFlags[2] = true;
                }
            }

            // Visibility Buffer Pass
            {
                cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Visibility Buffer Pass");

                // Resource Transition
                {
                    TFRenderTargetBarrier barriersVB[] = { { pRenderTargetVBPass, TF_RESOURCE_STATE_SHADER_RESOURCE,
                                                             TF_RESOURCE_STATE_RENDER_TARGET } };
                    cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriersVB);
                }

#if TEST_GPU_BREADCRUMBS
                if (pRenderer->pGpu->mGpuMarkers)
                {
                    TFMarkerDesc marker = {};
                    marker.mOffset = MARKER_OFFSET(MARKER_TASK_INDEX);
                    marker.mValue = RENDERING_STEP_DRAW_VB_PASS;
                    marker.pBuffer = pMarkerBuffer;
                    cmdWriteMarker(cmd, &marker);
                }
#endif
                // Bind Render Targets
                {
                    TFBindRenderTargetsDesc bindRenderTargets = {};
                    bindRenderTargets.mRenderTargetCount = 1;
                    bindRenderTargets.mRenderTargets[0] = { pRenderTargetVBPass, TF_LOAD_ACTION_CLEAR };
                    bindRenderTargets.mDepthStencil = { pRenderTargetDepth, TF_LOAD_ACTION_CLEAR };
                    cmdBindRenderTargets(cmd, &bindRenderTargets);
                    cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTargetVBPass->mWidth, (float)pRenderTargetVBPass->mHeight, 0.0f, 1.0f);
                    cmdSetScissor(cmd, 0, 0, pRenderTargetVBPass->mWidth, pRenderTargetVBPass->mHeight);
                }
                // Draw
                {
                    TFBuffer* pIndexBuffer = pVisibilityBuffer->ppFilteredIndexBuffer[VIEW_CAMERA];
                    cmdBindIndexBuffer(cmd, pIndexBuffer, TF_INDEX_TYPE_UINT32, 0);

                    const char* profileNames[gNumGeomSets] = { "VB pass Opaque", "VB pass Alpha" };
                    for (uint32_t i = 0; i < NUM_GEOMETRY_SETS; ++i)
                    {
                        cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, profileNames[i]);
                        TFPipeline* pipeline = pPipelineVBBufferPass[i];
#if TEST_GPU_BREADCRUMBS
                        // Using the malfunctioned pipeline
                        if (pRenderer->pGpu->mGpuMarkers && bCrashedSteps[RENDERING_STEP_DRAW_VB_PASS])
                        {
                            bCrashedSteps[RENDERING_STEP_DRAW_VB_PASS] = false;
                            bHasCrashed = true;
                            pipeline = pPipelineVBBufferCrashPass;
                            LOGF(LogLevel::eERROR, "[Breadcrumb] Simulating a GPU crash situation (DRAW VISIBILITY BUFFER)...");
                        }
#endif
                        cmdBindPipeline(cmd, pipeline);
                        cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorSetVBPass);

                        uint64_t  indirectBufferByteOffset = GET_INDIRECT_DRAW_ELEM_INDEX(VIEW_CAMERA, i, 0) * sizeof(uint32_t);
                        TFBuffer* pIndirectDrawBuffer = pVisibilityBuffer->ppIndirectDrawArgBuffer[0];
                        if (gUsingPrimitiveIDFallback)
                        {
                            cmdExecuteIndirect(cmd, TF_INDIRECT_DRAW, 1, pIndirectDrawBuffer, indirectBufferByteOffset, NULL, 0);
                        }
                        else
                        {
                            cmdExecuteIndirect(cmd, TF_INDIRECT_DRAW_INDEX, 1, pIndirectDrawBuffer, indirectBufferByteOffset, NULL, 0);
                        }

                        cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                    }
                }

                cmdBindRenderTargets(cmd, NULL);
                cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
            }

            // MSAA Edges Stencil Pass
            if (gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1)
            {
                // This depth-only pass will render to the stencil buffer. Samples that must be shaded in
                // future shading or post-processing passes will be set to 0x01.
                cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "MSAA Edges Stencil Pass");

                // Resource Transition
                {
                    TFRenderTargetBarrier barriersMSAA[] = {
                        { pRenderTargetMSAAEdges, TF_RESOURCE_STATE_DEPTH_READ, TF_RESOURCE_STATE_DEPTH_WRITE },
                        { pRenderTargetDepth, TF_RESOURCE_STATE_DEPTH_WRITE, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE },
                    };
                    cmdResourceBarrier(cmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(barriersMSAA), barriersMSAA);
                }
                // Bind Render Targets
                {
                    TFBindRenderTargetsDesc bindRenderTargets = {};
                    bindRenderTargets.mDepthStencil = { pRenderTargetMSAAEdges, TF_LOAD_ACTION_DONTCARE, TF_STORE_ACTION_NONE,
                                                        TF_LOAD_ACTION_CLEAR, TF_STORE_ACTION_STORE };
                    cmdBindRenderTargets(cmd, &bindRenderTargets);
                    cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTargetMSAAEdges->mWidth, (float)pRenderTargetMSAAEdges->mHeight, 0.0f,
                                   1.0f);
                    cmdSetScissor(cmd, 0, 0, pRenderTargetMSAAEdges->mWidth, pRenderTargetMSAAEdges->mHeight);
                }
                // Draw
                {
                    cmdBindPipeline(cmd, pPipelineDrawMSAAEdges);
                    cmdBindDescriptorSet(cmd, 0, pDescriptorSetPersistent);
                    cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorSetPerFrame);
                    cmdSetStencilReferenceValue(cmd, MSAA_STENCIL_MASK);
                    cmdDraw(cmd, 3, 0);
                    cmdBindRenderTargets(cmd, NULL);
                }
                // Resource Transition
                {
                    TFRenderTargetBarrier barriersMSAA[] = {
                        { pRenderTargetMSAAEdges, TF_RESOURCE_STATE_DEPTH_WRITE, TF_RESOURCE_STATE_DEPTH_READ },
                    };
                    cmdResourceBarrier(cmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(barriersMSAA), barriersMSAA);
                }

                cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
            }

            // Screen Space Shadows Pass
            if (gCameraUniformData.mSSSEnabled)
            {
                cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Screen Space Shadows Pass");

                for (uint32_t viewIndex = 0; viewIndex < VR_MULTIVIEW_COUNT; ++viewIndex)
                {
                    DispatchParams dispatchList[8] = {};
                    int            dispatchCount = updateSSSParams(viewIndex, dispatchList);

                    // Clear Screen Space Shadows pass
                    {
                        char pClearTimestampQueryString[64];
                        snprintf(pClearTimestampQueryString, 64, "Clear Screen Space Shadows (%u)", viewIndex);
                        cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, pClearTimestampQueryString);

                        // Resource Transition
                        {
                            TFResourceState       previousDepthState = gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1
                                                                           ? TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                                                                           : TF_RESOURCE_STATE_DEPTH_WRITE;
                            TFRenderTargetBarrier rtb = { pRenderTargetDepth, previousDepthState, TF_RESOURCE_STATE_SHADER_RESOURCE };
                            if (gSupportTextureAtomics)
                            {
                                TFTextureBarrier tb = { pTextureSSS, TF_RESOURCE_STATE_SHADER_RESOURCE,
                                                        TF_RESOURCE_STATE_UNORDERED_ACCESS };
                                cmdResourceBarrier(cmd, 0, NULL, 1, &tb, 1, &rtb);
                            }
                            else
                            {
                                TFBufferBarrier bb = { pBufferSSS, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_UNORDERED_ACCESS };
                                cmdResourceBarrier(cmd, 1, &bb, 0, NULL, 1, &rtb);
                            }
                        }
                        // Dispatch
                        {
                            cmdBindPipeline(cmd, pPipelineSSSClear);
                            cmdBindDescriptorSet(cmd, viewIndex * gDataBufferCount + gFrameIndex, pDescriptorSetSSSPerBatch);

                            uint32_t dispatchSizeX = (gSceneRes.mWidth + 7) / 8;
                            uint32_t dispatchSizeY = (gSceneRes.mHeight + 7) / 8;
                            cmdDispatch(cmd, dispatchSizeX, dispatchSizeY, 1);
                        }
                        // Resource Transition
                        {
                            if (gSupportTextureAtomics)
                            {
                                TFTextureBarrier tb = { pTextureSSS, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                        TF_RESOURCE_STATE_UNORDERED_ACCESS };
                                cmdResourceBarrier(cmd, 0, NULL, 1, &tb, 0, NULL);
                            }
                            else
                            {
                                TFBufferBarrier bb = { pBufferSSS, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS };
                                cmdResourceBarrier(cmd, 1, &bb, 0, NULL, 0, NULL);
                            }
                        }

                        cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                    }

                    // Compute Screen Space Shadows pass
                    {
                        char pComputeTimestampQueryString[64];
                        snprintf(pComputeTimestampQueryString, 64, "Compute Screen Space Shadows (%u)", viewIndex);
                        cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, pComputeTimestampQueryString);

                        // Dispatch
                        {
                            TFPipeline* pipeline = pPipelineSSS;

#if TEST_GPU_BREADCRUMBS
                            if (pRenderer->pGpu->mGpuMarkers)
                            {
                                TFMarkerDesc marker = {};
                                marker.mOffset = MARKER_OFFSET(MARKER_TASK_INDEX);
                                marker.mValue = RENDERING_STEP_SS_SHADOWS_PASS;
                                marker.pBuffer = pMarkerBuffer;
                                cmdWriteMarker(cmd, &marker);
                            }

                            // Using the malfunctioned pipeline
                            if (pRenderer->pGpu->mGpuMarkers && bCrashedSteps[RENDERING_STEP_SS_SHADOWS_PASS])
                            {
                                bCrashedSteps[RENDERING_STEP_SS_SHADOWS_PASS] = false;
                                bHasCrashed = true;
                                pipeline = pPipelineSSSCrash;
                                LOGF(LogLevel::eERROR, "[Breadcrumb] Simulating a GPU crash situation (COMPUTE SCREEN SPACE SHADOWS)...");
                            }
#endif
                            cmdBindPipeline(cmd, pipeline);
                            cmdBindDescriptorSet(cmd, viewIndex * gDataBufferCount + gFrameIndex, pDescriptorSetSSSPerBatch);

                            // Dispatch the compute shader
                            for (int i = 0; i < dispatchCount; i++)
                            {
                                // Update Buffers
                                TFBufferUpdateDesc wwaveOffsetsBufferUpdate = { pBufferSSSWaveOffsets[i][gFrameIndex] };
                                beginUpdateResource(&wwaveOffsetsBufferUpdate);
                                memcpy(wwaveOffsetsBufferUpdate.pMappedData, dispatchList[i].WaveOffset, sizeof(uint2));
                                endUpdateResource(&wwaveOffsetsBufferUpdate);

                                cmdBindDescriptorSet(cmd, gFrameIndex * MAX_SSS_WAVE_OFFSETS + i, pDescriptorSetSSSPerDraw);
                                cmdDispatch(cmd, dispatchList[i].WaveCount[0], dispatchList[i].WaveCount[1], dispatchList[i].WaveCount[2]);
                            }
                        }
                        // Resource Transition
                        {
                            if (gSupportTextureAtomics)
                            {
                                TFTextureBarrier tb = { pTextureSSS, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                        TF_RESOURCE_STATE_SHADER_RESOURCE };
                                cmdResourceBarrier(cmd, 0, NULL, 1, &tb, 0, NULL);
                            }
                            else
                            {
                                TFBufferBarrier bb = { pBufferSSS, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_SHADER_RESOURCE };
                                cmdResourceBarrier(cmd, 1, &bb, 0, NULL, 0, NULL);
                            }
                        }

                        cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                    }
                }
                cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
            }

            if (gCurrentShadowType == SHADOW_TYPE_MESH_BAKED_SDF)
            {
                // Use the node processed during the SDF update pass
                SDFVolumeTextureNode* volumeTextureNode = pSDFVolumeTextureAtlas->GetLastProcessedNode();
                if (volumeTextureNode || gBufferUpdateSDFMeshConstantFlags[gFrameIndex])
                {
                    if (!volumeTextureNode)
                    {
                        gBufferUpdateSDFMeshConstantFlags[gFrameIndex] = false;
                    }
                }

                TFTextureBarrier textureBarriers[2];
                // Resource Transition
                {
                    TFResourceState previousDepthState = TF_RESOURCE_STATE_DEPTH_WRITE;
                    if (gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1)
                    {
                        previousDepthState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                    }

                    barriers[0] = { pRenderTargetDepth, previousDepthState, TF_RESOURCE_STATE_SHADER_RESOURCE };
                    textureBarriers[0] = { pTextureSDFVolumeAtlas, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_SHADER_RESOURCE };
                }

                // SDF Mesh Visualization pass
                if (gBakedSDFMeshSettings.mDrawSDFMeshVisualization)
                {
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "SDF Mesh Visualization Pass");

                    // Resource Transition
                    {
                        barriers[1] = { pRenderTargetSDFMeshVisualization, TF_RESOURCE_STATE_SHADER_RESOURCE,
                                        TF_RESOURCE_STATE_UNORDERED_ACCESS };
                        // If we rendered screen space shadows the depth RT is already in the proper state
                        if (gCameraUniformData.mSSSEnabled)
                        {
                            cmdResourceBarrier(cmd, 0, NULL, 1, textureBarriers, 1, &barriers[1]);
                        }
                        else
                        {
                            cmdResourceBarrier(cmd, 0, NULL, 1, textureBarriers, 2, barriers);
                        }
                    }
                    // Dispatch
                    {
                        for (uint32_t viewIndex = 0; viewIndex < VR_MULTIVIEW_COUNT; ++viewIndex)
                        {
                            cmdBindPipeline(cmd, pPipelineSDFMeshVisualization);
                            cmdBindDescriptorSet(cmd, 0, pDescriptorSetBakedSDFMeshShadow);

#ifdef QUEST_VR
                            const uint32_t bufferIndex = gFrameIndex * VR_MULTIVIEW_COUNT + viewIndex;
                            cmdBindDescriptorSet(cmd, bufferIndex, pDescriptorSetSDFShadowPerDraw);
#endif

                            cmdDispatch(cmd,
                                        (uint32_t)ceil((float)(pRenderTargetSDFMeshVisualization->mWidth) /
                                                       (float)(SDF_MESH_VISUALIZATION_THREAD_X)),
                                        (uint32_t)ceil((float)(pRenderTargetSDFMeshVisualization->mHeight) /
                                                       (float)(SDF_MESH_VISUALIZATION_THREAD_Y)),
                                        1);
                        }
                    }
                    // Resource Transition
                    {
                        barriers[1] = { pRenderTargetSDFMeshVisualization, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                        TF_RESOURCE_STATE_SHADER_RESOURCE };
                        cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, &barriers[1]);
                    }

                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }
                // SDF Shadow pass
                else
                {
                    // SDF Mesh Shadow Pass
                    {
                        cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "SDF Mesh Shadow Pass");

                        // Resource Transition
                        {
                            textureBarriers[1] = { pTextureSDFMeshShadow, TF_RESOURCE_STATE_SHADER_RESOURCE,
                                                   TF_RESOURCE_STATE_UNORDERED_ACCESS };
                            // If we rendered screen space shadows the depth RT is already in the proper state
                            uint32_t rtBarrierCount = gCameraUniformData.mSSSEnabled ? 0 : 1;
                            cmdResourceBarrier(cmd, 0, NULL, 2, textureBarriers, rtBarrierCount, barriers);
                        }
                        // Dispatch
                        {
                            for (uint32_t viewIndex = 0; viewIndex < VR_MULTIVIEW_COUNT; ++viewIndex)
                            {
                                cmdBindPipeline(cmd, pPipelineSDFMeshShadow);
                                cmdBindDescriptorSet(cmd, 0, pDescriptorSetBakedSDFMeshShadow);

#ifdef QUEST_VR
                                const uint32_t bufferIndex = gFrameIndex * VR_MULTIVIEW_COUNT + viewIndex;
                                cmdBindDescriptorSet(cmd, bufferIndex, pDescriptorSetSDFShadowPerDraw);
#endif

                                cmdDispatch(cmd, (uint32_t)ceil((float)(pTextureSDFMeshShadow->mWidth) / (float)(SDF_MESH_SHADOW_THREAD_X)),
                                            (uint32_t)ceil((float)(pTextureSDFMeshShadow->mHeight) / (float)(SDF_MESH_SHADOW_THREAD_Y)),
                                            pTextureSDFMeshShadow->mArraySizeMinusOne + 1);
                            }
                        }
                        // Resource Transition
                        {
                            textureBarriers[1] = { pTextureSDFMeshShadow, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                   TF_RESOURCE_STATE_SHADER_RESOURCE };
                            cmdResourceBarrier(cmd, 0, NULL, 1, &textureBarriers[1], 0, NULL);
                        }

                        cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                    }

#if ENABLE_SDF_SHADOW_DOWNSAMPLE
                    // Upsample SDF Shadow Pass
                    {
                        cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Upsample SDF Shadow Pass");

                        // Resource Transition
                        {
                            TFRenderTargetBarrier rtBarriers[] = { { pRenderTargetUpSampleSDFShadow, TF_RESOURCE_STATE_SHADER_RESOURCE,
                                                                     TF_RESOURCE_STATE_RENDER_TARGET } };
                            cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, rtBarriers);
                        }
                        // Bind Render Targets
                        {
                            TFBindRenderTargetsDesc bindRenderTargets = {};
                            bindRenderTargets.mRenderTargetCount = 1;
                            bindRenderTargets.mRenderTargets[0] = { pRenderTargetUpSampleSDFShadow, TF_LOAD_ACTION_CLEAR };
                            cmdBindRenderTargets(cmd, &bindRenderTargets);
                            cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTargetUpSampleSDFShadow->mWidth,
                                           (float)pRenderTargetUpSampleSDFShadow->mHeight, 0.0f, 1.0f);
                            cmdSetScissor(cmd, 0, 0, pRenderTargetUpSampleSDFShadow->mWidth, pRenderTargetUpSampleSDFShadow->mHeight);
                        }
                        // Draw
                        {
                            const uint32_t quadStride = sizeof(float) * 6;
                            cmdBindPipeline(cmd, pPipelineUpsampleSDFShadow);
                            cmdBindVertexBuffer(cmd, 1, &pBufferQuadVertex, &quadStride, NULL);
                            cmdDraw(cmd, 6, 0);
                            cmdBindRenderTargets(cmd, NULL);
                        }
                        // Resource Transition
                        {
                            TFRenderTargetBarrier rtBarriers[] = { { pRenderTargetUpSampleSDFShadow, TF_RESOURCE_STATE_RENDER_TARGET,
                                                                     TF_RESOURCE_STATE_SHADER_RESOURCE } };
                            cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, rtBarriers);
                        }

                        cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                    }
#endif
                }
            }

            // Visibility Buffer Shade Pass
            {
                // Render a fullscreen triangle to evaluate shading for every pixel.This render step uses the render target generated by
                // DrawVisibilityBufferPass
                //  to get the draw / triangle IDs to reconstruct and interpolate vertex attributes per pixel. This method doesn't set any
                //  vertex/index buffer because the triangle positions are calculated internally using vertex_id.
                cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Visibility Buffer Shade Pass");

                if (gCurrentShadowType == SHADOW_TYPE_ASM)
                    updateASMUniform();

                // Resource Transition
                {
                    TFRenderTargetBarrier rtBarriers[] = { { pRenderTargetVBPass, TF_RESOURCE_STATE_RENDER_TARGET,
                                                             TF_RESOURCE_STATE_SHADER_RESOURCE } };
                    cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, rtBarriers);
                }

#if TEST_GPU_BREADCRUMBS
                if (pRenderer->pGpu->mGpuMarkers)
                {
                    TFMarkerDesc marker = {};
                    marker.mOffset = MARKER_OFFSET(MARKER_TASK_INDEX);
                    marker.mValue = RENDERING_STEP_SHADE_VB_PASS;
                    marker.pBuffer = pMarkerBuffer;
                    cmdWriteMarker(cmd, &marker);
                }
#endif
                // Bind Render Targets
                {
                    TFRenderTarget* pDestRenderTarget =
                        gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1 ? pRenderTargetMSAA : pRenderTargetIntermediate;

                    TFBindRenderTargetsDesc bindRenderTargets = {};
                    bindRenderTargets.mRenderTargetCount = 1;
                    bindRenderTargets.mRenderTargets[0] = { pDestRenderTarget, TF_LOAD_ACTION_CLEAR };
                    if (gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1)
                    {
                        bindRenderTargets.mDepthStencil = { pRenderTargetMSAAEdges, TF_LOAD_ACTION_DONTCARE, TF_STORE_ACTION_DONTCARE,
                                                            TF_LOAD_ACTION_LOAD, TF_STORE_ACTION_NONE };
                    }
                    cmdBindRenderTargets(cmd, &bindRenderTargets);
                    cmdSetViewport(cmd, 0.0f, 0.0f, (float)pDestRenderTarget->mWidth, (float)pDestRenderTarget->mHeight, 0.0f, 1.0f);
                    cmdSetScissor(cmd, 0, 0, pDestRenderTarget->mWidth, pDestRenderTarget->mHeight);
                }
                // Draw
                {
                    TFPipeline* pipeline = pPipelineVBShadeSrgb;
#if TEST_GPU_BREADCRUMBS
                    // Using the malfunctioned pipeline
                    if (pRenderer->pGpu->mGpuMarkers && bCrashedSteps[RENDERING_STEP_SHADE_VB_PASS])
                    {
                        bCrashedSteps[RENDERING_STEP_SHADE_VB_PASS] = false;
                        bHasCrashed = true;
                        pipeline = pPipelineVBShadeSrgbCrash;
                        LOGF(LogLevel::eERROR, "[Breadcrumb] Simulating a GPU crash situation (SHADE VISIBILITY BUFFER)...");
                    }
#endif
                    cmdBindPipeline(cmd, pipeline);
                    cmdSetStencilReferenceValue(cmd, MSAA_STENCIL_MASK);

                    // A single triangle is rendered without specifying a vertex buffer (triangle positions are calculated internally using
                    // vertex_id)
                    cmdDraw(cmd, 3, 0);
                }

                cmdBindRenderTargets(cmd, NULL);
                cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
            }

#if TEST_GPU_BREADCRUMBS
            if (pRenderer->pGpu->mGpuMarkers)
            {
                // Reset task marker after checked passes.
                // This ensures that hangs in the next unmarked passes
                // are not blamed on previous marked passes.
                TFMarkerDesc marker = {};
                marker.pBuffer = pMarkerBuffer;
                marker.mOffset = MARKER_OFFSET(MARKER_TASK_INDEX);
                marker.mValue = gMarkerInitialValue;
                // Wait for previous marker buffer writes to complete
                // so that markers for hanging passes are not overwritten.
                marker.mFlags |= TF_MARKER_FLAG_WAIT_FOR_WRITE;
                cmdWriteMarker(cmd, &marker);
            }
#endif
            // MSAA Resolve pass
            if (gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1)
            {
                cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "MSAA Resolve Pass");

                // Resource Transition
                {
                    TFRenderTargetBarrier barrier = { pRenderTargetMSAA, TF_RESOURCE_STATE_RENDER_TARGET,
                                                      TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
                    cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, &barrier);
                }
                // Bind Render Targets
                {
                    TFBindRenderTargetsDesc bindRenderTargets = {};
                    bindRenderTargets.mRenderTargetCount = 1;
                    bindRenderTargets.mRenderTargets[0] = { pRenderTargetIntermediate, TF_LOAD_ACTION_CLEAR };
                    cmdBindRenderTargets(cmd, &bindRenderTargets);
                    cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTargetIntermediate->mWidth, (float)pRenderTargetIntermediate->mHeight,
                                   0.0f, 1.0f);
                    cmdSetScissor(cmd, 0, 0, pRenderTargetIntermediate->mWidth, pRenderTargetIntermediate->mHeight);
                }
                // Draw
                {
                    cmdBindPipeline(cmd, pPipelineResolve);
                    cmdDraw(cmd, 3, 0);
                }

                cmdBindRenderTargets(cmd, NULL);
                cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
            }

            // Resource Transition
            {
                barriers[0] = { pRenderTarget, TF_RESOURCE_STATE_PRESENT, TF_RESOURCE_STATE_RENDER_TARGET };
                barriers[1] = { pRenderTargetIntermediate, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_SHADER_RESOURCE };
                bool            depthNeedsBarrier = false;
                TFResourceState previousDepthState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                if (gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1)
                {
                    previousDepthState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                    depthNeedsBarrier = true;
                }
                if (gCameraUniformData.mSSSEnabled || gCurrentShadowType == SHADOW_TYPE_MESH_BAKED_SDF)
                {
                    previousDepthState = TF_RESOURCE_STATE_SHADER_RESOURCE;
                    depthNeedsBarrier = true;
                }
                barriers[2] = { pRenderTargetDepth, previousDepthState, TF_RESOURCE_STATE_DEPTH_WRITE };
                cmdResourceBarrier(cmd, 0, NULL, 0, NULL, depthNeedsBarrier ? 3 : 2, barriers);
            }

            // Draw Final Image Pass
            {
                // Draws the final fullscreen image into the acquired swapchain target
                cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Draw Final Image");

                uint32_t index = 0;
                if (gCurrentShadowType == SHADOW_TYPE_MESH_BAKED_SDF && gBakedSDFMeshSettings.mDrawSDFMeshVisualization)
                {
                    index = 1;
                }
                // Bind Render Targets
                {
                    TFBindRenderTargetsDesc bindRenderTargets = {};
                    bindRenderTargets.mRenderTargetCount = 1;
                    bindRenderTargets.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_DONTCARE };
                    cmdBindRenderTargets(cmd, &bindRenderTargets);
                    cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
                    cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);
                }
                // Draw
                {
                    cmdBindPipeline(cmd, pPipelinePresentPass);
                    cmdBindDescriptorSet(cmd, index, pDescriptorSetDisplayPerDraw);
                    cmdDraw(cmd, 3, 0);
                }

                cmdBindRenderTargets(cmd, NULL);
                cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
            }

            // UI Pass
            {
                cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Draw UI");

                // Bind Render Targets
                {
                    TFRenderTarget*         pTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];
                    TFBindRenderTargetsDesc bindRenderTargets = {};
                    bindRenderTargets.mRenderTargetCount = 1;
                    bindRenderTargets.mRenderTargets[0] = { pTarget, TF_LOAD_ACTION_LOAD };
                    cmdBindRenderTargets(cmd, &bindRenderTargets);
                    cmdSetViewport(cmd, 0.0f, 0.0f, (float)pTarget->mWidth, (float)pTarget->mHeight, 0.0f, 1.0f);
                    cmdSetScissor(cmd, 0, 0, pTarget->mWidth, pTarget->mHeight);
                }
                // Draw
                {
                    gFrameTimeDraw.mFontColor = 0xff00ffff;
                    gFrameTimeDraw.mFontSize = 18.0f;
                    gFrameTimeDraw.pFont = gFont;
                    float2 txtSize = cmdDrawCpuProfile(cmd, float2(8.0f, 15.0f), &gFrameTimeDraw);
                    cmdDrawGpuProfile(cmd, float2(8.0f, txtSize.y + 75.f), gCurrentGpuProfileToken, &gFrameTimeDraw);

                    uiCmdDrawUserInterface(cmd, pSwapChain, pSwapChain->ppRenderTargets[swapchainImageIndex], gCurrentGpuProfileToken);
                }

                cmdBindRenderTargets(cmd, NULL);
                cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
            }

            // Restore VB outputs for the next frame and transition the swapchain image to present
            {
                const uint32_t        numBarriers = NUM_CULLING_VIEWPORTS + 2;
                TFRenderTargetBarrier barrierPresent = { pRenderTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PRESENT };
                TFBufferBarrier       visibilityBufferRestoreBarriers[numBarriers] = {};
                uint32_t              visibilityBufferRestoreBarrierCount = 0;
                visibilityBufferRestoreBarriers[visibilityBufferRestoreBarrierCount++] = { pVisibilityBuffer->ppIndirectDrawArgBuffer[0],
                                                                                           TF_RESOURCE_STATE_INDIRECT_ARGUMENT |
                                                                                               TF_RESOURCE_STATE_SHADER_RESOURCE,
                                                                                           TF_RESOURCE_STATE_UNORDERED_ACCESS };
                visibilityBufferRestoreBarriers[visibilityBufferRestoreBarrierCount++] = {
                    pVisibilityBuffer->ppIndirectDataBuffer[gFrameIndex], TF_RESOURCE_STATE_SHADER_RESOURCE,
                    TF_RESOURCE_STATE_UNORDERED_ACCESS
                };
                for (uint32_t i = 0; i < NUM_CULLING_VIEWPORTS; ++i)
                {
                    visibilityBufferRestoreBarriers[visibilityBufferRestoreBarrierCount++] = { pVisibilityBuffer->ppFilteredIndexBuffer[i],
                                                                                               TF_RESOURCE_STATE_INDEX_BUFFER |
                                                                                                   TF_RESOURCE_STATE_SHADER_RESOURCE,
                                                                                               TF_RESOURCE_STATE_UNORDERED_ACCESS };
                }

                cmdResourceBarrier(cmd, visibilityBufferRestoreBarrierCount, visibilityBufferRestoreBarriers, 0, NULL, 1, &barrierPresent);
            }
            cmdEndGpuFrameProfile(cmd, gCurrentGpuProfileToken);
            endCmd(cmd);

            // Submit all the work to the GPU and present
            FlushResourceUpdateDesc flushUpdateDesc = {};
            flushUpdateDesc.mNodeIndex = 0;
            flushResourceUpdates(&flushUpdateDesc);
            TFSemaphore* waitSemaphores[] = { flushUpdateDesc.pOutSubmittedSemaphore, pImageAcquiredSemaphore[gFrameIndex] };

            // Submit all the work to the GPU and present
            TFQueueSubmitDesc submitDesc = {};
            submitDesc.mCmdCount = 1;
            submitDesc.mSignalSemaphoreCount = 1;
            submitDesc.mWaitSemaphoreCount = TF_ARRAY_COUNT(waitSemaphores);
            submitDesc.ppCmds = &cmd;
            submitDesc.ppSignalSemaphores = &elem.pSemaphore;
            submitDesc.ppWaitSemaphores = waitSemaphores;
            submitDesc.pSignalFence = elem.pFence;
            queueSubmit(pGraphicsQueue, &submitDesc);
            TFQueuePresentDesc presentDesc = {};
            presentDesc.mIndex = (uint8_t)swapchainImageIndex;
            presentDesc.mWaitSemaphoreCount = 1;
            presentDesc.ppWaitSemaphores = &elem.pSemaphore;
            presentDesc.pSwapChain = pSwapChain;
            presentDesc.mSubmitDone = true;
            queuePresent(pGraphicsQueue, &presentDesc);
            flipProfiler();
        }
    }

    const char* GetName() override { return "09_LightShadowPlayground"; }

private:
    bool addSwapChain() const
    {
        TFSwapChainDesc swapChainDesc = {};
        swapChainDesc.mWindowHandle = pWindow->handle;
        swapChainDesc.mPresentQueueCount = 1;
        swapChainDesc.ppPresentQueues = &pGraphicsQueue;
        swapChainDesc.mWidth = mSettings.mWidth;
        swapChainDesc.mHeight = mSettings.mHeight;
        swapChainDesc.mImageCount = getRecommendedSwapchainImageCount(pRenderer, &pWindow->handle);
        swapChainDesc.mColorFormat = getSupportedSwapchainFormat(pRenderer, &swapChainDesc, TF_COLOR_SPACE_SDR_SRGB);
        swapChainDesc.mColorSpace = TF_COLOR_SPACE_SDR_SRGB;
        swapChainDesc.mColorClearValue = { { 0, 0, 0, 0 } };
        swapChainDesc.mEnableVsync = mSettings.mVSyncEnabled;
        swapChainDesc.mFlags = TF_SWAP_CHAIN_CREATION_FLAG_ENABLE_2D_VR_LAYER;
        swapChainDesc.mVR.m2DLayer = gVR2DLayer;

        ::addSwapChain(pRenderer, &swapChainDesc, &pSwapChain);
        return pSwapChain != NULL;
    }

    TinyImageFormat getVsmFloat2Format() const
    {
        return pRenderer->pGpu->mFormatCaps[TinyImageFormat_R16G16_UNORM] & TF_FORMAT_CAP_LINEAR_FILTER ? TinyImageFormat_R16G16_UNORM
                                                                                                        : TinyImageFormat_R16G16_SFLOAT;
    }

    TinyImageFormat getVsmFloat4Format() const
    {
        return ((TF_FORMAT_CAP_READ_WRITE | TF_FORMAT_CAP_LINEAR_FILTER) ==
                (pRenderer->pGpu->mFormatCaps[TinyImageFormat_R16G16B16A16_UNORM] &
                 (TF_FORMAT_CAP_READ_WRITE | TF_FORMAT_CAP_LINEAR_FILTER)))
                   ? TinyImageFormat_R16G16B16A16_UNORM
               : ((TF_FORMAT_CAP_READ_WRITE | TF_FORMAT_CAP_LINEAR_FILTER) ==
                  (pRenderer->pGpu->mFormatCaps[TinyImageFormat_R32G32B32A32_SFLOAT] &
                   (TF_FORMAT_CAP_READ_WRITE | TF_FORMAT_CAP_LINEAR_FILTER)))
                   ? TinyImageFormat_R32G32B32A32_SFLOAT
                   : TinyImageFormat_R16G16B16A16_SFLOAT;
    }

    TinyImageFormat getVsmFormat() const
    {
        TinyImageFormat vsmFloat2RTFormat = getVsmFloat2Format();
        TinyImageFormat vsmFloat4RTFormat = getVsmFloat4Format();

        return gFloat2RWTextureSupported ? vsmFloat2RTFormat : vsmFloat4RTFormat;
    }

    static void loadSkybox(void*, uint64_t)
    {
        TFSyncToken       token = {};
        TFTextureLoadDesc skyboxTriDesc = {};
        skyboxTriDesc.pFileName = "SanMiguel_3/daytime_cube.tex";
        skyboxTriDesc.ppTexture = &pTextureSkybox;
        addResource(&skyboxTriDesc, &token);

        waitForToken(&token);

        // Generate sky box vertex buffer
        static const float skyBoxPoints[] = {
            0.5f,  -0.5f, -0.5f, 1.0f, // -z
            -0.5f, -0.5f, -0.5f, 1.0f,  -0.5f, 0.5f,  -0.5f, 1.0f,  -0.5f, 0.5f,
            -0.5f, 1.0f,  0.5f,  0.5f,  -0.5f, 1.0f,  0.5f,  -0.5f, -0.5f, 1.0f,

            -0.5f, -0.5f, 0.5f,  1.0f, //-x
            -0.5f, -0.5f, -0.5f, 1.0f,  -0.5f, 0.5f,  -0.5f, 1.0f,  -0.5f, 0.5f,
            -0.5f, 1.0f,  -0.5f, 0.5f,  0.5f,  1.0f,  -0.5f, -0.5f, 0.5f,  1.0f,

            0.5f,  -0.5f, -0.5f, 1.0f, //+x
            0.5f,  -0.5f, 0.5f,  1.0f,  0.5f,  0.5f,  0.5f,  1.0f,  0.5f,  0.5f,
            0.5f,  1.0f,  0.5f,  0.5f,  -0.5f, 1.0f,  0.5f,  -0.5f, -0.5f, 1.0f,

            -0.5f, -0.5f, 0.5f,  1.0f, // +z
            -0.5f, 0.5f,  0.5f,  1.0f,  0.5f,  0.5f,  0.5f,  1.0f,  0.5f,  0.5f,
            0.5f,  1.0f,  0.5f,  -0.5f, 0.5f,  1.0f,  -0.5f, -0.5f, 0.5f,  1.0f,

            -0.5f, 0.5f,  -0.5f, 1.0f, //+y
            0.5f,  0.5f,  -0.5f, 1.0f,  0.5f,  0.5f,  0.5f,  1.0f,  0.5f,  0.5f,
            0.5f,  1.0f,  -0.5f, 0.5f,  0.5f,  1.0f,  -0.5f, 0.5f,  -0.5f, 1.0f,

            0.5f,  -0.5f, 0.5f,  1.0f, //-y
            0.5f,  -0.5f, -0.5f, 1.0f,  -0.5f, -0.5f, -0.5f, 1.0f,  -0.5f, -0.5f,
            -0.5f, 1.0f,  -0.5f, -0.5f, 0.5f,  1.0f,  0.5f,  -0.5f, 0.5f,  1.0f,
        };

        uint64_t         skyBoxDataSize = 4 * 6 * 6 * sizeof(float);
        TFBufferLoadDesc skyboxVbDesc = {};
        skyboxVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        skyboxVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        skyboxVbDesc.mDesc.mSize = skyBoxDataSize;
        skyboxVbDesc.mDesc.mStartState = TF_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        skyboxVbDesc.pData = skyBoxPoints;
        skyboxVbDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        skyboxVbDesc.ppBuffer = &pBufferSkyboxVertex;
        addResource(&skyboxVbDesc, &token);

        for (int i = 0; i < MAX_BLUR_KERNEL_SIZE; i++)
        {
            gBlurWeightsUniform.mBlurWeights[i] = gaussian((float)i, 0.0f, gGaussianBlurSigma[0]);
        }

        TFBufferLoadDesc blurWeightsUBDesc = {};
        blurWeightsUBDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        blurWeightsUBDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        blurWeightsUBDesc.mDesc.mSize = sizeof(gBlurWeights);
        blurWeightsUBDesc.ppBuffer = &pBufferBlurWeights;
        blurWeightsUBDesc.pData = &gBlurWeightsUniform;
        blurWeightsUBDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        addResource(&blurWeightsUBDesc, &token);

        waitForToken(&token);
    }

    void addMsmRenderTargets() const
    {
        const TFClearValue clearColor = { { 1.f, 0 } };

        TFRenderTargetDesc MSMRTDesc = {};
        MSMRTDesc.mArraySize = 1;
        MSMRTDesc.mClearValue.depth = clearColor.depth;
        MSMRTDesc.mDepth = 1;
        MSMRTDesc.mFormat = getVsmFloat4Format();
        MSMRTDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        MSMRTDesc.mWidth = SHADOWMAP_RES;
        MSMRTDesc.mHeight = SHADOWMAP_RES;
        MSMRTDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        MSMRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_TEXTURE | TF_DESCRIPTOR_TYPE_TEXTURE;
        MSMRTDesc.mSampleQuality = 0;
        MSMRTDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        MSMRTDesc.pName = "MSM RT 0";
        addRenderTarget(pRenderer, &MSMRTDesc, &pRenderTargetMSM[0]);
        MSMRTDesc.pName = "MSM RT 1";
        addRenderTarget(pRenderer, &MSMRTDesc, &pRenderTargetMSM[1]);
    }

    void addVsmRenderTargets() const
    {
        const TFClearValue clearColor = { { 1.f, 0 } };

        TFRenderTargetDesc VSMRTDesc = {};
        VSMRTDesc.mArraySize = 1;
        VSMRTDesc.mClearValue.depth = clearColor.depth;
        VSMRTDesc.mDepth = 1;
        VSMRTDesc.mFormat = getVsmFormat();
        VSMRTDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        VSMRTDesc.mWidth = VSM_SHADOWMAP_RES;
        VSMRTDesc.mHeight = VSM_SHADOWMAP_RES;
        VSMRTDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        VSMRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_TEXTURE | TF_DESCRIPTOR_TYPE_TEXTURE;
        VSMRTDesc.mSampleQuality = 0;
        VSMRTDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        VSMRTDesc.pName = "VSM RT 0";
        addRenderTarget(pRenderer, &VSMRTDesc, &pRenderTargetVSM[0]);
        VSMRTDesc.pName = "VSM RT 1";
        addRenderTarget(pRenderer, &VSMRTDesc, &pRenderTargetVSM[1]);
    }

    void removeMsmRenderTargets()
    {
        removeRenderTarget(pRenderer, pRenderTargetMSM[0]);
        removeRenderTarget(pRenderer, pRenderTargetMSM[1]);

        pRenderTargetMSM[0] = NULL;
        pRenderTargetMSM[1] = NULL;
    }

    void removeVsmRenderTargets()
    {
        removeRenderTarget(pRenderer, pRenderTargetVSM[0]);
        removeRenderTarget(pRenderer, pRenderTargetVSM[1]);

        pRenderTargetVSM[0] = NULL;
        pRenderTargetVSM[1] = NULL;
    }

    void addSdfVolumeTextureAtlas()
    {
        TFTextureDesc sdfVolumeTextureAtlasDesc = {};
        sdfVolumeTextureAtlasDesc.mArraySize = 1;
        sdfVolumeTextureAtlasDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE | TF_DESCRIPTOR_TYPE_RW_TEXTURE;
        sdfVolumeTextureAtlasDesc.mClearValue = { { 0.f, 0.f, 0.f, 1.f } };
        sdfVolumeTextureAtlasDesc.mDepth = SDF_VOLUME_TEXTURE_ATLAS_DEPTH;
        sdfVolumeTextureAtlasDesc.mFormat = TinyImageFormat_R16_SFLOAT;
        sdfVolumeTextureAtlasDesc.mWidth = SDF_VOLUME_TEXTURE_ATLAS_WIDTH;
        sdfVolumeTextureAtlasDesc.mHeight = SDF_VOLUME_TEXTURE_ATLAS_HEIGHT;
        sdfVolumeTextureAtlasDesc.mMipLevels = 1;
        sdfVolumeTextureAtlasDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        sdfVolumeTextureAtlasDesc.mSampleQuality = 0;
        sdfVolumeTextureAtlasDesc.pName = "SDF Volume Texture Atlas";
        sdfVolumeTextureAtlasDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;

        TFTextureLoadDesc sdfVolumeTextureAtlasLoadDesc = {};
        sdfVolumeTextureAtlasLoadDesc.pDesc = &sdfVolumeTextureAtlasDesc;
        sdfVolumeTextureAtlasLoadDesc.ppTexture = &pTextureSDFVolumeAtlas;
        addResource(&sdfVolumeTextureAtlasLoadDesc, NULL);
    }

    void removeSdfVolumeTextureAtlas()
    {
        if (pTextureSDFVolumeAtlas)
        {
            removeResource(pTextureSDFVolumeAtlas);
            pTextureSDFVolumeAtlas = NULL;

            pSDFVolumeTextureAtlas->mNextNodeIndex = 0;
        }
    }

    void addASMRenderTargets()
    {
        ASMRenderTargets asmRenderTargets = {};
        asmRenderTargets.m_pASMIndirectionMips = pRenderTargetASMIndirection;
        asmRenderTargets.m_pASMPrerenderIndirectionMips = pRenderTargetASMPrerenderIndirection;
        asmRenderTargets.m_pRenderTargetASMLodClamp = pRenderTargetASMLodClamp;
        asmRenderTargets.m_pRenderTargetASMPrerenderLodClamp = pRenderTargetASMPrerenderLodClamp;
        pASM->Load(asmRenderTargets);
        pASM->Reset();
    }

    void removeRenderTargets()
    {
        removeRenderTarget(pRenderer, pDummyRenderTarget3D);
        removeRenderTarget(pRenderer, pDummyRenderTarget);

        if (gCameraUniformData.mShadowType == SHADOW_TYPE_MSM)
            removeMsmRenderTargets();
        if (gCameraUniformData.mShadowType == SHADOW_TYPE_VSM)
            removeVsmRenderTargets();

        removeRenderTarget(pRenderer, pRenderTargetASMColorPass);
        removeRenderTarget(pRenderer, pRenderTargetASMDEMAtlas);
        removeRenderTarget(pRenderer, pRenderTargetASMDepthAtlas);
        removeRenderTarget(pRenderer, pRenderTargetASMDepthPass);
        for (int32_t i = 0; i < gs_ASMMaxRefinement + 1; ++i)
        {
            removeRenderTarget(pRenderer, pRenderTargetASMIndirection[i]);
            removeRenderTarget(pRenderer, pRenderTargetASMPrerenderIndirection[i]);

            pRenderTargetASMIndirection[i] = NULL;
            pRenderTargetASMPrerenderIndirection[i] = NULL;
        }

        removeRenderTarget(pRenderer, pRenderTargetASMLodClamp);
        removeRenderTarget(pRenderer, pRenderTargetASMPrerenderLodClamp);

        pRenderTargetASMColorPass = NULL;
        pRenderTargetASMDEMAtlas = NULL;
        pRenderTargetASMDepthAtlas = NULL;
        pRenderTargetASMDepthPass = NULL;

        pRenderTargetASMLodClamp = NULL;
        pRenderTargetASMPrerenderLodClamp = NULL;

        removeRenderTarget(pRenderer, pRenderTargetIntermediate);
        removeRenderTarget(pRenderer, pRenderTargetMSAAEdges);
        removeRenderTarget(pRenderer, pRenderTargetMSAA);

        removeRenderTarget(pRenderer, pRenderTargetVBPass);

        removeRenderTarget(pRenderer, pRenderTargetDepth);
        removeRenderTarget(pRenderer, pRenderTargetShadowMap);

        removeResource(pTextureSDFMeshShadow);
        if (gSupportTextureAtomics)
        {
            removeResource(pTextureSSS);
        }
        else
        {
            removeResource(pBufferSSS);
        }
        removeRenderTarget(pRenderer, pRenderTargetSDFMeshVisualization);
        removeRenderTarget(pRenderer, pRenderTargetUpSampleSDFShadow);
    }

    void addRenderTargets() const
    {
        const uint32_t sceneWidth = gSceneRes.mWidth;
        const uint32_t sceneHeight = gSceneRes.mHeight;

        const TFClearValue depthStencilClear = { { 0.0f, 0 } };

        // const TFClearValue reverseDepthStencilClear = { {{1.0f, 0}} };
        const TFClearValue colorClearBlack = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        const TFClearValue colorClearWhite = { { 1.0f, 1.0f, 1.0f, 1.0f } };
        const TFClearValue optimizedColorClearBlack = { { 0.0f, 0.0f, 0.0f, 0.0f } };

        TFRenderTargetDesc dummyRT = {};
        dummyRT.mArraySize = 1;
        dummyRT.mClearValue = depthStencilClear;
        dummyRT.mDepth = 1;
        dummyRT.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE | TF_DESCRIPTOR_TYPE_RW_TEXTURE;
        dummyRT.mFormat = getVsmFormat();
        dummyRT.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        dummyRT.mWidth = 2;
        dummyRT.mHeight = 2;
        dummyRT.mSampleCount = TF_SAMPLE_COUNT_1;
        dummyRT.mSampleQuality = 0;
        dummyRT.mFlags = TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        dummyRT.pName = "Dummy RT";
        addRenderTarget(pRenderer, &dummyRT, &pDummyRenderTarget);
        dummyRT.mFlags = TF_TEXTURE_CREATION_FLAG_NONE;
        dummyRT.mDepth = 2;
        addRenderTarget(pRenderer, &dummyRT, &pDummyRenderTarget3D);

        /************************************************************************/
        // Main depth buffer
        /************************************************************************/
        uint32_t currentOffsetESRAM = 0;

        TF_ESRAM_BEGIN_ALLOC(pRenderer, "Depth", currentOffsetESRAM);
        TFRenderTargetDesc depthRT = {};
        depthRT.mArraySize = 1;
        depthRT.mClearValue = depthStencilClear;
        depthRT.mDepth = 1;
        depthRT.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        depthRT.mFormat = TinyImageFormat_D32_SFLOAT;
        depthRT.mStartState = TF_RESOURCE_STATE_DEPTH_WRITE;
        depthRT.mWidth = sceneWidth;
        depthRT.mHeight = sceneHeight;
        depthRT.mSampleCount = gAppSettings.mMsaaLevel;
        depthRT.mSampleQuality = 0;
        depthRT.pName = "Depth RT";
        depthRT.mFlags = (gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_2 ? TF_TEXTURE_CREATION_FLAG_NONE : TF_TEXTURE_CREATION_FLAG_ESRAM) |
                         TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        if (depthRT.mSampleCount == TF_SAMPLE_COUNT_1)
        {
            depthRT.mFlags |= TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        }
        addRenderTarget(pRenderer, &depthRT, &pRenderTargetDepth);

        TF_ESRAM_CURRENT_OFFSET(pRenderer, depthOffsetESRAM);
        TF_ESRAM_END_ALLOC(pRenderer);

        /************************************************************************/
        // Intermediate render target
        /************************************************************************/
        TF_ESRAM_BEGIN_ALLOC(pRenderer, "Intermediate", depthOffsetESRAM);
        TFRenderTargetDesc postProcRTDesc = {};
        postProcRTDesc.mArraySize = 1;
        postProcRTDesc.mClearValue = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        postProcRTDesc.mDepth = 1;
        postProcRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        postProcRTDesc.mFormat = pSwapChain->mFormat;
        postProcRTDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        postProcRTDesc.mHeight = sceneHeight;
        postProcRTDesc.mWidth = sceneWidth;
        postProcRTDesc.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        postProcRTDesc.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        postProcRTDesc.pName = "pIntermediateRenderTarget";
        postProcRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        postProcRTDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_ESRAM;
        if (postProcRTDesc.mSampleCount == TF_SAMPLE_COUNT_1)
        {
            postProcRTDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        }
        addRenderTarget(pRenderer, &postProcRTDesc, &pRenderTargetIntermediate);

        TF_ESRAM_CURRENT_OFFSET(pRenderer, intermediateOffsetESRAM);
        TF_ESRAM_END_ALLOC(pRenderer);
        currentOffsetESRAM = intermediateOffsetESRAM;

        /************************************************************************/
        // Shadow Map Render Target
        /************************************************************************/
        TF_ESRAM_BEGIN_ALLOC(pRenderer, "Shadow Map", currentOffsetESRAM);

        TFRenderTargetDesc shadowRTDesc = {};
        shadowRTDesc.mArraySize = 1;
        shadowRTDesc.mClearValue.depth = depthStencilClear.depth;
        shadowRTDesc.mDepth = 1;
        shadowRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        shadowRTDesc.mFormat = TinyImageFormat_D32_SFLOAT;
        TinyImageFormat depthFormats[] = { TinyImageFormat_D32_SFLOAT, TinyImageFormat_D24_UNORM_S8_UINT, TinyImageFormat_D16_UNORM };
        for (uint32_t i = 0; i < TF_ARRAY_COUNT(depthFormats); ++i)
        {
            if (pRenderer->pGpu->mFormatCaps[depthFormats[i]] & TF_FORMAT_CAP_LINEAR_FILTER)
            {
                shadowRTDesc.mFormat = depthFormats[i];
                break;
            }
        }
        shadowRTDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        shadowRTDesc.mWidth = ESM_SHADOWMAP_RES;
        shadowRTDesc.mHeight = ESM_SHADOWMAP_RES;
        shadowRTDesc.mSampleCount = (TFSampleCount)ESM_MSAA_SAMPLES;
        shadowRTDesc.mSampleQuality = 0;
        shadowRTDesc.pName = "Shadow Map RT";
        shadowRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_ESRAM;
        shadowRTDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        addRenderTarget(pRenderer, &shadowRTDesc, &pRenderTargetShadowMap);

        TF_ESRAM_CURRENT_OFFSET(pRenderer, shadowMapOffsetESRAM);
        TF_ESRAM_END_ALLOC(pRenderer);
        currentOffsetESRAM = max(shadowMapOffsetESRAM, currentOffsetESRAM);

        /************************************************************************/
        // Screen Space Shadow Map Render Target
        /************************************************************************/
        TF_ESRAM_BEGIN_ALLOC(pRenderer, "Screen Space Shadows", currentOffsetESRAM);
        if (gSupportTextureAtomics)
        {
            TFTextureDesc SSSRTDesc = {};
            SSSRTDesc.mWidth = sceneWidth;
            SSSRTDesc.mHeight = sceneHeight;
            SSSRTDesc.mDepth = 1;
            SSSRTDesc.mArraySize = VR_MULTIVIEW_COUNT;
            SSSRTDesc.mMipLevels = 1;
            SSSRTDesc.mSampleCount = TF_SAMPLE_COUNT_1;
            SSSRTDesc.mFormat = TinyImageFormat_R32_UINT;
            SSSRTDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
            SSSRTDesc.mClearValue.r = 1.0f;
            SSSRTDesc.mClearValue.g = 1.0f;
            SSSRTDesc.mClearValue.b = 1.0f;
            SSSRTDesc.mClearValue.a = 1.0f;
            SSSRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE | TF_DESCRIPTOR_TYPE_RW_TEXTURE;
            SSSRTDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_ESRAM;
            SSSRTDesc.pName = "Screen Space Shadows Texture";

            TFTextureLoadDesc SSSTextureLoadDesc = {};
            SSSTextureLoadDesc.pDesc = &SSSRTDesc;
            SSSTextureLoadDesc.ppTexture = &pTextureSSS;
            addResource(&SSSTextureLoadDesc, NULL);
        }
        else
        {
            TFBufferLoadDesc SSSRTDesc = {};
            SSSRTDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER;
            SSSRTDesc.mDesc.mElementCount = sceneWidth * sceneHeight;
            SSSRTDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
            SSSRTDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_OWN_MEMORY_BIT;
            SSSRTDesc.mDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
            SSSRTDesc.mDesc.mStructStride = sizeof(uint);
            SSSRTDesc.mDesc.mSize = (uint64_t)SSSRTDesc.mDesc.mElementCount * SSSRTDesc.mDesc.mStructStride;
            SSSRTDesc.mDesc.pName = "Screen Space Shadows Buffer";
            SSSRTDesc.ppBuffer = &pBufferSSS;
            addResource(&SSSRTDesc, NULL);
        }
        TF_ESRAM_CURRENT_OFFSET(pRenderer, screenShadowOffsetESRAM);
        TF_ESRAM_END_ALLOC(pRenderer);
        currentOffsetESRAM = max(screenShadowOffsetESRAM, currentOffsetESRAM);

        if (gCurrentShadowType == SHADOW_TYPE_VSM)
            addVsmRenderTargets();
        if (gCurrentShadowType == SHADOW_TYPE_MSM)
            addMsmRenderTargets();

        /************************************************************************/
        // ASM Depth Pass Render Target
        /************************************************************************/
        TFRenderTargetDesc ASMDepthPassRT = {};
        ASMDepthPassRT.mArraySize = 1;
        ASMDepthPassRT.mClearValue.depth = depthStencilClear.depth;
        ASMDepthPassRT.mDepth = 1;
        ASMDepthPassRT.mFormat = TinyImageFormat_D32_SFLOAT;
        ASMDepthPassRT.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        ASMDepthPassRT.mMipLevels = 1;
        ASMDepthPassRT.mSampleCount = TF_SAMPLE_COUNT_1;
        ASMDepthPassRT.mSampleQuality = 0;
        ASMDepthPassRT.mWidth = ASM_WORK_BUFFER_DEPTH_PASS_WIDTH;
        ASMDepthPassRT.mHeight = ASM_WORK_BUFFER_DEPTH_PASS_HEIGHT;
        ASMDepthPassRT.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        ASMDepthPassRT.mFlags = TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        ASMDepthPassRT.pName = "ASM Depth Pass RT";
        addRenderTarget(pRenderer, &ASMDepthPassRT, &pRenderTargetASMDepthPass);
        /*************************************************************************/
        // ASM Color Pass Render Target
        /*************************************************************************/
        TFRenderTargetDesc ASMColorPassRT = {};
        ASMColorPassRT.mArraySize = 1;
        ASMColorPassRT.mClearValue = colorClearWhite;
        ASMColorPassRT.mDepth = 1;
        ASMColorPassRT.mFormat = pRenderer->pGpu->mFormatCaps[TinyImageFormat_R32_SFLOAT] & TF_FORMAT_CAP_LINEAR_FILTER
                                     ? TinyImageFormat_R32_SFLOAT
                                     : TinyImageFormat_R16_SFLOAT;
        ASMColorPassRT.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        ASMColorPassRT.mMipLevels = 1;
        ASMColorPassRT.mSampleCount = TF_SAMPLE_COUNT_1;
        ASMColorPassRT.mSampleQuality = 0;
        ASMColorPassRT.mWidth = ASM_WORK_BUFFER_COLOR_PASS_WIDTH;
        ASMColorPassRT.mHeight = ASM_WORK_BUFFER_COLOR_PASS_HEIGHT;
        ASMColorPassRT.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        ASMColorPassRT.mFlags = TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        ASMColorPassRT.pName = "ASM Depth Pass RT";
        addRenderTarget(pRenderer, &ASMColorPassRT, &pRenderTargetASMColorPass);

        /************************************************************************/
        // ASM Depth Atlas Render Target
        /************************************************************************/
        TFRenderTargetDesc depthAtlasRTDesc = {};
        depthAtlasRTDesc.mArraySize = 1;
        depthAtlasRTDesc.mClearValue = colorClearBlack;
        depthAtlasRTDesc.mDepth = 1;
        depthAtlasRTDesc.mFormat = TinyImageFormat_R32_SFLOAT;
        depthAtlasRTDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        depthAtlasRTDesc.mWidth = gs_ASMDepthAtlasTextureWidth;
        depthAtlasRTDesc.mHeight = gs_ASMDepthAtlasTextureHeight;
        depthAtlasRTDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        depthAtlasRTDesc.mSampleQuality = 0;
        depthAtlasRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        depthAtlasRTDesc.pName = "ASM Depth Atlas RT";
        depthAtlasRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        addRenderTarget(pRenderer, &depthAtlasRTDesc, &pRenderTargetASMDepthAtlas);

        TFRenderTargetDesc DEMAtlasRTDesc = {};
        DEMAtlasRTDesc.mArraySize = 1;
        DEMAtlasRTDesc.mClearValue = colorClearBlack;
        DEMAtlasRTDesc.mDepth = 1;
        DEMAtlasRTDesc.mFormat = pRenderer->pGpu->mFormatCaps[TinyImageFormat_R32_SFLOAT] & TF_FORMAT_CAP_LINEAR_FILTER
                                     ? TinyImageFormat_R32_SFLOAT
                                     : TinyImageFormat_R16_SFLOAT;
        DEMAtlasRTDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        DEMAtlasRTDesc.mWidth = gs_ASMDEMAtlasTextureWidth;
        DEMAtlasRTDesc.mHeight = gs_ASMDEMAtlasTextureHeight;
        DEMAtlasRTDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        DEMAtlasRTDesc.mSampleQuality = 0;
        DEMAtlasRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        DEMAtlasRTDesc.pName = "ASM DEM Atlas RT";
        DEMAtlasRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        addRenderTarget(pRenderer, &DEMAtlasRTDesc, &pRenderTargetASMDEMAtlas);
        /************************************************************************/
        // ASM Indirection texture Render Target
        /************************************************************************/
        uint32_t indirectionTextureSize = (1 << gs_ASMMaxRefinement) * gsASMIndexSize;

        TFRenderTargetDesc indirectionRTDesc = {};
        indirectionRTDesc.mArraySize = 1;
        indirectionRTDesc.mClearValue = colorClearBlack;
        indirectionRTDesc.mDepth = 1;
        indirectionRTDesc.mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
        indirectionRTDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        indirectionRTDesc.mWidth = indirectionTextureSize;
        indirectionRTDesc.mHeight = indirectionTextureSize;
        indirectionRTDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        indirectionRTDesc.mSampleQuality = 0;
        indirectionRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        indirectionRTDesc.mMipLevels = 1;
        indirectionRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        indirectionRTDesc.pName = "ASM Indirection RT";

        for (int32_t i = 0; i <= gs_ASMMaxRefinement; ++i)
        {
            uint32_t mewIndirectionTextureSize = (indirectionTextureSize >> i);
            indirectionRTDesc.mWidth = mewIndirectionTextureSize;
            indirectionRTDesc.mHeight = mewIndirectionTextureSize;
            addRenderTarget(pRenderer, &indirectionRTDesc, &pRenderTargetASMIndirection[i]);
            addRenderTarget(pRenderer, &indirectionRTDesc, &pRenderTargetASMPrerenderIndirection[i]);
        }

        TFRenderTargetDesc lodClampRTDesc = {};
        lodClampRTDesc.mArraySize = 1;
        lodClampRTDesc.mClearValue = colorClearWhite;
        lodClampRTDesc.mDepth = 1;
        lodClampRTDesc.mFormat = TinyImageFormat_R16_SFLOAT;
        lodClampRTDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        lodClampRTDesc.mWidth = indirectionTextureSize;
        lodClampRTDesc.mHeight = indirectionTextureSize;
        lodClampRTDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        lodClampRTDesc.mSampleQuality = 0;
        lodClampRTDesc.mMipLevels = 1;
        lodClampRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        lodClampRTDesc.pName = "ASM Lod Clamp RT";
        lodClampRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        addRenderTarget(pRenderer, &lodClampRTDesc, &pRenderTargetASMLodClamp);
        addRenderTarget(pRenderer, &lodClampRTDesc, &pRenderTargetASMPrerenderLodClamp);

        /*************************************/
        // SDF mesh visualization render target
        /*************************************/
        TFRenderTargetDesc sdfMeshVisualizationRTDesc = {};
        sdfMeshVisualizationRTDesc.mArraySize = 1;
        sdfMeshVisualizationRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE | TF_DESCRIPTOR_TYPE_RW_TEXTURE;
        sdfMeshVisualizationRTDesc.mClearValue = colorClearBlack;
        sdfMeshVisualizationRTDesc.mDepth = 1;
        sdfMeshVisualizationRTDesc.mFormat = pRenderer->pGpu->mFormatCaps[TinyImageFormat_R32G32B32A32_SFLOAT] & TF_FORMAT_CAP_LINEAR_FILTER
                                                 ? TinyImageFormat_R32G32B32A32_SFLOAT
                                                 : TinyImageFormat_R16G16B16A16_SFLOAT;
        sdfMeshVisualizationRTDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        sdfMeshVisualizationRTDesc.mWidth = sceneWidth / SDF_SHADOW_DOWNSAMPLE_VALUE;
        sdfMeshVisualizationRTDesc.mHeight = sceneHeight / SDF_SHADOW_DOWNSAMPLE_VALUE;
        sdfMeshVisualizationRTDesc.mMipLevels = 1;
        sdfMeshVisualizationRTDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        sdfMeshVisualizationRTDesc.mSampleQuality = 0;
        sdfMeshVisualizationRTDesc.pName = "SDF Mesh Visualization RT";
        sdfMeshVisualizationRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        sdfMeshVisualizationRTDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        addRenderTarget(pRenderer, &sdfMeshVisualizationRTDesc, &pRenderTargetSDFMeshVisualization);

        TFTextureDesc sdfMeshShadowRTDesc = {};
        sdfMeshShadowRTDesc.mArraySize = 1;
        sdfMeshShadowRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE | TF_DESCRIPTOR_TYPE_RW_TEXTURE;
        sdfMeshShadowRTDesc.mClearValue = colorClearBlack;
        sdfMeshShadowRTDesc.mDepth = 1;
        sdfMeshShadowRTDesc.mFormat = pRenderer->pGpu->mFormatCaps[TinyImageFormat_R32G32_SFLOAT] & TF_FORMAT_CAP_LINEAR_FILTER
                                          ? TinyImageFormat_R32G32_SFLOAT
                                          : TinyImageFormat_R16G16_SFLOAT;
        sdfMeshShadowRTDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
#if ENABLE_SDF_SHADOW_DOWNSAMPLE
        sdfMeshShadowRTDesc.mWidth = sceneWidth / SDF_SHADOW_DOWNSAMPLE_VALUE;
        sdfMeshShadowRTDesc.mHeight = sceneHeight / SDF_SHADOW_DOWNSAMPLE_VALUE;
#else
        sdfMeshShadowRTDesc.mWidth = sceneWidth;
        sdfMeshShadowRTDesc.mHeight = sceneHeight;
#endif
        sdfMeshShadowRTDesc.mMipLevels = 1;
        sdfMeshShadowRTDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        sdfMeshShadowRTDesc.mSampleQuality = 0;
        sdfMeshShadowRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        sdfMeshShadowRTDesc.pName = "SDF Mesh Shadow Pass RT";
        TFTextureLoadDesc sdfMeshShadowLoadDesc = {};
        sdfMeshShadowLoadDesc.pDesc = &sdfMeshShadowRTDesc;
        sdfMeshShadowLoadDesc.ppTexture = &pTextureSDFMeshShadow;
        addResource(&sdfMeshShadowLoadDesc, NULL);

        TFRenderTargetDesc upSampleSDFShadowRTDesc = {};
        upSampleSDFShadowRTDesc.mArraySize = 1;
        upSampleSDFShadowRTDesc.mClearValue = colorClearBlack;
        upSampleSDFShadowRTDesc.mDepth = 1;
        upSampleSDFShadowRTDesc.mFormat = TinyImageFormat_R16_SFLOAT;
        upSampleSDFShadowRTDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        upSampleSDFShadowRTDesc.mWidth = sceneWidth;
        upSampleSDFShadowRTDesc.mHeight = sceneHeight;
        upSampleSDFShadowRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        upSampleSDFShadowRTDesc.mMipLevels = 1;
        upSampleSDFShadowRTDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        upSampleSDFShadowRTDesc.mSampleQuality = 0;
        upSampleSDFShadowRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        upSampleSDFShadowRTDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        upSampleSDFShadowRTDesc.pName = "Upsample SDF Mesh Shadow";
        addRenderTarget(pRenderer, &upSampleSDFShadowRTDesc, &pRenderTargetUpSampleSDFShadow);

        /************************************************************************/
        // Visibility buffer Pass Render Target
        /************************************************************************/
        TFRenderTargetDesc vbRTDesc = {};
        vbRTDesc.mArraySize = 1;
        vbRTDesc.mClearValue = colorClearWhite;
        vbRTDesc.mDepth = 1;
        vbRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        vbRTDesc.mFormat = TinyImageFormat_R8G8B8A8_UNORM;
        vbRTDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        vbRTDesc.mWidth = sceneWidth;
        vbRTDesc.mHeight = sceneHeight;
        vbRTDesc.mSampleCount = gAppSettings.mMsaaLevel;
        vbRTDesc.mSampleQuality = 0;
        vbRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        if (vbRTDesc.mSampleCount == TF_SAMPLE_COUNT_1)
        {
            vbRTDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        }
        vbRTDesc.pName = "VB RT";
        addRenderTarget(pRenderer, &vbRTDesc, &pRenderTargetVBPass);
        /************************************************************************/
        // MSAA render target
        /************************************************************************/
        TFRenderTargetDesc msaaRTDesc = {};
        msaaRTDesc.mArraySize = 1;
        msaaRTDesc.mClearValue = optimizedColorClearBlack;
        msaaRTDesc.mDepth = 1;
        msaaRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        msaaRTDesc.mFormat = pSwapChain->mFormat;
        msaaRTDesc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        msaaRTDesc.mHeight = sceneHeight;
        msaaRTDesc.mSampleCount = gAppSettings.mMsaaLevel;
        msaaRTDesc.mSampleQuality = 0;
        msaaRTDesc.mWidth = sceneWidth;
        msaaRTDesc.pName = "MSAA RT";
        // Disabling compression data will avoid decompression phase before resolve pass.
        // However, the shading pass will require more memory bandwidth.
        // We measured with and without compression and without compression is faster in our case.
#ifndef PROSPERO
        msaaRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_NO_COMPRESSION;
#endif
        msaaRTDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        addRenderTarget(pRenderer, &msaaRTDesc, &pRenderTargetMSAA);

        /************************************************************************/
        // MSAA edges render target
        /************************************************************************/
        TinyImageFormat       stencilImageFormat = TinyImageFormat_S8_UINT;
        const TinyImageFormat candidateStencilFormats[] = { TinyImageFormat_S8_UINT, TinyImageFormat_D24_UNORM_S8_UINT,
                                                            TinyImageFormat_D16_UNORM_S8_UINT, TinyImageFormat_D32_SFLOAT_S8_UINT };
        for (uint32_t i = 0; i < TF_ARRAY_COUNT(candidateStencilFormats); ++i)
        {
            TinyImageFormat    candidateFormat = candidateStencilFormats[i];
            TFFormatCapability capMask = TF_FORMAT_CAP_DEPTH_STENCIL | TF_FORMAT_CAP_READ;
            if ((pRenderer->pGpu->mFormatCaps[candidateFormat] & capMask) == capMask)
            {
                stencilImageFormat = candidateFormat;
                break;
            }
        }
        TFRenderTargetDesc msaaStencilRT = {};
        msaaStencilRT.mArraySize = 1;
        msaaStencilRT.mClearValue = depthStencilClear;
        msaaStencilRT.mDepth = 1;
        msaaStencilRT.mFormat = stencilImageFormat;
        msaaStencilRT.mStartState = TF_RESOURCE_STATE_DEPTH_READ;
        msaaStencilRT.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        msaaStencilRT.mHeight = sceneHeight;
        msaaStencilRT.mWidth = sceneWidth;
        msaaStencilRT.mSampleCount = gAppSettings.mMsaaLevel;
        msaaStencilRT.mSampleQuality = 0;
        msaaStencilRT.mFlags = TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        msaaStencilRT.pName = "Stencil Buffer MSAA";
        addRenderTarget(pRenderer, &msaaStencilRT, &pRenderTargetMSAAEdges);
    }

    bool addDescriptorSets()
    {
        // Persistent set.
        TFDescriptorSetDesc setDesc = SRT_SET_DESC(SrtData, Persistent, 1, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPersistent);

        // Per Frame set.
        setDesc = SRT_SET_DESC(SrtData, PerFrame, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPerFrame);

        // Baked SDF Mesh Shadow
        setDesc = SRT_SET_DESC(BakedSDFMeshShadowSrtData, PerBatch, 1, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetBakedSDFMeshShadow);

#ifdef QUEST_VR
        // Per dispatch SDF shadows set
        setDesc = SRT_SET_DESC(BakedSDFMeshShadowSrtData, PerDraw, gDataBufferCount * VR_MULTIVIEW_COUNT, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetSDFShadowPerDraw);
#endif
        // Update region 3d texture
        setDesc = SRT_SET_DESC(UpdateRegion3DTextureSrtData, PerBatch, 1, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetUpdateRegion3DTexture);

        // Triangle filtering set.
        setDesc = SRT_SET_DESC(TriangleFilteringSrtData, PerBatch, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetTriangleFiltering);

        // Gaussian blur
        setDesc = SRT_SET_DESC(GaussianBlurSrtData, PerDraw, 1, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetGaussianBlurVSMPerDraw);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetGaussianBlurMSMPerDraw);

        setDesc = SRT_SET_DESC(GaussianBlurSrtData, PerBatch, gDataBufferCount * 2, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetGaussianBlurPerBatch);

        // Display
        setDesc = SRT_SET_DESC(DisplaySrtData, PerDraw, 2, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetDisplayPerDraw);

        // SSS
        setDesc = SRT_SET_DESC(ScreenSpaceShadowsSrtData, PerBatch, gDataBufferCount * VR_MULTIVIEW_COUNT, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetSSSPerBatch);
        setDesc = SRT_SET_DESC(ScreenSpaceShadowsSrtData, PerDraw, gDataBufferCount * MAX_SSS_WAVE_OFFSETS, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetSSSPerDraw);

        // ASM Depth, VB Pass
        setDesc = SRT_SET_DESC(VisibilityBufferPassSrtData, PerDraw, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetVBPass);
        setDesc = SRT_SET_DESC(VisibilityBufferShadowPassSrtData, PerDraw, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetDepthVBPass);
        setDesc = SRT_SET_DESC(VisibilityBufferShadowPassSrtData, PerDraw, gDataBufferCount * ASM_MAX_TILES_PER_PASS, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetASMDepthPass);
        // ASM indirection
        setDesc = SRT_SET_DESC(QuadDataSrtData, PerDraw, gDataBufferCount * (gs_ASMMaxRefinement + 1), 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetASMFillIndirection[0]);
        setDesc = SRT_SET_DESC(QuadDataSrtData, PerDraw, gDataBufferCount * (gs_ASMMaxRefinement + 1), 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetASMFillIndirection[1]);

        // Atlas to Color
        setDesc = SRT_SET_DESC(QuadDataSrtData, PerBatch, 1, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetAtlasToColor[0]);

        setDesc = SRT_SET_DESC(QuadDataSrtData, PerDraw, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetAtlasToColor[1]);

        // Color to Atlas
        setDesc = SRT_SET_DESC(QuadDataSrtData, PerBatch, 1, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetColorToAtlas[0]);

        setDesc = SRT_SET_DESC(QuadDataSrtData, PerDraw, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetColorToAtlas[1]);

        // ASM atlas quads
        setDesc = SRT_SET_DESC(QuadDataSrtData, PerBatch, 1, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetAsmAtlasQuads[0]);

        setDesc = SRT_SET_DESC(QuadDataSrtData, PerDraw, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetAsmAtlasQuads[1]);

        // ASM copy dem
        setDesc = SRT_SET_DESC(QuadDataSrtData, PerBatch, 1, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetAsmCopyDEM[0]);

        setDesc = SRT_SET_DESC(QuadDataSrtData, PerDraw, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetAsmCopyDEM[1]);

        // ASM lod clamp
        setDesc = SRT_SET_DESC(QuadDataSrtData, PerDraw, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetAsmLodClamp);

        // ASM clear indirection
        setDesc = SRT_SET_DESC(QuadDataSrtData, PerDraw, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetAsmClearIndirection);

        return true;
    }

    void removeDescriptorSets()
    {
        removeDescriptorSet(pRenderer, pDescriptorSetAtlasToColor[1]);
        removeDescriptorSet(pRenderer, pDescriptorSetAtlasToColor[0]);
        removeDescriptorSet(pRenderer, pDescriptorSetColorToAtlas[1]);
        removeDescriptorSet(pRenderer, pDescriptorSetColorToAtlas[0]);
        removeDescriptorSet(pRenderer, pDescriptorSetAsmAtlasQuads[1]);
        removeDescriptorSet(pRenderer, pDescriptorSetAsmAtlasQuads[0]);
        removeDescriptorSet(pRenderer, pDescriptorSetAsmCopyDEM[1]);
        removeDescriptorSet(pRenderer, pDescriptorSetAsmCopyDEM[0]);
        removeDescriptorSet(pRenderer, pDescriptorSetAsmLodClamp);
        removeDescriptorSet(pRenderer, pDescriptorSetAsmClearIndirection);
        removeDescriptorSet(pRenderer, pDescriptorSetBakedSDFMeshShadow);
#ifdef QUEST_VR
        removeDescriptorSet(pRenderer, pDescriptorSetSDFShadowPerDraw);
#endif
        removeDescriptorSet(pRenderer, pDescriptorSetSSSPerDraw);
        removeDescriptorSet(pRenderer, pDescriptorSetSSSPerBatch);
        removeDescriptorSet(pRenderer, pDescriptorSetTriangleFiltering);
        removeDescriptorSet(pRenderer, pDescriptorSetDisplayPerDraw);
        removeDescriptorSet(pRenderer, pDescriptorSetGaussianBlurMSMPerDraw);
        removeDescriptorSet(pRenderer, pDescriptorSetGaussianBlurVSMPerDraw);
        removeDescriptorSet(pRenderer, pDescriptorSetGaussianBlurPerBatch);
        removeDescriptorSet(pRenderer, pDescriptorSetUpdateRegion3DTexture);
        removeDescriptorSet(pRenderer, pDescriptorSetVBPass);
        removeDescriptorSet(pRenderer, pDescriptorSetDepthVBPass);
        removeDescriptorSet(pRenderer, pDescriptorSetASMDepthPass);
        removeDescriptorSet(pRenderer, pDescriptorSetASMFillIndirection[0]);
        removeDescriptorSet(pRenderer, pDescriptorSetASMFillIndirection[1]);
        removeDescriptorSet(pRenderer, pDescriptorSetPerFrame);
        removeDescriptorSet(pRenderer, pDescriptorSetPersistent);
    }

    void updateDescriptorSets()
    {
#if ENABLE_SDF_SHADOW_DOWNSAMPLE
        TFTexture* sdfShadowTexture = pRenderTargetUpSampleSDFShadow->pTexture;
#else
        TFTexture* sdfShadowTexture = pTextureSDFMeshShadow;
#endif
        TFTexture* esmShadowMap = pRenderTargetShadowMap->pTexture;

        TFRenderTarget* const* indirectionTexMips = pASM->m_longRangeShadows->m_indirectionTexturesMips;
        TFRenderTarget* const* prerenderIndirectionTexMips = pASM->m_longRangePreRender->m_indirectionTexturesMips;
        TFTexture*             entireTextureList[10] = {};
        {
            uint32_t idx = 0;
            entireTextureList[idx++] = indirectionTexMips[0]->pTexture;
            entireTextureList[idx++] = indirectionTexMips[1]->pTexture, entireTextureList[idx++] = indirectionTexMips[2]->pTexture;
            entireTextureList[idx++] = indirectionTexMips[3]->pTexture, entireTextureList[idx++] = indirectionTexMips[4]->pTexture;
            entireTextureList[idx++] = prerenderIndirectionTexMips[0]->pTexture;
            entireTextureList[idx++] = prerenderIndirectionTexMips[1]->pTexture;
            entireTextureList[idx++] = prerenderIndirectionTexMips[2]->pTexture;
            entireTextureList[idx++] = prerenderIndirectionTexMips[3]->pTexture;
            entireTextureList[idx++] = prerenderIndirectionTexMips[4]->pTexture;
        }

        // Persistent set
        {
            uint32_t         descriptorCount = 0;
            TFDescriptorData persistentSetParams[40] = {};
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gVBConstantBuffer);
            persistentSetParams[descriptorCount++].ppBuffers = &pVisibilityBuffer->pVBConstantBuffer;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gVertexPositionBuffer);
            persistentSetParams[descriptorCount++].ppBuffers = &pGeom->pVertexBuffers[0];
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gIndexDataBuffer);
            persistentSetParams[descriptorCount++].ppBuffers = &pGeom->pIndexBuffer;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gMeshConstantsBuffer);
            persistentSetParams[descriptorCount++].ppBuffers = &pBufferMeshConstants;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gVBConstantBuffer);
            persistentSetParams[descriptorCount++].ppBuffers = &pVisibilityBuffer->pVBConstantBuffer;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gBlurWeights);
            persistentSetParams[descriptorCount++].ppBuffers = &pBufferBlurWeights;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gDepthTexture);
            persistentSetParams[descriptorCount].ppTextures = &pRenderTargetDepth->pTexture;
            persistentSetParams[descriptorCount++].mCount = 1;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gSDFShadowTexture);
            persistentSetParams[descriptorCount++].ppTextures = &pTextureSDFMeshShadow;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gSDFVolumeTextureAtlas);
            persistentSetParams[descriptorCount++].ppTextures =
                gCameraUniformData.mShadowType == SHADOW_TYPE_MESH_BAKED_SDF ? &pTextureSDFVolumeAtlas : &pDummyRenderTarget3D->pTexture;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gDepthTextureUpSample);
            persistentSetParams[descriptorCount++].ppTextures = &pRenderTargetDepth->pTexture;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gSkyboxTex);
            persistentSetParams[descriptorCount++].ppTextures = &pTextureSkybox;

            if (gUsingTextureAtlasFallback)
            {
                persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gAtlasTextures);
                persistentSetParams[descriptorCount].mCount = (uint32_t)pPackage->mAtlasCount;
                persistentSetParams[descriptorCount++].ppTextures = pPackage->ppAtlasTextures;
            }
            else
            {
                persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gAllTextures);
                persistentSetParams[descriptorCount].mCount = (uint32_t)gAllTexturesCount;
                persistentSetParams[descriptorCount++].ppTextures = pPackage->ppAllTextures;
            }

            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gVertexTexCoordBuffer);
            persistentSetParams[descriptorCount++].ppBuffers = &pGeom->pVertexBuffers[1];
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gMsaaSource);
            persistentSetParams[descriptorCount++].ppTextures = &pRenderTargetMSAA->pTexture;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gShadowCmpSampler);
            persistentSetParams[descriptorCount++].ppSamplers = &pSamplerComparisonShadow;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gClampMiplessLinearSampler);
            persistentSetParams[descriptorCount++].ppSamplers = &pSamplerMiplessLinear;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gClampBorderNearSampler);
            persistentSetParams[descriptorCount++].ppSamplers = &pSamplerMiplessClampToBorderNear;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gTextureSampler);
            persistentSetParams[descriptorCount++].ppSamplers = &pSamplerTrilinearAniso;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gClampToEdgeNearSampler);
            persistentSetParams[descriptorCount++].ppSamplers = &pSamplerMiplessNear;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gVBPassTexture);
            persistentSetParams[descriptorCount++].ppTextures = &pRenderTargetVBPass->pTexture;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gVertexNormalBuffer);
            persistentSetParams[descriptorCount++].ppBuffers = &pGeom->pVertexBuffers[2];
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gIndexTextureArray);
            persistentSetParams[descriptorCount].mCount = (gs_ASMMaxRefinement + 1) * 2;
            persistentSetParams[descriptorCount++].ppTextures = entireTextureList;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gDepthAtlasTexture);
            persistentSetParams[descriptorCount++].ppTextures = &pRenderTargetASMDepthAtlas->pTexture;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gDEMTexture);
            persistentSetParams[descriptorCount++].ppTextures = &pRenderTargetASMDEMAtlas->pTexture;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gPrerenderLodClampTexture);
            persistentSetParams[descriptorCount++].ppTextures = &pASM->m_longRangePreRender->m_lodClampTexture->pTexture;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gESMShadowTexture);
            persistentSetParams[descriptorCount++].ppTextures = &esmShadowMap;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gVSMShadowTexture);
            persistentSetParams[descriptorCount++].ppTextures =
                gCameraUniformData.mShadowType == SHADOW_TYPE_VSM ? &pRenderTargetVSM[0]->pTexture : &pDummyRenderTarget->pTexture;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gMSMShadowTexture);
            persistentSetParams[descriptorCount++].ppTextures =
                gCameraUniformData.mShadowType == SHADOW_TYPE_MSM ? &pRenderTargetMSM[0]->pTexture : &pDummyRenderTarget->pTexture;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gSDFShadowTexture);
            persistentSetParams[descriptorCount++].ppTextures = &sdfShadowTexture;
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gScreenSpaceShadowTexture);
            if (gSupportTextureAtomics)
            {
                persistentSetParams[descriptorCount++].ppTextures = &pTextureSSS;
            }
            else
            {
                persistentSetParams[descriptorCount++].ppBuffers = &pBufferSSS;
            }
            persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gVBConstantBuffer);
            persistentSetParams[descriptorCount++].ppBuffers = &pVisibilityBuffer->pVBConstantBuffer;

            if (gUsingTextureAtlasFallback)
            {
                persistentSetParams[descriptorCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gAtlasPlacementsBuffer);
                persistentSetParams[descriptorCount].mCount = 1;
                persistentSetParams[descriptorCount++].ppBuffers = &pPackage->pAtlasPlacementsBuffer;
            }

            updateDescriptorSet(pRenderer, 0, pDescriptorSetPersistent, descriptorCount, persistentSetParams);

            // Baked sdf mesh shadow
            {
                TFDescriptorData bakedSdfShadowSetParams[2] = {};
                bakedSdfShadowSetParams[0].mIndex = SRT_RES_IDX(BakedSDFMeshShadowSrtData, PerBatch, gOutTextureF2);
                bakedSdfShadowSetParams[0].ppTextures = &pTextureSDFMeshShadow;
                bakedSdfShadowSetParams[1].mIndex = SRT_RES_IDX(BakedSDFMeshShadowSrtData, PerBatch, gOutTexture);
                bakedSdfShadowSetParams[1].ppTextures = &pRenderTargetSDFMeshVisualization->pTexture;
                updateDescriptorSet(pRenderer, 0, pDescriptorSetBakedSDFMeshShadow, 2, bakedSdfShadowSetParams);

#ifdef QUEST_VR
                for (uint32_t i = 0; i < gDataBufferCount; ++i)
                {
                    for (uint32_t viewIndex = 0; viewIndex < VR_MULTIVIEW_COUNT; ++viewIndex)
                    {
                        TFDescriptorData perDrawSDFShadowParams[1] = {};
                        const uint32_t   descriptorIndex = i * VR_MULTIVIEW_COUNT + viewIndex;
                        perDrawSDFShadowParams[0].mIndex = SRT_RES_IDX(BakedSDFMeshShadowSrtData, PerDraw, gPerDrawSDFData);
                        perDrawSDFShadowParams[0].ppBuffers = &pBufferSDFPerDraw[descriptorIndex];
                        updateDescriptorSet(pRenderer, descriptorIndex, pDescriptorSetSDFShadowPerDraw, 1, perDrawSDFShadowParams);
                    }
                }
#endif
                // Update region 3d texture
                TFDescriptorData updateRegion3DTextureSetParams[1] = {};
                updateRegion3DTextureSetParams[0].mIndex = SRT_RES_IDX(UpdateRegion3DTextureSrtData, PerBatch, gSDFVolumeTextureAtlasRW);
                updateRegion3DTextureSetParams[0].ppTextures = gCameraUniformData.mShadowType == SHADOW_TYPE_MESH_BAKED_SDF
                                                                   ? &pTextureSDFVolumeAtlas
                                                                   : &pDummyRenderTarget3D->pTexture;
                updateDescriptorSet(pRenderer, 0, pDescriptorSetUpdateRegion3DTexture, 1, updateRegion3DTextureSetParams);
            }

            for (uint32_t i = 0; i < gDataBufferCount; ++i)
            {
                TFDescriptorData perFrameSetParams[12] = {};
                perFrameSetParams[0].mIndex = SRT_RES_IDX(SrtData, PerFrame, gPerFrameVBConstants);
                perFrameSetParams[0].ppBuffers = &pBufferVBConstants[i];
                perFrameSetParams[1].mIndex = SRT_RES_IDX(SrtData, PerFrame, gFilterDispatchGroupDataBuffer);
                perFrameSetParams[1].ppBuffers = &pVisibilityBuffer->ppFilterDispatchGroupDataBuffer[i];
                perFrameSetParams[2].mIndex = SRT_RES_IDX(SrtData, PerFrame, gIndirectDataBuffer);
                perFrameSetParams[2].ppBuffers = &pVisibilityBuffer->ppIndirectDataBuffer[i];
                perFrameSetParams[3].mIndex = SRT_RES_IDX(SrtData, PerFrame, gSDFVolumeDataBuffer);
                perFrameSetParams[3].ppBuffers = &pBufferSDFVolumeData[i];
                perFrameSetParams[4].mIndex = SRT_RES_IDX(SrtData, PerFrame, gUpdateSDFVolumeAtlas);
                perFrameSetParams[4].ppBuffers = &pBufferUpdateSDFVolumeTextureAtlasConstants[i];
                perFrameSetParams[5].mIndex = SRT_RES_IDX(SrtData, PerFrame, gCameraUniformBlock);
                perFrameSetParams[5].ppBuffers = &pBufferCameraUniform[i];
                perFrameSetParams[6].mIndex = SRT_RES_IDX(SrtData, PerFrame, gObjectUniformBlockPerFrame);
                perFrameSetParams[6].ppBuffers = &pBufferMeshTransforms[0][i];
                perFrameSetParams[7].mIndex = SRT_RES_IDX(SrtData, PerFrame, gIndirectDataBuffer);
                perFrameSetParams[7].ppBuffers = &pVisibilityBuffer->ppIndirectDataBuffer[i];
                perFrameSetParams[8].mIndex = SRT_RES_IDX(SrtData, PerFrame, gFilteredIndexBuffer);
                perFrameSetParams[8].ppBuffers = &pVisibilityBuffer->ppFilteredIndexBuffer[VIEW_CAMERA];
                perFrameSetParams[9].mIndex = SRT_RES_IDX(SrtData, PerFrame, gCameraUniformBlock);
                perFrameSetParams[9].ppBuffers = &pBufferCameraUniform[i];
                perFrameSetParams[10].mIndex = SRT_RES_IDX(SrtData, PerFrame, gASMUniformBlock);
                perFrameSetParams[10].ppBuffers = &pBufferASMDataUniform[i];
                perFrameSetParams[11].mIndex = SRT_RES_IDX(SrtData, PerFrame, gMeshSDFUniformBlock);
                perFrameSetParams[11].ppBuffers = &pBufferMeshSDFConstants[i];
                updateDescriptorSet(pRenderer, i, pDescriptorSetPerFrame, 12, perFrameSetParams);
            }

            // Triangle filtering set
            for (uint32_t i = 0; i < gDataBufferCount; ++i)
            {
                TFDescriptorData triangleFilteringParams[4] = {};
                triangleFilteringParams[0].mIndex = SRT_RES_IDX(TriangleFilteringSrtData, PerBatch, gIndirectDrawClearArgsRW);
                triangleFilteringParams[0].ppBuffers = &pVisibilityBuffer->ppIndirectDrawArgBuffer[0];
                triangleFilteringParams[1].mIndex = SRT_RES_IDX(TriangleFilteringSrtData, PerBatch, gFilteredIndicesBufferRW);
                triangleFilteringParams[1].mCount = NUM_CULLING_VIEWPORTS;
                triangleFilteringParams[1].ppBuffers = &pVisibilityBuffer->ppFilteredIndexBuffer[0];
                triangleFilteringParams[2].mIndex = SRT_RES_IDX(TriangleFilteringSrtData, PerBatch, gIndirectDataBufferRW);
                triangleFilteringParams[2].ppBuffers = &pVisibilityBuffer->ppIndirectDataBuffer[i];
                triangleFilteringParams[3].mIndex = SRT_RES_IDX(TriangleFilteringSrtData, PerBatch, gIndirectDrawFilteringArgsRW);
                triangleFilteringParams[3].ppBuffers = &pVisibilityBuffer->ppIndirectDrawArgBuffer[0];
                updateDescriptorSet(pRenderer, i, pDescriptorSetTriangleFiltering, 4, triangleFilteringParams);
            }
        }
        // Gaussian Blur
        TFDescriptorData BlurDescParams[1] = {};
        TFTexture*       textures[2] = {};
        {
            textures[0] = gCameraUniformData.mShadowType == SHADOW_TYPE_VSM ? pRenderTargetVSM[0]->pTexture : pDummyRenderTarget->pTexture;
            textures[1] = gCameraUniformData.mShadowType == SHADOW_TYPE_VSM ? pRenderTargetVSM[1]->pTexture : pDummyRenderTarget->pTexture;

            BlurDescParams[0].mIndex = SRT_RES_IDX(GaussianBlurSrtData, PerDraw, gShadowMapTextures);
            BlurDescParams[0].ppTextures = textures;
            BlurDescParams[0].mCount = 2;
            updateDescriptorSet(pRenderer, 0, pDescriptorSetGaussianBlurVSMPerDraw, 1, BlurDescParams);
        }

        {
            textures[0] = gCameraUniformData.mShadowType == SHADOW_TYPE_MSM ? pRenderTargetMSM[0]->pTexture : pDummyRenderTarget->pTexture;
            textures[1] = gCameraUniformData.mShadowType == SHADOW_TYPE_MSM ? pRenderTargetMSM[1]->pTexture : pDummyRenderTarget->pTexture;

            BlurDescParams[0].mIndex = SRT_RES_IDX(GaussianBlurSrtData, PerDraw, gShadowMapTextures);
            BlurDescParams[0].ppTextures = textures;
            BlurDescParams[0].mCount = 2;
            updateDescriptorSet(pRenderer, 0, pDescriptorSetGaussianBlurMSMPerDraw, 1, BlurDescParams);
        }

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            BlurDescParams[0].mIndex = SRT_RES_IDX(GaussianBlurSrtData, PerBatch, gGaussianBlurConstants);
            BlurDescParams[0].ppBuffers = &pBufferGaussianBlurConstants[i][0];
            BlurDescParams[0].mCount = 1;
            updateDescriptorSet(pRenderer, i * 2, pDescriptorSetGaussianBlurPerBatch, 1, BlurDescParams);

            BlurDescParams[0].ppBuffers = &pBufferGaussianBlurConstants[i][1];
            BlurDescParams[0].mCount = 1;
            updateDescriptorSet(pRenderer, i * 2 + 1, pDescriptorSetGaussianBlurPerBatch, 1, BlurDescParams);
        }

        // Present Pass
        {
            TFDescriptorData params[1] = {};
            params[0].mIndex = SRT_RES_IDX(DisplaySrtData, PerDraw, gSourceTexture);
            params[0].ppTextures = &pRenderTargetIntermediate->pTexture;
            updateDescriptorSet(pRenderer, 0, pDescriptorSetDisplayPerDraw, 1, params);
            params[0].ppTextures = &pRenderTargetSDFMeshVisualization->pTexture;
            updateDescriptorSet(pRenderer, 1, pDescriptorSetDisplayPerDraw, 1, params);
        }
        // ASM Atlas to Color
        {
            TFDescriptorData params[1] = {};
            params[0].mIndex = SRT_RES_IDX(QuadDataSrtData, PerBatch, gDepthPassTexture);
            params[0].ppTextures = &pRenderTargetASMDEMAtlas->pTexture;
            updateDescriptorSet(pRenderer, 0, pDescriptorSetAtlasToColor[0], 1, params);
            for (uint32_t i = 0; i < gDataBufferCount; ++i)
            {
                params[0].mIndex = SRT_RES_IDX(QuadDataSrtData, PerDraw, gPackedAtlasQuads_CB);
                params[0].ppBuffers = &pBufferASMAtlasToColorPackedQuadsUniform[i];
                updateDescriptorSet(pRenderer, i, pDescriptorSetAtlasToColor[1], 1, params);
            }
        }
        // ASM Color to Atlas
        {
            TFDescriptorData colorToAtlasParams[2] = {};
            colorToAtlasParams[0].mIndex = SRT_RES_IDX(QuadDataSrtData, PerBatch, gDepthPassTexture);
            colorToAtlasParams[0].ppTextures = &pRenderTargetASMColorPass->pTexture;
            updateDescriptorSet(pRenderer, 0, pDescriptorSetColorToAtlas[0], 1, colorToAtlasParams);
            for (uint32_t i = 0; i < gDataBufferCount; ++i)
            {
                colorToAtlasParams[0].mIndex = SRT_RES_IDX(QuadDataSrtData, PerDraw, gPackedAtlasQuads_CB);
                colorToAtlasParams[0].ppBuffers = &pBufferASMColorToAtlasPackedQuadsUniform[i];
                updateDescriptorSet(pRenderer, i, pDescriptorSetColorToAtlas[1], 1, colorToAtlasParams);
            }
        }
        // ASM Copy Depth
        {
            TFDescriptorData params[1] = {};
            params[0].mIndex = SRT_RES_IDX(QuadDataSrtData, PerBatch, gDepthPassTexture);
            params[0].ppTextures = &pRenderTargetASMDepthPass->pTexture;
            updateDescriptorSet(pRenderer, 0, pDescriptorSetAsmAtlasQuads[0], 1, params);
            for (uint32_t i = 0; i < gDataBufferCount; ++i)
            {
                params[0].mIndex = SRT_RES_IDX(QuadDataSrtData, PerDraw, gPackedAtlasQuads_CB);
                params[0].ppBuffers = &pBufferASMAtlasQuadsUniform[i];
                updateDescriptorSet(pRenderer, i, pDescriptorSetAsmAtlasQuads[1], 1, params);
            }
        }
        // Copy DEM
        {
            TFDescriptorData params[1] = {};
            params[0].mIndex = SRT_RES_IDX(QuadDataSrtData, PerBatch, gDepthPassTexture);
            params[0].ppTextures = &pRenderTargetASMDepthAtlas->pTexture;
            updateDescriptorSet(pRenderer, 0, pDescriptorSetAsmCopyDEM[0], 1, params);
            for (uint32_t i = 0; i < gDataBufferCount; ++i)
            {
                params[0].mIndex = SRT_RES_IDX(QuadDataSrtData, PerDraw, gPackedAtlasQuads_CB);
                params[0].ppBuffers = &pBufferASMCopyDEMPackedQuadsUniform[i];
                updateDescriptorSet(pRenderer, i, pDescriptorSetAsmCopyDEM[1], 1, params);
            }
        }
        // ASM Depth, VB Pass
        {
            for (uint32_t i = 0; i < gDataBufferCount; ++i)
            {
                TFDescriptorData objectParams[1] = {};
                objectParams[0].mIndex = SRT_RES_IDX(VisibilityBufferPassSrtData, PerDraw, gObjectUniformBlockPerDraw);
                objectParams[0].ppBuffers = &pBufferMeshTransforms[0][i];
                updateDescriptorSet(pRenderer, i, pDescriptorSetVBPass, 1, objectParams);

                objectParams[0].mIndex = SRT_RES_IDX(VisibilityBufferShadowPassSrtData, PerDraw, gObjectUniformBlockPerDraw);
                objectParams[0].ppBuffers = &pBufferMeshShadowProjectionTransforms[0][0][i];
                updateDescriptorSet(pRenderer, i, pDescriptorSetDepthVBPass, 1, objectParams);

                for (int j = 0; j < ASM_MAX_TILES_PER_PASS; j++)
                {
                    objectParams[0].mIndex = SRT_RES_IDX(VisibilityBufferShadowPassSrtData, PerDraw, gObjectUniformBlockPerDraw);
                    objectParams[0].ppBuffers = &pBufferMeshShadowProjectionTransforms[0][j][i];
                    updateDescriptorSet(pRenderer, i + j * gDataBufferCount, pDescriptorSetASMDepthPass, 1, objectParams);
                }
            }
        }
        // ASM Fill LOD Clamp
        {
            for (uint32_t i = 0; i < gDataBufferCount; ++i)
            {
                TFDescriptorData params[1] = {};
                params[0].mIndex = SRT_RES_IDX(QuadDataSrtData, PerDraw, gPackedAtlasQuads_CB);
                params[0].ppBuffers = &pBufferASMLodClampPackedQuadsUniform[i];
                updateDescriptorSet(pRenderer, i, pDescriptorSetAsmLodClamp, 1, params);
            }
        }
        // ASM Fill Indirection
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            TFDescriptorData params[1] = {};
            params[0].mIndex = SRT_RES_IDX(QuadDataSrtData, PerDraw, gPackedAtlasQuads_CB);
            params[0].ppBuffers = &pBufferASMClearIndirectionQuadsUniform[i];
            updateDescriptorSet(pRenderer, i, pDescriptorSetAsmClearIndirection, 1, params);

            for (uint32_t j = 0; j < gs_ASMMaxRefinement + 1; ++j)
            {
                params[0].ppBuffers = &pBufferASMPackedIndirectionQuadsUniform[j][i];
                updateDescriptorSet(pRenderer, i * (gs_ASMMaxRefinement + 1) + j, pDescriptorSetASMFillIndirection[0], 1, params);

                params[0].ppBuffers = &pBufferASMPackedPrerenderIndirectionQuadsUniform[j][i];
                updateDescriptorSet(pRenderer, i * (gs_ASMMaxRefinement + 1) + j, pDescriptorSetASMFillIndirection[1], 1, params);
            }
        }
        // Screen Space Shadow Mapping
        {
            TFDescriptorData params[2] = {};
            for (uint32_t viewIndex = 0; viewIndex < VR_MULTIVIEW_COUNT; viewIndex++)
            {
                for (uint32_t i = 0; i < gDataBufferCount; i++)
                {
                    params[0].mIndex = SRT_RES_IDX(ScreenSpaceShadowsSrtData, PerBatch, gSSSUniform);
                    params[0].ppBuffers = &pBufferSSSUniform[viewIndex][i];
                    params[0].mCount = 1;
                    params[1].mIndex = SRT_RES_IDX(ScreenSpaceShadowsSrtData, PerBatch, gOutputTexture);
                    params[1].mCount = 1;
                    if (gSupportTextureAtomics)
                    {
                        params[1].ppTextures = &pTextureSSS;
                    }
                    else
                    {
                        params[1].ppBuffers = &pBufferSSS;
                    }
                    updateDescriptorSet(pRenderer, viewIndex * gDataBufferCount + i, pDescriptorSetSSSPerBatch, 2, params);

                    for (uint32_t k = 0; k < MAX_SSS_WAVE_OFFSETS; k++)
                    {
                        params[0].mIndex = SRT_RES_IDX(ScreenSpaceShadowsSrtData, PerDraw, gSSSWaveOffsets);
                        params[0].ppBuffers = &pBufferSSSWaveOffsets[k][i];
                        params[0].mCount = 1;
                        updateDescriptorSet(pRenderer, i * MAX_SSS_WAVE_OFFSETS + k, pDescriptorSetSSSPerDraw, 1, params);
                    }
                }
            }
        }
    }

    void addShaders()
    {
        TFShaderLoadDesc indirectDepthPassShaderDesc = {};
        indirectDepthPassShaderDesc.mVert.pFileName = "meshDepthPass.vert";

        TFShaderLoadDesc indirectAlphaDepthPassShaderDesc = {};
        indirectAlphaDepthPassShaderDesc.mVert.pFileName = "meshDepthPassAlpha.vert";
        indirectAlphaDepthPassShaderDesc.mFrag.pFileName =
            gUsingTextureAtlasFallback ? "meshDepthPassAlpha_atlas.frag" : "meshDepthPassAlpha.frag";

        TFShaderLoadDesc indirectVSMDepthPassShaderDesc = {};
        indirectVSMDepthPassShaderDesc.mVert.pFileName = "meshDepthPass.vert";
        indirectVSMDepthPassShaderDesc.mFrag.pFileName =
            gFloat2RWTextureSupported ? "meshVSMDepthPass.frag" : "meshVSMDepthPass_F4_VSM.frag";

        TFShaderLoadDesc indirectMSMDepthPassShaderDesc = {};
        indirectMSMDepthPassShaderDesc.mVert.pFileName = "meshDepthPass.vert";
        indirectMSMDepthPassShaderDesc.mFrag.pFileName = "meshMSMDepthPass.frag";

        TFShaderLoadDesc indirectVSMAlphaDepthPassShaderDesc = {};
        indirectVSMAlphaDepthPassShaderDesc.mVert.pFileName = "meshDepthPassAlpha.vert";

        if (gUsingTextureAtlasFallback)
        {
            indirectVSMAlphaDepthPassShaderDesc.mFrag.pFileName =
                gFloat2RWTextureSupported ? "meshVSMDepthPassAlpha_atlas.frag" : "meshVSMDepthPassAlpha_atlas_F4_VSM.frag";
        }
        else
        {
            indirectVSMAlphaDepthPassShaderDesc.mFrag.pFileName =
                gFloat2RWTextureSupported ? "meshVSMDepthPassAlpha.frag" : "meshVSMDepthPassAlpha_F4_VSM.frag";
        }

        TFShaderLoadDesc indirectMSMAlphaDepthPassShaderDesc = {};
        indirectMSMAlphaDepthPassShaderDesc.mVert.pFileName = "meshDepthPassAlpha.vert";
        indirectMSMAlphaDepthPassShaderDesc.mFrag.pFileName =
            gUsingTextureAtlasFallback ? "meshMSMDepthPassAlpha_atlas.frag" : "meshMSMDepthPassAlpha.frag";

        TFShaderLoadDesc ASMCopyDepthQuadsShaderDesc = {};
        ASMCopyDepthQuadsShaderDesc.mVert.pFileName = "copyDepthQuads.vert";
        ASMCopyDepthQuadsShaderDesc.mFrag.pFileName = "copyDepthQuads.frag";

        TFShaderLoadDesc ASMFillIndirectionShaderDesc = {};
        ASMFillIndirectionShaderDesc.mVert.pFileName = "fill_Indirection.vert";
        ASMFillIndirectionShaderDesc.mFrag.pFileName = "fill_Indirection.frag";

        TFShaderLoadDesc ASMCopyDEMQuadsShaderDesc = {};
        ASMCopyDEMQuadsShaderDesc.mVert.pFileName = "copyDEMQuads.vert";
        ASMCopyDEMQuadsShaderDesc.mFrag.pFileName = "copyDEMQuads.frag";

        addShader(pRenderer, &ASMCopyDEMQuadsShaderDesc, &pShaderASMCopyDEM);

        TFShaderLoadDesc ASMGenerateDEMShaderDesc = {};
        ASMGenerateDEMShaderDesc.mVert.pFileName = "generateAsmDEM.vert";
        ASMGenerateDEMShaderDesc.mFrag.pFileName = "generateAsmDEM.frag";
        addShader(pRenderer, &ASMGenerateDEMShaderDesc, &pShaderASMGenerateDEM);

        TFShaderLoadDesc visibilityBufferPassShaderDesc = {};
        visibilityBufferPassShaderDesc.mVert.pFileName =
            gUsingPrimitiveIDFallback ? "visibilityBufferPass_primid.vert" : "visibilityBufferPass.vert";
        visibilityBufferPassShaderDesc.mFrag.pFileName =
            gUsingPrimitiveIDFallback ? "visibilityBufferPass_primid.frag" : "visibilityBufferPass.frag";

        // Some vulkan driver doesn't generate glPrimitiveID without a geometry pass (steam deck as 03/30/2023)
        bool addGeometryPassThrough = gGpuSettings.mAddGeometryPassThrough;
        if (addGeometryPassThrough)
        {
            // A passthrough gs
            visibilityBufferPassShaderDesc.mGeom.pFileName = "visibilityBufferPass.geom";
        }

        addShader(pRenderer, &visibilityBufferPassShaderDesc, &pShaderVBBufferPass[GEOMSET_OPAQUE]);

#if TEST_GPU_BREADCRUMBS
        // VB crash shader
        TFShaderLoadDesc vbCrashDesc = visibilityBufferPassShaderDesc;
        vbCrashDesc.mVert.pFileName =
            gUsingPrimitiveIDFallback ? "visibilityBufferPassCrash_primid.vert" : "visibilityBufferPassCrash.vert";
        addShader(pRenderer, &vbCrashDesc, &pShaderVBBufferCrashPass);
#endif

        TFShaderLoadDesc visibilityBufferPassAlphaShaderDesc = {};
        visibilityBufferPassAlphaShaderDesc.mVert.pFileName =
            gUsingPrimitiveIDFallback ? "visibilityBufferPassAlpha_primid.vert" : "visibilityBufferPassAlpha.vert";
        visibilityBufferPassAlphaShaderDesc.mFrag.pFileName =
            gUsingPrimitiveIDFallback ? "visibilityBufferPassAlpha_primid.frag" : "visibilityBufferPassAlpha.frag";

        if (gUsingTextureAtlasFallback)
        {
            visibilityBufferPassAlphaShaderDesc.mFrag.pFileName =
                gUsingPrimitiveIDFallback ? "visibilityBufferPassAlpha_atlas_primid.frag" : "visibilityBufferPassAlpha_atlas.frag";
        }

        if (addGeometryPassThrough)
        {
            // A passthrough gs
            visibilityBufferPassAlphaShaderDesc.mGeom.pFileName = "visibilityBufferPassAlpha.geom";
        }

        addShader(pRenderer, &visibilityBufferPassAlphaShaderDesc, &pShaderVBBufferPass[GEOMSET_ALPHA_CUTOUT]);

        TFShaderLoadDesc clearBuffersShaderDesc = {};
        clearBuffersShaderDesc.mComp.pFileName = "clearVisibilityBuffers.comp";
        addShader(pRenderer, &clearBuffersShaderDesc, &pShaderClearBuffers);

        TFShaderLoadDesc triangleFilteringShaderDesc = {};
        triangleFilteringShaderDesc.mComp.pFileName = "triangleFiltering.comp";
        addShader(pRenderer, &triangleFilteringShaderDesc, &pShaderTriangleFiltering);

        TFShaderLoadDesc BlurCompShaderDesc = {};

        BlurCompShaderDesc.mComp.pFileName = "gaussianBlur.comp";
        addShader(pRenderer, &BlurCompShaderDesc, &pShaderBlurComp);

        TFShaderLoadDesc updateSDFVolumeTextureAtlasShaderDesc = {};
        updateSDFVolumeTextureAtlasShaderDesc.mComp.pFileName = "updateRegion3DTexture.comp";

        addShader(pRenderer, &updateSDFVolumeTextureAtlasShaderDesc, &pShaderUpdateSDFVolumeTextureAtlas);

        const char* visualizeSDFMesh[] = {
            "visualizeSDFMesh_SAMPLE_COUNT_1.comp",
            "visualizeSDFMesh_SAMPLE_COUNT_2.comp",
            "visualizeSDFMesh_SAMPLE_COUNT_4.comp",
        };

        const char* bakedSDFMeshShadow[] = {
            "bakedSDFMeshShadow_SAMPLE_COUNT_1.comp",
            "bakedSDFMeshShadow_SAMPLE_COUNT_2.comp",
            "bakedSDFMeshShadow_SAMPLE_COUNT_4.comp",
        };

        const char* upsampleSDFShadow[] = {
            "upsampleSDFShadow_SAMPLE_COUNT_1.frag",
            "upsampleSDFShadow_SAMPLE_COUNT_2.frag",
            "upsampleSDFShadow_SAMPLE_COUNT_4.frag",
        };

        const char* visibilityBufferShade[] = {
            "visibilityBufferShade_SAMPLE_COUNT_1.frag",
            "visibilityBufferShade_SAMPLE_COUNT_2.frag",
            "visibilityBufferShade_SAMPLE_COUNT_4.frag",
        };

        const char* visibilityBufferShadeUseFloat4Vsm[] = {
            "visibilityBufferShade_F4_VSM_SAMPLE_COUNT_1.frag",
            "visibilityBufferShade_F4_VSM_SAMPLE_COUNT_2.frag",
            "visibilityBufferShade_F4_VSM_SAMPLE_COUNT_4.frag",
        };

        const char* visibilityBufferShadeAtlas[] = {
            "visibilityBufferShade_atlas_SAMPLE_COUNT_1.frag",
            "visibilityBufferShade_atlas_SAMPLE_COUNT_2.frag",
            "visibilityBufferShade_atlas_SAMPLE_COUNT_4.frag",
        };

        const char* visibilityBufferShadeAtlasUseFloat4Vsm[] = {
            "visibilityBufferShade_atlas_F4_VSM_SAMPLE_COUNT_1.frag",
            "visibilityBufferShade_atlas_F4_VSM_SAMPLE_COUNT_2.frag",
            "visibilityBufferShade_atlas_F4_VSM_SAMPLE_COUNT_4.frag",
        };

        const char* resolve[] = {
            "progMSAAResolve_SAMPLE_COUNT_1.frag",
            "progMSAAResolve_SAMPLE_COUNT_2.frag",
            "progMSAAResolve_SAMPLE_COUNT_4.frag",
        };

        TFWaveOpsSupportFlags waveFlags = pRenderer->pGpu->mWaveOpsSupportFlags;

        const char* ScreenSpaceShadowsShaders[] = { "screenSpaceShadows_SAMPLE_COUNT_1.comp", "screenSpaceShadows_SAMPLE_COUNT_2.comp",
                                                    "screenSpaceShadows_SAMPLE_COUNT_4.comp" };

        const char* ScreenSpaceShadowsShadersWaveSupport[] = { "screenSpaceShadows_wave_SAMPLE_COUNT_1.comp",
                                                               "screenSpaceShadows_wave_SAMPLE_COUNT_2.comp",
                                                               "screenSpaceShadows_wave_SAMPLE_COUNT_4.comp" };

        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT; ++i)
        {
            TFShaderLoadDesc meshSDFVisualizationShaderDesc = {};
            meshSDFVisualizationShaderDesc.mComp.pFileName = visualizeSDFMesh[i];
            addShader(pRenderer, &meshSDFVisualizationShaderDesc, &pShaderSDFMeshVisualization[i]);

            TFShaderLoadDesc sdfShadowMeshShaderDesc = {};
            sdfShadowMeshShaderDesc.mComp.pFileName = bakedSDFMeshShadow[i];
            addShader(pRenderer, &sdfShadowMeshShaderDesc, &pShaderSDFMeshShadow[i]);

            TFShaderLoadDesc upSampleSDFShadowShaderDesc = {};
            upSampleSDFShadowShaderDesc.mVert.pFileName = "upsampleSDFShadow.vert";
            upSampleSDFShadowShaderDesc.mFrag.pFileName = upsampleSDFShadow[i];
            addShader(pRenderer, &upSampleSDFShadowShaderDesc, &pShaderUpsampleSDFShadow[i]);

            TFShaderLoadDesc visibilityBufferShadeShaderDesc = {};
            visibilityBufferShadeShaderDesc.mVert.pFileName = "visibilityBufferShade.vert";
            if (gUsingTextureAtlasFallback)
            {
                visibilityBufferShadeShaderDesc.mFrag.pFileName =
                    gFloat2RWTextureSupported ? visibilityBufferShadeAtlas[i] : visibilityBufferShadeAtlasUseFloat4Vsm[i];
            }
            else
            {
                visibilityBufferShadeShaderDesc.mFrag.pFileName =
                    gFloat2RWTextureSupported ? visibilityBufferShade[i] : visibilityBufferShadeUseFloat4Vsm[i];
            }
            addShader(pRenderer, &visibilityBufferShadeShaderDesc, &pShaderVBShade[i]);

            // Resolve shader
            TFShaderLoadDesc resolvePass = {};
            resolvePass.mVert.pFileName = "progMSAAResolve.vert";
            resolvePass.mFrag.pFileName = resolve[i];
            addShader(pRenderer, &resolvePass, &pShaderResolve[i]);

            TFShaderLoadDesc screenSpaceShadowsShaderDesc = {};
            if ((waveFlags & TF_WAVE_OPS_SUPPORT_FLAG_BASIC_BIT) && (waveFlags & TF_WAVE_OPS_SUPPORT_FLAG_ARITHMETIC_BIT))
            {
                screenSpaceShadowsShaderDesc.mComp.pFileName = ScreenSpaceShadowsShadersWaveSupport[i];
            }
            else
            {
                screenSpaceShadowsShaderDesc.mComp.pFileName = ScreenSpaceShadowsShaders[i];
            }
            addShader(pRenderer, &screenSpaceShadowsShaderDesc, &pShaderSSS[i]);
        }

        TFShaderLoadDesc msaaEdgesShader[MSAA_LEVELS_COUNT - 1] = {};
        const char*      edgeDetectShaders[] = { "msaa_edge_detect_SAMPLE_2.frag", "msaa_edge_detect_SAMPLE_4.frag" };
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT - 1; ++i)
        {
            msaaEdgesShader[i].mVert.pFileName = "display.vert";
            msaaEdgesShader[i].mFrag.pFileName = edgeDetectShaders[i];
        }
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT - 1; ++i)
        {
            addShader(pRenderer, &msaaEdgesShader[i], &pShaderDrawMSAAEdges[i]);
        }

#if TEST_GPU_BREADCRUMBS
        // Crash shaders
        TFShaderLoadDesc vbShadeCrashShaderDesc = {};
        vbShadeCrashShaderDesc.mVert.pFileName = "visibilityBufferShade.vert";
        vbShadeCrashShaderDesc.mFrag.pFileName =
            gUsingTextureAtlasFallback ? "visibilityBufferShadeCrash_atlas.frag" : "visibilityBufferShadeCrash.frag";
        addShader(pRenderer, &vbShadeCrashShaderDesc, &pShaderVBShadeCrash);
        TFShaderLoadDesc sssCrashShaderDesc = {};
        sssCrashShaderDesc.mComp.pFileName = "screenSpaceShadowsCrash.comp";
        addShader(pRenderer, &sssCrashShaderDesc, &pShaderSSSCrash);
#endif

        TFShaderLoadDesc screenSpaceShadowsShaderDesc = {};
        screenSpaceShadowsShaderDesc.mComp.pFileName = "clearScreenSpaceShadows.comp";
        addShader(pRenderer, &screenSpaceShadowsShaderDesc, &pShaderSSSClear);

        TFShaderLoadDesc presentShaderDesc = {};
        presentShaderDesc.mVert.pFileName = "display.vert";
        presentShaderDesc.mFrag.pFileName = "display.frag";
        addShader(pRenderer, &presentShaderDesc, &pShaderPresentPass);

        /************************************************************************/
        // Add shaders
        /************************************************************************/
        addShader(pRenderer, &indirectAlphaDepthPassShaderDesc, &pShaderIndirectAlphaDepthPass);
        addShader(pRenderer, &indirectDepthPassShaderDesc, &pShaderIndirectDepthPass);

        addShader(pRenderer, &indirectVSMAlphaDepthPassShaderDesc, &pShaderIndirectVSMAlphaDepthPass);
        addShader(pRenderer, &indirectVSMDepthPassShaderDesc, &pShaderIndirectVSMDepthPass);

        addShader(pRenderer, &indirectMSMAlphaDepthPassShaderDesc, &pShaderIndirectMSMAlphaDepthPass);
        addShader(pRenderer, &indirectMSMDepthPassShaderDesc, &pShaderIndirectMSMDepthPass);

        addShader(pRenderer, &ASMCopyDepthQuadsShaderDesc, &pShaderASMCopyDepthQuadPass);
        addShader(pRenderer, &ASMFillIndirectionShaderDesc, &pShaderASMFillIndirection);
#if defined(ORBIS) || defined(PROSPERO)
        ASMFillIndirectionShaderDesc.mFrag.pFileName = "fill_Indirection_fp16.frag";
        addShader(pRenderer, &ASMFillIndirectionShaderDesc, &pShaderASMFillIndirectionFP16);
#endif
    }

    void removeShaders()
    {
        removeShader(pRenderer, pShaderPresentPass);

        removeShader(pRenderer, pShaderASMCopyDEM);
        removeShader(pRenderer, pShaderASMCopyDepthQuadPass);
        removeShader(pRenderer, pShaderIndirectDepthPass);
        removeShader(pRenderer, pShaderIndirectAlphaDepthPass);

        removeShader(pRenderer, pShaderIndirectVSMDepthPass);
        removeShader(pRenderer, pShaderIndirectVSMAlphaDepthPass);

        removeShader(pRenderer, pShaderIndirectMSMDepthPass);
        removeShader(pRenderer, pShaderIndirectMSMAlphaDepthPass);
        removeShader(pRenderer, pShaderBlurComp);

        removeShader(pRenderer, pShaderASMFillIndirection);
#if defined(ORBIS) || defined(PROSPERO)
        removeShader(pRenderer, pShaderASMFillIndirectionFP16);
#endif
        removeShader(pRenderer, pShaderASMGenerateDEM);

        for (uint32_t i = 0; i < gNumGeomSets; ++i)
        {
            removeShader(pRenderer, pShaderVBBufferPass[i]);
        }

        removeShader(pRenderer, pShaderClearBuffers);
        removeShader(pRenderer, pShaderTriangleFiltering);

        removeShader(pRenderer, pShaderUpdateSDFVolumeTextureAtlas);
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT; ++i)
        {
            removeShader(pRenderer, pShaderVBShade[i]);
            removeShader(pRenderer, pShaderResolve[i]);
            removeShader(pRenderer, pShaderSDFMeshVisualization[i]);
            removeShader(pRenderer, pShaderSDFMeshShadow[i]);
            removeShader(pRenderer, pShaderUpsampleSDFShadow[i]);
            removeShader(pRenderer, pShaderSSS[i]);
        }
        removeShader(pRenderer, pShaderSSSClear);

#if TEST_GPU_BREADCRUMBS
        removeShader(pRenderer, pShaderVBBufferCrashPass);
        removeShader(pRenderer, pShaderSSSCrash);
        removeShader(pRenderer, pShaderVBShadeCrash);
#endif
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT - 1; ++i)
        {
            removeShader(pRenderer, pShaderDrawMSAAEdges[i]);
        }
    }

    void addPipelines()
    {
        TFPipelineDesc desc = {};
        desc.mType = TF_PIPELINE_TYPE_GRAPHICS;

        TFDepthStateDesc      depthStateDisableDesc = {};
        /************************************************************************/
        // Setup MSAA resolve pipeline
        /************************************************************************/
        TFRasterizerStateDesc rasterizerStateCullNoneDesc = { TF_CULL_MODE_NONE };

        desc.mGraphicsDesc = {};
        PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
        desc.mType = TF_PIPELINE_TYPE_GRAPHICS;
        TFGraphicsPipelineDesc& resolvePipelineSettings = desc.mGraphicsDesc;
        resolvePipelineSettings = { 0 };
        resolvePipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        resolvePipelineSettings.mRenderTargetCount = 1;
        resolvePipelineSettings.pDepthState = &depthStateDisableDesc;
        resolvePipelineSettings.pColorFormats = &pRenderTargetIntermediate->mFormat;
        resolvePipelineSettings.mSampleCount = TF_SAMPLE_COUNT_1;
        resolvePipelineSettings.mSampleQuality = 0;
        resolvePipelineSettings.pRasterizerState = &rasterizerStateCullNoneDesc;
        resolvePipelineSettings.pShaderProgram = pShaderResolve[gAppSettings.mMsaaIndex];
        addPipeline(pRenderer, &desc, &pPipelineResolve);

        /************************************************************************/
        // Setup vertex layout for all shaders
        /************************************************************************/
        TFVertexLayout vertexLayoutQuad = {};
        vertexLayoutQuad.mBindingCount = 1;
        vertexLayoutQuad.mAttribCount = 2;
        vertexLayoutQuad.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
        vertexLayoutQuad.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
        vertexLayoutQuad.mAttribs[0].mBinding = 0;
        vertexLayoutQuad.mAttribs[0].mLocation = 0;
        vertexLayoutQuad.mAttribs[0].mOffset = 0;

        vertexLayoutQuad.mAttribs[1].mSemantic = TF_SEMANTIC_TEXCOORD0;
        vertexLayoutQuad.mAttribs[1].mFormat = TinyImageFormat_R32G32_SFLOAT;
        vertexLayoutQuad.mAttribs[1].mBinding = 0;
        vertexLayoutQuad.mAttribs[1].mLocation = 1;
        vertexLayoutQuad.mAttribs[1].mOffset = 4 * sizeof(float);

        TFDepthStateDesc depthStateEnabledDesc = {};
        depthStateEnabledDesc.mDepthFunc = TF_CMP_GEQUAL;
        depthStateEnabledDesc.mDepthWrite = true;
        depthStateEnabledDesc.mDepthTest = true;

        TFRasterizerStateDesc rasterStateCullNoneDesc = { TF_CULL_MODE_NONE };
        TFRasterizerStateDesc rasterStateCullNoneMsDesc = { TF_CULL_MODE_NONE, 0, 0, TF_FILL_MODE_SOLID };
        rasterStateCullNoneMsDesc.mMultiSample = true;

        /************************************************************************/
        // Setup the resources needed for upsaming sdf model scene
        /******************************/
        desc.mGraphicsDesc = {};
        TFGraphicsPipelineDesc& upSampleSDFShadowPipelineSettings = desc.mGraphicsDesc;
        upSampleSDFShadowPipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        upSampleSDFShadowPipelineSettings.mRenderTargetCount = 1;
        upSampleSDFShadowPipelineSettings.pDepthState = NULL;
        upSampleSDFShadowPipelineSettings.pRasterizerState = &rasterStateCullNoneDesc;
        upSampleSDFShadowPipelineSettings.pShaderProgram = pShaderUpsampleSDFShadow[gAppSettings.mMsaaIndex];
        upSampleSDFShadowPipelineSettings.mSampleCount = TF_SAMPLE_COUNT_1;
        upSampleSDFShadowPipelineSettings.pColorFormats = &pRenderTargetUpSampleSDFShadow->mFormat;
        upSampleSDFShadowPipelineSettings.mSampleQuality = pRenderTargetUpSampleSDFShadow->mSampleQuality;
        upSampleSDFShadowPipelineSettings.pVertexLayout = &vertexLayoutQuad;

        addPipeline(pRenderer, &desc, &pPipelineUpsampleSDFShadow);

        /************************************************************************/
        // Setup the resources needed for the Visibility Buffer Pipeline
        /******************************/
        desc.mGraphicsDesc = {};
        TFGraphicsPipelineDesc& vbPassPipelineSettings = desc.mGraphicsDesc;
        vbPassPipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        vbPassPipelineSettings.mRenderTargetCount = 1;
        vbPassPipelineSettings.pDepthState = &depthStateEnabledDesc;
        vbPassPipelineSettings.pColorFormats = &pRenderTargetVBPass->mFormat;
        vbPassPipelineSettings.mSampleCount = pRenderTargetVBPass->mSampleCount;
        vbPassPipelineSettings.mSampleQuality = pRenderTargetVBPass->mSampleQuality;
        vbPassPipelineSettings.mDepthStencilFormat = pRenderTargetDepth->mFormat;
        vbPassPipelineSettings.pVertexLayout = NULL;
        vbPassPipelineSettings.pRasterizerState =
            gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1 ? &rasterStateCullNoneMsDesc : &rasterStateCullNoneDesc;
        PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(VisibilityBufferPassSrtData, Persistent),
                             SRT_LAYOUT_DESC(VisibilityBufferPassSrtData, PerFrame), NULL,
                             SRT_LAYOUT_DESC(VisibilityBufferPassSrtData, PerDraw));
        for (uint32_t i = 0; i < gNumGeomSets; ++i)
        {
            vbPassPipelineSettings.pShaderProgram = pShaderVBBufferPass[i];

#if defined(GFX_EXTENDED_PSO_OPTIONS)
            ExtendedGraphicsPipelineDesc edescs[2] = {};
            edescs[0].type = EXTENDED_GRAPHICS_PIPELINE_TYPE_SHADER_LIMITS;
            initExtendedGraphicsShaderLimits(&edescs[0].shaderLimitsDesc);
            edescs[0].shaderLimitsDesc.maxWavesWithLateAllocParameterCache = 16;

            edescs[1].type = EXTENDED_GRAPHICS_PIPELINE_TYPE_PIXEL_SHADER_OPTIONS;
            edescs[1].pixelShaderOptions.outOfOrderRasterization = PIXEL_SHADER_OPTION_OUT_OF_ORDER_RASTERIZATION_ENABLE_WATER_MARK_7;
            edescs[1].pixelShaderOptions.depthBeforeShader =
                !i ? PIXEL_SHADER_OPTION_DEPTH_BEFORE_SHADER_ENABLE : PIXEL_SHADER_OPTION_DEPTH_BEFORE_SHADER_DEFAULT;

            desc.mExtensionCount = 2;
            desc.pPipelineExtensions = edescs;
#endif
            addPipeline(pRenderer, &desc, &pPipelineVBBufferPass[i]);

            desc.mExtensionCount = 0;
        }

#if TEST_GPU_BREADCRUMBS
        vbPassPipelineSettings.pRasterizerState = &rasterStateCullNoneDesc;
        vbPassPipelineSettings.pShaderProgram = pShaderVBBufferCrashPass;
        addPipeline(pRenderer, &desc, &pPipelineVBBufferCrashPass);
#endif

        TFDepthStateDesc depthStateOnlyReadStencilDesc = {};
        depthStateOnlyReadStencilDesc.mStencilWriteMask = 0x00;
        depthStateOnlyReadStencilDesc.mStencilReadMask = 0xFF;
        depthStateOnlyReadStencilDesc.mStencilTest = true;
        depthStateOnlyReadStencilDesc.mStencilFrontFunc = TF_CMP_EQUAL;
        depthStateOnlyReadStencilDesc.mStencilFrontFail = TF_STENCIL_OP_KEEP;
        depthStateOnlyReadStencilDesc.mStencilFrontPass = TF_STENCIL_OP_KEEP;
        depthStateOnlyReadStencilDesc.mDepthFrontFail = TF_STENCIL_OP_KEEP;
        depthStateOnlyReadStencilDesc.mStencilBackFunc = TF_CMP_EQUAL;
        depthStateOnlyReadStencilDesc.mStencilBackFail = TF_STENCIL_OP_KEEP;
        depthStateOnlyReadStencilDesc.mStencilBackPass = TF_STENCIL_OP_KEEP;
        depthStateOnlyReadStencilDesc.mDepthBackFail = TF_STENCIL_OP_KEEP;

        desc.mGraphicsDesc = {};
        bool                    isMSAAEnabled = gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1;
        TFGraphicsPipelineDesc& vbShadePipelineSettings = desc.mGraphicsDesc;
        vbShadePipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        vbShadePipelineSettings.mRenderTargetCount = 1;
        vbShadePipelineSettings.pDepthState = isMSAAEnabled ? &depthStateOnlyReadStencilDesc : &depthStateDisableDesc;
        vbShadePipelineSettings.mDepthStencilFormat = isMSAAEnabled ? pRenderTargetMSAAEdges->mFormat : TinyImageFormat_UNDEFINED;
        vbShadePipelineSettings.pRasterizerState = isMSAAEnabled ? &rasterStateCullNoneMsDesc : &rasterStateCullNoneDesc;
        vbShadePipelineSettings.pShaderProgram = pShaderVBShade[gAppSettings.mMsaaIndex];
        vbShadePipelineSettings.mSampleCount = gAppSettings.mMsaaLevel;

        if (gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1)
        {
            vbShadePipelineSettings.pColorFormats = &pRenderTargetMSAA->mFormat;
            vbShadePipelineSettings.mSampleQuality = pRenderTargetMSAA->mSampleQuality;
        }
        else
        {
            vbShadePipelineSettings.pColorFormats = &pRenderTargetIntermediate->mFormat;
            vbShadePipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        }
#if defined(GFX_EXTENDED_PSO_OPTIONS)
        ExtendedGraphicsPipelineDesc edescs[2] = {};
        edescs[0].type = EXTENDED_GRAPHICS_PIPELINE_TYPE_SHADER_LIMITS;
        initExtendedGraphicsShaderLimits(&edescs[0].shaderLimitsDesc);
        // edescs[0].ShaderLimitsDesc.MaxWavesWithLateAllocParameterCache = 22;

        edescs[1].type = EXTENDED_GRAPHICS_PIPELINE_TYPE_PIXEL_SHADER_OPTIONS;
        edescs[1].pixelShaderOptions.outOfOrderRasterization = PIXEL_SHADER_OPTION_OUT_OF_ORDER_RASTERIZATION_ENABLE_WATER_MARK_7;
        edescs[1].pixelShaderOptions.depthBeforeShader = PIXEL_SHADER_OPTION_DEPTH_BEFORE_SHADER_ENABLE;

        desc.mExtensionCount = 2;
        desc.pPipelineExtensions = edescs;
#endif
        addPipeline(pRenderer, &desc, &pPipelineVBShadeSrgb);
#if TEST_GPU_BREADCRUMBS
        vbShadePipelineSettings.pColorFormats = &pRenderTargetIntermediate->mFormat;
        vbShadePipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        vbShadePipelineSettings.pShaderProgram = pShaderVBShadeCrash;
        addPipeline(pRenderer, &desc, &pPipelineVBShadeSrgbCrash);
#endif

        desc.mExtensionCount = 0;
        desc.mGraphicsDesc = {};
        PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(TriangleFilteringSrtData, Persistent),
                             SRT_LAYOUT_DESC(TriangleFilteringSrtData, PerFrame), SRT_LAYOUT_DESC(TriangleFilteringSrtData, PerBatch),
                             NULL);
        desc.mType = TF_PIPELINE_TYPE_COMPUTE;
        desc.mComputeDesc = {};

        TFComputePipelineDesc& clearBufferPipelineSettings = desc.mComputeDesc;
        clearBufferPipelineSettings.pShaderProgram = pShaderClearBuffers;
        addPipeline(pRenderer, &desc, &pPipelineClearBuffers);

        desc.mComputeDesc = {};
        TFComputePipelineDesc& triangleFilteringPipelineSettings = desc.mComputeDesc;
        triangleFilteringPipelineSettings.pShaderProgram = pShaderTriangleFiltering;
        addPipeline(pRenderer, &desc, &pPipelineTriangleFiltering);

        desc.mComputeDesc = {};
        PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(GaussianBlurSrtData, Persistent), SRT_LAYOUT_DESC(GaussianBlurSrtData, PerFrame),
                             SRT_LAYOUT_DESC(GaussianBlurSrtData, PerBatch), SRT_LAYOUT_DESC(GaussianBlurSrtData, PerDraw));

        TFComputePipelineDesc& BlurCompPipelineSettings = desc.mComputeDesc;
        BlurCompPipelineSettings.pShaderProgram = pShaderBlurComp;
        addPipeline(pRenderer, &desc, &pPipelineBlur);

        /*-----------------------------------------------------------*/
        // Setup the resources needed SDF volume texture update
        /*-----------------------------------------------------------*/
        PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(UpdateRegion3DTextureSrtData, Persistent),
                             SRT_LAYOUT_DESC(UpdateRegion3DTextureSrtData, PerFrame),
                             SRT_LAYOUT_DESC(UpdateRegion3DTextureSrtData, PerBatch), NULL);
        desc.mComputeDesc = {};
        TFComputePipelineDesc& updateSDFVolumeTexturePipeline = desc.mComputeDesc;
        updateSDFVolumeTexturePipeline.pShaderProgram = pShaderUpdateSDFVolumeTextureAtlas;
        addPipeline(pRenderer, &desc, &pPipelineUpdateSDFVolumeTextureAtlas);

        /*-----------------------------------------------------------*/
        // Setup the resources needed SDF mesh visualization
        /*-----------------------------------------------------------*/
        desc.mComputeDesc = {};
        PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(BakedSDFMeshShadowSrtData, Persistent),
                             SRT_LAYOUT_DESC(BakedSDFMeshShadowSrtData, PerFrame), SRT_LAYOUT_DESC(BakedSDFMeshShadowSrtData, PerBatch),
#ifdef QUEST_VR
                             SRT_LAYOUT_DESC(BakedSDFMeshShadowSrtData, PerDraw)
#else
                             NULL
#endif
        );
        TFComputePipelineDesc& sdfMeshVisualizationDesc = desc.mComputeDesc;
        sdfMeshVisualizationDesc.pShaderProgram = pShaderSDFMeshVisualization[gAppSettings.mMsaaIndex];
        addPipeline(pRenderer, &desc, &pPipelineSDFMeshVisualization);

        desc.mComputeDesc = {};
        PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(BakedSDFMeshShadowSrtData, Persistent),
                             SRT_LAYOUT_DESC(BakedSDFMeshShadowSrtData, PerFrame), SRT_LAYOUT_DESC(BakedSDFMeshShadowSrtData, PerBatch),
#ifdef QUEST_VR
                             SRT_LAYOUT_DESC(BakedSDFMeshShadowSrtData, PerDraw)
#else
                             NULL
#endif
        );
        TFComputePipelineDesc& sdfMeshShadowDesc = desc.mComputeDesc;
        sdfMeshShadowDesc.pShaderProgram = pShaderSDFMeshShadow[gAppSettings.mMsaaIndex];
        addPipeline(pRenderer, &desc, &pPipelineSDFMeshShadow);

        desc.mComputeDesc = {};
        PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(ScreenSpaceShadowsSrtData, Persistent),
                             SRT_LAYOUT_DESC(ScreenSpaceShadowsSrtData, PerFrame), SRT_LAYOUT_DESC(ScreenSpaceShadowsSrtData, PerBatch),
                             SRT_LAYOUT_DESC(ScreenSpaceShadowsSrtData, PerDraw));
        TFComputePipelineDesc& screenSpaceShadowDesc = desc.mComputeDesc;
        screenSpaceShadowDesc.pShaderProgram = pShaderSSS[gAppSettings.mMsaaIndex];
        addPipeline(pRenderer, &desc, &pPipelineSSS);
#if TEST_GPU_BREADCRUMBS
        screenSpaceShadowDesc.pShaderProgram = pShaderSSSCrash;
        addPipeline(pRenderer, &desc, &pPipelineSSSCrash);
#endif

        desc.mComputeDesc = {};
        TFComputePipelineDesc& screenSpaceShadowClearDesc = desc.mComputeDesc;
        screenSpaceShadowClearDesc.pShaderProgram = pShaderSSSClear;
        addPipeline(pRenderer, &desc, &pPipelineSSSClear);

        /************************************************************************/
        // Setup the resources needed SDF volume texture update
        /************************************************************************/
        /************************************************************************/
        // Setup the resources needed for Sdf box
        /************************************************************************/
        desc.mType = TF_PIPELINE_TYPE_GRAPHICS;
        desc.mGraphicsDesc = {};
        PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(VisibilityBufferShadowPassSrtData, Persistent),
                             SRT_LAYOUT_DESC(VisibilityBufferShadowPassSrtData, PerFrame), NULL,
                             SRT_LAYOUT_DESC(VisibilityBufferShadowPassSrtData, PerDraw));
        TFGraphicsPipelineDesc& ASMIndirectDepthPassPipelineDesc = desc.mGraphicsDesc;
        ASMIndirectDepthPassPipelineDesc.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        ASMIndirectDepthPassPipelineDesc.mRenderTargetCount = 0;
        ASMIndirectDepthPassPipelineDesc.pDepthState = &depthStateEnabledDesc;
        ASMIndirectDepthPassPipelineDesc.mDepthStencilFormat = pRenderTargetASMDepthPass->mFormat;
        ASMIndirectDepthPassPipelineDesc.mSampleCount = pRenderTargetASMDepthPass->mSampleCount;
        ASMIndirectDepthPassPipelineDesc.mSampleQuality = pRenderTargetASMDepthPass->mSampleQuality;
        ASMIndirectDepthPassPipelineDesc.pShaderProgram = pShaderIndirectDepthPass;
        ASMIndirectDepthPassPipelineDesc.pRasterizerState = &rasterStateCullNoneDesc;
        ASMIndirectDepthPassPipelineDesc.pVertexLayout = NULL;
        addPipeline(pRenderer, &desc, &pPipelineIndirectDepthPass);

        ASMIndirectDepthPassPipelineDesc.pShaderProgram = pShaderIndirectAlphaDepthPass;
        addPipeline(pRenderer, &desc, &pPipelineIndirectAlphaDepthPass);

        desc.mGraphicsDesc = {};
        TFGraphicsPipelineDesc& indirectESMDepthPassPipelineDesc = desc.mGraphicsDesc;
        indirectESMDepthPassPipelineDesc.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        indirectESMDepthPassPipelineDesc.mRenderTargetCount = 0;
        indirectESMDepthPassPipelineDesc.pDepthState = &depthStateEnabledDesc;
        indirectESMDepthPassPipelineDesc.mDepthStencilFormat = pRenderTargetShadowMap->mFormat;
        indirectESMDepthPassPipelineDesc.mSampleCount = pRenderTargetShadowMap->mSampleCount;
        indirectESMDepthPassPipelineDesc.mSampleQuality = pRenderTargetShadowMap->mSampleQuality;
        indirectESMDepthPassPipelineDesc.pRasterizerState = &rasterStateCullNoneDesc;
        indirectESMDepthPassPipelineDesc.pVertexLayout = NULL;
        indirectESMDepthPassPipelineDesc.pShaderProgram = pShaderIndirectDepthPass;
        addPipeline(pRenderer, &desc, &pPipelineESMIndirectDepthPass);

        indirectESMDepthPassPipelineDesc.pShaderProgram = pShaderIndirectAlphaDepthPass;
        addPipeline(pRenderer, &desc, &pPipelineESMIndirectAlphaDepthPass);

        desc.mGraphicsDesc = {};
        TFGraphicsPipelineDesc& indirectVSMDepthPassPipelineDesc = desc.mGraphicsDesc;
        TinyImageFormat         vsmFormat = getVsmFormat();
        indirectVSMDepthPassPipelineDesc.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        indirectVSMDepthPassPipelineDesc.mRenderTargetCount = 1;
        indirectVSMDepthPassPipelineDesc.pDepthState = &depthStateEnabledDesc;
        indirectVSMDepthPassPipelineDesc.mDepthStencilFormat = pRenderTargetShadowMap->mFormat;
        indirectVSMDepthPassPipelineDesc.pColorFormats = &vsmFormat;
        indirectVSMDepthPassPipelineDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        indirectVSMDepthPassPipelineDesc.mSampleQuality = 0;
        indirectVSMDepthPassPipelineDesc.pRasterizerState = &rasterStateCullNoneDesc;
        indirectVSMDepthPassPipelineDesc.pVertexLayout = NULL;
        indirectVSMDepthPassPipelineDesc.pShaderProgram = pShaderIndirectVSMDepthPass;
        addPipeline(pRenderer, &desc, &pPipelineVSMIndirectDepthPass);

        indirectVSMDepthPassPipelineDesc.pShaderProgram = pShaderIndirectVSMAlphaDepthPass;
        addPipeline(pRenderer, &desc, &pPipelineVSMIndirectAlphaDepthPass);

        desc.mGraphicsDesc = {};
        TFGraphicsPipelineDesc& indirectMSMDepthPassPipelineDesc = desc.mGraphicsDesc;
        TinyImageFormat         msmFormat = getVsmFloat4Format();
        indirectMSMDepthPassPipelineDesc.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        indirectMSMDepthPassPipelineDesc.mRenderTargetCount = 1;
        indirectMSMDepthPassPipelineDesc.pDepthState = &depthStateEnabledDesc;
        indirectMSMDepthPassPipelineDesc.mDepthStencilFormat = pRenderTargetShadowMap->mFormat;
        indirectMSMDepthPassPipelineDesc.pColorFormats = &msmFormat;
        indirectMSMDepthPassPipelineDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        indirectMSMDepthPassPipelineDesc.mSampleQuality = 0;
        indirectMSMDepthPassPipelineDesc.pRasterizerState = &rasterStateCullNoneDesc;
        indirectMSMDepthPassPipelineDesc.pVertexLayout = NULL;
        indirectMSMDepthPassPipelineDesc.pShaderProgram = pShaderIndirectMSMDepthPass;
        addPipeline(pRenderer, &desc, &pPipelineMSMIndirectDepthPass);

        indirectMSMDepthPassPipelineDesc.pShaderProgram = pShaderIndirectMSMAlphaDepthPass;
        addPipeline(pRenderer, &desc, &pPipelineMSMIndirectAlphaDepthPass);

        desc.mGraphicsDesc = {};
        PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(QuadDataSrtData, Persistent), SRT_LAYOUT_DESC(QuadDataSrtData, PerFrame),
                             SRT_LAYOUT_DESC(QuadDataSrtData, PerBatch), SRT_LAYOUT_DESC(QuadDataSrtData, PerDraw));
        TFGraphicsPipelineDesc& ASMCopyDepthQuadPipelineDesc = desc.mGraphicsDesc;
        ASMCopyDepthQuadPipelineDesc.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        ASMCopyDepthQuadPipelineDesc.mRenderTargetCount = 1;
        ASMCopyDepthQuadPipelineDesc.pDepthState = NULL;
        ASMCopyDepthQuadPipelineDesc.pColorFormats = &pRenderTargetASMDepthAtlas->mFormat;
        ASMCopyDepthQuadPipelineDesc.mSampleCount = pRenderTargetASMDepthAtlas->mSampleCount;
        ASMCopyDepthQuadPipelineDesc.mSampleQuality = pRenderTargetASMDepthAtlas->mSampleQuality;
        ASMCopyDepthQuadPipelineDesc.pShaderProgram = pShaderASMCopyDepthQuadPass;
        ASMCopyDepthQuadPipelineDesc.pRasterizerState = &rasterStateCullNoneDesc;
        addPipeline(pRenderer, &desc, &pPipelineASMCopyDepthQuadPass);

        desc.mGraphicsDesc = {};
        TFGraphicsPipelineDesc& ASMCopyDEMQuadPipelineDesc = desc.mGraphicsDesc;
        ASMCopyDEMQuadPipelineDesc.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        ASMCopyDEMQuadPipelineDesc.mRenderTargetCount = 1;
        ASMCopyDEMQuadPipelineDesc.pDepthState = NULL;
        ASMCopyDEMQuadPipelineDesc.pColorFormats = &pRenderTargetASMDEMAtlas->mFormat;
        ASMCopyDEMQuadPipelineDesc.mSampleCount = pRenderTargetASMDEMAtlas->mSampleCount;
        ASMCopyDEMQuadPipelineDesc.mSampleQuality = pRenderTargetASMDEMAtlas->mSampleQuality;
        ASMCopyDEMQuadPipelineDesc.pShaderProgram = pShaderASMCopyDEM;
        ASMCopyDEMQuadPipelineDesc.pRasterizerState = &rasterStateCullNoneDesc;
        addPipeline(pRenderer, &desc, &pPipelineASMCopyDEM);

        desc.mGraphicsDesc = {};
        TFGraphicsPipelineDesc& ASMAtlasToColorPipelineDesc = desc.mGraphicsDesc;
        ASMAtlasToColorPipelineDesc.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        ASMAtlasToColorPipelineDesc.mRenderTargetCount = 1;
        ASMAtlasToColorPipelineDesc.pDepthState = NULL;
        ASMAtlasToColorPipelineDesc.pColorFormats = &pRenderTargetASMColorPass->mFormat;
        ASMAtlasToColorPipelineDesc.mSampleCount = pRenderTargetASMColorPass->mSampleCount;
        ASMAtlasToColorPipelineDesc.mSampleQuality = pRenderTargetASMColorPass->mSampleQuality;
        ASMAtlasToColorPipelineDesc.pShaderProgram = pShaderASMGenerateDEM;
        ASMAtlasToColorPipelineDesc.pRasterizerState = &rasterStateCullNoneDesc;
        addPipeline(pRenderer, &desc, &pPipelineASMDEMAtlasToColor);

        desc.mGraphicsDesc = {};
        TFGraphicsPipelineDesc& ASMColorToAtlasPipelineDesc = desc.mGraphicsDesc;
        ASMColorToAtlasPipelineDesc.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        ASMColorToAtlasPipelineDesc.mRenderTargetCount = 1;
        ASMColorToAtlasPipelineDesc.pDepthState = NULL;
        ASMColorToAtlasPipelineDesc.pColorFormats = &pRenderTargetASMDEMAtlas->mFormat;
        ASMColorToAtlasPipelineDesc.mSampleCount = pRenderTargetASMDEMAtlas->mSampleCount;
        ASMColorToAtlasPipelineDesc.mSampleQuality = pRenderTargetASMDEMAtlas->mSampleQuality;
        ASMColorToAtlasPipelineDesc.pShaderProgram = pShaderASMGenerateDEM;
        ASMColorToAtlasPipelineDesc.pRasterizerState = &rasterStateCullNoneDesc;
        addPipeline(pRenderer, &desc, &pPipelineASMDEMColorToAtlas);

        desc.mGraphicsDesc = {};
        TFGraphicsPipelineDesc& ASMIndirectionPipelineDesc = desc.mGraphicsDesc;
        ASMIndirectionPipelineDesc.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        ASMIndirectionPipelineDesc.mRenderTargetCount = 1;
        ASMIndirectionPipelineDesc.pDepthState = NULL;
        ASMIndirectionPipelineDesc.pColorFormats = &pRenderTargetASMIndirection[0]->mFormat;
        ASMIndirectionPipelineDesc.mSampleCount = pRenderTargetASMIndirection[0]->mSampleCount;
        ASMIndirectionPipelineDesc.mSampleQuality = pRenderTargetASMIndirection[0]->mSampleQuality;
        ASMIndirectionPipelineDesc.pShaderProgram = pShaderASMFillIndirection;
        ASMIndirectionPipelineDesc.pRasterizerState = &rasterStateCullNoneDesc;
        addPipeline(pRenderer, &desc, &pPipelineASMFillIndirection);

        desc.mGraphicsDesc = {};
        TFGraphicsPipelineDesc& ASMFillLodClampPipelineDesc = desc.mGraphicsDesc;
        ASMFillLodClampPipelineDesc.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        ASMFillLodClampPipelineDesc.mRenderTargetCount = 1;
        ASMFillLodClampPipelineDesc.pDepthState = NULL;
        ASMFillLodClampPipelineDesc.pColorFormats = &pRenderTargetASMLodClamp->mFormat;
        ASMFillLodClampPipelineDesc.mSampleCount = pRenderTargetASMLodClamp->mSampleCount;
        ASMFillLodClampPipelineDesc.mSampleQuality = pRenderTargetASMLodClamp->mSampleQuality;
#if defined(ORBIS) || defined(PROSPERO)
        ASMFillLodClampPipelineDesc.pShaderProgram = pShaderASMFillIndirectionFP16;
#else
        ASMFillLodClampPipelineDesc.pShaderProgram = pShaderASMFillIndirection;
#endif
        ASMFillLodClampPipelineDesc.pRasterizerState = &rasterStateCullNoneDesc;
        addPipeline(pRenderer, &desc, &pPipelineASMFillLodClamp);

        /************************************************************************/
        // Setup Present pipeline
        /************************************************************************/
        desc.mGraphicsDesc = {};
        PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(DisplaySrtData, Persistent), SRT_LAYOUT_DESC(DisplaySrtData, PerFrame), NULL,
                             SRT_LAYOUT_DESC(DisplaySrtData, PerDraw));
        TFGraphicsPipelineDesc& pipelineSettingsFinalPass = desc.mGraphicsDesc;
        pipelineSettingsFinalPass.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        pipelineSettingsFinalPass.pRasterizerState = &rasterStateCullNoneDesc;
        pipelineSettingsFinalPass.mRenderTargetCount = 1;
        pipelineSettingsFinalPass.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
        pipelineSettingsFinalPass.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        pipelineSettingsFinalPass.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        pipelineSettingsFinalPass.pShaderProgram = pShaderPresentPass;

        addPipeline(pRenderer, &desc, &pPipelinePresentPass);

        /************************************************************************/
        // Setup MSAA edge detect pipeline
        /************************************************************************/
        if (gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1)
        {
            TFDepthStateDesc depthStateOnlyWriteStencilDesc = {};
            depthStateOnlyWriteStencilDesc.mStencilWriteMask = 0xFF;
            depthStateOnlyWriteStencilDesc.mStencilReadMask = 0xFF;
            depthStateOnlyWriteStencilDesc.mStencilTest = true;
            depthStateOnlyWriteStencilDesc.mStencilFrontFunc = TF_CMP_GEQUAL;
            depthStateOnlyWriteStencilDesc.mStencilFrontFail = TF_STENCIL_OP_KEEP;
            depthStateOnlyWriteStencilDesc.mStencilFrontPass = TF_STENCIL_OP_REPLACE;
            depthStateOnlyWriteStencilDesc.mDepthFrontFail = TF_STENCIL_OP_KEEP;
            depthStateOnlyWriteStencilDesc.mStencilBackFunc = TF_CMP_GEQUAL;
            depthStateOnlyWriteStencilDesc.mStencilBackFail = TF_STENCIL_OP_KEEP;
            depthStateOnlyWriteStencilDesc.mStencilBackPass = TF_STENCIL_OP_REPLACE;
            depthStateOnlyWriteStencilDesc.mDepthBackFail = TF_STENCIL_OP_KEEP;

            TFPipelineDesc pipelineDesc = {};
            PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
            pipelineDesc.mType = TF_PIPELINE_TYPE_GRAPHICS;
            TFGraphicsPipelineDesc& msaaEdgesPipeline = pipelineDesc.mGraphicsDesc;
            msaaEdgesPipeline = { 0 };
            msaaEdgesPipeline.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
            msaaEdgesPipeline.mRenderTargetCount = 0;
            msaaEdgesPipeline.pDepthState = &depthStateOnlyWriteStencilDesc;
            msaaEdgesPipeline.mDepthStencilFormat = pRenderTargetMSAAEdges->mFormat;
            msaaEdgesPipeline.mSampleCount = gAppSettings.mMsaaLevel;
            msaaEdgesPipeline.mSampleQuality = 0;
            msaaEdgesPipeline.pRasterizerState = &rasterStateCullNoneMsDesc;
            msaaEdgesPipeline.pShaderProgram = pShaderDrawMSAAEdges[gAppSettings.mMsaaIndex - 1];
            pipelineDesc.pName = "Render Stencil MSAA edges";
            addPipeline(pRenderer, &pipelineDesc, &pPipelineDrawMSAAEdges);
        }
    }

    void removePipelines()
    {
        if (gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1)
        {
            removePipeline(pRenderer, pPipelineDrawMSAAEdges);
        }
        removePipeline(pRenderer, pPipelinePresentPass);
        removePipeline(pRenderer, pPipelineResolve);

        removePipeline(pRenderer, pPipelineESMIndirectDepthPass);
        removePipeline(pRenderer, pPipelineESMIndirectAlphaDepthPass);

        removePipeline(pRenderer, pPipelineVSMIndirectDepthPass);
        removePipeline(pRenderer, pPipelineVSMIndirectAlphaDepthPass);

        removePipeline(pRenderer, pPipelineMSMIndirectDepthPass);
        removePipeline(pRenderer, pPipelineMSMIndirectAlphaDepthPass);
        removePipeline(pRenderer, pPipelineBlur);

        removePipeline(pRenderer, pPipelineASMCopyDEM);
        removePipeline(pRenderer, pPipelineASMCopyDepthQuadPass);
        removePipeline(pRenderer, pPipelineASMDEMAtlasToColor);
        removePipeline(pRenderer, pPipelineASMDEMColorToAtlas);
        removePipeline(pRenderer, pPipelineIndirectAlphaDepthPass);
        removePipeline(pRenderer, pPipelineIndirectDepthPass);

        removePipeline(pRenderer, pPipelineASMFillIndirection);
        removePipeline(pRenderer, pPipelineASMFillLodClamp);
        removePipeline(pRenderer, pPipelineVBShadeSrgb);

        for (uint32_t i = 0; i < gNumGeomSets; ++i)
        {
            removePipeline(pRenderer, pPipelineVBBufferPass[i]);
        }
        removePipeline(pRenderer, pPipelineClearBuffers);
        removePipeline(pRenderer, pPipelineTriangleFiltering);

        removePipeline(pRenderer, pPipelineUpdateSDFVolumeTextureAtlas);
        removePipeline(pRenderer, pPipelineSDFMeshVisualization);
        removePipeline(pRenderer, pPipelineSDFMeshShadow);
        removePipeline(pRenderer, pPipelineUpsampleSDFShadow);
        removePipeline(pRenderer, pPipelineSSS);
        removePipeline(pRenderer, pPipelineSSSClear);

#if TEST_GPU_BREADCRUMBS
        removePipeline(pRenderer, pPipelineVBBufferCrashPass);
        removePipeline(pRenderer, pPipelineSSSCrash);
        removePipeline(pRenderer, pPipelineVBShadeSrgbCrash);
#endif
    }

    void updateMeshSDFConstants()
    {
        const vec3 inverseSDFTextureAtlasSize(1.f / (float)SDF_VOLUME_TEXTURE_ATLAS_WIDTH, 1.f / (float)SDF_VOLUME_TEXTURE_ATLAS_HEIGHT,
                                              1.f / (float)SDF_VOLUME_TEXTURE_ATLAS_DEPTH);

        gSDFNumObjects = (uint32_t)arrlen(gSDFVolumeInstances) -
                         (uint32_t)(arrlenu(pSDFVolumeTextureAtlas->mPendingNodeQueue) - pSDFVolumeTextureAtlas->mNextNodeIndex);
        for (size_t i = 0; i < arrlenu(gSDFVolumeInstances); ++i)
        {
            // const mat4& meshModelMat = gMeshInfoUniformData[0].mWorldMat;
            const mat4 meshModelMat = mat4::identity();
            if (!gSDFVolumeInstances[i])
            {
                continue;
            }
            const SDFVolumeData& sdfVolumeData = *gSDFVolumeInstances[i];

            const TFAABB& sdfVolumeBBox = sdfVolumeData.mLocalBoundingBox;
            const ivec3&  sdfVolumeDimensionSize = sdfVolumeData.mSDFVolumeSize;

            // mat4 volumeToWorldMat = meshModelMat * mat4::translation(sdfVolumeData.mLocalBoundingBox.GetCenter())
            //*  mat4::scale(sdfVolumeData.mLocalBoundingBox.GetExtent());
            vec3  sdfVolumeBBoxExtent = calculateAABBExtent(&sdfVolumeBBox);
            float maxExtentValue = maxElem(sdfVolumeBBoxExtent);

            mat4 uniformScaleVolumeToWorld =
                meshModelMat * mat4::translation(calculateAABBCenter(&sdfVolumeBBox)) * mat4::scale(vec3(maxExtentValue));

            vec3 invSDFVolumeDimSize(1.f / sdfVolumeDimensionSize.x, 1.f / sdfVolumeDimensionSize.y, 1.f / sdfVolumeDimensionSize.z);
            gMeshSDFConstants.mWorldToVolumeMat[i] = inverse(uniformScaleVolumeToWorld);

            // Get the extent position in the 0.... 1 scale
            vec3 localPositionExtent = sdfVolumeBBoxExtent / maxExtentValue;

            vec3 uvScale =
                vec3(sdfVolumeDimensionSize.x * inverseSDFTextureAtlasSize.x, sdfVolumeDimensionSize.y * inverseSDFTextureAtlasSize.y,
                     sdfVolumeDimensionSize.z * inverseSDFTextureAtlasSize.z);

            vec3 col0Scale = uniformScaleVolumeToWorld[0].getXYZ();
            vec3 col1Scale = uniformScaleVolumeToWorld[1].getXYZ();
            vec3 col2Scale = uniformScaleVolumeToWorld[2].getXYZ();

            float col0SquaredLength = dot(col0Scale, col0Scale);
            float col1SquaredLength = dot(col1Scale, col1Scale);
            float col2SquaredLength = dot(col2Scale, col2Scale);

            float finalColSquaredLength = fmaxf(fmaxf(col0SquaredLength, col1SquaredLength), col2SquaredLength);

            float maximumVolumeScale = sqrtf(finalColSquaredLength);

            gMeshSDFConstants.mLocalPositionExtent[i] = vec4(localPositionExtent - invSDFVolumeDimSize, 1.f);

            vec3 initialUV =
                vec3(sdfVolumeDimensionSize.x * inverseSDFTextureAtlasSize.x, sdfVolumeDimensionSize.y * inverseSDFTextureAtlasSize.y,
                     sdfVolumeDimensionSize.z * inverseSDFTextureAtlasSize.z) *
                0.5f;

            vec3 newUV =
                vec3(initialUV.x / localPositionExtent.x, initialUV.y / localPositionExtent.y, initialUV.z / localPositionExtent.z);

            maximumVolumeScale *= (sdfVolumeData.mIsTwoSided ? -1.f : 1.0f);
            gMeshSDFConstants.mUVScaleAndVolumeScale[i] = vec4(newUV, maximumVolumeScale);

            const ivec3& atlasAllocationCoord = sdfVolumeData.mSDFVolumeTextureNode.mAtlasAllocationCoord;

            vec3 offsetUV =
                vec3(atlasAllocationCoord.x * inverseSDFTextureAtlasSize.x, atlasAllocationCoord.y * inverseSDFTextureAtlasSize.y,
                     atlasAllocationCoord.z * inverseSDFTextureAtlasSize.z);

            offsetUV += (0.5f * uvScale);
            gMeshSDFConstants.mUVAddAndSelfShadowBias[i] = vec4(offsetUV, 0.f);

            gMeshSDFConstants.mSDFMAD[i] = vec4(sdfVolumeData.mDistMinMax.y - sdfVolumeData.mDistMinMax.x, sdfVolumeData.mDistMinMax.x,
                                                sdfVolumeData.mTwoSidedWorldSpaceBias, 0.f);

            gMeshSDFConstants.mNumObjects[0] = gSDFNumObjects;
        }
    }

    void updateASM()
    {
        ASMTickData&           tickData = gASMTickData;
        IndirectionRenderData& indirectionRenderData = tickData.mIndirectionRenderData;
        for (int32_t i = 0; i < gs_ASMMaxRefinement + 1; ++i)
        {
            indirectionRenderData.pBufferASMPackedIndirectionQuadsUniform[i] = pBufferASMPackedIndirectionQuadsUniform[i][gFrameIndex];
        }
        indirectionRenderData.pBufferASMClearIndirectionQuadsUniform = pBufferASMClearIndirectionQuadsUniform[gFrameIndex];
        indirectionRenderData.m_pGraphicsPipeline = pPipelineASMFillIndirection;

        IndirectionRenderData& prerenderIndirectionRenderData = tickData.mPrerenderIndirectionRenderData;

        for (int32_t i = 0; i < gs_ASMMaxRefinement + 1; ++i)
        {
            prerenderIndirectionRenderData.pBufferASMPackedIndirectionQuadsUniform[i] =
                pBufferASMPackedPrerenderIndirectionQuadsUniform[i][gFrameIndex];
        }
        prerenderIndirectionRenderData.pBufferASMClearIndirectionQuadsUniform = pBufferASMClearIndirectionQuadsUniform[gFrameIndex];
        prerenderIndirectionRenderData.m_pGraphicsPipeline = pPipelineASMFillIndirection;

        ASMProjectionData mainViewProjection;
        mainViewProjection.mViewMat = gCameraUniformData.mView.mMatrices[MONO_CAMERA_VIEW_INDEX];
        mainViewProjection.mProjMat = gCameraUniformData.mProject;
        mainViewProjection.mInvViewMat = inverse(mainViewProjection.mViewMat);
        mainViewProjection.mInvProjMat = camMatInverse(&mainViewProjection.mProjMat);
        mainViewProjection.mViewProjMat = camMatMulMat4(&mainViewProjection.mProjMat, &mainViewProjection.mViewMat);
        mainViewProjection.mInvViewProjMat = mat4MulCamMat(&mainViewProjection.mInvViewMat, &mainViewProjection.mInvProjMat);

        pASM->PrepareRender(mainViewProjection, false);

        size_t              size = 0;
        ASMProjectionData** renderBatchProjection = pASM->m_cache->GetRenderBatchProjection(size);
        if (renderBatchProjection != NULL)
        {
            for (size_t i = 0; i < size; i++)
            {
                gPerFrameData[gFrameIndex].gEyeObjectSpace[VIEW_SHADOW + i] =
                    (renderBatchProjection[i]->mInvViewProjMat.mMatrices[MONO_CAMERA_VIEW_INDEX] * vec4(0.f, 0.f, 0.f, 1.f)).getXYZ();

                gVBConstants[gFrameIndex].transform[VIEW_SHADOW + i].mvp =
                    camMatMulMat4(&renderBatchProjection[i]->mViewProjMat, &gMeshInfoData[0].mWorldMat);
                gVBConstants[gFrameIndex].cullingViewports[VIEW_SHADOW + i].sampleCount = 1;

                vec2 windowSize = vec2(gs_ASMDepthAtlasTextureWidth, gs_ASMDepthAtlasTextureHeight);
                gVBConstants[gFrameIndex].cullingViewports[VIEW_SHADOW + i].windowSize = (windowSize);
                gVBConstants[gFrameIndex].numViewports = (uint32_t)size + 1;
#ifdef QUEST_VR
                gVBConstants[gFrameIndex].cullingMVP[VIEW_SHADOW + i] =
                    gVBConstants[gFrameIndex].transform[VIEW_SHADOW + i].mvp.mMatrices[MONO_CAMERA_VIEW_INDEX];
#endif
            }
            tf_free(renderBatchProjection);
        }
    }

    void updateASMUniform()
    {
        gAsmModelUniformBlockData.mIndexTexMat = pASM->m_longRangeShadows->m_indexTexMat;
        gAsmModelUniformBlockData.mPrerenderIndexTexMat = pASM->m_longRangePreRender->m_indexTexMat;
        gAsmModelUniformBlockData.mWarpVector = vec4(pASM->m_longRangeShadows->m_receiverWarpVector, 0.0);
        gAsmModelUniformBlockData.mPrerenderWarpVector = vec4(pASM->m_longRangePreRender->m_receiverWarpVector, 0.0);

        gAsmModelUniformBlockData.mSearchVector = vec4(pASM->m_longRangeShadows->m_blockerSearchVector, 0.0);
        gAsmModelUniformBlockData.mPrerenderSearchVector = vec4(pASM->m_longRangePreRender->m_blockerSearchVector, 0.0);

        gAsmModelUniformBlockData.mMiscBool.x = ((float)pASM->PreRenderDone());
        gAsmModelUniformBlockData.mMiscBool.y = ((float)gASMCpuSettings.mEnableParallax);
        gAsmModelUniformBlockData.mMiscBool.z = ((float)preRenderSwap);
        gAsmModelUniformBlockData.mMiscBool.w = (gASMCpuSettings.mPenumbraSize);
        TFBufferUpdateDesc asmUpdateUbDesc = { pBufferASMDataUniform[gFrameIndex] };
        beginUpdateResource(&asmUpdateUbDesc);
        memcpy(asmUpdateUbDesc.pMappedData, &gAsmModelUniformBlockData, sizeof(gAsmModelUniformBlockData));
        endUpdateResource(&asmUpdateUbDesc);
    }

    void updateUniformData()
    {
        // Update camera with time
        TFCameraMatrix viewMat = pCamera->getViewMatrix();
        /************************************************************************/
        // Update Camera
        /************************************************************************/
        const uint32_t width = gSceneRes.mWidth;
        const uint32_t height = gSceneRes.mHeight;

        float           aspectInverse = (float)height / (float)width;
        constexpr float horizontal_fov = PI / 2.0f;
        constexpr float nearValue = 0.1f;
        constexpr float farValue = 1000.f;
        TFCameraMatrix  projMat = camMatPerspectiveReverseZ(horizontal_fov, aspectInverse, nearValue, farValue);

        gCameraUniformData.mView = viewMat;
        gCameraUniformData.mProject = projMat;
        gCameraUniformData.mViewProject = camMatMul(&projMat, &viewMat);
        gCameraUniformData.mInvProj = camMatInverse(&projMat);
        gCameraUniformData.mInvView = camMatInverse(&viewMat);
        gCameraUniformData.mInvViewProject = camMatInverse(&gCameraUniformData.mViewProject);
        gCameraUniformData.mNear = nearValue;
        gCameraUniformData.mFar = farValue;
        gCameraUniformData.mFarNearDiff = farValue - nearValue; // if OpenGL convention was used this would be 2x the value
        gCameraUniformData.mFarNear = nearValue * farValue;
        gCameraUniformData.mCameraPos = vec4(pCamera->getViewPosition(), 1.f);

        gCameraUniformData.mTwoOverRes = { 2.0f / float(width), 2.0f / float(height) };

        float depthMul = projMat.mMatrices[MONO_CAMERA_VIEW_INDEX][2][2];
        float depthAdd = projMat.mMatrices[MONO_CAMERA_VIEW_INDEX][3][2];

        if (depthAdd == 0.f)
        {
            // Avoid dividing by 0 in this case
            depthAdd = 0.00000001f;
        }

        if (projMat.mMatrices[MONO_CAMERA_VIEW_INDEX][3][3] < 1.0f)
        {
            float subtractValue = depthMul / depthAdd;
            subtractValue -= 0.00000001f;
            gCameraUniformData.mDeviceZToWorldZ = vec4(0.f, 0.f, 1.f / depthAdd, subtractValue);
        }
        gCameraUniformData.mWindowSize = vec2((float)width, (float)height);

        /************************************************************************/
        // Skybox
        /************************************************************************/
        for (uint32_t viewIndex = 0; viewIndex < VR_MULTIVIEW_COUNT; ++viewIndex)
        {
            viewMat.mMatrices[viewIndex].setTranslation(vec3(0));
        }
        TFCameraMatrix viewProj = camMatMul(&projMat, &viewMat);
        gCameraUniformData.mInverseSkyViewProject = camMatInverse(&viewProj);

        /************************************************************************/
        // Light Matrix Update
        /************************************************************************/
        float timeNormalized = (gLightCpuSettings.mTimeOfDay - gLightCpuSettings.mTimeOfDaySunrise) /
                               (gLightCpuSettings.mTimeOfDaySunset - gLightCpuSettings.mTimeOfDaySunrise);

        // Direction from light to scene (for BRDFs)
        float sunRotation = PI * timeNormalized;
        mat4  rotationLight = mat4::rotationYX(gLightCpuSettings.mSunriseDirection, sunRotation);
        vec3  lightDir = (rotationLight * vec4::zAxis()).getXYZ();

        // Direction from shadowmap camera to its pivot position
        float shadowPadding = (1.0f - gLightCpuSettings.mShadowRange) / 2.0f;
        float shadowRotationTime = lerp(shadowPadding, 1.0f - shadowPadding, timeNormalized);
        float sunRotationShadow = PI * shadowRotationTime;
        mat4  rotationShadow = mat4::rotationYX(gLightCpuSettings.mSunriseDirection, sunRotationShadow);
        vec3  shadowDir = (rotationShadow * vec4::zAxis()).getXYZ();

        // seems like we're off by a factor of about 10??? (MESH_SCALE)
        float3 shadowCameraPivot = float3(SAN_MIGUEL_OFFSETX, 13.93f * MESH_SCALE, 6.815f * MESH_SCALE);
        float3 shadowCameraOffset = (-shadowDir * 25.0f) * MESH_SCALE;
        mat4   invTranslationShadow = mat4::translation(-(shadowCameraPivot + shadowCameraOffset));

        float shadowBounds = 120.0f;
        mat4  shadowView = transpose(rotationShadow) * invTranslationShadow;
        mat4 shadowProj = mat4::orthographicLH_ReverseZ(-shadowBounds, shadowBounds, -shadowBounds, shadowBounds, 0.1f, 55.0f * MESH_SCALE);

        gCameraUniformData.mLight.mLightPosition = vec4(0.f);
        gCameraUniformData.mLight.mLightViewProj = shadowProj * shadowView;
        gCameraUniformData.mLight.mLightColor = vec4(1, 1, 1, 1);
        gCameraUniformData.mLight.mLightUpVec = transpose(shadowView)[1];
        gCameraUniformData.mLight.mLightDir = vec4(lightDir, 0.0f);

        const float lightSourceAngle = clamp(gLightCpuSettings.mSourceAngle, 0.001f, 4.0f) * PI / 180.0f;
        gCameraUniformData.mLight.mTanLightAngleAndThresholdValue =
            vec4(tanf(lightSourceAngle), cosf(PI / 2 + lightSourceAngle), SDF_LIGHT_THERESHOLD_VAL, 0.f);

        for (int32_t i = 0; i < MESH_COUNT; ++i)
        {
            gMeshInfoData[i].mTranslationMat = mat4::translation((gMeshInfoData[i].mTranslation));
            gMeshInfoData[i].mScaleMat = mat4::scale((gMeshInfoData[i].mScale));
            mat4 offsetTranslationMat = mat4::translation((gMeshInfoData[i].mOffsetTranslation));
            gMeshInfoData[i].mWorldMat = gMeshInfoData[i].mTranslationMat * gMeshInfoData[i].mScaleMat * offsetTranslationMat;

            gMeshInfoUniformData[i][gFrameIndex].mWorldViewProjMat =
                camMatMulMat4(&gCameraUniformData.mViewProject, &gMeshInfoData[i].mWorldMat);

            if (gCurrentShadowType == SHADOW_TYPE_ASM)
            {
                gMeshASMProjectionInfoUniformData[i][gFrameIndex].mWorldViewProjMat = mat4::identity();
            }
            else if (gCurrentShadowType == SHADOW_TYPE_ESM || gCurrentShadowType == SHADOW_TYPE_VSM ||
                     gCurrentShadowType == SHADOW_TYPE_MSM)
            {
                gMeshASMProjectionInfoUniformData[i][gFrameIndex].mWorldViewProjMat =
                    gCameraUniformData.mLight.mLightViewProj * gMeshInfoData[i].mWorldMat;
                gMeshASMProjectionInfoUniformData[i][gFrameIndex].mViewID = VIEW_SHADOW;
            }
        }

        // Only for view camera, for shadow it depends on the alggorithm being used
        gPerFrameData[gFrameIndex].gEyeObjectSpace[VIEW_CAMERA] =
            (gCameraUniformData.mInvView.mMatrices[MONO_CAMERA_VIEW_INDEX] * vec4(0.f, 0.f, 0.f, 1.f)).getXYZ();

        gVBConstants[gFrameIndex].transform[VIEW_CAMERA].mvp = camMatMulMat4(&gCameraUniformData.mViewProject, &gMeshInfoData[0].mWorldMat);
        gVBConstants[gFrameIndex].cullingViewports[VIEW_CAMERA].windowSize = { (float)gSceneRes.mWidth, (float)gSceneRes.mHeight };
        gVBConstants[gFrameIndex].cullingViewports[VIEW_CAMERA].sampleCount = gAppSettings.mMsaaLevel;
#ifdef QUEST_VR
        mat4 superFrustumView;
        mat4 superFrustumProject;
        camMatSuperFrustum(&gCameraUniformData.mView, nearValue, farValue, &superFrustumView, &superFrustumProject, false);
        gVBConstants[gFrameIndex].cullingMVP[VIEW_CAMERA] = (superFrustumProject * superFrustumView) * gMeshInfoData[0].mWorldMat;
#endif
        gVBConstants[gFrameIndex].numViewports = 2;
    }

    struct DispatchParams
    {
        int WaveCount[3];
        int WaveOffset[2];
    };

    int updateSSSParams(uint32_t viewIndex, DispatchParams (&dispatchList)[8])
    {
        memset(dispatchList, 0, sizeof(dispatchList));

        int dispatchCount = 0;

        vec3      lightDir = normalize(-gCameraUniformData.mLight.mLightDir.getXYZ());
        mat4      viewProject = gCameraUniformData.mViewProject.mMatrices[viewIndex];
        vec4      lightProjection = viewProject * vec4(lightDir, 0.0f);
        const int waveSize = 64;

        float xy_light_w = lightProjection.w;
        float FP_limit = 0.000002f * (float)waveSize;
        if (xy_light_w >= 0 && xy_light_w < FP_limit)
            xy_light_w = FP_limit;
        else if (xy_light_w < 0 && xy_light_w > -FP_limit)
            xy_light_w = -FP_limit;

        float4 mLightCoordinate;

        mLightCoordinate[0] = ((lightProjection[0] / xy_light_w) * +0.5f + 0.5f) * (float)gSceneRes.mWidth;
        mLightCoordinate[1] = ((lightProjection[1] / xy_light_w) * -0.5f + 0.5f) * (float)gSceneRes.mHeight;
        mLightCoordinate[2] = lightProjection[3] == 0 ? 0.0f : (lightProjection[2] / lightProjection[3]);
        mLightCoordinate[3] = lightProjection[3] > 0 ? 1.0f : -1.0f;

        int light_xy[2] = { (int)(mLightCoordinate[0] + 0.5f), (int)(mLightCoordinate[1] + 0.5f) };

        // Make the bounds relative to the light
        const int biased_bounds[4] = {
            -light_xy[0],
            -((int32_t)gSceneRes.mHeight - light_xy[1]),
            (int32_t)gSceneRes.mWidth - light_xy[0],
            light_xy[1],
        };

        // Process 4 quadrants around the light center,
        // They each form a rectangle with one corner on the light XY coordinate
        // If the rectangle isn't square, it will need breaking in two on the larger axis
        // 0 = bottom left, 1 = bottom right, 2 = top left, 2 = top right
        for (int q = 0; q < 4; q++)
        {
            // Quads 0 and 3 needs to be +1 vertically, 1 and 2 need to be +1 horizontally
            bool vertical = q == 0 || q == 3;

            // Bounds relative to the quadrant
            const int bounds[4] = {
                max(0, ((q & 1) ? biased_bounds[0] : -biased_bounds[2])) / waveSize,
                max(0, ((q & 2) ? biased_bounds[1] : -biased_bounds[3])) / waveSize,
                max(0, (((q & 1) ? biased_bounds[2] : -biased_bounds[0]) + waveSize * (vertical ? 1 : 2) - 1)) / waveSize,
                max(0, (((q & 2) ? biased_bounds[3] : -biased_bounds[1]) + waveSize * (vertical ? 2 : 1) - 1)) / waveSize,
            };

            if ((bounds[2] - bounds[0]) > 0 && (bounds[3] - bounds[1]) > 0)
            {
                int bias_x = (q == 2 || q == 3) ? 1 : 0;
                int bias_y = (q == 1 || q == 3) ? 1 : 0;

                DispatchParams& disp = dispatchList[dispatchCount++];
                disp.WaveCount[0] = waveSize;
                disp.WaveCount[1] = bounds[2] - bounds[0];
                disp.WaveCount[2] = bounds[3] - bounds[1];
                disp.WaveOffset[0] = ((q & 1) ? bounds[0] : -bounds[2]) + bias_x;
                disp.WaveOffset[1] = ((q & 2) ? -bounds[3] : bounds[1]) + bias_y;

                // We want the far corner of this quadrant relative to the light,
                // as we need to know where the diagonal light ray intersects with the edge of the bounds
                int axis_delta = +biased_bounds[0] - biased_bounds[1];
                if (q == 1)
                    axis_delta = +biased_bounds[2] + biased_bounds[1];
                if (q == 2)
                    axis_delta = -biased_bounds[0] - biased_bounds[3];
                if (q == 3)
                    axis_delta = -biased_bounds[2] + biased_bounds[3];

                axis_delta = (axis_delta + waveSize - 1) / waveSize;

                if (axis_delta > 0)
                {
                    DispatchParams& disp2 = dispatchList[dispatchCount++];

                    // Take copy of current volume
                    disp2 = disp;

                    if (q == 0)
                    {
                        // Split on Y, split becomes -1 larger on x
                        disp2.WaveCount[2] = min(disp.WaveCount[2], axis_delta);
                        disp.WaveCount[2] -= disp2.WaveCount[2];
                        disp2.WaveOffset[1] = disp.WaveOffset[1] + disp.WaveCount[2];
                        disp2.WaveOffset[0]--;
                        disp2.WaveCount[1]++;
                    }
                    if (q == 1)
                    {
                        // Split on X, split becomes +1 larger on y
                        disp2.WaveCount[1] = min(disp.WaveCount[1], axis_delta);
                        disp.WaveCount[1] -= disp2.WaveCount[1];
                        disp2.WaveOffset[0] = disp.WaveOffset[0] + disp.WaveCount[1];
                        disp2.WaveCount[2]++;
                    }
                    if (q == 2)
                    {
                        // Split on X, split becomes -1 larger on y
                        disp2.WaveCount[1] = min(disp.WaveCount[1], axis_delta);
                        disp.WaveCount[1] -= disp2.WaveCount[1];
                        disp.WaveOffset[0] += disp2.WaveCount[1];
                        disp2.WaveCount[2]++;
                        disp2.WaveOffset[1]--;
                    }
                    if (q == 3)
                    {
                        // Split on Y, split becomes +1 larger on x
                        disp2.WaveCount[2] = min(disp.WaveCount[2], axis_delta);
                        disp.WaveCount[2] -= disp2.WaveCount[2];
                        disp.WaveOffset[1] += disp2.WaveCount[2];
                        disp2.WaveCount[1]++;
                    }

                    // Remove if too small
                    if (disp2.WaveCount[1] <= 0 || disp2.WaveCount[2] <= 0)
                    {
                        disp2 = dispatchList[--dispatchCount];
                    }
                    if (disp.WaveCount[1] <= 0 || disp.WaveCount[2] <= 0)
                    {
                        disp = dispatchList[--dispatchCount];
                    }
                }
            }
        }

        // Scale the shader values by the wave count, the shader expects this
        for (int i = 0; i < dispatchCount; i++)
        {
            dispatchList[i].WaveOffset[0] *= waveSize;
            dispatchList[i].WaveOffset[1] *= waveSize;
        }

        gSSSUniformData.mScreenSize = float4((float)gSceneRes.mWidth, (float)gSceneRes.mHeight, 0.0f, 0.0f);
        gSSSUniformData.mLightCoordinate = mLightCoordinate;
        gSSSUniformData.mViewIndex = viewIndex;

        TFBufferUpdateDesc bufferUpdate = { pBufferSSSUniform[viewIndex][gFrameIndex] };
        beginUpdateResource(&bufferUpdate);
        memcpy(bufferUpdate.pMappedData, &gSSSUniformData, sizeof(gSSSUniformData));
        endUpdateResource(&bufferUpdate);

        return dispatchCount;
    }

    void drawASMPass(TFCmd* cmd)
    {
        ASMProjectionData mainViewProjection;
        mainViewProjection.mViewMat = gCameraUniformData.mView.mMatrices[MONO_CAMERA_VIEW_INDEX];
        mainViewProjection.mProjMat = gCameraUniformData.mProject;
        mainViewProjection.mInvViewMat = inverse(mainViewProjection.mViewMat);
        mainViewProjection.mInvProjMat = camMatInverse(&mainViewProjection.mProjMat);
        mainViewProjection.mViewProjMat = camMatMulMat4(&mainViewProjection.mProjMat, &mainViewProjection.mViewMat);
        mainViewProjection.mInvViewProjMat = mat4MulCamMat(&mainViewProjection.mInvViewMat, &mainViewProjection.mInvProjMat);

        ASMRendererContext rendererContext;
        rendererContext.m_pCmd = cmd;
        rendererContext.m_pRenderer = pRenderer;

        pASM->Render(pRenderTargetASMDepthPass, pRenderTargetASMColorPass, rendererContext, &mainViewProjection);
    }

#if TEST_GPU_BREADCRUMBS
    void initMarkers()
    {
        TFBufferLoadDesc breadcrumbBuffer = {};
        breadcrumbBuffer.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNDEFINED;
        breadcrumbBuffer.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_TO_CPU;
        breadcrumbBuffer.mDesc.mSize = TF_GPU_MARKER_SIZE * MARKER_COUNT;
        breadcrumbBuffer.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
#if defined(VULKAN)
        breadcrumbBuffer.mDesc.mFlags |= TF_BUFFER_CREATION_FLAG_MARKER;
#endif
        breadcrumbBuffer.mDesc.mStartState = TF_RESOURCE_STATE_COPY_DEST;
        breadcrumbBuffer.pData = NULL;
        breadcrumbBuffer.ppBuffer = &pMarkerBuffer;
        addResource(&breadcrumbBuffer, NULL);
    }

    bool checkMarkers()
    {
        if (!bHasCrashed)
        {
            return false;
        }

        threadSleep(2000);
        uint32_t* markersValue = (uint32_t*)pMarkerBuffer->pCpuMappedAddress;
        if (!markersValue)
        {
            return true;
        }
        uint32_t    taskIndex = TF_GPU_MARKER_VALUE(pMarkerBuffer, MARKER_TASK_INDEX * TF_GPU_MARKER_SIZE);
        uint32_t    frameIndex = TF_GPU_MARKER_VALUE(pMarkerBuffer, MARKER_FRAME_INDEX * TF_GPU_MARKER_SIZE);
        const char* stepName = taskIndex != gMarkerInitialValue ? gMarkerNames[taskIndex] : "Unknown";
        LOGF(LogLevel::eINFO, "Last rendering step (approx): %s, crashed frame: %u", stepName, frameIndex);
        bHasCrashed = false;
        return true;
    }

    void exitMarkers() { removeResource(pMarkerBuffer); }
#endif
};

void GuiController::updateDynamicUI()
{
    if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gLoadingGuiDesc)))
    {
        uiLayoutAutoTextRows(1);
        uiLabel("               [ProgressBar]               ", TF_ALIGN_LEFT);
        uiProgressBar(&sSDFProgressValue, sSDFProgressMaxValue, false);
    }
    uiEndWidgetWindow();

    if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gGuiWindowDesc)))
    {
        uiLayoutAutoTextRows(1);
#if TEST_GPU_BREADCRUMBS
        if (pRenderer->pGpu->mGpuMarkers)
        {
            for (uint32_t i = 0; i < RENDERING_STEP_COUNT; i++)
            {
                if (UI_WIDGET_IS_PRESSED(uiButton(gSimulateCrashButtonLabels[i])))
                {
                    bCrashedSteps[gUIRenderingStepIndices[i]] = true;
                }
            }
        }
#endif

        uiCheckbox("Hold triangles", &gAppSettings.mHoldFilteredTriangles);

        uiLayoutAutoTextRows(2);
        static const char* shadowTypeNames[] = { "ESM Exponential Shadow Mapping", "ASM Adaptive Shadow Map",
                                                 "SDF Signed Distance Field Mesh Shadow", "VSM Variance Shadow Mapping",
                                                 "MSM Moments Shadow Mapping" };

        static const uint32_t shadowTypeCount = sizeof(shadowTypeNames) / sizeof(shadowTypeNames[0]);
        uiLabel("Shadow Type", TF_ALIGN_LEFT);
        gCameraUniformData.mShadowType =
            UI_WIDGET_GET_SELECTED(uiDropdown(shadowTypeNames, shadowTypeCount, gCameraUniformData.mShadowType));

        uiLayoutAutoRows(2);
        uiLabel("Time of Day", TF_ALIGN_LEFT);
        bool sunControlChanged = UI_WIDGET_IS_CHANGED(
            uiSliderFloat(&gLightCpuSettings.mTimeOfDay, gLightCpuSettings.mTimeOfDaySunrise, gLightCpuSettings.mTimeOfDaySunset, 0.1f));

        uiLayoutAutoRows(4);
        uiLabel("Camera Position", TF_ALIGN_LEFT);
        float3 cameraPos = pCamera->getViewPosition();
        uiSliderFloat3(&cameraPos, float3(-1000.0f), float3(1000.0f), float3(0.1f));

        if (gCameraUniformData.mShadowType == SHADOW_TYPE_ASM && sunControlChanged)
        {
            LightShadowPlayground::refreshASM(NULL);
        }

        // ESM
        if (gCameraUniformData.mShadowType == SHADOW_TYPE_ESM)
        {
            uiLayoutAutoTextRows(2);
            uiLabel("ESM Control", TF_ALIGN_LEFT);
            uiSliderFloat(&gEsmCpuSettings.mEsmControl, 1.0f, 400.0f, 1.0f);
        }
        // VSM
        else if (gCameraUniformData.mShadowType == SHADOW_TYPE_VSM)
        {
            uiLayoutAutoTextRows(2);
            uiLabel("Min Variance", TF_ALIGN_LEFT);
            uiSliderFloat(&gCameraUniformData.mVSM.mMinVariance, 0.0f, 0.0002f, 0.00001f);
            uiLayoutAutoTextRows(3);
            uiLabel("VSM Bleeding Reduction", TF_ALIGN_LEFT);
            uiSliderFloat2(&gCameraUniformData.mVSM.mBleedingReduction, float2(0.0f), float2(1.0f), float2(0.01f));
        }
        // MSM
        else if (gCameraUniformData.mShadowType == SHADOW_TYPE_MSM)
        {
            uiLayoutAutoTextRows(2);
            uiLabel("Rounding Error Correction", TF_ALIGN_LEFT);
            uiSliderFloat(&gCameraUniformData.mMSM.mRoundingErrorCorrection, 1.0e-6f, 6.0e-5f, 1.0e-7f);
            uiLabel("MSM Bleeding Reduction", TF_ALIGN_LEFT);
            uiSliderFloat(&gCameraUniformData.mMSM.mBleedingReduction, 0.01f, 1.0f, 0.01f);
        }
        // ASM
        else if (gCameraUniformData.mShadowType == SHADOW_TYPE_ASM)
        {
            uiLayoutAutoTextRows(1);
            if (UI_WIDGET_IS_PRESSED(uiButton("Refresh Cache")))
            {
                LightShadowPlayground::refreshASM(NULL);
            }
            uiCheckbox("Sun can move", &gASMCpuSettings.mSunCanMove);
            uiCheckbox("Parallax corrected", &gASMCpuSettings.mEnableParallax);
            uiLayoutAutoTextRows(2);
            uiLabel("Max tiles to render per frame", TF_ALIGN_LEFT);
            uiSliderUint(&gASMMaxTilesPerPass, 1, ASM_MAX_TILES_PER_PASS, 1);
            uiLabel("Light Update Angle", TF_ALIGN_LEFT);
            uiSliderFloat(&gLightDirUpdateAngle, 0.0f, 5.0f, 0.01f);
            uiLayoutAutoTextRows(1);
            if (UI_WIDGET_IS_PRESSED(uiButton("Reset Light Dir")))
            {
                LightShadowPlayground::resetLightDir(NULL);
            }
            uiLayoutAutoTextRows(2);
            uiLabel("Penumbra Size", TF_ALIGN_LEFT);
            uiSliderFloat(&gASMCpuSettings.mPenumbraSize, 1.0f, 150.0f, 1.0f);
            uiLabel("Parallax Step Distance", TF_ALIGN_LEFT);
            uiSliderFloat(&gASMCpuSettings.mParallaxStepDistance, 1.0f, 100.0f, 1.0f);
            uiLabel("Parallax Step Z Bias", TF_ALIGN_LEFT);
            uiSliderFloat(&gASMCpuSettings.mParallaxStepBias, 1.0f, 200.0f, 1.0f);
            uiLayoutAutoTextRows(1);
            uiCheckbox("Display ASM Debug Textures", &gASMCpuSettings.mShowDebugTextures);

            if ((int)gCameraUniformData.mShadowType != GuiController::currentlyShadowType)
            {
                LightShadowPlayground::refreshASM(nullptr);
                LightShadowPlayground::resetLightDir(nullptr);
            }
        }
        // Baked SDF
        else if (gCameraUniformData.mShadowType == SHADOW_TYPE_MESH_BAKED_SDF)
        {
            uiLayoutAutoTextRows(1);
            if (UI_WIDGET_IS_PRESSED(uiButton("Generate Missing SDF")))
            {
                LightShadowPlayground::checkForMissingSDFDataAsync(NULL);
            }
            uiCheckbox("Automatic Sun Movement", &gLightCpuSettings.mAutomaticSunMovement);
            uiLayoutAutoTextRows(2);
            uiLabel("Light Source Angle", TF_ALIGN_LEFT);
            uiSliderFloat(&gLightCpuSettings.mSourceAngle, 0.001f, 4.0f, 0.001f);
            uiLayoutAutoTextRows(1);
            uiCheckbox("Display baked SDF mesh data on the screen", &gBakedSDFMeshSettings.mDrawSDFMeshVisualization);
        }

        // VSM and MSM blur
        if (gCameraUniformData.mShadowType == SHADOW_TYPE_VSM || gCameraUniformData.mShadowType == SHADOW_TYPE_MSM)
        {
            uiLayoutAutoTextRows(2);
            uiLabel("Gaussian Blur Kernel Radius", TF_ALIGN_LEFT);
            uiSliderUint(&gBlurConstantsData.mFilterRadius, 1, 16, 1);
            uiLabel("Gaussian Blur Sigma", TF_ALIGN_LEFT);
            uiSliderFloat(&gGaussianBlurSigma[0], 0.1f, 5.0f, 0.1f);
        }

        // Scripts
        {
            int testScriptCount = sizeof(gTestScripts) / sizeof(gTestScripts[0]);
            uiLayoutAutoTextRows(2);
            uiLabel("Test Scripts", TF_ALIGN_LEFT);
            gCurrentScriptIndex = UI_WIDGET_GET_SELECTED(uiDropdown(gTestScripts, testScriptCount, gCurrentScriptIndex));
            uiLayoutAutoTextRows(1);
            if (UI_WIDGET_IS_PRESSED(uiButton("Run")))
            {
                RunScript(NULL);
            }
        }

        GuiController::currentlyShadowType = (ShadowType)gCameraUniformData.mShadowType;
    }
    uiEndWidgetWindow();

    if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gSSSGuiDesc)))
    {
        uiLayoutAutoTextRows(1);
        uiCheckbox("Enable Screen Space Shadows", (bool*)&gCameraUniformData.mSSSEnabled);
        uiLayoutAutoTextRows(2);
        uiLabel("Surface Thickness", TF_ALIGN_LEFT);
        uiSliderFloat(&gSSSUniformData.mSurfaceThickness, 0.0f, 0.3f, 0.0001f);
        uiLabel("Bilinear Threshold", TF_ALIGN_LEFT);
        uiSliderFloat(&gSSSUniformData.mBilinearThreshold, 0.0f, 0.5f, 0.001f);
        uiLabel("Shadow Contrast", TF_ALIGN_LEFT);
        uiSliderFloat(&gSSSUniformData.mShadowContrast, 0.0f, 16.0f, 0.1f);
        uiLayoutAutoTextRows(1);
        uiCheckbox("Ignore Edge Pixels", (bool*)&gSSSUniformData.mIgnoreEdgePixels);
        uiCheckbox("Bilinear Sampling Offset Mode", (bool*)&gSSSUniformData.mBilinearSamplingOffsetMode);
        {
            uiLayoutAutoTextRows(2);
            static const char* debugOutputNames[] = { "None", "Edge Mask", "Thread Index", "Wave Index" };
            uiLabel("Debug Ouput Mode", TF_ALIGN_LEFT);
            gSSSUniformData.mDebugOutputMode =
                UI_WIDGET_GET_SELECTED(uiDropdown(debugOutputNames, TF_ARRAY_COUNT(debugOutputNames), gSSSUniformData.mDebugOutputMode));
        }
    }
    uiEndWidgetWindow();

    if (gASMCpuSettings.mShowDebugTextures)
    {
        if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gUIASMDebugTexturesWindowDesc)))
        {
            float                   scale = 0.15f;
            float2                  screenSize = { (float)pRenderTargetVBPass->mWidth, (float)pRenderTargetVBPass->mHeight };
            float2                  texSize = screenSize * scale;
            static const TFTexture* textures[3];
            textures[0] = pRenderTargetASMDepthAtlas->pTexture;
            textures[1] = pRenderTargetASMDEMAtlas->pTexture;
            textures[2] = pRenderTargetASMIndirection[0]->pTexture;
            int texturesCount = TF_ARRAY_COUNT(textures);

            uiLayoutAutoTextRows(1);
            uiLabel("Debug RTs", TF_ALIGN_CENTER);
            uiLayoutSpaceBegin(TF_LAYOUT_STATIC, 0, texturesCount);
            float texPosX = 0.0f;
            for (int i = 0; i < texturesCount; i++)
            {
                uiLayoutSpacePush(vec2(texPosX, 0), texSize);
                uiDebugTexture(textures[i], texSize);
                texPosX += texSize.x + 10.0f;
            }
            uiLayoutSpaceEnd();
        }
        uiEndWidgetWindow();
    }
}

void GuiController::luaRegisterGui()
{
    TFLuaWidgetVariableDesc luaVarDesc = {};
    TFLuaWidgetFunctionDesc luaFuncDesc = {};

    // Checkboxes
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
    luaVarDesc.pLabel = "Enable Screen Space Shadows";
    luaVarDesc.pBool = (bool*)&gCameraUniformData.mSSSEnabled;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Ignore Edge Pixels";
    luaVarDesc.pBool = (bool*)&gSSSUniformData.mIgnoreEdgePixels;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Bilinear Sampling Offset Mode";
    luaVarDesc.pBool = (bool*)&gSSSUniformData.mBilinearSamplingOffsetMode;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Hold triangles";
    luaVarDesc.pBool = &gAppSettings.mHoldFilteredTriangles;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Sun can move";
    luaVarDesc.pBool = &gASMCpuSettings.mSunCanMove;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Parallax corrected";
    luaVarDesc.pBool = &gASMCpuSettings.mEnableParallax;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Display ASM Debug Textures";
    luaVarDesc.pBool = &gASMCpuSettings.mShowDebugTextures;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Automatic Sun Movement";
    luaVarDesc.pBool = &gLightCpuSettings.mAutomaticSunMovement;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Display baked SDF mesh data on the screen";
    luaVarDesc.pBool = &gBakedSDFMeshSettings.mDrawSDFMeshVisualization;
    luaRegisterWidgetVariable(&luaVarDesc);

    // float sliders
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
    luaVarDesc.pLabel = "Surface Thickness";
    luaVarDesc.pFloat = &gSSSUniformData.mSurfaceThickness;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Bilinear Threshold";
    luaVarDesc.pFloat = &gSSSUniformData.mBilinearThreshold;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Shadow Contrast";
    luaVarDesc.pFloat = &gSSSUniformData.mShadowContrast;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "ESM Control";
    luaVarDesc.pFloat = &gEsmCpuSettings.mEsmControl;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Min Variance";
    luaVarDesc.pFloat = &gCameraUniformData.mVSM.mMinVariance;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Gaussian Blur Sigma";
    luaVarDesc.pFloat = &gGaussianBlurSigma[0];
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Rounding Error Correction";
    luaVarDesc.pFloat = &gCameraUniformData.mMSM.mRoundingErrorCorrection;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "MSM Bleeding Reduction";
    luaVarDesc.pFloat = &gCameraUniformData.mMSM.mBleedingReduction;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Light Update Angle";
    luaVarDesc.pFloat = &gLightDirUpdateAngle;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Penumbra Size";
    luaVarDesc.pFloat = &gASMCpuSettings.mPenumbraSize;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Parallax Step Distance";
    luaVarDesc.pFloat = &gASMCpuSettings.mParallaxStepDistance;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Parallax Step Z Bias";
    luaVarDesc.pFloat = &gASMCpuSettings.mParallaxStepBias;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Light Source Angle";
    luaVarDesc.pFloat = &gLightCpuSettings.mSourceAngle;
    luaRegisterWidgetVariable(&luaVarDesc);

    // float2 sliders
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
    luaVarDesc.pLabel = "Time of Day";
    luaVarDesc.pFloat = &gLightCpuSettings.mTimeOfDay;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
    luaFuncDesc.pLabel = "Time of Day";
    luaFuncDesc.pFuncData = NULL;
    luaFuncDesc.pFunc = LightShadowPlayground::refreshASM;
    luaRegisterWidgetFunction(&luaFuncDesc);
    luaVarDesc.pLabel = "VSM Bleeding Reduction";
    luaVarDesc.pFloat2 = &gCameraUniformData.mVSM.mBleedingReduction;
    luaRegisterWidgetVariable(&luaVarDesc);

    // uint slider
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_UINT;
    luaVarDesc.pLabel = "Gaussian Blur Kernel Radius";
    luaVarDesc.pUint = &gBlurConstantsData.mFilterRadius;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Max tiles to render per frame";
    luaVarDesc.pUint = &gASMMaxTilesPerPass;
    luaRegisterWidgetVariable(&luaVarDesc);

    // Dropdowns
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_DROPDOWN;
    luaVarDesc.pLabel = "Debug Ouput Mode";
    luaVarDesc.pUint = &gSSSUniformData.mDebugOutputMode;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Shadow Type";
    luaVarDesc.pUint = &gCameraUniformData.mShadowType;
    luaRegisterWidgetVariable(&luaVarDesc);

    luaVarDesc.pLabel = "Test Scripts";
    luaVarDesc.pUint = &gCurrentScriptIndex;
    luaRegisterWidgetVariable(&luaVarDesc);

    // Progress bars
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_PROGRESS_BAR;
    luaVarDesc.pLabel = "               [ProgressBar]               ";
    luaVarDesc.pSize = &sSDFProgressValue;
    luaRegisterWidgetVariable(&luaVarDesc);

    // Button functions
    luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
    luaFuncDesc.pFuncData = NULL;
    luaFuncDesc.pLabel = "Refresh Cache";
    luaFuncDesc.pFunc = LightShadowPlayground::refreshASM;
    luaRegisterWidgetFunction(&luaFuncDesc);
    luaFuncDesc.pLabel = "Reset Light Dir";
    luaFuncDesc.pFunc = LightShadowPlayground::resetLightDir;
    luaRegisterWidgetFunction(&luaFuncDesc);
    luaFuncDesc.pLabel = "Generate Missing SDF";
    luaFuncDesc.pFunc = LightShadowPlayground::checkForMissingSDFDataAsync;
    luaRegisterWidgetFunction(&luaFuncDesc);
    luaFuncDesc.pLabel = "Run"; // script
    luaFuncDesc.pFunc = RunScript;
    luaRegisterWidgetFunction(&luaFuncDesc);

#if TEST_GPU_BREADCRUMBS
    if (pRenderer->pGpu->mGpuMarkers)
    {
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        for (uint32_t i = 0; i < RENDERING_STEP_COUNT; i++)
        {
            gUIRenderingStepIndices[i] = i;
            snprintf(gSimulateCrashButtonLabels[i], MAX_LABEL_STR_LENGTH, "Simulate crash (%s)", gMarkerNames[i]);
            LuaWidgetFunctionCallback crashCallback = [](void* pUserData) { bCrashedSteps[*(uint32_t*)pUserData] = true; };

            luaFuncDesc.pLabel = gSimulateCrashButtonLabels[i];
            luaFuncDesc.pFunc = crashCallback;
            luaFuncDesc.pFuncData = &gUIRenderingStepIndices[i];
            luaRegisterWidgetFunction(&luaFuncDesc);
        }
    }
#endif
}
DEFINE_APPLICATION_MAIN(LightShadowPlayground)
