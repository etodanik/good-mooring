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

#include "../Interfaces/INodeGraph.h"
#include "../Interfaces/IMath.h"

#include "../ThirdParty/OpenSource/Nothings/stb_ds.h"
#include "../ThirdParty/OpenSource/bstrlib/bstrlib.h"

typedef struct NodeDescT
{
    int32_t       key;
    TFNGNodeDesc* value;
} NodeDescT;

typedef struct PortTypeDescT
{
    int32_t           key;
    TFNGPortTypeDesc* value;
} PortTypeDescT;

typedef struct VisitedNodeT
{
    TFNGNode* key;
    bool      value;
} VisitedNodeT;

// We have to use node internal to save output connections with other nodes. To make removal of nodes more faster.
typedef struct NodeInternal
{
    TFNGNode         mNode;
    TFNGConnection** ppOutputConnectionArray;
} NodeInternal;

struct TFNodeGraphDesc
{
    NodeDescT*     pNodeDescHashSet;
    PortTypeDescT* pPortTypeDescHashSet;
    TFNGNodeDesc** ppNodeDescArray;
};

struct TFNodeGraphContext
{
    TFNodeGraphDesc* pDesc;
    NodeInternal**   ppNodeArray;

    VisitedNodeT* pVisitedFlag;
    TFNGNode**    ppLoopStack;
};

typedef TFNGUserData128 NGUserDataInt;
typedef TFNGUserData128 NGUserDataInt2;
typedef TFNGUserData128 NGUserDataInt3;
typedef TFNGUserData128 NGUserDataInt4;
typedef TFNGUserData128 NGUserDataFloat;
typedef TFNGUserData128 NGUserDataFloat2;
typedef TFNGUserData128 NGUserDataFloat3;
typedef TFNGUserData128 NGUserDataFloat4;

typedef struct NGUserDataClamp
{
    TFNGUserData mData;
    float        mMin;
    float        mMax;
} NGUserDataClamp;

typedef struct NGUserDataMin
{
    TFNGUserData mData;
    float        mMinA;
    float        mMinB;
} NGUserDataMin;

typedef struct NGUserDataMax
{
    TFNGUserData mData;
    float        mMaxA;
    float        mMaxB;
} NGUserDataMax;

REFLECT_STRUCT_BEGIN(NGUserDataInt)
REFLECT_STRUCT_MEMBER(NGUserDataInt, R_INT(int32_t), mX)
REFLECT_STRUCT_END(NGUserDataInt)

REFLECT_STRUCT_BEGIN(NGUserDataInt2)
REFLECT_STRUCT_MEMBER(NGUserDataInt2, R_INT(int32_t), mX)
REFLECT_STRUCT_MEMBER(NGUserDataInt2, R_INT(int32_t), mY)
REFLECT_STRUCT_END(NGUserDataInt2)

REFLECT_STRUCT_BEGIN(NGUserDataInt3)
REFLECT_STRUCT_MEMBER(NGUserDataInt3, R_INT(int32_t), mX)
REFLECT_STRUCT_MEMBER(NGUserDataInt3, R_INT(int32_t), mY)
REFLECT_STRUCT_MEMBER(NGUserDataInt3, R_INT(int32_t), mZ)
REFLECT_STRUCT_END(NGUserDataInt3)

REFLECT_STRUCT_BEGIN(NGUserDataInt4)
REFLECT_STRUCT_MEMBER(NGUserDataInt4, R_INT(int32_t), mX)
REFLECT_STRUCT_MEMBER(NGUserDataInt4, R_INT(int32_t), mY)
REFLECT_STRUCT_MEMBER(NGUserDataInt4, R_INT(int32_t), mZ)
REFLECT_STRUCT_MEMBER(NGUserDataInt4, R_INT(int32_t), mW)
REFLECT_STRUCT_END(NGUserDataInt4)

REFLECT_STRUCT_BEGIN(NGUserDataFloat)
REFLECT_STRUCT_MEMBER(NGUserDataFloat, R_FLOAT(float), mX)
REFLECT_STRUCT_END(NGUserDataFloat)

REFLECT_STRUCT_BEGIN(NGUserDataFloat2)
REFLECT_STRUCT_MEMBER(NGUserDataFloat2, R_FLOAT(float), mX)
REFLECT_STRUCT_MEMBER(NGUserDataFloat2, R_FLOAT(float), mY)
REFLECT_STRUCT_END(NGUserDataFloat2)

REFLECT_STRUCT_BEGIN(NGUserDataFloat3)
REFLECT_STRUCT_MEMBER(NGUserDataFloat3, R_FLOAT(float), mX)
REFLECT_STRUCT_MEMBER(NGUserDataFloat3, R_FLOAT(float), mY)
REFLECT_STRUCT_MEMBER(NGUserDataFloat3, R_FLOAT(float), mZ)
REFLECT_STRUCT_END(NGUserDataFloat3)

REFLECT_STRUCT_BEGIN(NGUserDataFloat4)
REFLECT_STRUCT_MEMBER(NGUserDataFloat4, R_FLOAT(float), mX)
REFLECT_STRUCT_MEMBER(NGUserDataFloat4, R_FLOAT(float), mY)
REFLECT_STRUCT_MEMBER(NGUserDataFloat4, R_FLOAT(float), mZ)
REFLECT_STRUCT_MEMBER(NGUserDataFloat4, R_FLOAT(float), mW)
REFLECT_STRUCT_END(NGUserDataFloat4)

#define FLOAT_LIMIT 1000000.0f

REFLECT_STRUCT_BEGIN(NGUserDataClamp)
R_HINT_MINMAX(float, -FLOAT_LIMIT, FLOAT_LIMIT)
REFLECT_STRUCT_MEMBER(NGUserDataClamp, R_FLOAT(float), mMin)
R_HINT_MINMAX(float, -FLOAT_LIMIT, FLOAT_LIMIT)
REFLECT_STRUCT_MEMBER(NGUserDataClamp, R_FLOAT(float), mMax)
REFLECT_STRUCT_END(NGUserDataClamp)

REFLECT_STRUCT_BEGIN(NGUserDataMin)
R_HINT_MINMAX(float, -FLOAT_LIMIT, FLOAT_LIMIT)
REFLECT_STRUCT_MEMBER(NGUserDataMin, R_FLOAT(float), mMinA)
R_HINT_MINMAX(float, -FLOAT_LIMIT, FLOAT_LIMIT)
REFLECT_STRUCT_MEMBER(NGUserDataMin, R_FLOAT(float), mMinB)
REFLECT_STRUCT_END(NGUserDataMin)

REFLECT_STRUCT_BEGIN(NGUserDataMax)
R_HINT_MINMAX(float, -FLOAT_LIMIT, FLOAT_LIMIT)
REFLECT_STRUCT_MEMBER(NGUserDataMax, R_FLOAT(float), mMaxA)
R_HINT_MINMAX(float, -FLOAT_LIMIT, FLOAT_LIMIT)
REFLECT_STRUCT_MEMBER(NGUserDataMax, R_FLOAT(float), mMaxB)
REFLECT_STRUCT_END(NGUserDataMax)

int32_t ngGetVectorDimOfNodeType(TFNodeGraphBasicNode nodeType)
{
    if (nodeType < TF_NODE_GRAPH_FLOAT || TF_NODE_GRAPH_INT_4 < nodeType)
    {
        return -1;
    }

    int32_t dims[TF_NODE_GRAPH_INT_4 - TF_NODE_GRAPH_FLOAT + 1] = { 1, 2, 3, 4, 1, 2, 3, 4 };

    int32_t offsetType = nodeType - TF_NODE_GRAPH_FLOAT;
    return dims[offsetType];
}

static void addConstNode(TFNodeGraphDesc* pDesc, const char* pName, TFNodeGraphBasicNode mType, TypeInfoStruct* pInfoStruct)
{
    BEGIN_REGISTER_NODE_GRAPH_DESC()
    BEGIN_REGISTER_NODE_GRAPH_NODE_TO_DESC(pDesc, pName, mType, sizeof(TFNGUserData128));
    ADD_OUTPUT_PORT_TO_NODE_DESC("Out", TF_NODE_GRAPH_TYPE_VECTOR);
    SET_REFLECTED_USER_STRUCT_INFO(pInfoStruct, 0);
    END_REGISTER_NODE_GRAPH_NODE_TO_DESC(pDesc);
    END_REGISTER_NODE_GRAPH_DESC()
}

static void addBinarMathNode(TFNodeGraphDesc* pDesc, const char* pName, TFNodeGraphBasicNode mType)
{
    BEGIN_REGISTER_NODE_GRAPH_DESC()
    BEGIN_REGISTER_NODE_GRAPH_NODE_TO_DESC(pDesc, pName, mType, sizeof(TFNGUserData));
    ADD_INPUT_PORT_TO_NODE_DESC("A", TF_NODE_GRAPH_TYPE_VECTOR);
    ADD_INPUT_PORT_TO_NODE_DESC("B", TF_NODE_GRAPH_TYPE_VECTOR);
    ADD_OUTPUT_PORT_TO_NODE_DESC("Out", TF_NODE_GRAPH_TYPE_VECTOR);
    END_REGISTER_NODE_GRAPH_NODE_TO_DESC(pDesc);
    END_REGISTER_NODE_GRAPH_DESC()
}

static void addUnaryMathNode(TFNodeGraphDesc* pDesc, const char* pName, TFNodeGraphBasicNode mType)
{
    BEGIN_REGISTER_NODE_GRAPH_DESC()
    BEGIN_REGISTER_NODE_GRAPH_NODE_TO_DESC(pDesc, pName, mType, sizeof(TFNGUserData));
    ADD_INPUT_PORT_TO_NODE_DESC("In", TF_NODE_GRAPH_TYPE_VECTOR);
    ADD_OUTPUT_PORT_TO_NODE_DESC("Out", TF_NODE_GRAPH_TYPE_VECTOR);
    END_REGISTER_NODE_GRAPH_NODE_TO_DESC(pDesc);
    END_REGISTER_NODE_GRAPH_DESC()
}

static void addBasicNodesToDesc(TFNodeGraphDesc* pDesc)
{
    BEGIN_REGISTER_NODE_GRAPH_DESC()
    REGISTER_NODE_GRAPH_TYPE_TO_DESC(pDesc, "Bool", TF_NODE_GRAPH_TYPE_BOOL, sizeof(bool));
    REGISTER_NODE_GRAPH_TYPE_TO_DESC(pDesc, "Vector", TF_NODE_GRAPH_TYPE_VECTOR, sizeof(uint32_t) * 4);

    BEGIN_REGISTER_NODE_GRAPH_NODE_TO_DESC(pDesc, "Constant/Toggle", TF_NODE_GRAPH_TOGGLE, sizeof(TFNGUserData128));
    ADD_OUTPUT_PORT_TO_NODE_DESC("Out", TF_NODE_GRAPH_TYPE_BOOL);
    SET_REFLECTED_USER_STRUCT_INFO(GET_TYPE_INFO(NGUserDataInt), 0);
    END_REGISTER_NODE_GRAPH_NODE_TO_DESC(pDesc);

    addConstNode(pDesc, "Constant/Float", TF_NODE_GRAPH_FLOAT, GET_TYPE_INFO(NGUserDataFloat));
    addConstNode(pDesc, "Constant/Float2", TF_NODE_GRAPH_FLOAT_2, GET_TYPE_INFO(NGUserDataFloat2));
    addConstNode(pDesc, "Constant/Float3", TF_NODE_GRAPH_FLOAT_3, GET_TYPE_INFO(NGUserDataFloat3));
    addConstNode(pDesc, "Constant/Float4", TF_NODE_GRAPH_FLOAT_4, GET_TYPE_INFO(NGUserDataFloat4));
    addConstNode(pDesc, "Constant/Int", TF_NODE_GRAPH_INT, GET_TYPE_INFO(NGUserDataInt));
    addConstNode(pDesc, "Constant/Int2", TF_NODE_GRAPH_INT_2, GET_TYPE_INFO(NGUserDataInt2));
    addConstNode(pDesc, "Constant/Int3", TF_NODE_GRAPH_INT_3, GET_TYPE_INFO(NGUserDataInt3));
    addConstNode(pDesc, "Constant/Int4", TF_NODE_GRAPH_INT_4, GET_TYPE_INFO(NGUserDataInt4));

    addBinarMathNode(pDesc, "Math/Add", TF_NODE_GRAPH_ADD);
    addBinarMathNode(pDesc, "Math/Sub", TF_NODE_GRAPH_SUB);
    addBinarMathNode(pDesc, "Math/Mul", TF_NODE_GRAPH_MUL);
    addBinarMathNode(pDesc, "Math/Div", TF_NODE_GRAPH_DIV);
    addBinarMathNode(pDesc, "Math/Dot", TF_NODE_GRAPH_DOT);
    addBinarMathNode(pDesc, "Math/Cross", TF_NODE_GRAPH_CROSS);

    addUnaryMathNode(pDesc, "Math/Normalize", TF_NODE_GRAPH_NORMALIZE);
    addUnaryMathNode(pDesc, "Math/Saturate", TF_NODE_GRAPH_SATURATE);
    addUnaryMathNode(pDesc, "Math/Abs", TF_NODE_GRAPH_ABS);

    BEGIN_REGISTER_NODE_GRAPH_NODE_TO_DESC(pDesc, "Math/Split", TF_NODE_GRAPH_SPLIT, sizeof(TFNGUserData128));
    ADD_INPUT_PORT_TO_NODE_DESC("In", TF_NODE_GRAPH_TYPE_VECTOR);
    ADD_OUTPUT_PORT_TO_NODE_DESC("X", TF_NODE_GRAPH_TYPE_VECTOR);
    ADD_OUTPUT_PORT_TO_NODE_DESC("Y", TF_NODE_GRAPH_TYPE_VECTOR);
    ADD_OUTPUT_PORT_TO_NODE_DESC("Z", TF_NODE_GRAPH_TYPE_VECTOR);
    ADD_OUTPUT_PORT_TO_NODE_DESC("W", TF_NODE_GRAPH_TYPE_VECTOR);
    END_REGISTER_NODE_GRAPH_NODE_TO_DESC(pDesc);

    BEGIN_REGISTER_NODE_GRAPH_NODE_TO_DESC(pDesc, "Math/Make Vector", TF_NODE_GRAPH_MAKE_VECTOR, sizeof(TFNGUserData128));
    ADD_INPUT_PORT_TO_NODE_DESC("X", TF_NODE_GRAPH_TYPE_VECTOR);
    ADD_INPUT_PORT_TO_NODE_DESC("Y", TF_NODE_GRAPH_TYPE_VECTOR);
    ADD_INPUT_PORT_TO_NODE_DESC("Z", TF_NODE_GRAPH_TYPE_VECTOR);
    ADD_INPUT_PORT_TO_NODE_DESC("W", TF_NODE_GRAPH_TYPE_VECTOR);
    ADD_OUTPUT_PORT_TO_NODE_DESC("Out", TF_NODE_GRAPH_TYPE_VECTOR);
    END_REGISTER_NODE_GRAPH_NODE_TO_DESC(pDesc);

    BEGIN_REGISTER_NODE_GRAPH_NODE_TO_DESC(pDesc, "Math/Clamp", TF_NODE_GRAPH_CLAMP, sizeof(NGUserDataClamp));
    SET_REFLECTED_USER_STRUCT_INFO(GET_TYPE_INFO(NGUserDataClamp), 0);
    ADD_INPUT_PORT_TO_NODE_DESC("In", TF_NODE_GRAPH_TYPE_VECTOR);
    ADD_INPUT_PORT_REFLECTED_MEMBER_TO_NODE_DESC(mMin);
    ADD_INPUT_PORT_REFLECTED_MEMBER_TO_NODE_DESC(mMax);
    ADD_OUTPUT_PORT_TO_NODE_DESC("Out", TF_NODE_GRAPH_TYPE_VECTOR);
    END_REGISTER_NODE_GRAPH_NODE_TO_DESC(pDesc);

    BEGIN_REGISTER_NODE_GRAPH_NODE_TO_DESC(pDesc, "Math/Min", TF_NODE_GRAPH_MIN, sizeof(NGUserDataMin));
    SET_REFLECTED_USER_STRUCT_INFO(GET_TYPE_INFO(NGUserDataMin), 0);
    ADD_INPUT_PORT_REFLECTED_MEMBER_TO_NODE_DESC(mMinA);
    ADD_INPUT_PORT_REFLECTED_MEMBER_TO_NODE_DESC(mMinB);
    ADD_OUTPUT_PORT_TO_NODE_DESC("Out", TF_NODE_GRAPH_TYPE_VECTOR);
    END_REGISTER_NODE_GRAPH_NODE_TO_DESC(pDesc);

    BEGIN_REGISTER_NODE_GRAPH_NODE_TO_DESC(pDesc, "Math/Max", TF_NODE_GRAPH_MAX, sizeof(NGUserDataMax));
    SET_REFLECTED_USER_STRUCT_INFO(GET_TYPE_INFO(NGUserDataMax), 0);
    ADD_INPUT_PORT_REFLECTED_MEMBER_TO_NODE_DESC(mMaxA);
    ADD_INPUT_PORT_REFLECTED_MEMBER_TO_NODE_DESC(mMaxB);
    ADD_OUTPUT_PORT_TO_NODE_DESC("Out", TF_NODE_GRAPH_TYPE_VECTOR);
    END_REGISTER_NODE_GRAPH_NODE_TO_DESC(pDesc);

    END_REGISTER_NODE_GRAPH_DESC()
}

TFNodeGraphDesc* ngAddNodeGraphDesc(bool addBasicNodes)
{
    TFNodeGraphDesc* pDesc = tf_calloc(1, sizeof(TFNodeGraphDesc));
    memset(pDesc, 0, sizeof(TFNodeGraphDesc));
    if (addBasicNodes)
    {
        addBasicNodesToDesc(pDesc);
    }
    return pDesc;
}

void ngRemoveNodeGraphDesc(TFNodeGraphDesc* pNGDesc)
{
    // free node descs
    {
        size_t len = hmlen(pNGDesc->pNodeDescHashSet);
        for (size_t i = 0; i < len; i++)
        {
            if (pNGDesc->pNodeDescHashSet[i].value)
            {
                tf_free(pNGDesc->pNodeDescHashSet[i].value);
            }
        }

        hmfree(pNGDesc->pNodeDescHashSet);
        arrfree(pNGDesc->ppNodeDescArray);
    }

    // free port type descs
    {
        size_t len = hmlen(pNGDesc->pPortTypeDescHashSet);
        for (size_t i = 0; i < len; i++)
        {
            if (pNGDesc->pPortTypeDescHashSet[i].value)
            {
                tf_free(pNGDesc->pPortTypeDescHashSet[i].value);
            }
        }

        hmfree(pNGDesc->pPortTypeDescHashSet);
    }
    tf_free(pNGDesc);
}

bool ngAddNodeDesc(TFNodeGraphDesc* pNGDesc, const TFNGNodeDesc* pNodeDesc)
{
    ptrdiff_t typeIdx = hmgeti(pNGDesc->pNodeDescHashSet, pNodeDesc->mType);
    ASSERT(typeIdx == -1);

    size_t size =
        sizeof(TFNGNodeDesc) + pNodeDesc->mInputPortCount * sizeof(TFNGPortDesc) + pNodeDesc->mOutputPortCount * sizeof(TFNGPortDesc);
    TFNGNodeDesc* pDesc = (TFNGNodeDesc*)tf_calloc(1, size);
    memset(pDesc, 0, size);

    *pDesc = *pNodeDesc;

    pDesc->pInputPortDesc = (TFNGPortDesc*)((uint8_t*)pDesc + sizeof(TFNGNodeDesc));
    pDesc->pOutputPortDesc = pDesc->pInputPortDesc + pDesc->mInputPortCount;
    pDesc->mNodeSize = sizeof(NodeInternal) + (sizeof(TFNGPort) + sizeof(TFNGConnection)) * pDesc->mInputPortCount +
                       sizeof(TFNGPort) * pDesc->mOutputPortCount + sizeof(uint32_t) * pDesc->mOutputPortCount + pDesc->mNodeUserDataSize;

    memcpy(pDesc->pInputPortDesc, pNodeDesc->pInputPortDesc, pNodeDesc->mInputPortCount * sizeof(TFNGPortDesc));
    memcpy(pDesc->pOutputPortDesc, pNodeDesc->pOutputPortDesc, pNodeDesc->mOutputPortCount * sizeof(TFNGPortDesc));

    for (uint32_t i = 0; i < pDesc->mOutputPortCount; i++)
    {
        int32_t                 portType = pDesc->pOutputPortDesc[i].mType;
        const TFNGPortTypeDesc* portDesc = ngGetPortTypeByType(pNGDesc, portType);
        pDesc->mOutputPortDataSize += portDesc->mDataSize;
    }

    for (uint32_t i = 0; i < pDesc->mInputPortCount; i++)
    {
        int32_t memberIdx = pDesc->pInputPortDesc[i].mReflectedMemeberIdx;
        if (memberIdx != -1)
        {
            int32_t requeredType = -1;
            ASSERT(pDesc->pReflectedUserDataInfo);

            TypeInfoStructMember* pMember = pDesc->pReflectedUserDataInfo->pMembers + memberIdx;
            if (pMember->pType->mKind == TYPE_INFO_TAG_INT && pMember->mIsBool)
            {
                requeredType = TF_NODE_GRAPH_TYPE_BOOL;
            }
            else if (pMember->pType->mKind == TYPE_INFO_TAG_FLOAT || pMember->pType->mKind == TYPE_INFO_TAG_INT)
            {
                requeredType = TF_NODE_GRAPH_TYPE_VECTOR;
            }
            else if (pMember->pType->mKind == TYPE_INFO_TAG_STRUCT &&
                     (strcmp(pMember->pType->pName, "float2") == 0 || strcmp(pMember->pType->pName, "float3") == 0 ||
                      strcmp(pMember->pType->pName, "float4") == 0))
            {
                requeredType = TF_NODE_GRAPH_TYPE_VECTOR;
            }

            if (requeredType != -1)
            {
                pDesc->pInputPortDesc[i].mType = requeredType;
            }
            else
            {
                pDesc->pInputPortDesc[i].mReflectedMemeberIdx = -1;
                pDesc->pInputPortDesc[i].mType = TF_NODE_GRAPH_TYPE_VECTOR;
            }
        }
    }

    hmput(pNGDesc->pNodeDescHashSet, pDesc->mType, pDesc);
    arrpush(pNGDesc->ppNodeDescArray, pDesc);
    return true;
}

bool ngAddPortType(TFNodeGraphDesc* pNGDesc, const TFNGPortTypeDesc* pPortType)
{
    ptrdiff_t typeIdx = hmgeti(pNGDesc->pPortTypeDescHashSet, pPortType->mType);
    ASSERT(typeIdx == -1);

    TFNGPortTypeDesc* pDesc = (TFNGPortTypeDesc*)tf_calloc(1, sizeof(TFNGPortTypeDesc));
    *pDesc = *pPortType;

    hmput(pNGDesc->pPortTypeDescHashSet, pDesc->mType, pDesc);
    return true;
}

TFNodeGraphContext* ngAddNodeGraphContext(TFNodeGraphDesc* pNGDesc)
{
    TFNodeGraphContext* pCtx = tf_calloc(1, sizeof(TFNodeGraphContext));
    memset(pCtx, 0, sizeof(TFNodeGraphContext));
    pCtx->pDesc = pNGDesc;

    hmdefault(pCtx->pVisitedFlag, false);
    arrsetlen(pCtx->ppLoopStack, 0);
    return pCtx;
}

void ngRemoveNodeGraphContext(TFNodeGraphContext* pCtx)
{
    // to free nodes
    {
        size_t len = arrlen(pCtx->ppNodeArray);
        for (size_t i = 0; i < len; i++)
        {
            NodeInternal* pNodeInternal = pCtx->ppNodeArray[i];

            if (pNodeInternal->mNode.pDesc->pRemoveNodeCallback)
            {
                pNodeInternal->mNode.pDesc->pRemoveNodeCallback(&pNodeInternal->mNode);
            }

            arrfree(pNodeInternal->ppOutputConnectionArray);
            tf_free(pNodeInternal);
        }

        arrfree(pCtx->ppNodeArray);
    }

    hmfree(pCtx->pVisitedFlag);
    arrfree(pCtx->ppLoopStack);
    tf_free(pCtx);
}

TFNGNode* ngAddNode(TFNodeGraphContext* pCtx, int32_t type)
{
    ptrdiff_t typeIdx = hmgeti(pCtx->pDesc->pNodeDescHashSet, type);
    ASSERT(typeIdx != -1);

    TFNGNodeDesc* pDesc = pCtx->pDesc->pNodeDescHashSet[typeIdx].value;
    NodeInternal* pNode = (NodeInternal*)tf_calloc(1, pDesc->mNodeSize);
    memset(pNode, 0, pDesc->mNodeSize);

    pNode->mNode.mInputPortCount = pDesc->mInputPortCount;
    pNode->mNode.mOutputPortCount = pDesc->mOutputPortCount;
    pNode->mNode.mType = pDesc->mType;
    pNode->mNode.pDesc = pDesc;

    uint8_t* pOffsetPointer = (uint8_t*)pNode + sizeof(NodeInternal);
    pNode->mNode.pInputConnections = (TFNGConnection*)pOffsetPointer;

    pOffsetPointer += sizeof(TFNGConnection) * pDesc->mInputPortCount;
    pNode->mNode.pInputPorts = (TFNGPort*)pOffsetPointer;

    pOffsetPointer += sizeof(TFNGPort) * pDesc->mInputPortCount;
    pNode->mNode.pOutputPorts = (TFNGPort*)pOffsetPointer;

    pOffsetPointer += sizeof(TFNGPort) * pDesc->mOutputPortCount;
    pNode->mNode.pOutputConnectionCounts = (uint32_t*)pOffsetPointer;

    pOffsetPointer += sizeof(uint32_t) * pDesc->mOutputPortCount;
    pNode->mNode.pUserData = (void*)pOffsetPointer;
    pNode->mNode.mUserDataSize = pDesc->mNodeUserDataSize;

    for (uint32_t i = 0; i < pDesc->mInputPortCount; i++)
    {
        TFNGConnection connection = { &pNode->mNode, NULL, i, 0 };
        pNode->mNode.pInputConnections[i] = connection;
        pNode->mNode.pInputPorts[i].mType = pDesc->pInputPortDesc[i].mType;

        const TFNGPortTypeDesc* portDesc = ngGetPortTypeByType(pCtx->pDesc, pNode->mNode.pInputPorts[i].mType);
        pNode->mNode.pInputPorts[i].mDataSize = portDesc->mDataSize;
    }

    for (uint32_t i = 0; i < pDesc->mOutputPortCount; i++)
    {
        pNode->mNode.pOutputPorts[i].mType = pDesc->pOutputPortDesc[i].mType;
        const TFNGPortTypeDesc* portDesc = ngGetPortTypeByType(pCtx->pDesc, pDesc->pOutputPortDesc[i].mType);
        pNode->mNode.pOutputPorts[i].mDataSize = portDesc->mDataSize;
        pNode->mNode.pOutputConnectionCounts[i] = 0;
    }

    arrpush(pCtx->ppNodeArray, pNode);

    if (pDesc->pAddNodeCallback)
    {
        pDesc->pAddNodeCallback(&pNode->mNode);
    }

    return &pNode->mNode;
}

TFNGNode* ngAddNodeByTypeName(TFNodeGraphContext* pCtx, const char* pName)
{
    uint32_t nodeDescCount = ngGetNodeDescCount(pCtx->pDesc);
    for (uint32_t i = 0; i < nodeDescCount; i++)
    {
        TFNGNodeDesc* pDesc = pCtx->pDesc->ppNodeDescArray[i];
        if (strcmp(pDesc->mName, pName) == 0)
        {
            return ngAddNode(pCtx, pDesc->mType);
        }
    }

    return NULL;
}

static void removeAllOutputConnection(NodeInternal* pOutputNode, TFNGNode* pInputNode)
{
    ASSERT(pOutputNode);
    ASSERT(pInputNode);

    size_t len = arrlen(pOutputNode->ppOutputConnectionArray);
    for (size_t i = 0; i < len; i++)
    {
        TFNGConnection* connection = pOutputNode->ppOutputConnectionArray[i]; //-V595
        if (connection != NULL && connection->pInputNode == pInputNode)
        {
            connection->pOutputNode = NULL;
            connection->mOutputPortIdx = 0;
            pOutputNode->mNode.pOutputConnectionCounts[connection->mOutputPortIdx] -= 1;

            arrdel(pOutputNode->ppOutputConnectionArray, i);    //-V595
            len = arrlen(pOutputNode->ppOutputConnectionArray); //-V595
            i--;
        }
    }
}

void ngRemoveNode(TFNodeGraphContext* pCtx, TFNGNode* pNode)
{
    // remove pNode as input for other nodes
    NodeInternal* pNodeIn = (NodeInternal*)pNode;

    if (pNodeIn->mNode.pDesc->pRemoveNodeCallback)
    {
        pNodeIn->mNode.pDesc->pRemoveNodeCallback(pNode);
    }

    size_t len = arrlen(pNodeIn->ppOutputConnectionArray);
    for (size_t i = 0; i < len; i++)
    {
        TFNGConnection* outputConnection = pNodeIn->ppOutputConnectionArray[i];
        outputConnection->pOutputNode = NULL;
        outputConnection->mOutputPortIdx = 0;
    }
    arrfree(pNodeIn->ppOutputConnectionArray);

    // remove pNode as output for other nodes
    for (uint32_t i = 0; i < pNode->mInputPortCount; i++)
    {
        TFNGConnection inputConnection = pNode->pInputConnections[i];
        if (inputConnection.pOutputNode == NULL)
        {
            continue;
        }

        NodeInternal* pOutputNode = (NodeInternal*)inputConnection.pOutputNode;
        removeAllOutputConnection(pOutputNode, pNode);
    }

    // remove pNodeIn from contex array
    len = arrlen(pCtx->ppNodeArray);
    for (size_t i = 0; i < len; i++)
    {
        if (pCtx->ppNodeArray[i] == pNodeIn)
        {
            arrdel(pCtx->ppNodeArray, i);
            break;
        }
    }

    tf_free(pNodeIn);
}

static bool wouldCreateLoop(TFNodeGraphContext* pCtx, TFNGNode* from, TFNGNode* to)
{
    // clear visited nodes
    for (uint32_t i = 0; i < (uint32_t)arrlen(pCtx->ppNodeArray); i++)
    {
        TFNGNode* pNode = &pCtx->ppNodeArray[i]->mNode;
        ptrdiff_t idx = hmgeti(pCtx->pVisitedFlag, pNode); //-V::568, 574
        if (idx == -1)
        {
            hmput(pCtx->pVisitedFlag, pNode, true);
        }
        else
        {
            pCtx->pVisitedFlag[idx].value = false;
        }
    }
    arrsetlen(pCtx->ppLoopStack, 0);
    arrpush(pCtx->ppLoopStack, to);

    while (arrlen(pCtx->ppLoopStack) != 0)
    {
        TFNGNode* pCurrNode = arrpop(pCtx->ppLoopStack);
        if (pCurrNode == from)
        {
            return true;
        }

        for (uint32_t i = 0; i < pCurrNode->mInputPortCount; i++)
        {
            if (pCurrNode->pInputConnections[i].pOutputNode != NULL)
            {
                TFNGNode* pNext = pCurrNode->pInputConnections[i].pOutputNode;
                ptrdiff_t idx = hmgeti(pCtx->pVisitedFlag, pNext); //-V::568, 574
                if (!pCtx->pVisitedFlag[idx].value)
                {
                    arrpush(pCtx->ppLoopStack, pNext);
                }
            }
        }

        hmput(pCtx->pVisitedFlag, pCurrNode, true);
    }
    return false;
}

bool ngAddConnection(TFNodeGraphContext* pCtx, TFNGNode* pNodeOutput, uint32_t outputPortIdx, TFNGNode* pNodeInput, uint32_t inputPortIdx,
                     bool strictType)
{
    ASSERT(pNodeOutput != pNodeInput);
    ASSERT(pNodeOutput && pNodeOutput->mOutputPortCount > outputPortIdx);
    ASSERT(pNodeInput && pNodeInput->mInputPortCount > inputPortIdx);

    if (strictType && pNodeInput->pInputPorts[inputPortIdx].mType != pNodeOutput->pOutputPorts[outputPortIdx].mType)
    {
        return false;
    }

    if (pNodeInput->pInputConnections[inputPortIdx].pOutputNode != NULL)
    {
        ngRemoveConnection(pCtx, pNodeInput, inputPortIdx);
    }

    pNodeInput->pInputConnections[inputPortIdx].pOutputNode = pNodeOutput;
    pNodeInput->pInputConnections[inputPortIdx].mOutputPortIdx = outputPortIdx;

    NodeInternal* pOutputNodeInt = (NodeInternal*)pNodeOutput;

    pOutputNodeInt->mNode.pOutputConnectionCounts[outputPortIdx] += 1;
    arrpush(pOutputNodeInt->ppOutputConnectionArray, &pNodeInput->pInputConnections[inputPortIdx]);

    if (wouldCreateLoop(pCtx, pNodeInput, pNodeOutput))
    {
        ngRemoveConnection(pCtx, pNodeInput, inputPortIdx);
        return false;
    }
    else
    {
        return true;
    }
}

bool ngRemoveConnection(TFNodeGraphContext* pCtx, TFNGNode* pNode, uint32_t connectionIdx)
{
    UNREF_PARAM(pCtx);

    ASSERT(pNode && pNode->mInputPortCount > connectionIdx);

    TFNGConnection* connection = pNode->pInputConnections + connectionIdx;
    if (connection->pOutputNode == NULL)
    {
        return false;
    }
    NodeInternal* pOutputNodeInt = (NodeInternal*)connection->pOutputNode;

    size_t len = arrlen(pOutputNodeInt->ppOutputConnectionArray);
    for (size_t i = 0; i < len; i++)
    {
        TFNGConnection* outputConnection = pOutputNodeInt->ppOutputConnectionArray[i];
        if (outputConnection->pInputNode == pNode && outputConnection->mInputPortIdx == connectionIdx)
        {
            pOutputNodeInt->mNode.pOutputConnectionCounts[outputConnection->mOutputPortIdx] -= 1;
            arrdel(pOutputNodeInt->ppOutputConnectionArray, i);
            break;
        }
    }

    connection->mOutputPortIdx = 0;
    connection->pOutputNode = NULL;
    return true;
}

TFNodeGraphDesc* ngGetNodeGraphDesc(TFNodeGraphContext* pCtx) { return pCtx->pDesc; }

uint32_t ngGetNodeCount(TFNodeGraphContext* pCtx) { return (uint32_t)arrlen(pCtx->ppNodeArray); }

TFNGNode* ngGetNode(TFNodeGraphContext* pCtx, uint32_t nodeIdx) { return &pCtx->ppNodeArray[nodeIdx]->mNode; }

uint32_t ngGetNodeCountByType(TFNodeGraphContext* pCtx, int32_t nodeType)
{
    uint32_t count = 0;
    uint32_t len = (uint32_t)arrlen(pCtx->ppNodeArray);
    for (uint32_t i = 0; i < len; i++)
    {
        NodeInternal* node = pCtx->ppNodeArray[i];
        if (node->mNode.mType == nodeType)
        {
            count++;
        }
    }
    return count;
}

uint32_t ngGetNodeDescCount(TFNodeGraphDesc* pNGDesc) { return (uint32_t)arrlen(pNGDesc->ppNodeDescArray); }

const TFNGNodeDesc* ngGetNodeDesc(TFNodeGraphDesc* pNGDesc, uint32_t idx) { return pNGDesc->ppNodeDescArray[idx]; }

const TFNGNodeDesc* ngGetNodeDescByType(TFNodeGraphDesc* pNGDesc, int32_t nodeType)
{
    ptrdiff_t typeIdx = hmgeti(pNGDesc->pNodeDescHashSet, nodeType);
    if (typeIdx == -1)
    {
        return NULL;
    }

    return pNGDesc->pNodeDescHashSet[typeIdx].value;
}

const TFNGPortTypeDesc* ngGetPortTypeByType(TFNodeGraphDesc* pNGDesc, int32_t portType)
{
    ptrdiff_t typeIdx = hmgeti(pNGDesc->pPortTypeDescHashSet, portType);
    if (typeIdx == -1)
    {
        return NULL;
    }

    return pNGDesc->pPortTypeDescHashSet[typeIdx].value;
}

typedef struct NodeOutputDataT
{
    TFNGNode* key;   // pointer to node
    uint32_t  value; // index of node in global buffer
} NodeOutputDataT;

typedef struct AssembleFunctionT
{
    int32_t            key;
    NGAssembleFunction value;
} AssembleFunctionT;

typedef struct NodeOutputData
{
    uint8_t*           pOutputData[NODE_GRAPH_PORT_MAX];
    NGAssembleFunction pAssembleFunction;
    TFNGNode*          pNode;
    bool               mCalculated;
} NodeOutputData;

struct TFNodeGraphAssembler
{
    TFNodeGraphDesc*   pNodeGraphDesc;
    AssembleFunctionT* pHmAssembleFunctions;

    // temp data
    NodeOutputData*  pNodesData;             // output datas for all nodes in context
    uint8_t*         pOutputBuffer;          // buffer with data for output ports
    NodeOutputDataT* pHmPointerToIndexTable; // table to convert from pointer to index in the global buffer
    uint32_t*        pStackNodes;
};

static void constantAssembleFunction(TFNGAssemblerContext* pCtx)
{
    TFNGNode* pNode = pCtx->pNode;

    bool useFloat = (pNode->mType >= TF_NODE_GRAPH_FLOAT && pNode->mType <= TF_NODE_GRAPH_FLOAT_4);
    if (pNode->mType >= TF_NODE_GRAPH_INT && pNode->mType <= TF_NODE_GRAPH_INT_4) // we need to convert from uint to float
    {
        TFNGUserData128* pUserData = (TFNGUserData128*)pNode->pUserData;
        int*             pIntData = (int*)pUserData->mSubData; // ui writes there as int
        float            floatData[4] = { (float)pIntData[0], (float)pIntData[1], (float)pIntData[2], (float)pIntData[3] };
        memcpy(pCtx->pOutputPorts[0].pData, floatData, pCtx->pOutputPorts[0].mDataSize);
    }
    if (pNode->mType == TF_NODE_GRAPH_TOGGLE || useFloat)
    {
        TFNGUserData128* pUserData = (TFNGUserData128*)pNode->pUserData;
        memcpy(pCtx->pOutputPorts[0].pData, pUserData->mSubData, pCtx->pOutputPorts[0].mDataSize);
    }
}

static void binaryMathAssembleFunction(TFNGAssemblerContext* pCtx)
{
    float inputA[4] = { 0, 0, 0, 0 };
    float inputB[4] = { 0, 0, 0, 0 };
    float output[4] = { 0, 0, 0, 0 };
    if (pCtx->pInputPorts[0].mIsConnected)
    {
        memcpy(inputA, pCtx->pInputPorts[0].pData, pCtx->pInputPorts[0].mDataSize);
    }
    if (pCtx->pInputPorts[1].mIsConnected)
    {
        memcpy(inputB, pCtx->pInputPorts[1].pData, pCtx->pInputPorts[1].mDataSize);
    }

    if (pCtx->pNode->mType == TF_NODE_GRAPH_ADD)
    {
        for (uint32_t i = 0; i < 4; i++)
            output[i] = inputA[i] + inputB[i];
    }
    else if (pCtx->pNode->mType == TF_NODE_GRAPH_SUB)
    {
        for (uint32_t i = 0; i < 4; i++)
            output[i] = inputA[i] - inputB[i];
    }
    else if (pCtx->pNode->mType == TF_NODE_GRAPH_MUL)
    {
        for (uint32_t i = 0; i < 4; i++)
            output[i] = inputA[i] * inputB[i];
    }
    else if (pCtx->pNode->mType == TF_NODE_GRAPH_DIV)
    {
        for (uint32_t i = 0; i < 4; i++)
            output[i] = inputA[i] / inputB[i];
    }
    else if (pCtx->pNode->mType == TF_NODE_GRAPH_DOT)
    {
        float res = 0;
        for (uint32_t i = 0; i < 4; i++)
        {
            res += inputA[i] * inputB[i];
        }

        output[0] = res;
    }
    else if (pCtx->pNode->mType == TF_NODE_GRAPH_CROSS)
    {
        output[0] = inputA[1] * inputB[2] - inputA[2] * inputB[1];
        output[1] = inputA[2] * inputB[0] - inputA[0] * inputB[2];
        output[2] = inputA[0] * inputB[1] - inputA[1] * inputB[0];
    }
    memcpy(pCtx->pOutputPorts->pData, output, pCtx->pOutputPorts->mDataSize);
}

static void unaryMathAssembleFunction(TFNGAssemblerContext* pCtx)
{
    float input[4] = { 0, 0, 0, 0 };
    float output[4] = { 0, 0, 0, 0 };
    if (pCtx->pInputPorts[0].mIsConnected)
    {
        memcpy(input, pCtx->pInputPorts[0].pData, pCtx->pInputPorts[0].mDataSize);
    }

    if (pCtx->pNode->mType == TF_NODE_GRAPH_ABS)
    {
        for (uint32_t i = 0; i < 4; i++)
        {
            output[i] = fabsf(input[i]);
        }
    }
    else if (pCtx->pNode->mType == TF_NODE_GRAPH_SATURATE)
    {
        for (uint32_t i = 0; i < 4; i++)
        {
            output[i] = clampf(input[i], 0, 1);
        }
    }
    else if (pCtx->pNode->mType == TF_NODE_GRAPH_NORMALIZE)
    {
        float length = sqrtf(input[0] * input[0] + input[1] * input[1] + input[2] * input[2] + input[3] * input[3]);
        if (length != 0)
        {
            output[0] = input[0] / length;
            output[1] = input[1] / length;
            output[2] = input[2] / length;
            output[3] = input[3] / length;
        }
        else
        {
            output[0] = 1;
            output[1] = 0;
            output[2] = 0;
            output[3] = 0;
        }
    }

    memcpy(pCtx->pOutputPorts->pData, output, pCtx->pOutputPorts->mDataSize);
}

static void splitAssembleFunction(TFNGAssemblerContext* pCtx)
{
    float input[4] = { 0, 0, 0, 0 };
    if (pCtx->pInputPorts[0].mIsConnected)
    {
        memcpy(input, pCtx->pInputPorts[0].pData, pCtx->pInputPorts[0].mDataSize);
    }
    for (uint32_t i = 0; i < pCtx->mOutputPortCount; i++)
    {
        memcpy(pCtx->pOutputPorts[i].pData, input + i, sizeof(float));
    }
}

static void makeVectorAssembleFunction(TFNGAssemblerContext* pCtx)
{
    float input[4] = { 0, 0, 0, 0 };
    for (uint32_t i = 0; i < pCtx->mInputPortCount; i++)
    {
        if (pCtx->pInputPorts[0].mIsConnected)
        {
            memcpy(input + i, pCtx->pInputPorts[i].pData, sizeof(float));
        }
    }

    memcpy(pCtx->pOutputPorts[0].pData, input, pCtx->pOutputPorts[0].mDataSize);
}

static void clampVectorAssembleFunction(TFNGAssemblerContext* pCtx)
{
    float input[4] = { 0, 0, 0, 0 };
    float min[4] = { 0, 0, 0, 0 };
    float max[4] = { 0, 0, 0, 0 };
    float output[4] = { 0, 0, 0, 0 };

    NGUserDataClamp* clampData = (NGUserDataClamp*)pCtx->pNode->pUserData;
    if (pCtx->pInputPorts[0].mIsConnected)
    {
        memcpy(input, pCtx->pInputPorts[0].pData, pCtx->pInputPorts[0].mDataSize);
    }

    if (pCtx->pInputPorts[1].mIsConnected)
    {
        memcpy(min, pCtx->pInputPorts[1].pData, pCtx->pInputPorts[1].mDataSize);
    }
    else
    {
        min[0] = clampData->mMin;
        min[1] = clampData->mMin;
        min[2] = clampData->mMin;
        min[3] = clampData->mMin;
    }

    if (pCtx->pInputPorts[2].mIsConnected)
    {
        memcpy(max, pCtx->pInputPorts[2].pData, pCtx->pInputPorts[2].mDataSize);
    }
    else
    {
        max[0] = clampData->mMax;
        max[1] = clampData->mMax;
        max[2] = clampData->mMax;
        max[3] = clampData->mMax;
    }

    for (uint32_t i = 0; i < 4; i++)
    {
        output[i] = clampf(input[i], min[i], max[i]);
    }

    memcpy(pCtx->pOutputPorts->pData, output, pCtx->pOutputPorts->mDataSize);
}

static void minVectorAssembleFunction(TFNGAssemblerContext* pCtx)
{
    float minA[4] = { 0, 0, 0, 0 };
    float minB[4] = { 0, 0, 0, 0 };
    float output[4] = { 0, 0, 0, 0 };

    NGUserDataMin* minData = (NGUserDataMin*)pCtx->pNode->pUserData;

    if (pCtx->pInputPorts[0].mIsConnected)
    {
        memcpy(minA, pCtx->pInputPorts[0].pData, pCtx->pInputPorts[0].mDataSize);
    }
    else
    {
        minA[0] = minData->mMinA;
        minA[1] = minData->mMinA;
        minA[2] = minData->mMinA;
        minA[3] = minData->mMinA;
    }

    if (pCtx->pInputPorts[1].mIsConnected)
    {
        memcpy(minB, pCtx->pInputPorts[1].pData, pCtx->pInputPorts[1].mDataSize);
    }
    else
    {
        minB[0] = minData->mMinB;
        minB[1] = minData->mMinB;
        minB[2] = minData->mMinB;
        minB[3] = minData->mMinB;
    }

    for (uint32_t i = 0; i < 4; i++)
    {
        output[i] = minf(minA[i], minB[i]);
    }

    memcpy(pCtx->pOutputPorts->pData, output, pCtx->pOutputPorts->mDataSize);
}

static void maxVectorAssembleFunction(TFNGAssemblerContext* pCtx)
{
    float maxA[4] = { 0, 0, 0, 0 };
    float maxB[4] = { 0, 0, 0, 0 };
    float output[4] = { 0, 0, 0, 0 };

    NGUserDataMax* maxData = (NGUserDataMax*)pCtx->pNode->pUserData;

    if (pCtx->pInputPorts[0].mIsConnected)
    {
        memcpy(maxA, pCtx->pInputPorts[0].pData, pCtx->pInputPorts[0].mDataSize);
    }
    else
    {
        maxA[0] = maxData->mMaxA;
        maxA[1] = maxData->mMaxA;
        maxA[2] = maxData->mMaxA;
        maxA[3] = maxData->mMaxA;
    }

    if (pCtx->pInputPorts[1].mIsConnected)
    {
        memcpy(maxB, pCtx->pInputPorts[1].pData, pCtx->pInputPorts[1].mDataSize);
    }
    else
    {
        maxB[0] = maxData->mMaxB;
        maxB[1] = maxData->mMaxB;
        maxB[2] = maxData->mMaxB;
        maxB[3] = maxData->mMaxB;
    }

    for (uint32_t i = 0; i < 4; i++)
    {
        output[i] = maxf(maxA[i], maxB[i]);
    }

    memcpy(pCtx->pOutputPorts->pData, output, pCtx->pOutputPorts->mDataSize);
}

TFNodeGraphAssembler* ngAddNodeGraphAssembler(TFNodeGraphDesc* pNGDesc)
{
    TFNodeGraphAssembler* pAssembler = tf_calloc(1, sizeof(TFNodeGraphAssembler));
    memset(pAssembler, 0, sizeof(TFNodeGraphAssembler));
    pAssembler->pNodeGraphDesc = pNGDesc;

    ngSetAssembleFunction(pAssembler, constantAssembleFunction, TF_NODE_GRAPH_TOGGLE);
    ngSetAssembleFunction(pAssembler, constantAssembleFunction, TF_NODE_GRAPH_FLOAT);
    ngSetAssembleFunction(pAssembler, constantAssembleFunction, TF_NODE_GRAPH_FLOAT_2);
    ngSetAssembleFunction(pAssembler, constantAssembleFunction, TF_NODE_GRAPH_FLOAT_3);
    ngSetAssembleFunction(pAssembler, constantAssembleFunction, TF_NODE_GRAPH_FLOAT_4);
    ngSetAssembleFunction(pAssembler, constantAssembleFunction, TF_NODE_GRAPH_INT);
    ngSetAssembleFunction(pAssembler, constantAssembleFunction, TF_NODE_GRAPH_INT_2);
    ngSetAssembleFunction(pAssembler, constantAssembleFunction, TF_NODE_GRAPH_INT_3);
    ngSetAssembleFunction(pAssembler, constantAssembleFunction, TF_NODE_GRAPH_INT_4);
    ngSetAssembleFunction(pAssembler, binaryMathAssembleFunction, TF_NODE_GRAPH_ADD);
    ngSetAssembleFunction(pAssembler, binaryMathAssembleFunction, TF_NODE_GRAPH_SUB);
    ngSetAssembleFunction(pAssembler, binaryMathAssembleFunction, TF_NODE_GRAPH_MUL);
    ngSetAssembleFunction(pAssembler, binaryMathAssembleFunction, TF_NODE_GRAPH_DIV);
    ngSetAssembleFunction(pAssembler, binaryMathAssembleFunction, TF_NODE_GRAPH_DOT);
    ngSetAssembleFunction(pAssembler, binaryMathAssembleFunction, TF_NODE_GRAPH_CROSS);
    ngSetAssembleFunction(pAssembler, splitAssembleFunction, TF_NODE_GRAPH_SPLIT);
    ngSetAssembleFunction(pAssembler, makeVectorAssembleFunction, TF_NODE_GRAPH_MAKE_VECTOR);
    ngSetAssembleFunction(pAssembler, unaryMathAssembleFunction, TF_NODE_GRAPH_ABS);
    ngSetAssembleFunction(pAssembler, unaryMathAssembleFunction, TF_NODE_GRAPH_SATURATE);
    ngSetAssembleFunction(pAssembler, unaryMathAssembleFunction, TF_NODE_GRAPH_NORMALIZE);
    ngSetAssembleFunction(pAssembler, clampVectorAssembleFunction, TF_NODE_GRAPH_CLAMP);
    ngSetAssembleFunction(pAssembler, minVectorAssembleFunction, TF_NODE_GRAPH_MIN);
    ngSetAssembleFunction(pAssembler, maxVectorAssembleFunction, TF_NODE_GRAPH_MAX);
    return pAssembler;
}

void ngRemoveNodeGraphAssembler(TFNodeGraphAssembler* pAssembler)
{
    hmfree(pAssembler->pHmAssembleFunctions);
    hmfree(pAssembler->pHmPointerToIndexTable);
    arrfree(pAssembler->pNodesData);
    arrfree(pAssembler->pOutputBuffer);
    arrfree(pAssembler->pStackNodes);
    tf_free(pAssembler);
}

void ngSetAssembleFunction(TFNodeGraphAssembler* pAssembler, NGAssembleFunction function, int32_t nodeType)
{
    hmput(pAssembler->pHmAssembleFunctions, nodeType, function);
}

static void assemblePortReflectedMembers(TFNGAssemblerContext* pCtx)
{
    TFNGNodeDesc* pDesc = pCtx->pNode->pDesc;
    for (uint32_t i = 0; i < pDesc->mInputPortCount; i++)
    {
        int32_t reflectedMemberIdx = pDesc->pInputPortDesc[i].mReflectedMemeberIdx;
        if (reflectedMemberIdx == -1 || !pCtx->pInputPorts[i].mIsConnected)
        {
            continue;
        }

        TypeInfoStructMember* pMember = pDesc->pReflectedUserDataInfo->pMembers + reflectedMemberIdx;

        uint8_t* pInputData = pCtx->pInputPorts[i].pData;
        uint8_t* pInnerData = (uint8_t*)pCtx->pNode->pUserData + pDesc->mReflectedUserDataOffset + pMember->mOffsetInStruct;

        if (pMember->pType->mKind == TYPE_INFO_TAG_INT)
        {
            if (pDesc->pInputPortDesc[i].mType == TF_NODE_GRAPH_TYPE_BOOL)
            {
                int32_t data = *(int32_t*)pInputData;
                bool*   pBoolInnerData = (bool*)pInnerData;
                *pBoolInnerData = data != 0 ? true : false;
            }
            else
            {
                int32_t data = (int32_t)(*(float*)pInputData);
                if (pMember->mMinMaxSet)
                {
                    int32_t min = *((int32_t*)pMember->pMinMem);
                    int32_t max = *((int32_t*)pMember->pMinMem);

                    data = (int32_t)clampi(data, min, max);
                }

                *(int32_t*)pInnerData = data;
            }
        }
        else // it's float
        {
            uint32_t dimension = (uint32_t)pMember->pType->mSize / sizeof(float);
            ASSERT(dimension <= 4);

            float min[4];
            float max[4];
            memcpy(min, pMember->pMinMem, sizeof(float) * dimension);
            memcpy(max, pMember->pMaxMem, sizeof(float) * dimension);
            bool useMinMax = pMember->mMinMaxSet;
            if (pMember->mFlags & REFLECT_MEMBER_FLAG_COLOR_RGB || pMember->mFlags & REFLECT_MEMBER_FLAG_COLOR_RGBA)
            {
                min[0] = min[1] = min[2] = min[3] = 0.0f;
                max[0] = max[1] = max[2] = max[3] = 1.0f;

                useMinMax = true;
            }

            float* pFloatInnerData = (float*)pInnerData;
            for (uint32_t i2 = 0; i2 < dimension; i2++)
            {
                float floatInputData = ((float*)pInputData)[i2];
                if (useMinMax)
                {
                    floatInputData = clampf(floatInputData, min[i2], max[i2]);
                }
                pFloatInnerData[i2] = floatInputData;
            }
        }
    }
}

void ngAssembleNodeGraph(TFNodeGraphAssembler* pAssembler, TFNodeGraphContext* pCtx, TFNGNode* pOutputNode, uint32_t outputSize,
                         uint8_t* pOutputData)
{
    arrsetlen(pAssembler->pNodesData, 0);

    uint32_t totalSizeOfOutputs = 0;

    uint32_t len = (uint32_t)arrlen(pCtx->ppNodeArray);

    // collect and add output data for nodes
    {
        for (uint32_t i = 0; i < len; i++)
        {
            TFNGNode*      pNode = &pCtx->ppNodeArray[i]->mNode;
            NodeOutputData data;
            memset(&data, 0, sizeof(NodeOutputData));
            data.pNode = pNode;

            ptrdiff_t funcIdx = hmgeti(pAssembler->pHmAssembleFunctions, pNode->mType);
            ASSERT(funcIdx != -1);
            data.pAssembleFunction = pAssembler->pHmAssembleFunctions[funcIdx].value;

            uint32_t totalSize = 0;

            for (uint32_t i2 = 0; i2 < pNode->mOutputPortCount; i2++)
            {
                totalSize += pNode->pOutputPorts[i2].mDataSize;
            }

            totalSizeOfOutputs += totalSize;
            arrpush(pAssembler->pNodesData, data);

            hmput(pAssembler->pHmPointerToIndexTable, pNode, i);
        }
    }

    arrsetlen(pAssembler->pOutputBuffer, totalSizeOfOutputs);
    memset(pAssembler->pOutputBuffer, 0, totalSizeOfOutputs);

    // put pointers for output data
    {
        uint32_t offset = 0;
        for (uint32_t i = 0; i < len; i++)
        {
            TFNGNode* pNode = pAssembler->pNodesData[i].pNode;
            uint32_t  outputCount = pNode->mOutputPortCount;
            for (uint32_t i2 = 0; i2 < outputCount; i2++)
            {
                pAssembler->pNodesData[i].pOutputData[i2] = pAssembler->pOutputBuffer + offset;
                offset += pNode->pOutputPorts[i2].mDataSize;
            }
        }
    }

    TFNGAssemblerPortData inputData[NODE_GRAPH_PORT_MAX];
    TFNGAssemblerPortData outputData[NODE_GRAPH_PORT_MAX];

    uint32_t outputNodeIdx = hmget(pAssembler->pHmPointerToIndexTable, pOutputNode); //-V::568, 574

    arrsetlen(pAssembler->pStackNodes, 0);
    arrpush(pAssembler->pStackNodes, outputNodeIdx);

    while (arrlen(pAssembler->pStackNodes))
    {
        uint32_t* pNodeIdx = arrback(pAssembler->pStackNodes);

        ASSERT(pNodeIdx);
        uint32_t        nodeIdx = *pNodeIdx;
        NodeOutputData* pData = pAssembler->pNodesData + nodeIdx;

        ASSERT(pData);
        if (pData->mCalculated)
        {
            arrdelback(pAssembler->pStackNodes);
            continue;
        }
        TFNGNode* pCurrentNode = pData->pNode;

        bool inputsIsReady = true;
        for (uint32_t i = 0; i < pCurrentNode->mInputPortCount; i++)
        {
            TFNGNode* pNextNode = pCurrentNode->pInputConnections[i].pOutputNode;
            if (pNextNode == NULL)
            {
                continue;
            }

            uint32_t        nextNodeIdx = hmget(pAssembler->pHmPointerToIndexTable, pNextNode); //-V::568, 574
            NodeOutputData* nextNodeData = pAssembler->pNodesData + nextNodeIdx;

            inputsIsReady &= nextNodeData->mCalculated;

            if (!nextNodeData->mCalculated)
            {
                arrpush(pAssembler->pStackNodes, nextNodeIdx);
            }
        }

        if (inputsIsReady)
        {
            for (uint32_t i = 0; i < pCurrentNode->mInputPortCount; i++)
            {
                TFNGNode* pNextNode = pCurrentNode->pInputConnections[i].pOutputNode;
                uint32_t  outputPortIdx = pCurrentNode->pInputConnections[i].mOutputPortIdx;

                inputData[i].mIsConnected = pNextNode != NULL;
                inputData[i].mPortIdx = i;
                inputData[i].mDataSize = pCurrentNode->pInputPorts[i].mDataSize;
                inputData[i].pData = NULL;

                if (pNextNode != NULL)
                {
                    uint32_t        nextNodeIdx = hmget(pAssembler->pHmPointerToIndexTable, pNextNode); //-V::568, 574
                    NodeOutputData* nextNodeData = pAssembler->pNodesData + nextNodeIdx;
                    inputData[i].pData = nextNodeData->pOutputData[outputPortIdx];
                }
            }

            for (uint32_t i = 0; i < pCurrentNode->mOutputPortCount; i++)
            {
                outputData[i].mDataSize = pCurrentNode->pOutputPorts[i].mDataSize;
                outputData[i].mIsConnected = true; // we don't need this info
                outputData[i].mPortIdx = i;
                outputData[i].pData = pData->pOutputData[i];
            }

            TFNGAssemblerContext context;
            context.pNode = pCurrentNode;
            context.mInputPortCount = pCurrentNode->mInputPortCount;
            context.pInputPorts = inputData;
            context.mOutputPortCount = pCurrentNode->mOutputPortCount;
            context.pOutputPorts = outputData;
            context.mOutputDataSize = 0;
            context.pOutputData = NULL;

            assemblePortReflectedMembers(&context);
            if (pCurrentNode == pOutputNode)
            {
                context.mOutputDataSize = outputSize;
                context.pOutputData = pOutputData;
                pData->pAssembleFunction(&context);
            }
            else
            {
                pData->pAssembleFunction(&context);
            }
            pData->mCalculated = true;

            arrdelback(pAssembler->pStackNodes);
        }
    }
}

typedef struct BasicNodeReflectedData
{
    char  mTypeName[NODE_GRAPH_NAME_MAX_SIZE];
    float mX;
    float mY;
} BasicNodeReflectedData;

REFLECT_STRUCT_BEGIN(BasicNodeReflectedData)
R_HINT_FLAGS(REFLECT_MEMBER_FLAG_STRING)
REFLECT_STRUCT_MEMBER(BasicNodeReflectedData, R_ARRAY(R_INT(char), NODE_GRAPH_NAME_MAX_SIZE), mTypeName)
REFLECT_STRUCT_MEMBER(BasicNodeReflectedData, R_FLOAT(float), mX)
REFLECT_STRUCT_MEMBER(BasicNodeReflectedData, R_FLOAT(float), mY)
REFLECT_STRUCT_END(BasicNodeReflectedData)

typedef struct ConnectionReflectedData
{
    uint32_t mOutputNodeIdx;
    uint32_t mOutputPortIdx;
    uint32_t mInputNodeIdx;
    uint32_t mInputPortIdx;
} ConnectionReflectedData;

REFLECT_STRUCT_BEGIN(ConnectionReflectedData)
REFLECT_STRUCT_MEMBER(ConnectionReflectedData, R_INT(uint32_t), mOutputNodeIdx)
REFLECT_STRUCT_MEMBER(ConnectionReflectedData, R_INT(uint32_t), mOutputPortIdx)
REFLECT_STRUCT_MEMBER(ConnectionReflectedData, R_INT(uint32_t), mInputNodeIdx)
REFLECT_STRUCT_MEMBER(ConnectionReflectedData, R_INT(uint32_t), mInputPortIdx)
REFLECT_STRUCT_END(ConnectionReflectedData)

#define MAX_NODE_COUNT       256
#define MAX_CONNECTION_COUNT 2048

typedef struct BasicNodeGraphReflectedData
{
    BasicNodeReflectedData  mBasicNodeData[MAX_NODE_COUNT];
    ConnectionReflectedData mConnectionData[MAX_CONNECTION_COUNT];
    uint32_t                mNodeCount;
    uint32_t                mConnectionCount;
} BasicNodeGraphReflectedData;

REFLECT_STRUCT_BEGIN(BasicNodeGraphReflectedData)
REFLECT_STRUCT_MEMBER(BasicNodeGraphReflectedData, R_ARRAY(R_STRUCT(BasicNodeReflectedData), MAX_NODE_COUNT), mBasicNodeData)
REFLECT_STRUCT_MEMBER(BasicNodeGraphReflectedData, R_ARRAY(R_STRUCT(ConnectionReflectedData), MAX_CONNECTION_COUNT), mConnectionData)
REFLECT_STRUCT_MEMBER(BasicNodeGraphReflectedData, R_INT(uint32_t), mNodeCount)
REFLECT_STRUCT_MEMBER(BasicNodeGraphReflectedData, R_INT(uint32_t), mConnectionCount)
REFLECT_STRUCT_END(BasicNodeGraphReflectedData)

typedef struct NodeGraphReflectedInfo
{
    TypeInfoStruct       mEntryStructInfo;
    TypeInfoStructMember mEntryGraphMember;
    TypeInfoArray        mBasicNodeInfoArray;
    TypeInfoArray        mConnectionInfoArray;
    TypeInfoStruct       mBasicNodeGraphInfo;
} NodeGraphReflectedInfo;

typedef enum NodeGraphReflectionType
{
    NODE_GRAPH_REFLECTION_TYPE_NONE = 0,
    NODE_GRAPH_REFLECTION_TYPE_COUNTS = 1,
    NODE_GRAPH_REFLECTION_TYPE_BASIC_DATA = 2,
    NODE_GRAPH_REFLECTION_TYPE_USER_DATA_DATA = 4,
} NodeGraphReflectionType;

typedef struct NodeGraphReflectedDesc
{
    const char*             pNameOfGraph;
    NodeInternal**          ppInternalNodes;
    NodeGraphReflectionType mReflectionType;
    uint32_t                mNodeCount;
    uint32_t                mConnectionCount;
    uint32_t                mTotalDataSize;
} NodeGraphReflectedDesc;

static NodeGraphReflectedInfo* addNodeGraphReflectedInfo(NodeGraphReflectedDesc desc)
{
    uint32_t memberCount = 0;
    uint32_t userDataCount = 0;
    if (desc.mReflectionType & NODE_GRAPH_REFLECTION_TYPE_COUNTS)
    {
        memberCount += 2;
    }
    if (desc.mReflectionType & NODE_GRAPH_REFLECTION_TYPE_BASIC_DATA)
    {
        memberCount += 2;
    }
    if (desc.mReflectionType & NODE_GRAPH_REFLECTION_TYPE_USER_DATA_DATA)
    {
        memberCount += desc.mNodeCount;
        userDataCount = desc.mNodeCount;
    }
    const uint32_t userDataNameSize = 256;

    NodeGraphReflectedInfo* pInfo = (NodeGraphReflectedInfo*)tf_calloc(
        1, sizeof(NodeGraphReflectedInfo) + sizeof(TypeInfoStructMember) * memberCount + sizeof(char) * userDataNameSize * userDataCount);

    TypeInfoStructMember* pMembers = (TypeInfoStructMember*)((uint8_t*)pInfo + sizeof(NodeGraphReflectedInfo));

    pInfo->mEntryStructInfo.mBase.mKind = TYPE_INFO_TAG_STRUCT;
    pInfo->mEntryStructInfo.mBase.mSize = desc.mTotalDataSize;
    strcpy(pInfo->mEntryStructInfo.mBase.pName, "EntryNodeGraphNodeInfo");
    pInfo->mEntryStructInfo.mMemberCount = 1;
    pInfo->mEntryStructInfo.pMembers = &pInfo->mEntryGraphMember;
    {
        pInfo->mEntryGraphMember.pType = &pInfo->mBasicNodeGraphInfo.mBase;
        pInfo->mEntryGraphMember.mOffsetInStruct = 0;
        pInfo->mEntryGraphMember.pName = desc.pNameOfGraph;
    }

    pInfo->mBasicNodeGraphInfo.mBase.mKind = TYPE_INFO_TAG_STRUCT;
    pInfo->mBasicNodeGraphInfo.mBase.mSize = desc.mTotalDataSize;
    strcpy(pInfo->mBasicNodeGraphInfo.mBase.pName, "NodeGraphReflectedData");
    pInfo->mBasicNodeGraphInfo.mMemberCount = 0;
    pInfo->mBasicNodeGraphInfo.pMembers = pMembers;
    {
        pInfo->mBasicNodeInfoArray.mArrayCount = desc.mNodeCount;
        pInfo->mBasicNodeInfoArray.pElementType = &(GET_TYPE_INFO(BasicNodeReflectedData)->mBase);
        pInfo->mBasicNodeInfoArray.mBase.mKind = TYPE_INFO_TAG_ARRAY;
        pInfo->mBasicNodeInfoArray.mBase.mSize = sizeof(BasicNodeReflectedData) * desc.mNodeCount;
        strcpy(pInfo->mBasicNodeInfoArray.mBase.pName, "NodeBasicDataArray");

        pInfo->mConnectionInfoArray.mArrayCount = desc.mConnectionCount;
        pInfo->mConnectionInfoArray.pElementType = &(GET_TYPE_INFO(ConnectionReflectedData)->mBase);
        pInfo->mConnectionInfoArray.mBase.mKind = TYPE_INFO_TAG_ARRAY;
        pInfo->mConnectionInfoArray.mBase.mSize = sizeof(ConnectionReflectedData) * desc.mConnectionCount;
        strcpy(pInfo->mConnectionInfoArray.mBase.pName, "ConnectionDataArray");
    }

    {
        uint32_t memberIdx = 0;
        uint32_t offset = 0;
        if (desc.mReflectionType & NODE_GRAPH_REFLECTION_TYPE_COUNTS)
        {
            pMembers[memberIdx].pType = lookupTypeByIdentifier("u32");
            pMembers[memberIdx].mOffsetInStruct = offset;
            pMembers[memberIdx].pName = "mNodeCount";
            offset += sizeof(uint32_t);
            memberIdx++;

            pMembers[memberIdx].pType = lookupTypeByIdentifier("u32");
            pMembers[memberIdx].mOffsetInStruct = offset;
            pMembers[memberIdx].pName = "mConnectionCount";
            offset += sizeof(uint32_t);
            memberIdx++;
        }

        if (desc.mReflectionType & NODE_GRAPH_REFLECTION_TYPE_BASIC_DATA)
        {
            pMembers[memberIdx].pType = &(pInfo->mBasicNodeInfoArray.mBase);
            pMembers[memberIdx].mOffsetInStruct = offset;
            pMembers[memberIdx].pName = "mNodeBasicData";
            offset += (uint32_t)pInfo->mBasicNodeInfoArray.mBase.mSize;
            memberIdx++;

            pMembers[memberIdx].pType = &(pInfo->mConnectionInfoArray.mBase);
            pMembers[memberIdx].mOffsetInStruct = offset;
            pMembers[memberIdx].pName = "mConnectionData";
            offset += (uint32_t)pInfo->mConnectionInfoArray.mBase.mSize;
            memberIdx++;
        }

        if (desc.mReflectionType & NODE_GRAPH_REFLECTION_TYPE_USER_DATA_DATA)
        {
            char* pNames = (char*)((uint8_t*)pInfo + sizeof(NodeGraphReflectedInfo) + sizeof(TypeInfoStructMember) * memberCount);

            for (uint32_t i = 0; i < desc.mNodeCount; i++)
            {
                TFNGNodeDesc* pDesc = desc.ppInternalNodes[i]->mNode.pDesc;
                if (pDesc->pReflectedUserDataInfo)
                {
                    sprintf(pNames, "mNodeUserData_%i", (int32_t)i);
                    pMembers[memberIdx].pType = &(pDesc->pReflectedUserDataInfo->mBase);
                    pMembers[memberIdx].mOffsetInStruct = offset;
                    pMembers[memberIdx].pName = pNames;
                    pNames += userDataNameSize;
                    offset += (uint32_t)pDesc->pReflectedUserDataInfo->mBase.mSize;
                    memberIdx++;
                }
            }
        }

        pInfo->mBasicNodeGraphInfo.mMemberCount = memberIdx;
    }

    return pInfo;
}

static void removeNodeGraphReflectedInfo(NodeGraphReflectedInfo* pTypeInfo) { tf_free(pTypeInfo); }

void ngSerializeNodeGraph(TFNodeGraphContext* pCtx, Serializer* pSerializer, const char* pNameOfGraph)
{
    uint32_t nodeCount = (uint32_t)arrlen(pCtx->ppNodeArray);
    uint32_t connectionCount = 0;
    uint32_t userDatas = 0;
    for (uint32_t i = 0; i < nodeCount; i++)
    {
        NodeInternal* pNode = pCtx->ppNodeArray[i];
        for (uint32_t i2 = 0; i2 < pNode->mNode.mInputPortCount; i2++)
        {
            if (pNode->mNode.pInputConnections[i2].pOutputNode != NULL)
            {
                connectionCount++;
            }
        }

        if (pNode->mNode.pDesc->pReflectedUserDataInfo)
        {
            userDatas += (uint32_t)pNode->mNode.pDesc->pReflectedUserDataInfo->mBase.mSize;
        }
    }

    uint32_t dataSize =
        sizeof(BasicNodeReflectedData) * nodeCount + sizeof(ConnectionReflectedData) * connectionCount + sizeof(uint32_t) * 2 + userDatas;

    NodeGraphReflectedDesc reflectedDesc;
    reflectedDesc.pNameOfGraph = pNameOfGraph;
    reflectedDesc.ppInternalNodes = pCtx->ppNodeArray;
    reflectedDesc.mReflectionType =
        NODE_GRAPH_REFLECTION_TYPE_COUNTS | NODE_GRAPH_REFLECTION_TYPE_BASIC_DATA | NODE_GRAPH_REFLECTION_TYPE_USER_DATA_DATA;
    reflectedDesc.mNodeCount = nodeCount;
    reflectedDesc.mConnectionCount = connectionCount;
    reflectedDesc.mTotalDataSize = dataSize;

    NodeGraphReflectedInfo* pTypeInfo = addNodeGraphReflectedInfo(reflectedDesc);

    uint8_t* pNodeGraphData = (uint8_t*)tf_calloc(1, dataSize);
    uint8_t* pData = pNodeGraphData;

    *(uint32_t*)pData = nodeCount;
    pData += sizeof(uint32_t);

    *(uint32_t*)pData = connectionCount;
    pData += sizeof(uint32_t);

    // fill in node graph data
    for (uint32_t i = 0; i < nodeCount; i++)
    {
        NodeInternal*           pNode = pCtx->ppNodeArray[i];
        TFNGNodeDesc*           desc = pNode->mNode.pDesc;
        BasicNodeReflectedData* pBasicNodeData = ((BasicNodeReflectedData*)pData) + i;
        strcpy(pBasicNodeData->mTypeName, desc->mName);
        if (pNode->mNode.pUserData)
        {
            TFNGUserData* pUserData = (TFNGUserData*)(pNode->mNode.pUserData);
            pBasicNodeData->mX = pUserData->mPosition[0];
            pBasicNodeData->mY = pUserData->mPosition[1];
        }
    }

    pData += sizeof(BasicNodeReflectedData) * nodeCount;

    // fill in index table
    NodeOutputDataT* pHmPointerToIndexTable = NULL;
    for (uint32_t i = 0; i < nodeCount; i++)
    {
        TFNGNode* pNode = &pCtx->ppNodeArray[i]->mNode;
        hmput(pHmPointerToIndexTable, pNode, i);
    }

    uint32_t connectionIdx = 0;
    for (uint32_t i = 0; i < nodeCount; i++)
    {
        NodeInternal* pNode = pCtx->ppNodeArray[i];
        for (uint32_t i2 = 0; i2 < pNode->mNode.mInputPortCount; i2++)
        {
            TFNGNode* pOutputNode = pNode->mNode.pInputConnections[i2].pOutputNode;
            if (pOutputNode != NULL)
            {
                ConnectionReflectedData* pConnection = ((ConnectionReflectedData*)pData) + connectionIdx;
                pConnection->mInputNodeIdx = i;
                pConnection->mInputPortIdx = i2;
                pConnection->mOutputNodeIdx = hmget(pHmPointerToIndexTable, pOutputNode);
                pConnection->mOutputPortIdx = pNode->mNode.pInputConnections[i2].mOutputPortIdx;
                connectionIdx++;
            }
        }
    }

    hmfree(pHmPointerToIndexTable);

    pData += sizeof(ConnectionReflectedData) * connectionCount;

    for (uint32_t i = 0; i < nodeCount; i++)
    {
        NodeInternal* pNode = pCtx->ppNodeArray[i];
        TFNGNodeDesc* pDesc = pNode->mNode.pDesc;
        if (pDesc->pReflectedUserDataInfo)
        {
            ASSERT(pNode->mNode.pUserData);
            uint32_t size = (uint32_t)pDesc->pReflectedUserDataInfo->mBase.mSize;
            memcpy(pData, (uint8_t*)pNode->mNode.pUserData + pDesc->mReflectedUserDataOffset, size);
            pData += size;
        }
    }

    serializeStruct(pSerializer, &pTypeInfo->mEntryStructInfo, pNodeGraphData);

    tf_free(pNodeGraphData);
    removeNodeGraphReflectedInfo(pTypeInfo);
}

void ngDeserializeNodeGraph(TFNodeGraphContext* pCtx, DeserializeResult* pDeserializeResult, const char* pNameOfGraph)
{
    // clear node graph context

    while (ngGetNodeCount(pCtx))
    {
        ngRemoveNode(pCtx, ngGetNode(pCtx, 0));
    }

    // stage one
    uint32_t nodeGraphCounts[2];

    NodeGraphReflectedDesc reflectedDesc;
    reflectedDesc.pNameOfGraph = pNameOfGraph;
    reflectedDesc.ppInternalNodes = pCtx->ppNodeArray;
    reflectedDesc.mReflectionType = NODE_GRAPH_REFLECTION_TYPE_COUNTS;
    reflectedDesc.mNodeCount = 0;
    reflectedDesc.mConnectionCount = 0;
    reflectedDesc.mTotalDataSize = sizeof(nodeGraphCounts);

    NodeGraphReflectedInfo* pTypeInfo = addNodeGraphReflectedInfo(reflectedDesc);
    deserializeStruct(&pDeserializeResult->mGlobal, &pTypeInfo->mEntryStructInfo, &nodeGraphCounts);
    removeNodeGraphReflectedInfo(pTypeInfo);

    // stage two
    reflectedDesc.mNodeCount = nodeGraphCounts[0];
    reflectedDesc.mConnectionCount = nodeGraphCounts[1];
    reflectedDesc.mReflectionType = NODE_GRAPH_REFLECTION_TYPE_BASIC_DATA;

    uint32_t dataSize =
        sizeof(BasicNodeReflectedData) * reflectedDesc.mNodeCount + sizeof(ConnectionReflectedData) * reflectedDesc.mConnectionCount;
    reflectedDesc.mTotalDataSize = dataSize;

    uint8_t*   pData = (uint8_t*)tf_calloc(1, dataSize);
    TFNGNode** createdNodes = (TFNGNode**)tf_calloc(reflectedDesc.mNodeCount, sizeof(TFNGNode*));

    pTypeInfo = addNodeGraphReflectedInfo(reflectedDesc);
    deserializeStruct(&pDeserializeResult->mGlobal, &pTypeInfo->mEntryStructInfo, pData);
    removeNodeGraphReflectedInfo(pTypeInfo);

    BasicNodeReflectedData*  pBasicNodeGraphData = (BasicNodeReflectedData*)pData;
    ConnectionReflectedData* pConnectionData =
        (ConnectionReflectedData*)(pData + sizeof(BasicNodeReflectedData) * reflectedDesc.mNodeCount);

    uint32_t userDataSize = 0;
    for (uint32_t i = 0; i < reflectedDesc.mNodeCount; i++)
    {
        TFNGNode* pNode = ngAddNodeByTypeName(pCtx, pBasicNodeGraphData[i].mTypeName);
        if (pNode && pNode->pUserData)
        {
            TFNGUserData* pUserData = (TFNGUserData*)pNode->pUserData;
            pUserData->mPosition[0] = pBasicNodeGraphData[i].mX;
            pUserData->mPosition[1] = pBasicNodeGraphData[i].mY;

            if (pNode->pDesc->pReflectedUserDataInfo)
            {
                userDataSize += (uint32_t)pNode->pDesc->pReflectedUserDataInfo->mBase.mSize;
            }
        }
        createdNodes[i] = pNode;
    }

    for (uint32_t i = 0; i < reflectedDesc.mConnectionCount; i++)
    {
        ConnectionReflectedData connection = pConnectionData[i];

        if (createdNodes[connection.mInputNodeIdx] && createdNodes[connection.mOutputNodeIdx])
        {
            ngAddConnection(pCtx, createdNodes[connection.mOutputNodeIdx], connection.mOutputPortIdx,
                            createdNodes[connection.mInputNodeIdx], connection.mInputPortIdx, true);
        }
    }

    tf_free(pData);
    tf_free(createdNodes);

    // stage three
    reflectedDesc.ppInternalNodes = pCtx->ppNodeArray;
    reflectedDesc.mNodeCount = ngGetNodeCount(pCtx);
    reflectedDesc.mReflectionType = NODE_GRAPH_REFLECTION_TYPE_USER_DATA_DATA;
    reflectedDesc.mTotalDataSize = userDataSize;

    uint8_t* pUserData = (uint8_t*)tf_calloc(1, userDataSize);
    pTypeInfo = addNodeGraphReflectedInfo(reflectedDesc);
    deserializeStruct(&pDeserializeResult->mGlobal, &pTypeInfo->mEntryStructInfo, pUserData);
    removeNodeGraphReflectedInfo(pTypeInfo);

    uint32_t userDataOffset = 0;
    for (uint32_t i = 0; i < reflectedDesc.mNodeCount; i++)
    {
        TFNGNode*     pNode = &pCtx->ppNodeArray[i]->mNode;
        TFNGNodeDesc* pDesc = pNode->pDesc;
        if (pDesc->pReflectedUserDataInfo)
        {
            uint32_t size = (uint32_t)pDesc->pReflectedUserDataInfo->mBase.mSize;
            memcpy((uint8_t*)pNode->pUserData + pDesc->mReflectedUserDataOffset, pUserData + userDataOffset, size);
            userDataOffset += size;
        }
    }

    tf_free(pUserData);
}