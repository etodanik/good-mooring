/*
 * Copyright (c) 2017-2025 The Forge Interactive Inc.
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

#include "../../Application/Config.h"

#include <mach/clock.h>
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <time.h>

#include "../../Utilities/Interfaces/ILog.h"
#include "../../Utilities/Interfaces/IThread.h"
#include "../Interfaces/IOperatingSystem.h"

#include "../../Utilities/Threading/UnixThreadID.h"

#include "../../Utilities/Interfaces/IMemory.h"

#if defined(ENABLE_THREAD_PERFORMANCE_STATS)
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/processor_info.h>

TFMutex                  CPUUsageLock;
processor_info_array_t prevCpuInfo;
mach_msg_type_number_t numPrevCpuInfo;

#endif

void callOnce(CallOnceGuard* pGuard, CallOnceFn pFn) { pthread_once(pGuard, pFn); }

#ifdef TRACY_ENABLE
bool initMutexTracy(TFMutex* pMutex, const char* name)
#else
bool initMutex(TFMutex* pMutex)
#endif
{
    pMutex->mSpinCount = MUTEX_DEFAULT_SPIN_COUNT;
    pMutex->pHandle = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    pthread_mutexattr_t attr;
    int                 status = pthread_mutexattr_init(&attr);
    status |= pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    status |= pthread_mutex_init(&pMutex->pHandle, &attr);
    status |= pthread_mutexattr_destroy(&attr);
#ifdef TRACY_ENABLE
    TracyCLockAnnounce(pMutex->pTracyLock);
    TracyCLockCustomName(pMutex->pTracyLock, name, strlen(name));
#endif
    return status == 0;
}

void exitMutex(TFMutex* pMutex)
{
    pthread_mutex_destroy(&pMutex->pHandle);
#ifdef TRACY_ENABLE
    TracyCLockTerminate(pMutex->pTracyLock);
#endif
}

void acquireMutex(TFMutex* pMutex)
{
    MTRACY_ZONE_COLOR("Mutex acquire", 0xBA7B44);
#ifdef TRACY_ENABLE
    const bool trace = ___tracy_before_lock_lockable_ctx(pMutex->pTracyLock);
#endif
    uint32_t count = 0;

    while (count < pMutex->mSpinCount && pthread_mutex_trylock(&pMutex->pHandle) != 0)
        ++count;

    if (count == pMutex->mSpinCount)
    {
        int r = pthread_mutex_lock(&pMutex->pHandle);
        UNREF_PARAM(r);
        ASSERT(r == 0 && "TFMutex::Acquire failed to take the lock");
    }
#ifdef TRACY_ENABLE
    if (trace) TracyCLockAfterLock(pMutex->pTracyLock);
#endif
}

bool tryAcquireMutex(TFMutex* pMutex)
{
    bool acquired = pthread_mutex_trylock(&pMutex->pHandle) == 0;
#ifdef TRACY_ENABLE
    // Tracy 0.14.1's capture validator requires Wait before Obtain, including
    // successful try-locks. Emit both only after success (no fictitious wait).
    if (acquired && ___tracy_before_lock_lockable_ctx(pMutex->pTracyLock))
        TracyCLockAfterLock(pMutex->pTracyLock);
#endif
    return acquired;
}

void releaseMutex(TFMutex* pMutex)
{
    // Report before unlocking so another thread cannot obtain ahead of release.
#ifdef TRACY_ENABLE
    TracyCLockAfterUnlock(pMutex->pTracyLock);
#endif
    pthread_mutex_unlock(&pMutex->pHandle);
}

bool initConditionVariable(TFConditionVariable* pCv)
{
    pCv->pHandle = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
    int res = pthread_cond_init(&pCv->pHandle, NULL);
    ASSERT(res == 0);
    return res == 0;
}

void exitConditionVariable(TFConditionVariable* pCv) { pthread_cond_destroy(&pCv->pHandle); }

void waitConditionVariable(TFConditionVariable* pCv, TFMutex* mutex, uint32_t ms)
{
    MTRACY_ZONE_COLOR("Condition wait", 0xBA7B44);
    MTRACY_VALUE(ms);
#ifdef TRACY_ENABLE
    TracyCLockAfterUnlock(mutex->pTracyLock);
#endif
    pthread_mutex_t* mutexHandle = (pthread_mutex_t*)&mutex->pHandle;

    if (ms == TIMEOUT_INFINITE)
    {
        pthread_cond_wait(&pCv->pHandle, mutexHandle);
    }
    else
    {
        struct timespec time;
        time.tv_sec = ms / 1000;                    // milliseconds to seconds
        time.tv_nsec = (ms % 1000) * NSEC_PER_MSEC; // remainder to nanoseconds

        pthread_cond_timedwait_relative_np(&pCv->pHandle, mutexHandle, &time);
    }
#ifdef TRACY_ENABLE
    if (___tracy_before_lock_lockable_ctx(mutex->pTracyLock))
        TracyCLockAfterLock(mutex->pTracyLock);
#endif
}

void wakeOneConditionVariable(TFConditionVariable* pCv) { pthread_cond_signal(&pCv->pHandle); }

void wakeAllConditionVariable(TFConditionVariable* pCv) { pthread_cond_broadcast(&pCv->pHandle); }

static ThreadID mainThreadID;

/*  void Thread::SetPriority(int priority)
{
      sched_param param;
      param.sched_priority = priority;
      pthread_setschedparam(pHandle, SCHED_OTHER, &param);
}*/

void setMainThread(void)
{
    mainThreadID = getCurrentThreadID();
#ifdef TRACY_ENABLE
    ___tracy_set_thread_name("Main / AppKit");
#endif
}

ThreadID getCurrentThreadID(void) { return getCurrentPthreadID(); }

void getCurrentThreadName(char* buffer, int buffer_size) { pthread_getname_np(pthread_self(), buffer, buffer_size); }

void setCurrentThreadName(const char* name)
{
    pthread_setname_np(name);
#ifdef TRACY_ENABLE
    ___tracy_set_thread_name(name);
#endif
}

bool isMainThread(void) { return getCurrentThreadID() == mainThreadID; }

void threadSleep(unsigned mSec) { MTRACY_ZONE("Thread sleep"); MTRACY_VALUE(mSec); usleep(mSec * 1000); }

// threading class (Static functions)
unsigned int getNumCPUCores(void)
{
    size_t       len;
    unsigned int ncpu;
    len = sizeof(ncpu);
    sysctlbyname("hw.ncpu", &ncpu, &len, NULL, 0);
    return ncpu;
}

void* ThreadFunctionStatic(void* data)
{
    TFThreadDesc item = *((TFThreadDesc*)(data));
    tf_free(data);

    if (item.mThreadName[0] != 0)
        setCurrentThreadName(item.mThreadName);

    // TODO: implement affinity mask, if Apple at some point allows to set it.

    item.pFunc(item.pData);
    return 0;
}

bool initThread(TFThreadDesc* pData, ThreadHandle* pHandle)
{
    // Copy the contents of TFThreadDesc because if the variable is in the stack we might access corrupted data.
    TFThreadDesc* pDataCopy = (TFThreadDesc*)tf_malloc(sizeof(TFThreadDesc));
    *pDataCopy = *pData;

    int res = pthread_create(pHandle, NULL, ThreadFunctionStatic, pDataCopy);
    if (res)
        tf_free(pDataCopy);
    return res == 0;
}

void joinThread(ThreadHandle handle) { MTRACY_ZONE("Thread join"); pthread_join(handle, NULL); }

void detachThread(ThreadHandle handle) { pthread_detach(handle); }

#if defined(ENABLE_THREAD_PERFORMANCE_STATS)

int initPerformanceStats(TFPerformanceStatsFlags flags)
{
    initMutex(&CPUUsageLock);
    processor_info_array_t cpuInfo;
    mach_msg_type_number_t numCpuInfo;

    natural_t     numCPUsU = 0U;
    kern_return_t err = host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &numCPUsU, &cpuInfo, &numCpuInfo);

    if (err != KERN_SUCCESS)
        return -1;
    return 0;
}

void updatePerformanceStats(void) {}

TFPerformanceStats getPerformanceStats(void)
{
    TFPerformanceStats       ret = { { 0 } };
    processor_info_array_t cpuInfo;
    mach_msg_type_number_t numCpuInfo;

    natural_t     numCPUsU = 0U;
    kern_return_t err = host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &numCPUsU, &cpuInfo, &numCpuInfo);

    if (err == KERN_SUCCESS)
    {
        acquireMutex(&CPUUsageLock);

        for (uint32_t i = 0; i < getNumCPUCores(); i++)
        {
            float inUse, total;

            if (prevCpuInfo)
            {
                inUse = ((cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_USER] - prevCpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_USER]) +
                         (cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_SYSTEM] - prevCpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_SYSTEM]) +
                         (cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_NICE] - prevCpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_NICE]));
                total = inUse + (cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_IDLE] - prevCpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_IDLE]);
            }
            else
            {
                inUse = cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_USER] + cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_SYSTEM] +
                        cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_NICE];
                total = inUse + cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_IDLE];
            }

            ret.mCoreUsagePercentage[i] = ((float)inUse / (float)total) * 100;

            if (ret.mCoreUsagePercentage[i] < 0)
                ret.mCoreUsagePercentage[i] = 0.0;
            else if (ret.mCoreUsagePercentage[i] > 100.0)
                ret.mCoreUsagePercentage[i] = 100.0;
        }

        releaseMutex(&CPUUsageLock);

        if (prevCpuInfo)
        {
            size_t prevCpuInfoSize = sizeof(integer_t) * numPrevCpuInfo;
            vm_deallocate(mach_task_self(), (vm_address_t)prevCpuInfo, prevCpuInfoSize);
        }

        prevCpuInfo = cpuInfo;
        numPrevCpuInfo = numCpuInfo;
    }

    return ret;
}

void exitPerformanceStats(void) {}

#endif // ENABLE_THREAD_PERFORMANCE_STATS
