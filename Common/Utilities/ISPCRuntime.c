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
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "ISPCRuntime.h"
#include "Threading/ThreadSystem.h"
#include "Interfaces/ILog.h"
#include "Interfaces/IThread.h"
#include "ThirdParty/OpenSource/Nothings/stb_ds.h"

#define MAX_ISPC_TASK_CONTEXTS 1024

typedef void (*ISPCTaskFunc)(void* data, int threadIndex, int threadCount, int taskIndex, int taskCount, int taskIndex0, int taskIndex1,
                             int taskIndex2, int taskCount0, int taskCount1, int taskCount2);
typedef struct ISPCTaskData
{
    void*        pISPCData;
    ISPCTaskFunc pISPCTaskFunc;
    int32_t      mThreadIndex;
    int32_t      mThreadCount;
    int32_t      mTaskTotalCount;
    int32_t      mTaskTotalCount0;
    int32_t      mTaskTotalCount1;
    int32_t      mTaskTotalCount2;
    int32_t      mTaskRangeStart;
    int32_t      mTaskRangeCount;
    int32_t      mTaskRangeStart0;
    int32_t      mTaskRangeCount0;
    int32_t      mTaskRangeStart1;
    int32_t      mTaskRangeCount1;
    int32_t      mTaskRangeStart2;
    int32_t      mTaskRangeCount2;
} ISPCTaskData;

typedef struct ISPCTaskContext
{
    ThreadSystem  mThreadSystem;
    void**        pAllocatedPointers; // stb array
    bool          mAllocated;
    ISPCTaskData* pTaskArgsBuffer; // stb array
} ISPCTaskContext;

ISPCTaskContext gTaskContexts[MAX_ISPC_TASK_CONTEXTS];
uint32_t        gTaskContextCount = 0;
bool            gISPCRuntimeInitialized = false;

void ExecuteISPCTasks(void* user, uint64_t threadId)
{
    (void)threadId;
    ISPCTaskData* task = (ISPCTaskData*)user;

    int32_t i = task->mTaskRangeStart;
    for (int32_t z = task->mTaskRangeStart2; z < task->mTaskRangeStart2 + task->mTaskRangeCount2; z += 1)
    {
        for (int32_t y = task->mTaskRangeStart1; y < task->mTaskRangeStart1 + task->mTaskRangeCount1; y += 1)
        {
            for (int32_t x = task->mTaskRangeStart0; x < task->mTaskRangeStart0 + task->mTaskRangeCount0; x += 1)
            {
                task->pISPCTaskFunc(task->pISPCData, task->mThreadIndex, task->mThreadCount, i, task->mTaskTotalCount, x, y, z,
                                    task->mTaskTotalCount0, task->mTaskTotalCount1, task->mTaskTotalCount2);
                i += 1;
            }
        }
    }
}

ISPCTaskContext* ISPCAddTaskContext()
{
    ASSERTMSG(gTaskContextCount < MAX_ISPC_TASK_CONTEXTS, "Max ISPC task contexts reached");
    ISPCTaskContext* taskContext = &gTaskContexts[gTaskContextCount++];
    memset(taskContext, 0, sizeof(ISPCTaskContext));
    struct ThreadSystemInitDesc desc = gThreadSystemInitDescDefault;
    desc.threadCount = getNumCPUCores();
    bool threadSystemOk = threadSystemInit(&taskContext->mThreadSystem, &desc);
    ASSERT(threadSystemOk);
    taskContext->mAllocated = false;
    return taskContext;
}

ISPCTaskContext* ISPCAddOrGetTaskContext(ISPCTaskContext** pHandle)
{
    ISPCTaskContext* taskContext = 0;
    if (!*pHandle)
    {
        // First see if there is a already initialized thread system that can be reused
        for (uint32_t i = 0; i < gTaskContextCount; i += 1)
        {
            if (!gTaskContexts[i].mAllocated)
            {
                taskContext = &gTaskContexts[i];
                taskContext->mAllocated = true;
            }
        }

        // If no reusable task context exists, create a new one
        if (!taskContext)
        {
            taskContext = ISPCAddTaskContext();
            taskContext->mAllocated = true;
        }

        *pHandle = taskContext;
        return taskContext;
    }
    else
    {
        taskContext = *pHandle;
        return taskContext;
    }
}

void* ISPCAlloc(ISPCTaskContext** pHandle, int64_t size, int32_t alignment)
{
    ASSERTMSG(gISPCRuntimeInitialized, "Using the 'launch' keyword in FSL CPU Shaders requires the ISPC runtime to be initialized. Please "
                                       "call initISPCRuntime() and exitISPCRuntime().");

    ISPCTaskContext* taskContext = ISPCAddOrGetTaskContext(pHandle);

    void* mem = tf_memalign(alignment, size + alignment);

    arrpush(taskContext->pAllocatedPointers, mem);

    return mem;
}

void ISPCLaunch(ISPCTaskContext** pHandle, ISPCTaskFunc f, void* data, int32_t count0, int32_t count1, int32_t count2)
{
    ASSERTMSG(gISPCRuntimeInitialized, "Using the 'launch' keyword in FSL CPU Shaders requires the ISPC runtime to be initialized. Please "
                                       "call initISPCRuntime() and exitISPCRuntime().");

    ISPCTaskContext* taskContext = ISPCAddOrGetTaskContext(pHandle);

    int32_t totalTaskCount = count0 * count1 * count2;

    int32_t partitionDim;
    int32_t partitionCount;
    if (count2 > 1)
    {
        partitionDim = 2;
        partitionCount = count2;
    }
    else if (count1 > 1)
    {
        partitionDim = 1;
        partitionCount = count1;
    }
    else
    {
        partitionDim = 0;
        partitionCount = count0;
    }

    int32_t availableCores = getNumCPUCores();
    int32_t threadCount = (partitionCount < availableCores) ? partitionCount : availableCores;

    arrsetcap(taskContext->pTaskArgsBuffer, threadCount);
    arrsetlen(taskContext->pTaskArgsBuffer, 0);

    for (int32_t i = 0; i < threadCount; i++)
    {
        ISPCTaskData arg_data;
        memset(&arg_data, 0, sizeof(arg_data));

        arg_data.pISPCData = data;
        arg_data.pISPCTaskFunc = f;
        arg_data.mThreadIndex = i;
        arg_data.mThreadCount = threadCount;
        arg_data.mTaskTotalCount = totalTaskCount;
        arg_data.mTaskTotalCount0 = count0;
        arg_data.mTaskTotalCount1 = count1;
        arg_data.mTaskTotalCount2 = count2;

        int32_t chunk = partitionCount / threadCount;
        int32_t remainder = partitionCount % threadCount;
        int32_t start = i * chunk + (i < remainder ? i : remainder);
        int32_t count = chunk + (i < remainder ? 1 : 0);

        if (partitionDim == 2)
        {
            arg_data.mTaskRangeStart2 = start;
            arg_data.mTaskRangeCount2 = count;
            arg_data.mTaskRangeStart1 = 0;
            arg_data.mTaskRangeCount1 = count1;
            arg_data.mTaskRangeStart0 = 0;
            arg_data.mTaskRangeCount0 = count0;
            arg_data.mTaskRangeStart = start * (count1 * count0);
            arg_data.mTaskRangeCount = count * (count1 * count0);
        }
        else if (partitionDim == 1)
        {
            arg_data.mTaskRangeStart1 = start;
            arg_data.mTaskRangeCount1 = count;
            arg_data.mTaskRangeStart2 = 0;
            arg_data.mTaskRangeCount2 = count2;
            arg_data.mTaskRangeStart0 = 0;
            arg_data.mTaskRangeCount0 = count0;
            arg_data.mTaskRangeStart = start * count0;
            arg_data.mTaskRangeCount = count2 * (count * count0);
        }
        else
        {
            arg_data.mTaskRangeStart0 = start;
            arg_data.mTaskRangeCount0 = count;
            arg_data.mTaskRangeStart2 = 0;
            arg_data.mTaskRangeCount2 = count2;
            arg_data.mTaskRangeStart1 = 0;
            arg_data.mTaskRangeCount1 = count1;
            arg_data.mTaskRangeStart = start;
            arg_data.mTaskRangeCount = count2 * count1 * count;
        }

        arrpush(taskContext->pTaskArgsBuffer, arg_data);
    }

    threadSystemAddTasks(taskContext->mThreadSystem, ExecuteISPCTasks, threadCount, sizeof(ISPCTaskData), taskContext->pTaskArgsBuffer);
}

void ISPCSync(ISPCTaskContext* taskContext)
{
    ASSERT(taskContext);

    threadSystemWaitIdle(taskContext->mThreadSystem);

    for (uint32_t i = 0; i < arrlen(taskContext->pAllocatedPointers); i += 1)
    {
        void* p = taskContext->pAllocatedPointers[i];
        tf_free(p);
        (void)p;
    }

    // Clear but keep memory for reuse
    arrsetlen(taskContext->pAllocatedPointers, 0);
    arrsetlen(taskContext->pTaskArgsBuffer, 0);
    taskContext->mAllocated = false;
}

void initISPCRuntime(void)
{
    gISPCRuntimeInitialized = true;

    ISPCAddTaskContext();
}
void exitISPCRuntime(void)
{
    for (uint32_t i = 0; i < gTaskContextCount; i += 1)
    {
        if (gTaskContexts[i].mAllocated)
        {
            threadSystemWaitIdle(gTaskContexts[i].mThreadSystem);
        }
        threadSystemExit(&gTaskContexts[i].mThreadSystem, &gThreadSystemExitDescDefault);

        if (gTaskContexts[i].pAllocatedPointers)
        {
            arrfree(gTaskContexts[i].pAllocatedPointers);
            arrfree(gTaskContexts[i].pTaskArgsBuffer);
            gTaskContexts[i].pAllocatedPointers = 0;
            gTaskContexts[i].pTaskArgsBuffer = 0;
        }
    }
    gTaskContextCount = 0;

    gISPCRuntimeInitialized = false;
}