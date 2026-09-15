#include "PlatformProbe.h"
#include "Interaction.h"
#include "Water/OceanRenderer.h"
#include "Water/WaterProfile.h"
#include "Tools/Mooring/TracyMetal.h"
#include "Water/PhysicsConfig.h"
#include "Common_3/Application/Interfaces/IApp.h"
#include <algorithm>
#include "Common_3/Application/Interfaces/IFont.h"
#include "Common_3/Application/Interfaces/IUI.h"
#include "Common_3/Application/Interfaces/IScreenshot.h"
#include "Common_3/Utilities/Interfaces/IToolFileSystem.h"
#include "Common_3/Utilities/Interfaces/ITime.h"
#include <cstdio>
#include <cstring>
#include "Common_3/Application/Interfaces/IProfiler.h"
#include "Common_3/Graphics/Interfaces/IGraphics.h"
#include "Common_3/Resources/ResourceLoader/Interfaces/IResourceLoader.h"
#include "Common_3/Utilities/RingBuffer.h"
#include "Common_3/Utilities/Interfaces/ILog.h"
#include "Common_3/OS/Interfaces/IInput.h"
#include "Common_3/Utilities/Interfaces/IFileSystem.h"
#include "Common_3/Utilities/Interfaces/IMemory.h"

static bool validateResources()
{
    MTRACY_ZONE("validateResources");
    struct RequiredResource { TFResourceDirectory directory; const char* path; };
    const RequiredResource required[] = {
        { TF_RD_TEXTURES, "FoamLace.ktx" },
        { TF_RD_SHADER_BINARIES, "MACOS/copy.comp.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/copy_array.comp.metal" },
        { TF_RD_GPU_CONFIG, "gpu.cfg" },
        { TF_RD_OTHER_FILES, "gpu.data" },
        { TF_RD_OTHER_FILES, "physics.ini" },
        { TF_RD_FONTS, "AtkinsonHyperlegible-Regular.msdf" },
        { TF_RD_SHADER_BINARIES, "MACOS/nuklear.vert.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/nuklear_SAMPLE_COUNT_1.frag.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/nuklear_SAMPLE_COUNT_2.frag.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/nuklear_SAMPLE_COUNT_4.frag.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/nuklear_SAMPLE_COUNT_8.frag.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/nuklear_SAMPLE_COUNT_16.frag.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/swapchain_draw.vert.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/swapchain_draw.frag.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/textured_mesh.vert.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/textured_mesh.frag.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/vr_marker.vert.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/vr_marker.frag.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/font.vert.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/font.frag.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/font_3D.frag.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/platform_probe.comp.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/marina.vert.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/marina.frag.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/ocean_spectrum.comp.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/ocean_fft.comp.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/ocean_surface.comp.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/ocean_packets.comp.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/water.vert.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/water.frag.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/water_light.vert.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/water_light.frag.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/ocean_mip.comp.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/water_effects.comp.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/water_sky.comp.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/water_sky_filter.comp.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/cloud_noise.comp.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/water_spray.vert.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/water_spray.frag.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/marina_post.vert.metal" },
        { TF_RD_SHADER_BINARIES, "MACOS/marina_post.frag.metal" },
    };
    bool valid = true;
    for (const auto& resource : required)
    {
        TFFileStream stream = {};
        if (!fsOpenStreamFromPath(resource.directory, resource.path, TF_FM_READ, &stream))
        {
            LOGF(eERROR, "Missing bundled resource in directory %u: %s. Rebuild the MooringSimulator target.",
                 (unsigned)resource.directory, resource.path);
            valid = false;
            continue;
        }
        valid &= fsGetStreamFileSize(&stream) > 0;
        fsCloseStream(&stream);
    }
    return valid;
}

static TFRenderer* gRenderer;
static TFQueue* gQueue;
static TFSwapChain* gSwapchain;
static GpuCmdRing gCommands;
static TFSemaphore* gAcquired[2];
static TFFont* gFont;
static ProfileToken gGpuProfile;
static mooring::Interaction gGame = {};
static mooring::Scene* gScene;
static mooring::OceanRenderer* gWater;
static unsigned gFFTSize;

class MooringSimulator final : public IApp
{
    int qaScenario=-1, qaEnd=11, qaFrame=0, qaView=0, qaInspect=0, qaBenchmarkFrames=0, qaMotionFrames=360;
    bool qaUltra=false, qaReload=false, qaMotion=false, qaStill=false, qaLow=false, qaResize=false, qaWaterline=false;
    bool qaProfile=false;
    mooring::WaterProfile waterProfile{};
    uint64_t updateWork=0, waitWork=0, drawWork=0;
    unsigned resizeWidth=0, resizeHeight=0;
    float qaSun=129.2894f, qaSunHeight=-1, qaRudder=0;
    void followQACamera(const mooring::Snapshot& state) {
        auto& camera=gGame.camera;
        camera.focus=camera.target=state.position;
        if(qaWaterline) {
            camera.elevation=0;
            auto eye=mooring::cameraEye(camera);
            camera.focus.y=camera.target.y=mooring::sampleOcean(mooring::worldOcean(gGame.world),eye.x,eye.z).height+1.2f;
        }
    }
    void updateWaterQA() {
    MTRACY_ZONE("updateWaterQA");
        if(qaReload) {
            if(qaFrame==810) { LOGF(eINFO,"Water graphics reload sequence complete"); requestShutdown(); return; }
            if(qaFrame%90==0) {
                unsigned preset=(qaFrame/90)%3;
                gGame.seaLab.look.preset(preset); gGame.seaLab.rebuild=true;
                LOGF(eINFO,"Graphics reload cycle %d, preset %u",qaFrame/90,preset);
            }
            mooring::advance(gGame.world,1.0f/30,1); ++qaFrame; return;
        }
        static const int views[][28]={{0,4,7,8,9,11,12,13,27,32,34,35,36,37,38,39,40,41,-1},{0,1,2,3,4,8,10,16,17,22,27,32,34,35,36,37,38,39,40,41,-1},
            {0,2,4,8,10,13,14,18,19,20,21,22,26,27,28,33,34,35,36,37,38,39,40,41,-1},{0,2,5,12,15,17,18,23,26,28,29,33,34,35,36,37,38,39,40,41,-1},{0,5,6,7,8,9,11,13,27,29,34,35,36,37,38,39,40,41,-1},
            {0,1,10,14,20,21,24,25,30,31,-1},{0,3,12,17,37,-1},{0,3,12,17,37,-1},{0,1,2,3,4,10,12,16,18,22,26,28,32,33,34,35,36,37,38,39,40,41,42,43,44,-1}};
        static const char* names[]={"calm","rough","storm","wake","dock","light","calm-low","glass","whitecaps","motion","catamaran"};
        if(qaScenario>=qaEnd) { LOGF(eINFO,"Water visual capture sequence complete"); requestShutdown(); return; }
        if(qaFrame==0) {
            mooringTracyScene(names[qaScenario]);
            if(qaScenario==10) { mooring::destroyWorld(gGame.world); gGame.world=mooring::createWorld(true); gGame.catamaran=true; }
            auto preset=qaScenario==0 || qaScenario==6?mooring::SeaPreset::Calm:qaScenario==2?mooring::SeaPreset::Storm:
                qaScenario==7?mooring::SeaPreset::Glass:qaScenario==8?mooring::SeaPreset::Whitecaps:qaScenario>=9?mooring::SeaPreset::Breeze:mooring::SeaPreset::Rough;
            mooring::setSeaPreset(gGame.seaLab,gGame.world,preset);
            mooring::SeaState sea=gGame.seaLab.edit; sea.current={};
            if(qaScenario==3) { sea.windSpeed=3; sea.windWaveHeight=.12f; sea.swellHeight=.15f; }
            if(qaScenario==4) { sea.depth=3; sea.windSpeed=4; sea.windWaveHeight=.3f; sea.swellHeight=.15f; }
            mooring::setEnvironment(gGame.world,sea);
            mooring::InitialConditions initial; initial.position={qaScenario==4?5.0f:-25.0f,0,0};
            if(qaScenario==3) initial.velocity={0,0,3.8f};
            mooring::resetWorld(gGame.world,&initial);
            if(qaScenario==3) mooring::enqueue(gGame.world,{mooring::CommandType::Throttle,0,mooring::Station::Helm,0,.7f});
            if(qaScenario==10) for(unsigned engine=0;engine<2;++engine)
                mooring::enqueue(gGame.world,{mooring::CommandType::Throttle,0,mooring::Station::Helm,uint8_t(engine),.7f});
            if((qaScenario==3 || qaScenario==10) && qaRudder!=0)
                mooring::enqueue(gGame.world,{mooring::CommandType::Wheel,0,mooring::Station::Helm,0,qaRudder*.01745329252f});
            if(qaScenario==3 || qaScenario==4 || qaScenario==5) {
                gGame.seaLab.look.overcast=qaScenario==5?.5f:.12f;
                gGame.seaLab.look.rain=0;
            }
            gGame.seaLab.look.debugView=0; gGame.seaLab.look.foam=1;
            gGame.seaLab.look.sunElevation=qaScenario==5?9:44.59f;
            gGame.seaLab.look.sunAzimuth=qaSun;
            if(qaSunHeight>=3) gGame.seaLab.look.sunElevation=qaSunHeight;
            gGame.camera.elevation=qaScenario==1?8:32.6f; gGame.camera.distance=gGame.camera.targetDistance=qaScenario==4?16:28;
            if(qaScenario==5) { gGame.camera.elevation=5; gGame.camera.distance=gGame.camera.targetDistance=85; }
            if(qaScenario>=6 && qaScenario<=8) gGame.camera.elevation=qaScenario==8?6:4;
            if(qaScenario==9) gGame.camera.elevation=18;
            if(qaLow) gGame.camera.elevation=12;
            qaView=0;
        }
        int warmup=qaScenario==3 || qaScenario==10?600:150;
        if(qaFrame<warmup || qaBenchmarkFrames>0 || (qaScenario==9 && !qaMotion && !qaStill && qaFrame<warmup+60)) mooring::advance(gGame.world,1.0f/30,1);
        auto state=mooring::snapshot(gGame.world);
        followQACamera(state);
        if(qaFrame==warmup) {
            float minimum=0,maximum=0;
            auto* ocean=mooring::worldOcean(gGame.world);
            for(int z=-24;z<=24;++z) for(int x=-16;x<=16;++x) {
                float height=mooring::sampleWavePackets(ocean,state.position.x+x*.5f,state.position.z+z*.5f).height;
                minimum=std::min(minimum,height); maximum=std::max(maximum,height);
            }
            LOGF(eINFO,"QA %s: speed %.2f kn, local height %.3f to %.3f m, packets %u, dropped %u",
                 names[qaScenario],state.speedKnots,minimum,maximum,mooring::activeWavePackets(ocean),mooring::droppedWavePackets(ocean));
        }
        if(qaBenchmarkFrames>0 && qaFrame>=warmup) {
            if(qaFrame==warmup+qaBenchmarkFrames) { LOGF(eINFO,"Water benchmark complete: %d measured beauty frames, no captures",qaBenchmarkFrames); requestShutdown(); }
            ++qaFrame; return;
        }
        if(qaResize && qaFrame>=warmup) {
            static const unsigned sizes[][2]={{1920,1200},{1281,721},{961,1281},{2560,1440},{1920,1200}};
            int stage=(qaFrame-warmup)/40, offset=(qaFrame-warmup)%40;
            if(stage==5) { LOGF(eINFO,"Water resize capture sequence complete"); requestShutdown(); return; }
            if(offset==0) { resizeWidth=sizes[stage][0]; resizeHeight=sizes[stage][1]; }
            if(offset>=12 && offset<24 && offset%3==0) {
                static const int views[]={0,8,11,26};
                int view=views[(offset-12)/3]; gGame.seaLab.look.debugView=view;
                char name[100]; snprintf(name,sizeof name,"resize-%d-%dx%d-%02d",stage,mSettings.mWidth,mSettings.mHeight,view);
                requestScreenshotCapture(name);
            }
            ++qaFrame; return;
        }
        if((qaMotion || qaStill) && qaFrame>=warmup) {
            if(qaFrame>=warmup+(qaStill?30:qaMotionFrames)) { qaScenario++; qaFrame=0; return; }
            if(!qaStill) mooring::advance(gGame.world,1.0f/30,1);
            followQACamera(mooring::snapshot(gGame.world));
            gGame.seaLab.look.debugView=qaInspect;
            if(qaStill || (qaFrame-warmup)%2==0) {
                char name[100];
                if(qaStill) snprintf(name,sizeof name,"%s-%s-still-%02d-%02d",qaUltra?"ultra":"balanced",names[qaScenario],qaInspect,qaFrame-warmup);
                else snprintf(name,sizeof name,"%s-%s-motion-%02d",qaUltra?"ultra":"balanced",names[qaScenario],(qaFrame-warmup)/2);
                requestScreenshotCapture(name);
            }
            ++qaFrame; return;
        }
        if(qaScenario==9 && qaFrame>=warmup) {
            if(qaFrame<warmup+60 && (qaFrame-warmup)%2==0) {
                char name[100]; snprintf(name,sizeof name,"%s-motion-%02d",qaUltra?"ultra":"balanced",(qaFrame-warmup)/2);
                requestScreenshotCapture(name);
            }
            if(qaFrame<warmup+60) gGame.camera.azimuth+=.1f;
            else if((qaFrame-warmup-60)%3==0) {
                static const int checks[]={0,2,3,4,5,6,10,11,12,-1};
                int view=checks[qaView++];
                if(view<0) { qaScenario++; qaFrame=0; return; }
                gGame.seaLab.look.debugView=view;
                char name[100]; snprintf(name,sizeof name,"%s-motion-check-%02d",qaUltra?"ultra":"balanced",view);
                requestScreenshotCapture(name);
            }
            qaFrame++; return;
        }
        if(qaFrame>=warmup && (qaFrame-warmup)%3==0) {
            int view=views[qaScenario==10?3:qaScenario][qaView++];
            if(view<0) { qaScenario++; qaFrame=0; return; }
            gGame.seaLab.look.debugView=view;
            char name[100]; snprintf(name,sizeof name,"%s-%s-%02d",qaUltra?"ultra":"balanced",names[qaScenario],view);
            requestScreenshotCapture(name);
        }
        qaFrame++;
    }
public:
    MooringSimulator()
    {
        mSettings.mWidth = 1920;
        mSettings.mHeight = 1200;
        mSettings.mShowPlatformUI = false;
        mSettings.mVSyncEnabled = true;
        mSettings.mFrameMaxCount = 2;
    }
    bool Init() override
    {
    MTRACY_ZONE("Init");
        if (!validateResources()) return false;
        int requestedScene=-1;
        for(int i=1;i<argc;i++) {
            if(strcmp(argv[i],"--water-qa")==0) qaScenario=0;
            if(strcmp(argv[i],"--water-qa-ultra")==0) { qaScenario=0; qaUltra=true; }
            if(strcmp(argv[i],"--water-qa-reload")==0) { qaScenario=11; qaReload=true; }
            if(strcmp(argv[i],"--water-qa-motion")==0) qaMotion=true;
            if(strcmp(argv[i],"--water-qa-still")==0) qaStill=true;
            if(strcmp(argv[i],"--water-qa-resize")==0) { qaResize=true; qaScenario=4; }
            sscanf(argv[i],"--water-qa-sun=%f",&qaSun);
            sscanf(argv[i],"--water-qa-rudder=%f",&qaRudder);
            sscanf(argv[i],"--water-qa-sun-height=%f",&qaSunHeight);
            if(strcmp(argv[i],"--water-qa-low")==0) qaLow=true;
            if(strcmp(argv[i],"--water-qa-waterline")==0) qaWaterline=true;
            if(strcmp(argv[i],"--water-profile")==0) qaProfile=true;
#ifdef TRACY_ENABLE
            if(strncmp(argv[i],"--tracy-images=",15)==0) mooringTracyMetalImages(unsigned(std::max(0,atoi(argv[i]+15))));
            if(strcmp(argv[i],"--tracy-wait")==0) {
                LOGF(eINFO,"Waiting up to 30 seconds for a Tracy capture on localhost:8086");
                for(unsigned wait=0; wait<3000 && !TracyCIsConnected; ++wait) threadSleep(10);
            }
#endif
            if(strcmp(argv[i],"--benchmark-unthrottled")==0) mSettings.mVSyncEnabled=false;
            int benchmark=0;
            if(sscanf(argv[i],"--water-benchmark=%d",&benchmark)==1 && benchmark>0) {
                qaBenchmarkFrames=std::clamp(benchmark,300,18000); if(qaScenario<0) qaScenario=2;
            }
            int scene=0;
            if(sscanf(argv[i],"--water-qa-scene=%d",&scene)==1 && scene>=0 && scene<11) requestedScene=scene;
            int view=0;
            if(sscanf(argv[i],"--water-qa-inspect=%d",&view)==1 && view>=0 && view<45) qaInspect=view;
            int seconds=0;
            if(sscanf(argv[i],"--water-qa-seconds=%d",&seconds)==1 && seconds>0 && seconds<=60) qaMotionFrames=seconds*30;
        }
        mooringTracyAppInfo(GetName());
        if(requestedScene>=0) { qaScenario=requestedScene; qaEnd=requestedScene+1; }
        TFRendererDesc renderer = {};
        initGPUConfig(nullptr);
        initRenderer(GetName(), &renderer, &gRenderer);
        if (!gRenderer) return false;
        TFQueueDesc queue = {};
        queue.mType = TF_QUEUE_TYPE_GRAPHICS;
        queue.mFlag = TF_QUEUE_FLAG_INIT_MICROPROFILE;
        initQueue(gRenderer, &queue, &gQueue);
        GpuCmdRingDesc commands = {};
        commands.pQueue = gQueue;
        commands.mPoolCount = 2;
        commands.mCmdPerPoolCount = 1;
        commands.mAddSyncPrimitives = true;
        initGpuCmdRing(gRenderer, &commands, &gCommands);
        for (auto& semaphore : gAcquired) initSemaphore(gRenderer, &semaphore);
        TFResourceLoaderDesc loader = { 8 * TF_MB, 2, false };
        initResourceLoaderInterface(gRenderer, &loader);
        if (!runPlatformProbe(gRenderer, gQueue)) return false;
        TFFontSystemDesc fonts = {};
        fonts.pRenderer = gRenderer;
        fonts.mFrameMaxCount = 2;
        fonts.pFrameIdx = &mSettings.mFrameIdx;
        initFontSystem(&fonts);
        TFFontDesc font = {};
        font.pFontPath = "AtkinsonHyperlegible-Regular.msdf";
        font.pFontName = "Atkinson Hyperlegible";
        font.mScaleMultiplier = 1.0f;
        font.mFlags = TF_FONT_ASCII | TF_FONT_ATLAS_AUTO_RESOLUTION_ON_INIT;
        gFont = addFont(&font);
        if (!gFont) return false;
        TFUserInterfaceDesc ui = {};
        ui.pRenderer = gRenderer;
        ui.pFont = gFont;
        ui.mFontHeight = 18.0f;
        ui.pFrameIdx = &mSettings.mFrameIdx;
        ui.mFrameMaxCount = 2;
        initUserInterface(&ui);
        uiSetActiveSWL(false);
        uiSetAutoSwitchSWL(false);
        TFProfilerDesc profiler = {};
        profiler.pRenderer = gRenderer;
        initProfiler(&profiler);
        gGpuProfile = initGpuProfiler(gRenderer, gQueue, "Marina");
        if(qaProfile) {
            const char* names[]={"FFT","Effects","Sky","Geometry","Surface","Post"};
            for(unsigned i=0;i<mooring::WaterPassCount;++i) waterProfile.passes[i]=initGpuProfiler(gRenderer,gQueue,names[i]);
        }
        gGame.world = mooring::createWorld();
        mooring::SeaState environment;
        unsigned fftResolution=256;
        if(!mooring::loadPhysicsConfig(environment,fftResolution)) { LOGF(eERROR,"Check Data/physics.ini for invalid settings."); return false; }
        mooring::setEnvironment(gGame.world,environment);
        gGame.difficulty=mooring::Difficulty::Advanced;
        if(qaScenario>=0) { gGame.seaLab.look.preset(qaUltra?2:1); fftResolution=gGame.seaLab.look.fftSize; }
        gFFTSize=fftResolution; gGame.seaLab.look.fftSize=int(fftResolution);
        gWater = mooring::createOceanRenderer(gRenderer,fftResolution);
        if (!mooring::verifyOceanGPU(gWater,gQueue)) return false;
        gScene = mooring::createScene(gRenderer);
        fsCreateDirectory(TF_RD_SCREENSHOTS,"",true);
        initScreenshotCapturer(gRenderer,gQueue,GetName());
        extern bool gCaptureCursorOnMouseDown;
        gCaptureCursorOnMouseDown = false;
        return true;
    }
    void Exit() override
    {
    MTRACY_ZONE("Exit");
        mooringTracyScene(nullptr);
        exitScreenshotCapturer();
        mooring::destroyScene(gScene);
        mooring::destroyOceanRenderer(gWater);
        mooring::destroyWorld(gGame.world);
        exitProfiler();
        exitUserInterface();
        removeFont(gFont);
        exitFontSystem();
        exitResourceLoaderInterface(gRenderer);
        for (auto* semaphore : gAcquired) exitSemaphore(gRenderer, semaphore);
        exitGpuCmdRing(gRenderer, &gCommands);
        exitQueue(gRenderer, gQueue);
        exitRenderer(gRenderer);
        exitGPUConfig();
    }
    bool Load(TFReloadDesc*) override
    {
    MTRACY_ZONE("Load");
        TFSwapChainDesc swap = {};
        swap.mWindowHandle = pWindow->handle;
        swap.mPresentQueueCount = 1;
        swap.ppPresentQueues = &gQueue;
        swap.mWidth = mSettings.mWidth;
        swap.mHeight = mSettings.mHeight;
        swap.mImageCount = getRecommendedSwapchainImageCount(gRenderer, &pWindow->handle);
        swap.mColorFormat = getSupportedSwapchainFormat(gRenderer, &swap, TF_COLOR_SPACE_SDR_SRGB);
        swap.mColorSpace = TF_COLOR_SPACE_SDR_SRGB;
        swap.mEnableVsync = mSettings.mVSyncEnabled;
        addSwapChain(gRenderer, &swap, &gSwapchain);
        if (!gSwapchain) return false;
        TFUserInterfaceLoadDesc ui = {};
        ui.mColorFormat = swap.mColorFormat;
        ui.mWidth = ui.mDisplayWidth = mSettings.mWidth;
        ui.mHeight = ui.mDisplayHeight = mSettings.mHeight;
        loadUserInterface(&ui);
        TFFontSystemLoadDesc fonts = {};
        fonts.mWidth = mSettings.mWidth;
        fonts.mHeight = mSettings.mHeight;
        fonts.mColorFormat = swap.mColorFormat;
        loadFontSystem(&fonts);
        loadProfilerUI(mSettings.mWidth, mSettings.mHeight);
        if(gFFTSize!=unsigned(gGame.seaLab.look.fftSize)) {
            mooring::destroyOceanRenderer(gWater); gFFTSize=unsigned(gGame.seaLab.look.fftSize);
            gWater=mooring::createOceanRenderer(gRenderer,gFFTSize);
            if(!mooring::verifyOceanGPU(gWater,gQueue)) return false;
        }
        mooring::loadScene(gScene, swap.mColorFormat, mSettings.mWidth, mSettings.mHeight,gGame.seaLab.look);
        mooring::loadOceanRenderer(gWater,TinyImageFormat_R16G16B16A16_SFLOAT);
        mooring::connectSceneWater(gScene,gWater);
        waitForAllResourceLoads();
        uint64_t used=0,allocated=0; calculateMemoryUse(gRenderer,&used,&allocated);
#ifdef ENABLE_MEMORY_TRACKING
        LOGF(eINFO,"Native memory: Forge CPU %.2f MiB, GPU resources %.2f MiB, GPU heaps %.2f MiB; ocean buffers %.2f MiB",
             memGetStatistics().totalReportedMemory/double(TF_MB),used/double(TF_MB),allocated/double(TF_MB),mooring::oceanGPUBytes(gWater)/double(TF_MB));
#else
        LOGF(eINFO,"Native memory: GPU resources %.2f MiB, GPU heaps %.2f MiB; ocean buffers %.2f MiB (Forge CPU tracker disabled)",
             used/double(TF_MB),allocated/double(TF_MB),mooring::oceanGPUBytes(gWater)/double(TF_MB));
#endif
        return true;
    }
    void Unload(TFReloadDesc*) override
    {
    MTRACY_ZONE("Unload");
        waitQueueIdle(gQueue);
        mooring::unloadOceanRenderer(gWater);
        mooring::unloadScene(gScene);
        unloadProfilerUI();
        unloadFontSystem();
        unloadUserInterface();
        removeSwapChain(gRenderer, gSwapchain);
    }
    void Update(float dt) override
    {
    MTRACY_ZONE("Update");
        MTRACY_PLOT("Frame / dt (ms)", dt*1000);
        MTRACY_PLOT("View / width", mSettings.mWidth);
        MTRACY_PLOT("View / height", mSettings.mHeight);
        MTRACY_PLOT("View / diagnostic", gGame.seaLab.look.debugView);
        const uint64_t started=qaProfile?getUSec(true):0;
        // Capture fixtures and normal interaction share the drawable size.
        // Updating this only at fixture creation left refraction, reflection,
        // ray reconstruction and particles using the previous aspect ratio.
        gGame.camera.width=mSettings.mWidth; gGame.camera.height=mSettings.mHeight;
        if(qaScenario>=0) updateWaterQA();
        else mooring::updateInteraction(gGame, dt, mSettings.mWidth, mSettings.mHeight);
        if(gGame.seaLab.capture) {
            gGame.seaLab.capture=false;
            char name[96]; snprintf(name,sizeof name,"view-%02d-frame-%llu",gGame.seaLab.look.debugView,(unsigned long long)mSettings.mFrames);
            requestScreenshotCapture(name);
        }
        if(gGame.seaLab.rebuild) {
            gGame.seaLab.rebuild=false;
            TFReloadDesc reload={TF_RELOAD_TYPE_RENDERTARGET}; requestReload(&reload);
        }
        if(qaProfile) updateWork+=getUSec(true)-started;
        if(mSettings.mFrames && mSettings.mFrames%300==0) {
#ifdef ENABLE_MEMORY_TRACKING
            const auto& stats=mooring::statistics(gGame.world);
            LOGF(eINFO,"Water run: %llu frames, interval %.2f ms, simulation allocations %llu, packet overflow %u, CPU %.2f MiB",
                 (unsigned long long)mSettings.mFrames,getCpuAvgFrameTime(),stats.steadyStepAllocations,
                 mooring::droppedWavePackets(mooring::worldOcean(gGame.world)),memGetStatistics().totalReportedMemory/double(TF_MB));
#else
            LOGF(eINFO,"Water run: %llu frames, interval %.2f ms, packet overflow %u (Forge CPU tracker disabled)",
                 (unsigned long long)mSettings.mFrames,getCpuAvgFrameTime(),mooring::droppedWavePackets(mooring::worldOcean(gGame.world)));
#endif
            if(qaProfile) LOGF(eINFO,"Water pass work (ms): FFT %.2f, effects %.2f, sky %.2f, geometry %.2f, surface %.2f, post %.2f",
                getGpuProfileAvgTime(waterProfile.passes[0]),getGpuProfileAvgTime(waterProfile.passes[1]),getGpuProfileAvgTime(waterProfile.passes[2]),
                getGpuProfileAvgTime(waterProfile.passes[3]),getGpuProfileAvgTime(waterProfile.passes[4]),getGpuProfileAvgTime(waterProfile.passes[5]));
            else LOGF(eINFO,"GPU encoder work: %.2f ms",getGpuProfileAvgTime(gGpuProfile));
            if(qaProfile) {
                LOGF(eINFO,"Water CPU work (ms): update %.2f, drawable/fence wait %.2f, encode/present %.2f",
                     updateWork/300000.0,waitWork/300000.0,drawWork/300000.0);
                updateWork=waitWork=drawWork=0;
            }
        }
    }
    void Draw() override
    {
    MTRACY_ZONE("Draw");
        uint64_t started=qaProfile?getUSec(true):0;
        uint32_t image = 0;
        acquireNextImage(gRenderer, gSwapchain, gAcquired[mSettings.mFrameIdx], nullptr, &image);
        GpuCmdRingElement frame = getNextGpuCmdRingElement(&gCommands, true, 1);
        waitForFences(gRenderer, 1, &frame.pFence);
        if(qaProfile) { const uint64_t ready=getUSec(true); waitWork+=ready-started; started=ready; }
        resetCmdPool(gRenderer, frame.pCmdPool);
        TFCmd* cmd = frame.pCmds[0];
        TFRenderTarget* target = gSwapchain->ppRenderTargets[image];
        beginCmd(cmd);
        const auto* profile=qaProfile?&waterProfile:nullptr;
        if(!profile) cmdBeginGpuFrameProfile(cmd, gGpuProfile);
        mooring::beginWaterPass(cmd,profile,mooring::WaterFFT);
        auto state=mooring::snapshot(gGame.world);
        mooring::computeOcean(gWater,cmd,mooring::worldOcean(gGame.world),mSettings.mFrameIdx,state.time,gGame.seaLab.look);
        mooring::endWaterPass(cmd,profile,mooring::WaterFFT);
        TFRenderTargetBarrier barrier = { target, TF_RESOURCE_STATE_PRESENT, TF_RESOURCE_STATE_RENDER_TARGET };
        cmdResourceBarrier(cmd, 0, nullptr, 0, nullptr, 1, &barrier);
        mooring::drawScene(gScene, cmd, target, mSettings.mFrameIdx, gGame.camera,
                           state, mooring::worldLayout(gGame.world),gWater,mooring::seaState(mooring::worldOcean(gGame.world)).level,
                           mooring::seaState(mooring::worldOcean(gGame.world)).depth,gGame.seaLab.look,profile);
        // The restored Metal backend supports one active stage-boundary query.
        // Keep a whole-frame query instead of nesting UI queries inside it.
        uiCmdDrawUserInterface(cmd, gSwapchain, target, PROFILE_INVALID_TOKEN);
        cmdBindRenderTargets(cmd, nullptr);
        barrier.mCurrentState = TF_RESOURCE_STATE_RENDER_TARGET;
        barrier.mNewState = TF_RESOURCE_STATE_PRESENT;
        cmdResourceBarrier(cmd, 0, nullptr, 0, nullptr, 1, &barrier);
        if(!profile) cmdEndGpuFrameProfile(cmd, gGpuProfile);
        mooringTracyMetalFrameImage(cmd, target);
        endCmd(cmd);
        FlushResourceUpdateDesc updates = {};
        flushResourceUpdates(&updates);
        TFSemaphore* waits[] = { updates.pOutSubmittedSemaphore, gAcquired[mSettings.mFrameIdx] };
        TFQueueSubmitDesc submit = {};
        submit.mCmdCount = 1;
        submit.ppCmds = &cmd;
        submit.pSignalFence = frame.pFence;
        submit.mWaitSemaphoreCount = 2;
        submit.ppWaitSemaphores = waits;
        submit.mSignalSemaphoreCount = 1;
        submit.ppSignalSemaphores = &frame.pSemaphore;
        queueSubmit(gQueue, &submit);
        if(isScreenshotCaptureRequested()) {
            TFScreenshotDesc capture={}; capture.pRenderTarget=target;
            capture.ppWaitSemaphores=&frame.pSemaphore; capture.mWaitSemaphoresCount=1;
            capture.mColorSpace=TF_COLOR_SPACE_SDR_SRGB; capture.discardAlpha=true;
            captureScreenshot(&capture);
        }
        TFQueuePresentDesc present = {};
        present.pSwapChain = gSwapchain;
        present.mIndex = image;
        present.mWaitSemaphoreCount = 1;
        present.ppWaitSemaphores = &frame.pSemaphore;
        present.mSubmitDone = true;
        queuePresent(gQueue, &present);
        flipProfiler();
        mooringTracyFrame();
        if(qaProfile) drawWork+=getUSec(true)-started;
        if(resizeWidth) {
            setWindowClientSize(pWindow,resizeWidth,resizeHeight);
            resizeWidth=resizeHeight=0;
        }
    }
    const char* GetName() override { return "MooringSimulator"; }
};
DEFINE_APPLICATION_MAIN(MooringSimulator)
