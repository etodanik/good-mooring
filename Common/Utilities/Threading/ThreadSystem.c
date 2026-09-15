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

#include "ThreadSystem.h"

#include "../ThirdParty/OpenSource/Nothings/stb_ds.h"

#include "../Interfaces/ILog.h"
#include "../Interfaces/IThread.h"
#include "../Interfaces/ITime.h"

#include "Atomics.h"

#define MAX_BUFFERED_TASKS 1024

struct ThreadSystemTask
{
    TaskFunc func;
    void*    user;
};

struct ThreadSystemData
{
    const char* pName;
    uint64_t    mThreadCount;

    ThreadHandle* pThreads;

    TFThreadSemaphore mTasksSemaphore;

    tfrg_atomic32_t mActivatedThreadCount;
    tfrg_atomic32_t mThreadReferenceCount;

    uint32_t            mWaitingTaskCount;
    TFMutex             mWaitingTaskCountMutex;
    TFConditionVariable mWaitIdleConditionVariable;

    bool mStopAbandon; // stop even if tasks are scheduled
    bool mStop;

    uint64_t                mTaskRingWorkIndex;
    uint64_t                mTaskRingAllocateIndex;
    TFThreadSemaphore       mFreeTaskMemorySemaphore;
    struct ThreadSystemTask mTaskRing[MAX_BUFFERED_TASKS];
};

static void threadSystemCleanup(struct ThreadSystemData* t)
{
    ASSERT(tfrg_atomic32_load_relaxed(&t->mThreadReferenceCount) == 0);

    exitThreadSemaphore(&t->mTasksSemaphore);
    exitMutex(&t->mWaitingTaskCountMutex);
    exitConditionVariable(&t->mWaitIdleConditionVariable);
    exitThreadSemaphore(&t->mFreeTaskMemorySemaphore);

    tf_free(t);
}

static inline void acquireThreadSystemHandle(struct ThreadSystemData* t) { tfrg_atomic32_add_relaxed(&t->mThreadReferenceCount, 1); }

static inline void releaseThreadSystemHandle(struct ThreadSystemData* t)
{
    uint64_t threadCount = tfrg_atomic32_add_relaxed(&t->mThreadReferenceCount, -1);
    if (threadCount == 1)
        threadSystemCleanup(t);
}

static struct ThreadSystemTask getTask(struct ThreadSystemData* t, uint64_t tid)
{
    struct ThreadSystemTask task = { 0 };
    UNREF_PARAM(tid);

    waitThreadSemaphore(&t->mTasksSemaphore, 1);

    if (t->mStopAbandon)
        return task;

    uint64_t taskIndex = tfrg_atomic64_add_relaxed(&t->mTaskRingWorkIndex, 1) % (MAX_BUFFERED_TASKS);
    task = t->mTaskRing[taskIndex];
    signalThreadSemaphore(&t->mFreeTaskMemorySemaphore, 1);

    return task;
}

static void incrementWaitingTaskCount(struct ThreadSystemData* t, uint32_t amount)
{
    acquireMutex(&t->mWaitingTaskCountMutex);
    t->mWaitingTaskCount += amount;
    releaseMutex(&t->mWaitingTaskCountMutex);
}

static void decrementWaitingTaskCount(struct ThreadSystemData* t)
{
    acquireMutex(&t->mWaitingTaskCountMutex);
    const bool wake = --t->mWaitingTaskCount == 0;
    releaseMutex(&t->mWaitingTaskCountMutex);
    if (wake)
    {
        wakeAllConditionVariable(&t->mWaitIdleConditionVariable);
    }
}

static void taskThreadFunc(void* threadUserData)
{
    struct ThreadSystemData* t = threadUserData;

    uint64_t tid = tfrg_atomic32_add_relaxed(&t->mActivatedThreadCount, 1);

    {
        char buffer[MAX_THREAD_NAME_LENGTH];
        snprintf(buffer, MAX_THREAD_NAME_LENGTH, "%s %llu", t->pName, (unsigned long long)tid);
        setCurrentThreadName(buffer);
    }

    struct ThreadSystemTask task = { 0 };
    while (!t->mStopAbandon)
    {
        if (task.func)
        {
            MTRACY_ZONE("Worker task");
            MTRACY_VALUE(tid);
            task.func(task.user, tid);
            decrementWaitingTaskCount(t);
        }

        task = getTask(t, tid);
        if (t->mStop && !task.func)
            break;
    }

    releaseThreadSystemHandle(t);
}

bool threadSystemInit(ThreadSystem* out, const struct ThreadSystemInitDesc* desc)
{
    MTRACY_ZONE("threadSystemInit");
    *out = NULL;
    if (desc->threadCount == 0) // dummy run
        return true;

    uint64_t cpuCount = getNumCPUCores();
    uint64_t count = desc->threadCount;

    if (count > cpuCount)
        count = cpuCount;

    if (count == 0) // something went wrong (maybe getNumCPUCores returned 0)
        return false;

    struct ThreadSystemData* t = tf_calloc(1, sizeof(struct ThreadSystemData) + sizeof(ThreadHandle) * count);
    if (!t)
        return false;
    memset(t, 0, sizeof(struct ThreadSystemData));

    t->pThreads = (ThreadHandle*)(t + 1);
    t->pName = desc->threadName ? desc->threadName : "ThreadSystem";

    bool success = true;

    if (!initThreadSemaphore(&t->mTasksSemaphore, 0, UINT32_MAX))
        success = false;
    if (!initMutex(&t->mWaitingTaskCountMutex))
        success = false;
    if (!initConditionVariable(&t->mWaitIdleConditionVariable))
        success = false;
    if (!initThreadSemaphore(&t->mFreeTaskMemorySemaphore, MAX_BUFFERED_TASKS, UINT32_MAX))
        success = false;

    if (!success)
    {
        threadSystemCleanup(t);
        return false;
    }

    TFThreadDesc threadDesc = { 0 };

    threadDesc.pFunc = taskThreadFunc;
    threadDesc.pData = t;

#if defined(_WINDOWS) // for some reason on Windows thread name won't change after creation
    strncpy(threadDesc.mThreadName, t->pName, sizeof threadDesc.mThreadName);
    threadDesc.mThreadName[sizeof(threadDesc.mThreadName) - 1] = 0;
#endif

    if (desc->setAffinityMask)
    {
        threadDesc.setAffinityMask = true;
        memcpy(threadDesc.affinityMask, desc->affinityMask, sizeof threadDesc.affinityMask);
    }

    t->mThreadCount = count;

    for (uint64_t ti = 0; ti < count; ++ti)
    {
        acquireThreadSystemHandle(t);

        if (initThread(&threadDesc, t->pThreads + ti))
            continue;

        t->mStop = true;
        t->mStopAbandon = true;

        releaseThreadSystemHandle(t);
        return false;
    }

    acquireThreadSystemHandle(t);
    *out = t;
    return true;
}

void threadSystemExit(ThreadSystem* thandle, const struct ThreadSystemExitDesc* desc)
{
    MTRACY_ZONE("threadSystemExit");
    struct ThreadSystemData* t = *thandle;
    if (!t)
        return;
    *thandle = NULL;

    threadSystemWaitIdle(*thandle);

    t->mStop = true;
    if (desc->abandonTasks)
        t->mStopAbandon = true;

    memset(t->mTaskRing, 0, sizeof(t->mTaskRing));
    signalThreadSemaphore(&t->mTasksSemaphore, (uint32_t)t->mThreadCount);

    if (!desc->detachThreads)
    {
        for (uint64_t ti = 0; ti < t->mThreadCount; ++ti)
            joinThread(t->pThreads[ti]);
    }
    else
    {
        for (uint64_t ti = 0; ti < t->mThreadCount; ++ti)
            detachThread(t->pThreads[ti]);
    }

    releaseThreadSystemHandle(t);
}

void threadSystemAddTasks(ThreadSystem thandle, TaskFunc func, uint64_t count, uint64_t userSize, void* users)
{
    MTRACY_ZONE("threadSystemAddTasks");
    if (count == 0)
        return;
    if (!VERIFY(func))
        return;

    struct ThreadSystemData* t = thandle;

    if (!t) // dummy run
    {
        for (uint64_t ti = 0; ti < count; ++ti)
            func((uint8_t*)users + ti * userSize, 0);
        return;
    }

    uint64_t batches = (count + MAX_BUFFERED_TASKS - 1) / MAX_BUFFERED_TASKS;

    uint64_t remaining_count = count;
    uint64_t userIndex = 0;
    for (uint64_t b = 0; b < batches; b += 1)
    {
        uint64_t thisCount = remaining_count > MAX_BUFFERED_TASKS ? MAX_BUFFERED_TASKS : remaining_count;
        waitThreadSemaphore(&t->mFreeTaskMemorySemaphore, (uint32_t)thisCount);
        for (uint64_t i = 0; i < thisCount; i += 1)
        {
            struct ThreadSystemTask* task = &t->mTaskRing[t->mTaskRingAllocateIndex];
            t->mTaskRingAllocateIndex = (t->mTaskRingAllocateIndex + 1) & (MAX_BUFFERED_TASKS - 1);

            task->func = func;
            task->user = users ? ((uint8_t*)users + userIndex * userSize) : NULL;
            userIndex += 1;
        }
        incrementWaitingTaskCount(t, (uint32_t)thisCount);
        signalThreadSemaphore(&t->mTasksSemaphore, (uint32_t)thisCount);
        remaining_count -= MAX_BUFFERED_TASKS;
    }

    return;
}

bool threadSystemAssist(ThreadSystem thandle)
{
    MTRACY_ZONE("threadSystemAssist");
    struct ThreadSystemData* t = thandle;
    if (!t)
        return false;

    struct ThreadSystemTask task = getTask(t, UINT64_MAX);
    if (task.func)
    {
        MTRACY_ZONE("Assisted task");
        task.func(task.user, UINT64_MAX);
        decrementWaitingTaskCount(t);
    }
    return task.func;
}

bool threadSystemWaitIdleTimeout(ThreadSystem thandle, uint32_t msTimeout)
{
    MTRACY_ZONE("threadSystemWaitIdleTimeout");
    struct ThreadSystemData* t = thandle;
    if (!t)
        return true;

    const int64_t startTime = getUSec(false) / 1000;
    acquireMutex(&t->mWaitingTaskCountMutex);
    for (;;)
    {
        if (t->mWaitingTaskCount == 0)
        {
            releaseMutex(&t->mWaitingTaskCountMutex);
            return true;
        }
        const int64_t elapsed = getUSec(false) / 1000 - startTime;
        const int64_t remaining = msTimeout - elapsed;
        if (remaining <= 0)
        {
            // we check the timing manually even though waitConditionVariable
            // has a timeout just in case of spurious wakeups
            releaseMutex(&t->mWaitingTaskCountMutex);
            return false;
        }
        waitConditionVariable(&t->mWaitIdleConditionVariable, &t->mWaitingTaskCountMutex, (uint32_t)remaining);
    }
}

void threadSystemWaitIdle(ThreadSystem thandle)
{
    MTRACY_ZONE("threadSystemWaitIdle");
    struct ThreadSystemData* t = thandle;
    if (!t)
        return;

    acquireMutex(&t->mWaitingTaskCountMutex);
    for (;;)
    {
        if (t->mWaitingTaskCount == 0)
        {
            releaseMutex(&t->mWaitingTaskCountMutex);
            return;
        }
        waitConditionVariable(&t->mWaitIdleConditionVariable, &t->mWaitingTaskCountMutex, TIMEOUT_INFINITE);
    }
}

void threadSystemGetInfo(ThreadSystem thandle, struct ThreadSystemInfo* outInfo)
{
    memset(outInfo, 0, sizeof *outInfo);

    struct ThreadSystemData* t = thandle;
    if (!t)
        return;

    outInfo->threadCount = t->mThreadCount;
    outInfo->executedThreadCount = tfrg_atomic32_load_relaxed(&t->mActivatedThreadCount);
    outInfo->activeThreadCount = tfrg_atomic32_load_relaxed(&t->mThreadReferenceCount) - 1;
    outInfo->threadName = t->pName;
}
