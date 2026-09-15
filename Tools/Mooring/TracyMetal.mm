#include "TracyMetal.h"
#include <tracy/Tracy.hpp>
#include <algorithm>
#include <atomic>
#include <mutex>
#include "Common/Utilities/Interfaces/IMemory.h"

// Keep the optional image pass in Forge's existing GPU fence chain.
void util_end_current_encoders(TFCmd* cmd, bool forceBarrier);
void util_barrier_required(TFCmd* cmd, const TFQueueType& encoderType);

// Forge already owns command-buffer fences. Resolve only completed buffers,
// reuse a bounded sample pool, and never wait for profiling readbacks. Unlike
// TracyMetal.hmm's timeout fallback, absent timestamps never become fake zones.
namespace
{
constexpr unsigned MaxEncoders = 256;
struct Command;
struct Queue
{
    id<MTLDevice>               device;
    id<MTLCounterSet>           counters;
    id<MTLComputePipelineState> thumbnail;
    std::mutex                  publish;
    uint8_t                     context;
    uint64_t                    firstTimestamp;
    Command*                    head = nullptr;
    Command*                    tail = nullptr;
};
struct Encoder
{
    int64_t                    cpuBegin, cpuEnd;
    uint32_t                   thread;
    uint64_t                   work;
    id<MTLCounterSampleBuffer> samples;
    NSUInteger                 startIndex, endIndex;
    const TFPipeline*          pipeline;
    bool                       namedPass;
    char                       name[128];
};
struct Command
{
    Queue*                     queue;
    Command*                   next = nullptr;
    id<MTLCounterSampleBuffer> samples;
    id<MTLCommandBuffer>       pending;
    id<MTLBuffer>              image;
    Encoder                    encoders[MaxEncoders];
    unsigned                   count = 0;
    int                        current = -1;
    uint64_t                   connection = 0, imageFrame = 0, lastTimestamp = 0;
    uint32_t                   width = 0, height = 0;
    bool                       recording = false, hasImage = false;
};
std::atomic<unsigned> imageInterval{ 0 };
std::atomic<uint64_t> frameIndex{ 0 };
std::atomic<uint64_t> dropped{ 0 };

uint64_t connection()
{
#ifdef TRACY_ON_DEMAND
    return tracy::GetProfiler().ConnectionId();
#else
    return 0;
#endif
}
bool recording()
{
#ifdef TRACY_ON_DEMAND
    return TracyIsConnected;
#else
    return true;
#endif
}
void publish(Queue* q, const Encoder& e, uint64_t start, uint64_t end)
{
    // Queue writes preserve the ORIGINAL CPU encode timestamps/thread, even
    // though publication waits for the GPU. IDs can be reused after GpuTime.
    using namespace tracy;
    const auto source =
        Profiler::AllocSourceLocation(__LINE__, __FILE__, sizeof(__FILE__) - 1, "Metal encoder", 13, e.name, strlen(e.name), 0x5599CC);
    auto* item = Profiler::QueueSerial();
    MemWrite(&item->hdr.type, QueueType::GpuZoneBeginAllocSrcLocSerial);
    MemWrite(&item->gpuZoneBegin.cpuTime, e.cpuBegin);
    MemWrite(&item->gpuZoneBegin.srcloc, source);
    MemWrite(&item->gpuZoneBegin.thread, e.thread);
    MemWrite(&item->gpuZoneBegin.queryId, uint16_t(0));
    MemWrite(&item->gpuZoneBegin.context, q->context);
    Profiler::QueueSerialFinish();
    item = Profiler::QueueSerial();
    MemWrite(&item->hdr.type, QueueType::GpuZoneEndSerial);
    MemWrite(&item->gpuZoneEnd.cpuTime, e.cpuEnd);
    MemWrite(&item->gpuZoneEnd.thread, e.thread);
    MemWrite(&item->gpuZoneEnd.queryId, uint16_t(1));
    MemWrite(&item->gpuZoneEnd.context, q->context);
    Profiler::QueueSerialFinish();
    ___tracy_emit_gpu_time_serial({ int64_t(start), 0, q->context });
    ___tracy_emit_gpu_time_serial({ int64_t(end), 1, q->context });
}
void collectCompleted(Command* c)
{
    if (c->connection == connection() && recording() && c->pending.status == MTLCommandBufferStatusCompleted)
    {
        if (c->count)
        {
            MTLTimestamp cpuNow, gpuNow;
            [c->queue->device sampleTimestamps:&cpuNow gpuTimestamp:&gpuNow];
            id<MTLCounterSampleBuffer> buffer = nil;
            NSData*                    resolved = nil;
            NSUInteger                 first = 0, last = 0;
            uint64_t                   latest = c->lastTimestamp;
            for (unsigned i = 0; i < c->count; ++i)
            {
                const auto& e = c->encoders[i];
                if (!e.work)
                    continue; // Metal may optimize empty encoders away.
                if (e.samples != buffer || e.startIndex < first || e.endIndex > last)
                {
                    buffer = e.samples;
                    first = e.startIndex;
                    last = e.endIndex;
                    // Resolve consecutive encoders sharing a query buffer together.
                    for (unsigned j = i + 1; j < c->count && c->encoders[j].samples == buffer; ++j)
                        last = std::max(last, c->encoders[j].endIndex);
                    resolved = [buffer resolveCounterRange:NSMakeRange(first, last - first + 1)];
                }
                const auto*    times = static_cast<const MTLCounterResultTimestamp*>(resolved.bytes);
                const bool     valid = times && resolved.length >= (last - first + 1) * sizeof(*times);
                const uint64_t start = valid ? times[e.startIndex - first].timestamp : 0;
                const uint64_t end = valid ? times[e.endIndex - first].timestamp : 0;
                if (start < c->queue->firstTimestamp || end < start || start <= c->lastTimestamp || end > gpuNow)
                {
                    if (dropped.fetch_add(1) < 4)
                    {
                        char message[256];
                        snprintf(message, sizeof(message), "Discarded Metal timestamps in %s: %llu..%llu, valid clock range %llu..%llu",
                                 e.name, (unsigned long long)start, (unsigned long long)end, (unsigned long long)c->queue->firstTimestamp,
                                 (unsigned long long)gpuNow);
                        TracyMessage(message, strlen(message));
                    }
                    continue;
                }
                publish(c->queue, e, start, end);
                latest = std::max(latest, end);
            }
            c->lastTimestamp = latest;
        }
        if (c->hasImage)
        {
            const uint64_t lag = frameIndex.load() - c->imageFrame;
            if (lag <= 255)
                FrameImage(c->image.contents, c->width, c->height, uint8_t(lag), false);
        }
    }
    c->pending = nil;
    c->hasImage = false;
    c->count = 0;
}
void collect(Queue* q)
{
    MTRACY_ZONE_COLOR("Tracy / collect completed Metal buffers", 0xBB6655);
    std::lock_guard lock(q->publish);
    // Publish in submission order, including buffers that are rarely reused.
    // Late, out-of-order timestamps look like counter wraparound to Tracy.
    while (q->head && q->head->pending.status >= MTLCommandBufferStatusCompleted)
    {
        Command* c = q->head;
        q->head = c->next;
        if (!q->head)
            q->tail = nullptr;
        c->next = nullptr;
        collectCompleted(c);
    }
}
Command* beginEncoder(TFCmd* cmd, const char* name)
{
    auto* c = static_cast<Command*>(cmd->pTracy);
    if (!c || !c->recording)
        return nullptr;
    if (c->count == MaxEncoders)
    {
        ++dropped;
        return nullptr;
    }
    c->current = int(c->count++);
    auto& e = c->encoders[c->current];
    e = {};
    e.cpuBegin = tracy::Profiler::GetTime();
    e.thread = tracy::GetThreadHandle();
    e.samples = c->samples;
    e.startIndex = c->current * 2;
    e.endIndex = c->current * 2 + 1;
#ifdef TF_ENABLE_GRAPHICS_DEBUG_ANNOTATION
    e.namedPass = cmd->mDebugMarker[0] != 0;
    snprintf(e.name, sizeof(e.name), "%s", e.namedPass ? cmd->mDebugMarker : name);
#else
    snprintf(e.name, sizeof(e.name), "%s", name);
#endif
    return c;
}
} // namespace

void     mooringTracyMetalImages(unsigned interval) { imageInterval = interval; }
unsigned mooringTracyMetalImageInterval() { return imageInterval.load(); }
void     mooringTracyMetalFrame()
{
    ++frameIndex;
    MTRACY_PLOT("Tracy / skipped or invalid GPU samples", dropped.load());
}

void mooringTracyMetalInitQueue(TFQueue* queue, TFRenderer* renderer)
{
    if (![renderer->pDevice supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary])
    {
        TracyMessageL("Metal stage timestamps unavailable on this device; CPU profiling remains available.");
        return;
    }
    auto* q = tf_new(Queue);
    q->device = renderer->pDevice;
    for (id<MTLCounterSet> set in q->device.counterSets)
        if ([set.name isEqualToString:MTLCommonCounterSetTimestamp])
            q->counters = set;
    if (!q->counters)
    {
        tf_delete(q);
        return;
    }
    const int32_t context = tracy::NextGpuContextId();
    if (context > UINT8_MAX)
    {
        TracyMessageL("Tracy's 256 GPU contexts are exhausted; CPU profiling remains available.");
        tf_delete(q);
        return;
    }
    q->context = uint8_t(context);
    MTLTimestamp cpu, gpu;
    [q->device sampleTimestamps:&cpu gpuTimestamp:&gpu];
    q->firstTimestamp = gpu;
    // Metal queues accept command buffers encoded by several CPU threads.
    // Tracy's C convenience API creates a thread-bound context; Metal needs
    // the thread-independent context used by its official Metal backend.
    using namespace tracy;
    auto* item = Profiler::QueueSerial();
    MemWrite(&item->hdr.type, QueueType::GpuNewContext);
    MemWrite(&item->gpuNewContext.cpuTime, Profiler::GetTime());
    MemWrite(&item->gpuNewContext.gpuTime, int64_t(gpu));
    MemWrite(&item->gpuNewContext.thread, uint32_t(0));
    MemWrite(&item->gpuNewContext.period, 1.0f);
    MemWrite(&item->gpuNewContext.context, q->context);
    MemWrite(&item->gpuNewContext.flags, GpuContextFlags(0));
    MemWrite(&item->gpuNewContext.type, GpuContextType::Metal);
#ifdef TRACY_ON_DEMAND
    GetProfiler().DeferItem(*item);
#endif
    Profiler::QueueSerialFinish();
    const char* name = queue->pCommandQueue.label.UTF8String;
    ___tracy_emit_gpu_context_name_serial({ q->context, name, uint16_t(strlen(name)) });
    queue->pTracy = q;
}
void mooringTracyMetalExitQueue(TFQueue* queue) { tf_delete(static_cast<Queue*>(queue->pTracy)); }
void mooringTracyMetalInitCmd(TFCmd* cmd)
{
    if (!cmd->pQueue->pTracy)
        return;
    auto* c = tf_new(Command);
    c->queue = static_cast<Queue*>(cmd->pQueue->pTracy);
    cmd->pTracy = c;
}
void mooringTracyMetalExitCmd(TFCmd* cmd)
{
    auto* c = static_cast<Command*>(cmd->pTracy);
    if (!c)
        return;
    collect(c->queue);
    if (c->pending)
    {
        // Forge normally waits before destroying commands. If it abandons one,
        // remove the profiling record without imposing an extra GPU wait.
        std::lock_guard lock(c->queue->publish);
        Command*        previous = nullptr;
        for (auto* p = c->queue->head; p; previous = p, p = p->next)
            if (p == c)
            {
                if (previous)
                    previous->next = c->next;
                else
                    c->queue->head = c->next;
                if (c->queue->tail == c)
                    c->queue->tail = previous;
                dropped += c->count;
                break;
            }
    }
    tf_delete(c);
}
void mooringTracyMetalBeginCmd(TFCmd* cmd)
{
    auto* c = static_cast<Command*>(cmd->pTracy);
    if (!c)
        return;
    collect(c->queue);
    c->recording = recording() && !c->pending;
    c->current = -1;
    if (!c->recording)
        return;
    c->count = 0;
    if (!c->samples)
    {
        auto* desc = [[MTLCounterSampleBufferDescriptor alloc] init];
        desc.counterSet = c->queue->counters;
        desc.sampleCount = MaxEncoders * 2;
        desc.storageMode = MTLStorageModeShared;
        desc.label = @"Tracy encoder timestamps";
        NSError* error;
        c->samples = [c->queue->device newCounterSampleBufferWithDescriptor:desc error:&error];
        if (!c->samples)
        {
            TracyMessageL("Cannot allocate Tracy Metal timestamp buffer.");
            c->recording = false;
            return;
        }
    }
    c->connection = connection();
}
void mooringTracyMetalCommit(TFCmd* cmd)
{
    auto* c = static_cast<Command*>(cmd->pTracy);
    if (!c)
    {
        [cmd->pCommandBuffer commit];
        return;
    }
    std::lock_guard lock(c->queue->publish);
    if (c->recording)
    {
        c->pending = cmd->pCommandBuffer;
        if (c->queue->tail)
            c->queue->tail->next = c;
        else
            c->queue->head = c;
        c->queue->tail = c;
    }
    [cmd->pCommandBuffer commit];
}
void mooringTracyMetalRender(TFCmd* cmd, MTLRenderPassDescriptor* desc)
{
    if (auto* c = beginEncoder(cmd, "Render"))
    {
        auto& e = c->encoders[c->current];
        auto* a = desc.sampleBufferAttachments[0];
        if (a.sampleBuffer)
        {
            // Apple GPUs write one timestamp attachment. Share Forge's samples
            // when present; a second attachment would silently disable its data.
            e.samples = a.sampleBuffer;
            e.startIndex = a.startOfVertexSampleIndex;
            e.endIndex = a.endOfFragmentSampleIndex;
        }
        else
        {
            a.sampleBuffer = c->samples;
            a.startOfVertexSampleIndex = e.startIndex;
            a.endOfVertexSampleIndex = MTLCounterDontSample;
            a.startOfFragmentSampleIndex = MTLCounterDontSample;
            a.endOfFragmentSampleIndex = e.endIndex;
        }
        for (unsigned i = 0; i < 8; ++i)
            if (desc.colorAttachments[i].loadAction == MTLLoadActionClear)
                c->encoders[c->current].work = 1;
        if (desc.depthAttachment.loadAction == MTLLoadActionClear)
            c->encoders[c->current].work = 1;
    }
}
void mooringTracyMetalCompute(TFCmd* cmd, MTLComputePassDescriptor* desc)
{
    if (auto* c = beginEncoder(cmd, "Compute"))
    {
        auto& e = c->encoders[c->current];
        auto* a = desc.sampleBufferAttachments[0];
        if (a.sampleBuffer)
        {
            e.samples = a.sampleBuffer;
            e.startIndex = a.startOfEncoderSampleIndex;
            e.endIndex = a.endOfEncoderSampleIndex;
        }
        else
        {
            a.sampleBuffer = c->samples;
            a.startOfEncoderSampleIndex = e.startIndex;
            a.endOfEncoderSampleIndex = e.endIndex;
        }
    }
}
id<MTLBlitCommandEncoder> mooringTracyMetalBlit(TFCmd* cmd)
{
    auto* desc = [MTLBlitPassDescriptor blitPassDescriptor];
    if (auto* c = beginEncoder(cmd, "Transfer"))
    {
        auto* a = desc.sampleBufferAttachments[0];
        a.sampleBuffer = c->samples;
        a.startOfEncoderSampleIndex = c->current * 2;
        a.endOfEncoderSampleIndex = c->current * 2 + 1;
        c->encoders[c->current].work = 1;
    }
    return [cmd->pCommandBuffer blitCommandEncoderWithDescriptor:desc];
}
void mooringTracyMetalEndEncoder(TFCmd* cmd)
{
    auto* c = static_cast<Command*>(cmd->pTracy);
    if (!c || c->current < 0)
        return;
    c->encoders[c->current].cpuEnd = tracy::Profiler::GetTime();
    c->current = -1;
}
void mooringTracyMetalWork(TFCmd* cmd, const char* kind, uint64_t count)
{
    auto* c = static_cast<Command*>(cmd->pTracy);
    if (!c || c->current < 0)
        return;
    auto& e = c->encoders[c->current];
    e.work += count;
    if (e.pipeline == cmd->pBoundPipeline)
        return;
    NSString* name = nil;
    if (cmd->pBoundPipeline)
        name = cmd->pBoundPipeline->mType == TF_PIPELINE_TYPE_COMPUTE ? cmd->pBoundPipeline->pComputePipelineState.label
                                                                      : cmd->pBoundPipeline->pRenderPipelineState.label;
    if (name.length)
    {
        if (!e.pipeline && !e.namedPass)
            snprintf(e.name, sizeof(e.name), "%s / %s", kind, name.UTF8String);
        else
        {
            const size_t used = strlen(e.name);
            snprintf(e.name + used, sizeof(e.name) - used, "%s%s", e.pipeline ? " + " : " / ", name.UTF8String);
        }
    }
    e.pipeline = cmd->pBoundPipeline;
}

void mooringTracyMetalFrameImage(TFCmd* cmd, TFRenderTarget* target)
{
    auto* c = static_cast<Command*>(cmd->pTracy);
    MTRACY_PLOT("Metal / driver allocated bytes", cmd->pRenderer->pDevice.currentAllocatedSize);
    const unsigned interval = imageInterval.load(std::memory_order_relaxed);
    if (!interval || !c || !c->recording || frameIndex.load() % interval)
        return;
    MTRACY_ZONE_COLOR("Tracy / encode thumbnail", 0xBB6655);
    auto* q = c->queue;
    if (!q->thumbnail)
    {
        NSString*      source = @"#include <metal_stdlib>\nusing namespace metal;\n"
                                 "kernel void thumbnail(texture2d<float, access::sample> image [[texture(0)]], device uchar4* dst [[buffer(0)]],"
                                 "constant uint3& info [[buffer(1)]], uint2 pos [[thread_position_in_grid]]) {"
                                 "if(any(pos >= info.xy)) return; constexpr sampler s(coord::normalized, filter::linear);"
                                 "float3 c = image.sample(s,(float2(pos)+0.5)/float2(info.xy)).rgb;"
                                 "if(info.z) c=select(c*12.92,1.055*pow(max(c,0.0),float3(1.0/2.4))-0.055,c>0.0031308);"
                                 "dst[pos.y*info.x+pos.x]=uchar4(uchar3(saturate(c)*255.0+0.5),255); }";
        NSError*       error;
        id<MTLLibrary> library = [q->device newLibraryWithSource:source options:nil error:&error];
        q->thumbnail = [q->device newComputePipelineStateWithFunction:[library newFunctionWithName:@"thumbnail"] error:&error];
        if (!q->thumbnail)
        {
            TracyMessageL("Tracy thumbnail shader failed to compile.");
            imageInterval = 0;
            return;
        }
    }
    const unsigned w = 320, h = std::clamp(unsigned(double(target->mHeight) * w / target->mWidth) & ~3u, 4u, 512u);
    if (c->width != w || c->height != h)
    {
        c->image = [q->device newBufferWithLength:w * h * 4 options:MTLResourceStorageModeShared];
        c->width = w;
        c->height = h;
    }
    if (!c->image)
        return;
    util_end_current_encoders(cmd, true);
    auto* desc = [MTLComputePassDescriptor computePassDescriptor];
    mooringTracyMetalCompute(cmd, desc);
    id<MTLComputeCommandEncoder> encoder = [cmd->pCommandBuffer computeCommandEncoderWithDescriptor:desc];
    cmd->pComputeEncoder = encoder;
    util_barrier_required(cmd, TF_QUEUE_TYPE_COMPUTE);
    const auto     format = target->pTexture->pTexture.pixelFormat;
    const uint32_t info[4] = { w, h, uint32_t(format == MTLPixelFormatBGRA8Unorm_sRGB || format == MTLPixelFormatRGBA8Unorm_sRGB), 0 };
    [encoder setComputePipelineState:q->thumbnail];
    [encoder setTexture:target->pTexture->pTexture atIndex:0];
    [encoder setBuffer:c->image offset:0 atIndex:0];
    [encoder setBytes:info length:sizeof(info) atIndex:1];
    [encoder dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
    if (c->current >= 0)
    {
        auto& e = c->encoders[c->current];
        e.work = 1;
        snprintf(e.name, sizeof(e.name), "Tracy thumbnail");
    }
    util_end_current_encoders(cmd, true);
    c->imageFrame = frameIndex.load();
    c->hasImage = true;
}
