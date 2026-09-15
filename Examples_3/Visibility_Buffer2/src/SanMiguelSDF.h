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

#ifndef SanMiguel_SDF_h
#define SanMiguel_SDF_h

#define MAX_MESH_NAME_LEN 128

#include "../../../Common_3/Utilities/Interfaces/IMath.h"

#include "SanMiguel.h"
/************************************************************************/
// Meshes
/************************************************************************/
struct SDFVolumeData;
struct MeshInfo;

typedef bool (*GenerateVolumeDataFromFileFunc)(SDFVolumeData**, MeshInfo*);

struct MeshInfo
{
    const char*     name = NULL;
    TFMaterialFlags materialFlags = MATERIAL_FLAG_NONE;
    float           twoSidedWorldSpaceBias = 0.0f;
    bool            sdfGenerated = false;

    MeshInfo() {}
    MeshInfo(const char* name, TFMaterialFlags materialFlags, float twoSidedWorldSpaceBias):
        name(name), materialFlags(materialFlags), twoSidedWorldSpaceBias(twoSidedWorldSpaceBias), sdfGenerated(false)
    {
    }
};

struct SDFMesh
{
    TFGeometry*     pGeometry = NULL;
    TFGeometryData* pGeometryData = NULL;
    MeshInfo*       pSubMeshesInfo = NULL;
    uint32_t*       pSubMeshesGroupsSizes = NULL;
    uint32_t*       pSubMeshesIndices = NULL;
    uint32_t        numSubMeshesGroups = 0;
    uint32_t        numGeneratedSDFMeshes = 0;
};

void adjustAABB(TFAABB* ownerAABB, const vec3& point);
void adjustAABB(TFAABB* ownerAABB, const TFAABB& otherAABB);
void alignAABB(TFAABB* ownerAABB, float alignment);
vec3 calculateAABBSize(const TFAABB* ownerAABB);
vec3 calculateAABBExtent(const TFAABB* ownerAABB);
vec3 calculateAABBCenter(const TFAABB* ownerAABB);

void loadBakedSDFData(SDFMesh* outMesh, uint32_t startIdx, bool generateSDFVolumeData, SDFVolumeData** sdfVolumeInstances,
                      GenerateVolumeDataFromFileFunc generateVolumeDataFromFileFunc);

#endif
