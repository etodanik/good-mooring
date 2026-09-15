#include "Tracy.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Common_3/Utilities/Interfaces/IThread.h"
#include "Common_3/Utilities/Interfaces/IMemory.h"

// Run with an assertion-enabled Tracy capture to validate the wire protocol,
// especially address reuse, recursion, successful try-lock and condition waits.
static TFMutex mutex;
static TFConditionVariable condition;
static unsigned turn;
static unsigned iterations = 300;
static std::atomic<unsigned> failures{0};
#define CHECK(x) do { if (!(x)) { ++failures; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); } } while (0)

static void worker(void* data)
{
    const unsigned id = unsigned(reinterpret_cast<uintptr_t>(data));
    MTRACY_ZONE("Tracy test worker"); MTRACY_VALUE(id);
    for (unsigned i=0; i<iterations; ++i) {
        acquireMutex(&mutex);
        while (turn != id) waitConditionVariable(&condition, &mutex, 10);
        // Both recursive acquire APIs must preserve lock depth.
        acquireMutex(&mutex);
        CHECK(tryAcquireMutex(&mutex)); releaseMutex(&mutex);
        releaseMutex(&mutex);
        turn = 1-id;
        wakeAllConditionVariable(&condition);
        releaseMutex(&mutex);

        // Concurrent realloc and rapid allocator address reuse remain ordered.
        auto* block = static_cast<unsigned char*>(tf_realloc(nullptr, 37));
        memset(block, int(i&255), 37);
        block = static_cast<unsigned char*>(tf_realloc(block, 16384));
        CHECK(block && block[0] == (i&255) && block[36] == (i&255));
        block = static_cast<unsigned char*>(tf_realloc(block, 19));
        CHECK(block && block[18] == (i&255));
        tf_free(block);
        auto* aligned = tf_memalign(256, 512);
        CHECK((reinterpret_cast<uintptr_t>(aligned)&255) == 0); tf_free(aligned);
    }
}

int main(int argc, char** argv)
{
    initMemAlloc(nullptr); setMainThread(); setCurrentThreadName("Tracy test main");
    mooringTracyAppInfo("MooringTracyTests");
    const unsigned rounds = argc>1 ? unsigned(atoi(argv[1])) : 1;
    if (argc>3) iterations = unsigned(atoi(argv[3]));
    if (argc>2) for (unsigned i=0; i<3000 && !TracyCIsConnected; ++i) threadSleep(10);
    CHECK(initMutex(&mutex)); CHECK(initConditionVariable(&condition));
    acquireMutex(&mutex);
    TFThreadDesc blocked={}; ThreadHandle blockedThread;
    blocked.pFunc=[](void*) { CHECK(!tryAcquireMutex(&mutex)); };
    snprintf(blocked.mThreadName,sizeof(blocked.mThreadName),"Try-lock failure");
    CHECK(initThread(&blocked,&blockedThread)); joinThread(blockedThread);
    releaseMutex(&mutex);
    for (unsigned round=0; round<rounds; ++round) {
        MTRACY_ZONE("Tracy test round"); MTRACY_VALUE(round);
        turn=0;
        TFThreadDesc desc={}; ThreadHandle threads[2];
        desc.pFunc=worker;
        for (unsigned i=0; i<2; ++i) {
            desc.pData=reinterpret_cast<void*>(uintptr_t(i));
            snprintf(desc.mThreadName,sizeof(desc.mThreadName),"Tracy worker %u",i);
            CHECK(initThread(&desc,&threads[i]));
        }
        for (auto thread:threads) joinThread(thread);
        acquireMutex(&mutex);
        waitConditionVariable(&condition,&mutex,1); // timed-out wait reacquires
        releaseMutex(&mutex);
        mooringTracyFrame();
        threadSleep(20);
    }
    tf_free(nullptr);
    exitConditionVariable(&condition); exitMutex(&mutex);
    mooringTracyScene(nullptr);
    printf("Tracy allocator / recursive lock / condition tests: %s (%u rounds)\n", failures ? "FAIL" : "PASS", rounds);
    exitMemAlloc();
    return failures ? 1 : 0;
}
