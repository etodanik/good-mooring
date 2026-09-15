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
#include "SanMiguelSDF.h"

#include "../../../Common_3/Utilities/Interfaces/IFileSystem.h"
#include "../../../Common_3/Utilities/Interfaces/ILog.h"

#include "../../../Common_3/Utilities/Interfaces/IMemory.h"

void loadBakedSDFData(SDFMesh* outMesh, uint32_t startIdx, bool generateSDFVolumeData, SDFVolumeData** sdfVolumeInstances,
                      GenerateVolumeDataFromFileFunc generateVolumeDataFromFileFunc)
{
    uint32_t idxFirstMeshInGroup = 0;

    // for each submesh group.
    // in case there is no submesh groups, numSubMeshesGroups = numSubMeshes in the geometry
    for (uint32_t groupNum = 0; groupNum < outMesh->numSubMeshesGroups; ++groupNum)
    {
        MeshInfo&      meshInfo = outMesh->pSubMeshesInfo[idxFirstMeshInGroup];
        uint32_t       meshGroupSize = outMesh->pSubMeshesGroupsSizes ? outMesh->pSubMeshesGroupsSizes[groupNum] : 1;
        SDFVolumeData* volumeData = NULL;

        (*generateVolumeDataFromFileFunc)(&volumeData, &meshInfo);

        sdfVolumeInstances[startIdx++] = volumeData;

        if (volumeData)
        {
            meshInfo.sdfGenerated = true;
            ++outMesh->numGeneratedSDFMeshes;
        }

        idxFirstMeshInGroup += meshGroupSize;
    }
}

void adjustAABB(TFAABB* ownerAABB, const vec3& point)
{
    ownerAABB->min.x = (fmin(point.x, ownerAABB->min.x));
    ownerAABB->min.y = (fmin(point.y, ownerAABB->min.y));
    ownerAABB->min.z = (fmin(point.z, ownerAABB->min.z));

    ownerAABB->max.x = (fmax(point.x, ownerAABB->max.x));
    ownerAABB->max.y = (fmax(point.y, ownerAABB->max.y));
    ownerAABB->max.z = (fmax(point.z, ownerAABB->max.z));
}
void adjustAABB(TFAABB* ownerAABB, const TFAABB& otherAABB)
{
    ownerAABB->min.x = (fmin(otherAABB.min.x, ownerAABB->min.x));
    ownerAABB->min.y = (fmin(otherAABB.min.y, ownerAABB->min.y));
    ownerAABB->min.z = (fmin(otherAABB.min.z, ownerAABB->min.z));

    ownerAABB->max.x = (fmax(otherAABB.max.x, ownerAABB->max.x));
    ownerAABB->max.y = (fmax(otherAABB.max.y, ownerAABB->max.y));
    ownerAABB->max.z = (fmax(otherAABB.max.z, ownerAABB->max.z));
}

void alignAABB(TFAABB* ownerAABB, float alignment)
{
    vec3 boxMin = ownerAABB->min / alignment;
    boxMin = vec3(floorf(boxMin.x) * alignment, floorf(boxMin.y) * alignment, 0.0f);
    vec3 boxMax = ownerAABB->max / alignment;
    boxMax = vec3(ceilf(boxMax.x) * alignment, ceilf(boxMax.y) * alignment, 0.0f);
    *ownerAABB = TFAABB(boxMin, boxMax);
}

vec3 calculateAABBSize(const TFAABB* ownerAABB) { return ownerAABB->max - ownerAABB->min; }

vec3 calculateAABBExtent(const TFAABB* ownerAABB) { return 0.5f * (ownerAABB->max - ownerAABB->min); }

vec3 calculateAABBCenter(const TFAABB* ownerAABB) { return (ownerAABB->max + ownerAABB->min) * 0.5f; }
