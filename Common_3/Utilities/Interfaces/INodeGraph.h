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

#ifndef I_NODE_GRAPH_H
#define I_NODE_GRAPH_H

#include "../../Application/Config.h"
#include "../Reflection/DataTypeReflection.h"
#include "../Reflection/Serialization.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define NODE_GRAPH_NAME_MAX_SIZE      128
#define NODE_GRAPH_PORT_NAME_MAX_SIZE 32
#define NODE_GRAPH_PORT_MAX           16

    typedef struct TFNodeGraphDesc       TFNodeGraphDesc;
    typedef struct TFNodeGraphContext    TFNodeGraphContext;
    typedef struct TFNodeGraphAssembler  TFNodeGraphAssembler;
    typedef struct TFNGAssemblerPortData TFNGAssemblerPortData;
    typedef struct TFNGAssemblerContext  TFNGAssemblerContext;
    typedef struct TFNGPortTypeDesc      TFNGPortTypeDesc;
    typedef struct TFNGPortDesc          TFNGPortDesc;
    typedef struct TFNGConnection        TFNGConnection;
    typedef struct TFNGPort              TFNGPort;
    typedef struct TFNGNodeDesc          TFNGNodeDesc;
    typedef struct TFNGNode              TFNGNode;

    typedef void (*NGAddNodeCallback)(TFNGNode* pNode);
    typedef void (*NGRemoveNodeCallback)(TFNGNode* pNode);
    typedef void (*NGAssembleFunction)(TFNGAssemblerContext* pCtx);

    struct TFNGConnection
    {
        TFNGNode* pInputNode;
        TFNGNode* pOutputNode;
        uint32_t  mInputPortIdx;
        uint32_t  mOutputPortIdx;
    };

    struct TFNGPort
    {
        int32_t  mType;
        uint32_t mDataSize;
    };

    struct TFNGPortDesc
    {
        char    mName[NODE_GRAPH_NAME_MAX_SIZE];
        int32_t mType;
        int32_t mReflectedMemeberIdx;
    };

    struct TFNGNodeDesc
    {
        char                 mName[NODE_GRAPH_NAME_MAX_SIZE];
        int32_t              mType;
        uint32_t             mInputPortCount;
        uint32_t             mOutputPortCount;
        uint32_t             mNodeSize;
        uint32_t             mOutputPortDataSize;
        TFNGPortDesc*        pInputPortDesc;
        TFNGPortDesc*        pOutputPortDesc;
        uint32_t             mNodeUserDataSize;
        TypeInfoStruct*      pReflectedUserDataInfo;
        uint32_t             mReflectedUserDataOffset;
        NGAddNodeCallback    pAddNodeCallback;
        NGRemoveNodeCallback pRemoveNodeCallback;
    };

    struct TFNGPortTypeDesc
    {
        char     mName[NODE_GRAPH_NAME_MAX_SIZE];
        int32_t  mType;
        uint32_t mDataSize;
    };

    struct TFNGNode
    {
        TFNGNodeDesc*   pDesc;
        TFNGPort*       pInputPorts;
        TFNGPort*       pOutputPorts;
        TFNGConnection* pInputConnections;       // the same amount as input ports
        uint32_t*       pOutputConnectionCounts; // the same amount as output ports
        void*           pUserData;
        int32_t         mType;
        uint32_t        mInputPortCount;
        uint32_t        mOutputPortCount;
        uint32_t        mUserDataSize;
    };

    typedef enum TFNodeGraphBasicNode
    {
        TF_NODE_GRAPH_TOGGLE,
        TF_NODE_GRAPH_FLOAT,
        TF_NODE_GRAPH_FLOAT_2,
        TF_NODE_GRAPH_FLOAT_3,
        TF_NODE_GRAPH_FLOAT_4,
        TF_NODE_GRAPH_INT,
        TF_NODE_GRAPH_INT_2,
        TF_NODE_GRAPH_INT_3,
        TF_NODE_GRAPH_INT_4,
        TF_NODE_GRAPH_ADD,
        TF_NODE_GRAPH_SUB,
        TF_NODE_GRAPH_MUL,
        TF_NODE_GRAPH_DIV,
        TF_NODE_GRAPH_SPLIT,
        TF_NODE_GRAPH_MAKE_VECTOR,
        TF_NODE_GRAPH_DOT,
        TF_NODE_GRAPH_CROSS,
        TF_NODE_GRAPH_NORMALIZE,
        TF_NODE_GRAPH_CLAMP,
        TF_NODE_GRAPH_SATURATE,
        TF_NODE_GRAPH_MIN,
        TF_NODE_GRAPH_MAX,
        TF_NODE_GRAPH_ABS,
        TF_NODE_GRAPH_BASIC_NODE_COUNT,
    } TFNodeGraphBasicNode;

    typedef enum TFNodeGraphBasicType
    {
        TF_NODE_GRAPH_TYPE_BOOL,
        TF_NODE_GRAPH_TYPE_VECTOR,
        TF_NODE_GRAPH_BASIC_TYPE_COUNT
    } TFNodeGraphBasicType;

    typedef struct TFNGUserData
    {
        float mPosition[2];
    } TFNGUserData;

    typedef struct TFNGUserData128
    {
        TFNGUserData mData;
        union
        {
            uint32_t mSubData[4];
            struct
            {
                int32_t mX;
                int32_t mY;
                int32_t mZ;
                int32_t mW;
            };
        };
    } TFNGUserData128;

#define NODE_GRAPH_MAX_GRADIENT_KEYFRAME_COUNT 16

    typedef struct TFNGGradient
    {
        TFNGUserData mData;
        float        mKeyColors[NODE_GRAPH_MAX_GRADIENT_KEYFRAME_COUNT * 4];
        float        mKeyValues[NODE_GRAPH_MAX_GRADIENT_KEYFRAME_COUNT];
        uint32_t     mAllocatedKeyCount;
        int32_t      mSelectedKeyIdx;
    } TFNGGradient;

    struct TFNGAssemblerPortData
    {
        uint32_t mPortIdx;
        uint32_t mDataSize;
        uint8_t* pData;
        bool     mIsConnected;
    };

    struct TFNGAssemblerContext
    {
        TFNGNode*              pNode;
        TFNGAssemblerPortData* pInputPorts;
        TFNGAssemblerPortData* pOutputPorts;
        uint8_t*               pOutputData;
        uint32_t               mInputPortCount;
        uint32_t               mOutputPortCount;
        uint32_t               mOutputDataSize;
    };

    // ng - node graph

    // return -1 if it's not a vector type of node
    int32_t ngGetVectorDimOfNodeType(TFNodeGraphBasicNode nodeType);

    TFNodeGraphDesc* ngAddNodeGraphDesc(bool addBasicNodes);

    void ngRemoveNodeGraphDesc(TFNodeGraphDesc* pNGDesc);

    bool ngAddNodeDesc(TFNodeGraphDesc* pNGDesc, const TFNGNodeDesc* pNodeDesc);

    bool ngAddPortType(TFNodeGraphDesc* pNGDesc, const TFNGPortTypeDesc* pPortType);

    TFNodeGraphContext* ngAddNodeGraphContext(TFNodeGraphDesc* pNGDesc);

    void ngRemoveNodeGraphContext(TFNodeGraphContext* pCtx);

    TFNGNode* ngAddNode(TFNodeGraphContext* pCtx, int32_t type);

    TFNGNode* ngAddNodeByTypeName(TFNodeGraphContext* pCtx, const char* pName);

    void ngRemoveNode(TFNodeGraphContext* pCtx, TFNGNode* pNode);

    bool ngAddConnection(TFNodeGraphContext* pCtx, TFNGNode* pNodeOutput, uint32_t outputPortIdx, TFNGNode* pNodeInput,
                         uint32_t inputPortIdx, bool strictType);

    bool ngRemoveConnection(TFNodeGraphContext* pCtx, TFNGNode* pNode, uint32_t connectionIdx);

    TFNodeGraphDesc* ngGetNodeGraphDesc(TFNodeGraphContext* pCtx);

    uint32_t ngGetNodeCount(TFNodeGraphContext* pCtx);

    TFNGNode* ngGetNode(TFNodeGraphContext* pCtx, uint32_t nodeIdx);

    uint32_t ngGetNodeCountByType(TFNodeGraphContext* pCtx, int32_t nodeType);

    uint32_t ngGetNodeDescCount(TFNodeGraphDesc* pNGDesc);

    const TFNGNodeDesc* ngGetNodeDesc(TFNodeGraphDesc* pNGDesc, uint32_t idx);

    const TFNGNodeDesc* ngGetNodeDescByType(TFNodeGraphDesc* pNGDesc, int32_t nodeType);

    const TFNGPortTypeDesc* ngGetPortTypeByType(TFNodeGraphDesc* pNGDesc, int32_t portType);

    // assembler

    TFNodeGraphAssembler* ngAddNodeGraphAssembler(TFNodeGraphDesc* pNGDesc);

    void ngRemoveNodeGraphAssembler(TFNodeGraphAssembler* pAssembler);

    void ngSetAssembleFunction(TFNodeGraphAssembler* pAssembler, NGAssembleFunction function, int32_t nodeType);

    void ngAssembleNodeGraph(TFNodeGraphAssembler* pAssembler, TFNodeGraphContext* pCtx, TFNGNode* pOutputNode, uint32_t outputSize,
                             uint8_t* pOutputData);

    // reflection

    void ngSerializeNodeGraph(TFNodeGraphContext* pCtx, Serializer* pSerializer, const char* pNameOfGraph);

    void ngDeserializeNodeGraph(TFNodeGraphContext* pCtx, DeserializeResult* pDeserializeResult, const char* pNameOfGraph);

#define BEGIN_REGISTER_NODE_GRAPH_DESC()                      \
    {                                                         \
        TFNGPortTypeDesc defTypeDesc;                         \
        TFNGNodeDesc     defNodeDesc;                         \
        TFNGPortDesc     defInputPorts[NODE_GRAPH_PORT_MAX];  \
        TFNGPortDesc     defOutputPorts[NODE_GRAPH_PORT_MAX]; \
        UNREF_PARAM(defTypeDesc);                             \
        UNREF_PARAM(defNodeDesc);                             \
        UNREF_PARAM(defInputPorts);                           \
        UNREF_PARAM(defOutputPorts);

#define END_REGISTER_NODE_GRAPH_DESC() }

#define REGISTER_NODE_GRAPH_TYPE_TO_DESC(DESC, NAME, TYPE, SIZE) \
    {                                                            \
        ASSERT(strlen(NAME) < NODE_GRAPH_NAME_MAX_SIZE);         \
        memset(&defTypeDesc, 0, sizeof(TFNGPortTypeDesc));       \
        strcpy(defTypeDesc.mName, NAME);                         \
        defTypeDesc.mType = (int32_t)TYPE;                       \
        defTypeDesc.mDataSize = (uint32_t)SIZE;                  \
        ngAddPortType(DESC, &defTypeDesc);                       \
    }

#define BEGIN_REGISTER_NODE_GRAPH_NODE_TO_DESC(DESC, NAME, TYPE, SIZE_USER_DATA) \
    {                                                                            \
        ASSERT(strlen(NAME) < NODE_GRAPH_NAME_MAX_SIZE);                         \
        memset(&defNodeDesc, 0, sizeof(TFNGNodeDesc));                           \
        strcpy(defNodeDesc.mName, NAME);                                         \
        defNodeDesc.mType = (int32_t)TYPE;                                       \
        defNodeDesc.mNodeUserDataSize = (uint32_t)SIZE_USER_DATA;                \
        defNodeDesc.pInputPortDesc = defInputPorts;                              \
        defNodeDesc.pOutputPortDesc = defOutputPorts;

#define SET_ADD_NODE_CALLBACK_NODE_DESC(FUNCTION)    defNodeDesc.pAddNodeCallback = FUNCTION;
#define SET_REMOVE_NODE_CALLBACK_NODE_DESC(FUNCTION) defNodeDesc.pRemoveNodeCallback = FUNCTION;
#define SET_REFLECTED_USER_STRUCT_INFO(USER_DATA_INFO, USER_DATA_INFO_OFFSET) \
    defNodeDesc.pReflectedUserDataInfo = USER_DATA_INFO;                      \
    defNodeDesc.mReflectedUserDataOffset = USER_DATA_INFO_OFFSET;

#define ADD_INPUT_PORT_TO_NODE_DESC(NAME, TYPE)                               \
    {                                                                         \
        ASSERT(strlen(NAME) < NODE_GRAPH_NAME_MAX_SIZE);                      \
        ASSERT(defNodeDesc.mInputPortCount < NODE_GRAPH_PORT_MAX);            \
        defInputPorts[defNodeDesc.mInputPortCount].mType = (int32_t)TYPE;     \
        defInputPorts[defNodeDesc.mInputPortCount].mReflectedMemeberIdx = -1; \
        strcpy(defInputPorts[defNodeDesc.mInputPortCount].mName, NAME);       \
        defNodeDesc.mInputPortCount++;                                        \
    }

#define ADD_OUTPUT_PORT_TO_NODE_DESC(NAME, TYPE)                                \
    {                                                                           \
        ASSERT(strlen(NAME) < NODE_GRAPH_NAME_MAX_SIZE);                        \
        ASSERT(defNodeDesc.mOutputPortCount < NODE_GRAPH_PORT_MAX);             \
        defOutputPorts[defNodeDesc.mOutputPortCount].mType = (int32_t)TYPE;     \
        defOutputPorts[defNodeDesc.mOutputPortCount].mReflectedMemeberIdx = -1; \
        strcpy(defOutputPorts[defNodeDesc.mOutputPortCount].mName, NAME);       \
        defNodeDesc.mOutputPortCount++;                                         \
    }

#define ADD_INPUT_PORT_REFLECTED_MEMBER_TO_NODE_DESC_NAME(NAME)                            \
    {                                                                                      \
        for (uint32_t it = 0; it < defNodeDesc.pReflectedUserDataInfo->mMemberCount; it++) \
        {                                                                                  \
            if (strcmp(NAME, defNodeDesc.pReflectedUserDataInfo->pMembers[it].pName) == 0) \
            {                                                                              \
                ASSERT(defNodeDesc.mInputPortCount < NODE_GRAPH_PORT_MAX);                 \
                defInputPorts[defNodeDesc.mInputPortCount].mType = -1;                     \
                defInputPorts[defNodeDesc.mInputPortCount].mReflectedMemeberIdx = it;      \
                strcpy(defInputPorts[defNodeDesc.mInputPortCount].mName, "");              \
                defNodeDesc.mInputPortCount++;                                             \
                break;                                                                     \
            }                                                                              \
        }                                                                                  \
    }

#define ADD_INPUT_PORT_REFLECTED_MEMBER_TO_NODE_DESC(MEMBER) ADD_INPUT_PORT_REFLECTED_MEMBER_TO_NODE_DESC_NAME(#MEMBER)

#define END_REGISTER_NODE_GRAPH_NODE_TO_DESC(DESC) \
    ngAddNodeDesc(DESC, &defNodeDesc);             \
    }

#ifdef __cplusplus
}
#endif

#endif
