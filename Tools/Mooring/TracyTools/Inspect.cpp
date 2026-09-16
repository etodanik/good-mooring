#include "TracyFileRead.hpp"
#include "TracyWorker.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

struct Totals
{
    uint64_t count = 0;
    int64_t  ns = 0;
};
using Zones = std::map<std::string, Totals>;

bool writeImage(tracy::Worker& w, const tracy::FrameImage& image, const char* path)
{
    // Preserve Tracy's DXT1 bytes inside a standard DDS container.
    uint32_t header[32] = { 0x20534444, 124, 0x81007, image.h, image.w, uint32_t(image.w * image.h / 2) };
    header[19] = 32;
    header[20] = 4;
    header[21] = 0x31545844;
    header[27] = 0x1000;
    FILE* out = fopen(path, "wb");
    if (!out)
        return false;
    const bool ok =
        fwrite(header, sizeof(header), 1, out) == 1 && fwrite(w.UnpackFrameImage(image), size_t(image.w) * image.h / 2, 1, out) == 1;
    return fclose(out) == 0 && ok;
}

// Tracy stores loaded timelines either as packed values or short pointers.
template<class T, class F>
void each(const tracy::Vector<tracy::short_ptr<T>>& v, F&& f)
{
    if (v.is_magic())
        for (const auto& e : reinterpret_cast<const tracy::Vector<T>&>(v))
            f(e);
    else
        for (const auto& e : v)
            f(*e);
}
void cpu(const tracy::Worker& w, const tracy::Vector<tracy::short_ptr<tracy::ZoneEvent>>& v, Zones& totals, uint64_t& open)
{
    each(v,
         [&](const auto& e)
         {
             auto& t = totals[w.GetZoneName(e)];
             ++t.count;
             if (e.IsEndValid())
                 t.ns += e.End() - e.Start();
             else
             {
                 ++open;
                 printf("Open zone: %s, source %d, start %lld\n", w.GetZoneName(e), e.SrcLoc(), (long long)e.Start());
             }
             if (e.HasChildren())
                 cpu(w, w.GetZoneChildren(e.Child()), totals, open);
         });
}
void gpu(const tracy::Worker& w, const tracy::Vector<tracy::short_ptr<tracy::GpuEvent>>& v, Zones& totals, uint64_t& invalid,
         uint64_t& stagePairs)
{
    each(v,
         [&](const auto& e)
         {
             auto& t = totals[w.GetZoneName(e)];
             ++t.count;
             if (e.GpuStart() >= 0 && e.GpuEnd() >= e.GpuStart() && e.CpuEnd() >= e.CpuStart())
                 t.ns += e.GpuEnd() - e.GpuStart();
             else
                 ++invalid;
             if (e.Child() >= 0)
             {
                 const auto&            children = w.GetGpuChildren(e.Child());
                 const tracy::GpuEvent *vertex = nullptr, *fragment = nullptr;
                 const std::string      name = w.GetZoneName(e);
                 each(children,
                      [&](const auto& child)
                      {
                          if (child.GpuStart() < e.GpuStart() || child.GpuEnd() > e.GpuEnd())
                              ++invalid;
                          if (name + " / Vertex" == w.GetZoneName(child))
                              vertex = &child;
                          if (name + " / Fragment" == w.GetZoneName(child))
                              fragment = &child;
                      });
                 if (vertex || fragment)
                 {
                     if (vertex && fragment && children.size() == 2 && vertex->GpuStart() == e.GpuStart() &&
                         vertex->GpuEnd() <= fragment->GpuStart() && fragment->GpuEnd() == e.GpuEnd())
                         ++stagePairs;
                     else
                         ++invalid;
                 }
                 gpu(w, children, totals, invalid, stagePairs);
             }
         });
}
int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: mooring-tracy-inspect capture.tracy [thumbnail.dds] [--app] [--images] [--images-dir=PATH] [--messages] "
                        "[--clean-memory] [--gpu-stages]\n");
        return 2;
    }
    bool        requireApp = false, requireImages = false, cleanMemory = false, messages = false, requireStages = false;
    const char* thumbnail = nullptr;
    const char* imageDirectory = nullptr;
    for (int i = 2; i < argc; ++i)
    {
        if (strcmp(argv[i], "--app") == 0)
            requireApp = true;
        else if (strcmp(argv[i], "--images") == 0)
            requireImages = true;
        else if (strcmp(argv[i], "--clean-memory") == 0)
            cleanMemory = true;
        else if (strcmp(argv[i], "--messages") == 0)
            messages = true;
        else if (strcmp(argv[i], "--gpu-stages") == 0)
            requireStages = true;
        else if (strncmp(argv[i], "--images-dir=", 13) == 0)
            imageDirectory = argv[i] + 13;
        else if (!thumbnail && argv[i][0] != '-')
            thumbnail = argv[i];
        else
            return 2;
    }
    try
    {
        std::unique_ptr<tracy::FileRead> file(tracy::FileRead::Open(argv[1]));
        if (!file)
            return 2;
        tracy::Worker w(*file, tracy::EventType::All, false);
        printf("CPU zones: %llu\nGPU zones: %llu\nFrame images: %u\nMessages: %zu\nCallstack frames: %llu\n",
               (unsigned long long)w.GetZoneCount(), (unsigned long long)w.GetGpuZoneCount(), w.GetFrameImageCount(),
               size_t(w.GetMessages().size()), (unsigned long long)w.GetCallstackFrameCount());
        for (const auto& frame : w.GetFrames())
            printf("Frames [%s]: %zu\n", frame->name ? w.GetString(frame->name) : "Render", size_t(frame->frames.size()));
        for (const auto& info : w.GetAppInfo())
            printf("App info: %s\n", w.GetString(info));
        if (messages)
            for (const auto& message : w.GetMessages())
                printf("Message [%lld, severity %u]: %s\n", (long long)message->time, unsigned(message->severity),
                       w.GetString(message->ref));
        Zones    cpuTotals, gpuTotals;
        uint64_t open = 0, invalid = 0, stagePairs = 0;
        for (const auto& thread : w.GetThreadData())
        {
            printf("Thread [%s]: %llu zones\n", w.GetThreadName(thread->id), (unsigned long long)thread->count);
            cpu(w, thread->timeline, cpuTotals, open);
        }
        for (const auto& context : w.GetGpuData())
        {
            printf("GPU [%s]: %llu zones\n", w.GetString(context->name), (unsigned long long)context->count);
            for (const auto& thread : context->threadData)
                gpu(w, thread.second.timeline, gpuTotals, invalid, stagePairs);
        }
        printf("Open CPU zones: %llu\nInvalid GPU timings: %llu\n", (unsigned long long)open, (unsigned long long)invalid);
        printf("GPU render stage pairs: %llu\n", (unsigned long long)stagePairs);
        bool memoryBalanced = true;
        for (const auto& pool : w.GetMemNameMap())
        {
            memoryBalanced &= pool.second->active.empty();
            uint64_t stacks = 0;
            for (const auto& e : pool.second->data)
                stacks += e.CsAlloc() != 0;
            printf("Memory [%s]: %zu allocations, %zu frees, %zu active, %llu bytes, %llu stacks\n",
                   pool.first ? w.GetString(pool.first) : "default", size_t(pool.second->data.size()), size_t(pool.second->frees.size()),
                   size_t(pool.second->active.size()), (unsigned long long)pool.second->usage, (unsigned long long)stacks);
        }
        for (const auto& lock : w.GetLockMap())
            printf("Lock [%s]: %zu events, %zu threads, contended=%d\n", w.GetString(lock.second->customName),
                   size_t(lock.second->timeline.size()), lock.second->threadList.size(), lock.second->isContended);
        for (const auto& plot : w.GetPlots())
            printf("Plot [%s]: %zu samples, min=%.6f max=%.6f\n", w.GetString(plot->name), size_t(plot->data.size()), plot->min, plot->max);
        for (const auto& category : w.GetSections())
            printf("Sections [%s]: %zu\n", w.GetSectionCategoryDescription(category.first), size_t(category.second.size()));
        for (const auto& [name, t] : cpuTotals)
            printf("CPU %s: %llu, %.6f ms total\n", name.c_str(), (unsigned long long)t.count, t.ns / 1e6);
        for (const auto& [name, t] : gpuTotals)
            printf("GPU %s: %llu, %.6f ms total\n", name.c_str(), (unsigned long long)t.count, t.ns / 1e6);
        if (thumbnail && w.GetFrameImageCount())
        {
            const auto& image = *w.GetFrameImages()[w.GetFrameImageCount() / 2];
            if (!writeImage(w, image, thumbnail))
                return 2;
            printf("Exported image: %ux%u, frame %u\n", image.w, image.h, image.frameRef);
        }
        if (imageDirectory)
        {
            std::filesystem::create_directories(imageDirectory);
            for (const auto& image : w.GetFrameImages())
            {
                const auto path = std::filesystem::path(imageDirectory) / (std::to_string(image->frameRef) + ".dds");
                if (!writeImage(w, *image, path.c_str()))
                    return 2;
            }
        }
        const bool app = !requireApp || (cpuTotals.count("Update") && cpuTotals.count("Draw") && cpuTotals.count("Physics fixed step") &&
                                         cpuTotals.count("Vessel forces") && w.GetGpuZoneCount() > 0 && !w.GetPlots().empty() &&
                                         !w.GetSections().empty() && !w.GetLockMap().empty() && w.GetMemNameMap().size() >= 5);
        const bool valid = !invalid && w.GetZoneCount() > 0 && app && (!requireImages || w.GetFrameImageCount() > 0) &&
                           (!cleanMemory || memoryBalanced) && (!requireStages || stagePairs > 0);
        printf("Capture validation: %s\n", valid ? "PASS" : "FAIL");
        return valid ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        fprintf(stderr, "%s\n", e.what());
        return 2;
    }
}
