> Native mooring simulator: see [MOORING.md](MOORING.md) for builds, controls, physics, verification, and current limits.

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/The-Forge-White-BlackOutline.webp">
  <source media="(prefers-color-scheme: light)" srcset="https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/The-Forge-Black-WhiteOutline.webp">
  <img src="https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/The-Forge-White-BlackOutline.webp" width="216" height="92">
</picture>



The Forge Framework™ is a cross-platform programming framework supporting the following platforms:
- Windows 10/11 with DirectX 12 / DXR
- Steam Deck with min spec Vulkan 1.1 with VK_KHR_ray_query Ray Tracing API
- Android 10.0 or higher with min spec Vulkan 1.1 and VK_KHR_ray_query support on higher-end phones
- Apple
    * iOS 14.1 / 17.0
    * iPadOS 14.1 / 17.0
    * macOS 11.0 / 14.0, with Intel and Apple silicon support
- Quest 3 using min spec Vulkan 1.1
- XBOX One / XBOX One X / XBOX Series S/X *
- PS4 / PS4 Pro *
- PS5 / PS5 Pro *
- Switch 1/2 using min spec Vulkan 1.1 *
- HoloLens2

*The console platforms are only available for accredited developers on request. Please note that you need a license from us to use any of the console platforms. This repository only holds the PC / DirectX 12 runtime.

The Forge Framework™ (TF) provides building blocks to:
- Extend:
   * Existing game engines so that they support more platforms (like Starfield & more...)
   * Old games (e.g., 20+ years) can be brought back to modern gaming platforms
   * Write custom game engines from scratch (most notably Supergiant's *Hades*, *Hypixel* Game Engine)
- Write SDKs (Adreno SDK, Oculus / Qualcomm VR SDKs, Dolby AR SDK, Dolby Vision, etc.), enable new technology (Google Stadia, Dolby Vision, Meta App framework, etc.)
- Supports most of the gaming platforms

These are just a few reasons why The Forge Framework™ has become the core of our business.

We offer a PC licence under the Apache License Version 2.0 through this repository. 

We also offer commercial licenses for all other platforms like game consoles (PlayStation, XBOX, and Switch). 

---

### Here is an overview:
![The Forge Overview](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/TheForgeOverview.webp) 

- Game Layer (partially provided)
- App
- Renderer / Scene (not provided) / Resource Streaming (not provided) / Resource Loading / Animation
- Graphics / OS / Utilities
What is not there: Physics / Networking / Sound

The "lego-like" High-Level Features supported on all platforms are the following at the moment:
- Resource Loader capable of loading textures, buffers, and geometry data asynchronously
- [Lua Scripting System](https://www.lua.org/) - This is currently used for automatic testing and in 06_Playground to load models and textures, animate the camera, as well as several other unit tests to cycle through the options they offer during automatic testing.
- Animation System based on [Ozz Animation System](https://github.com/guillaumeblanc/ozz-animation)
- Consistent Math Library based on an extended version of [Vectormath](https://github.com/glampert/vectormath) with NEON intrinsics for mobile platforms. It now also supports double precision.
- Consistent Memory Management: 
  * on GPU following [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) and the [D3D12 Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator)
  * on CPU [Fluid Studios Memory Manager](http://www.paulnettle.com/)
- Custom Input System Library with Gestures for Touch devices written in C
- Fast Entity Component System based on [flecs](https://github.com/SanderMertens/flecs) 
- Cross-platform FileSystem C API, supporting disk-based files, memory streams, and files in zip archives
- Consistent Math Library in FSL translated to [ISPC](https://ispc.github.io/) and all the shader languages. This allows the same math library to be used across several platforms, including running on the GPU through FSL
- UI system based on [Nuklear](https://github.com/Immediate-Mode-UI/Nuklear)
- Shader Translator using a superset of HLSL as the shader language, called The Forge Shading Language. Please see the Wiki page for more details: [The Forge Shading Language](https://codeberg.org/The-Forge/The-Forge/wiki/FSL-Programming-Guide)
- Various implementations of high-end Graphics Effects and game engine sub-systems as shown in the unit tests below

Please find the links and credits for all open-source packages used at the end of this README.

The Forge Interactive, Inc. is a [Khronos member](https://www.khronos.org/members/list)

---

# News
## Release 1.64 - August 12th, 2026 - Replaced ImGUI with Nuklear | Rebuild Aura, PixelPuzzle, Ephemeris and Gladiator 

Since the last release in March 2025, we’ve made many updates to The Forge Framework. The most important update was the replacement of ImGUI with Nuklear to reduce maintenance costs and streamline operations. We now also support the platforms Switch 2 and HoloLens2. 

The whole codebase was aligned with the new features in various APIs. There are so many changes that a README like this can not describe the Delta. Because of this, we will focus on the most notable improvements: 

### **Aura:**
Our Global Illumination system, Aura, now supports the original version of Aura from 2011, as well as a new version based on hardware ray tracing. Both versions run on mobile devices, but have their own advantages and drawbacks.

### **PixelPuzzle:**
PixelPuzzle, our PostFX pipeline, now supports different "real-world" cameras again. The original version of PixelPuzzle from 2011/12 had this feature initially, but it was lost over time. We’ve re-introduced this feature as well as many other improvements to this product.

### **Ephemeris:**
Ephemeris was upgraded for use in the Anduril codebase. It now features higher-quality settings and provides better visual fidelity. 

### **Gladiator:**
We developed a new version of our in-house editor. While the name remains the same as our old editor, Gladiator, from 2017, this new system is designed to support editing within the target platform. Editing a game on PC and exporting it to the target platform was never a good idea. Now the game can run natively on the target platform while only the editor runs on PC or macOS.

See the release notes from previous releases in the [Release section](https://github.com/ConfettiFX/The-Forge/releases).

---
  
# PC Windows Requirements:

1. Windows
    * Windows 10 1809 or higher
    * Windows 11

2. Latest GPU drivers
    * [AMD](https://www.amd.com/en/support)
    * [Intel](https://www.intel.com/content/www/us/en/download-center/home.html)
    * [NVIDIA](https://www.nvidia.com/download/index.aspx)

3. Visual Studio 2019 with Windows SDK 10.0.17763.0 (available in the Visual Studio Installer)
    * Direct downloads are also available [here](https://developer.microsoft.com/en-us/windows/downloads/sdk-archive)

4. The Forge is currently tested on: 
    * NVIDIA RTX 2060
    * NVIDIA RTX 3060
    * AMD rx 9060xt
    * AMD rx 6500xt
    * AMD rx 480
    * Intel Arc A770

Notes:
- If you get any undefined math operations errors, add -Im to your project’s Linker Command Line options
- If you receive an error related to "cannot use 'throw' with exceptions disabled", enable exceptions in C++ Project settings
- If you get an error related to multiple instances of ioctl, add BIONIC_IOCTL_NO_SIGNEDNESS_OVERLOAD in preprocessor definitions
- If you get any errors related to NEON support not enabled:
* Set Enable Advanced SIMD to Yes 
* Set Floating Point ABI to Softfp

---

# Install 

For PC Windows, run PRE_BUILD.bat. It will download and unzip the art assets and install the shader builder extension for Visual Studio 2019.
  
It will only download and unzip required Art Assets (no plugins or extensions will be installed). 

---

# Unit Tests

There are the following unit tests in The Forge:

## 1. Transformation

This unit test just shows a simple solar system. It is our "3D game Hello World" setup for cross-platform rendering.

**Switch:**
![Image of the Transformations Unit test](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Switch/UT1.webp)

**Switch 2:**
![Image of the Transformations Unit test](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Switch2/UT1.webp)

**Android:**
![Image of the Transformations Unit test](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Android/UT1.webp)

**PS4:**
![Image of the Transformations Unit test](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_PS4/01_Transformations.webp)

**PS5:**
![Image of the Transformations Unit test](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_PS5/01_Transformations.webp)

**Steamdeck:**
![Image of the Transformations Unit test](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_SteamDeck/01_Transformations.webp)

**macOS:**
![Image of the Transformations Unit test](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_MacOS/01_Transformations.webp)

**iOS:**
![Image of the Transformations Unit test](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_iOS/01_Transformations.webp)

**XBOX One (original):**
![Image of the Transformations Unit test](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_XboxOne/UT1.webp)

**PC:**
![Image of the Transformations Unit test](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Windows/01_Transformations.webp)

## 3. Multi-Threaded Rendering

This unit test shows how to generate a large number of command buffers on all platforms supported by The Forge. This unit test is based on [a demo by Intel called Stardust](https://www.intel.com/content/dam/develop/external/us/en/documents/stardust-code-sample-624535.pdf).

**Switch:**
![Image of the Multi-Threaded command buffer generation example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Switch/UT3.webp)

**Switch 2:**
![Image of the Multi-Threaded command buffer generation example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Switch2/UT3.webp)

**Android:**
![Image of the Multi-Threaded command buffer generation example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Android/UT3.webp)

**PS4:**
![Image of the Multi-Threaded command buffer generation example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_PS4/03_MultiThread.webp)

**PS5:**
![Image of the Multi-Threaded command buffer generation example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_PS5/03_MultThread.webp)

**Steamdeck:**
![Image of the Multi-Threaded command buffer generation example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_SteamDeck/03_MultiThread.webp)

**macOS:**
![Image of the Multi-Threaded command buffer generation example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_MacOS/02_MultThread.webp)

**iOS:**
![Image of the Multi-Threaded command buffer generation example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_iOS/02_MultiThread.webp)

**XBOX One (original):**
![Image of the Multi-Threaded command buffer generation example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_XboxOne/UT3.webp)

**PC:**
![Image of the Multi-Threaded command buffer generation example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Windows/03_MultThread.webp)


## 6. Material Playground

This unit test shows a range of game-related materials:

### Hair:
In 2012 / 2013, we helped AMD and Crystal Dynamics with the development of TressFX for *Tomb Raider*. We also wrote an article about the implementation in *GPU Pro 5* and gave a few joint presentations on this at conferences like FMX. At the end of last year we revisited TressFX. We took the latest version of the code from our GitHub repository, updated it, and ported it to The Forge. It now runs on PC with DirectX 12 / Vulkan, macOS and iOS with Metal 2, and on the XBOX One. We also created a few new hair assets to showcase it. 

### Here are screenshots of our programmer art:

**Switch 2:**
![Image of the Material Playground example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Material_Playground/Switch2/Hair1.webp)
![Image of the Material Playground example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Material_Playground/Switch2/Hair2.webp)
![Image of the Material Playground example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Material_Playground/Switch2/hair9.webp)

### Here are the various types of metals:

**Switch:**
![Image of the Material Playground example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Switch/UT6.webp)

**Switch 2:**
![Image of the Material Playground example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Switch2/UT6.webp)

**Android:**
![Image of the Material Playground example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Android/UT6.webp)

**PS4:**
![Image of the Material Playground example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_PS4/06_MaterialPlayground.webp)

**PS5:**
![Image of the Material Playground example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_PS5/06_MaterialPlayground.webp)

**Steamdeck:**
![Image of the Material Playground example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_SteamDeck/06_MaterialPlayground.webp)

**macOS:**
![Image of the Material Playground example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_MacOS/06_MaterialPlayground.webp)

**iOS:**
![Image of the Material Playground example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_iOS/06_MaterialPlayground.webp)

**XBOX One (original):**
![Image of the Material Playground example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_XboxOne/UT6.webp)

**PC:**
![Image of the Material Playground example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Windows/06_MaterialPlayground.webp)


### Here are the various types of wood:

**Switch 2:**
![Image of the Material Playground example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Material_Playground/Switch2/Wood1.webp)

![Image of the Material Playground example](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Material_Playground/Switch2/Wood2.webp)


## 9. Light and Shadow Playground
This unit test shows various shadow and lighting techniques that can be chosen from a drop down menu. There will be more shadow and lighting techniques in the future.

 * **Exponential Shadow Map** - this is based on [Marco Salvi's](https://pixelstoomany.wordpress.com/category/shadows/exponential-shadow-maps/) [(@marcosalvi papers9)](https://x.com/marcosalvi?lang=en). This technique filters out the edge of the shadow map by approximating the shadow test using an exponential function that involves three subjects: the depth value rendered by the light source, the actual depth value that is being tested against, and the constant value defined by the user to control the softness of the shadow

  * **Adaptive Shadow Map with Parallax Correction Cache** - this is based on the article "Parallax-Corrected Cached Shadow Maps" by Pavlo Turchyn in [*GPU Zen 2*](https://gpuzen.blogspot.com/2019/05/gpu-zen-2-parallax-corrected-cached.html). It adaptively chooses which light source view is used when rendering a shadow map based on a hierarchical grid structure. The grid structure is constantly updated depending on the user's point of view, using a caching system that only renders the uncovered part of the scene. The algorithm greatly reduces shadow aliasing that is normally found in traditional shadow maps due to insufficient resolution. Pavlo Turchyn's paper from *GPU Pro 2* added an additional improvement by implementing multi-resolution filtering, a technique that approximates a larger PCF kernel using multiple mipmaps to achieve cheap soft shadows. He also describes how he integrated a Parallax Correction Cache into the Adaptive Shadow Map, an algorithm that approximates the moving sun's shadow on a static scene without rendering tiles of the shadow map every frame. The algorithm is generally used in open-world games to approximate the simulation of day & night’s shadow cycle more realistically, without too much CPU/GPU cost.

  * **Signed Distance Field Soft Shadow** - this is based on [Daniel Wright's SIGGRAPH 2015](http://advances.realtimerendering.com/s2015/DynamicOcclusionWithSignedDistanceFields.pdf) [@EpicShaders](https://x.com/EpicShaders?lang=en) presentation. To achieve real-time SDF shadows, we store the distance to the nearest surface for all unique Meshes to a 3D volume texture atlas. The Mesh SDF is generated offline using triangle ray tracing, and half precision float 3D volume texture atlas is accurate enough to represent 3D meshes with SDF. The current implementation only supports rigid meshes and uniform transformations (non-uniform scale is not supported). An approximate cone intersection can be achieved by measuring the closest distance of a passed ray to an occluder, which gives us a cheap soft shadow when using SDF.

To achieve high-performance, the playground runs on our signature rendering architecture called the Triangle Visibility Buffer. The step that generates the SDF data also uses this architecture.

Please see the following link to see our Light and Shadow Playground at work:

[Signed Distance Field Soft Shadow Map](https://vimeo.com/352985038)

### The following screenshots are taken on Windows Switch 2.

**Exponential Shadow Maps:**

![Light and Shadow Playground - Exponential Shadow Map](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Light_Shadow_Playground/Switch2/ESM1.webp)

**Adaptive Shadow Map with Parallax Correction Cache:**

![Adaptive Shadow Map with Parallax Correction Cache](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Light_Shadow_Playground/Switch2/ASM1.webp)

**Signed Distance Field Soft Shadow:**

![Signed Distance Field Soft Shadow Map Switch](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Light_Shadow_Playground/Switch2/SDF1.webp)

**Signed Distance Field Soft Shadows - Debug Visualization:**

![Signed Distance Field Soft Shadow Map Switch Debug](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Light_Shadow_Playground/Switch2/SDF_Visualize2.webp)

**README for Signed Distance Field Soft Shadow Maps:**

To generate the SDF Mesh data, you should select “Signed Distance Field” as the selected shadow type in the Light and Shadow Playground. There is a button called “Generate Missing SDF”, and once it's clicked, it shows a progress bar that represents the remaining SDF mesh objects utilized for SDF data generation. This process is multithreaded, so the user can still move around the scene while waiting for the SDF process to be finished. This is a long process, and it could consume up to 8+ hours depending on your CPU specs. To check how many SDF objects there are presently in the scene, you can mark the checkbox "Visualize SDF Geometry On The Scene".

This unit test also supports screen-space shadows. These are complementary to regular shadow mapping and add more detail. We also fixed many inconsistencies with the other shadow map approaches.

**Switch 2 - Screen-Space Shadows off:**
![Screen-Space Shadows PS5](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Light_Shadow_Playground/Switch2/SSR_Off.webp) 

**Switch 2 - Screen-Space Shadows on:**
![Screen-Space Shadows PS5](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Light_Shadow_Playground/Switch2/SSR_On.webp) 

**PS5 - Screen-Space Shadows on:**
![Screen-Space Shadows Switch](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_PS5/SSR_On.webp) 

**PS4 - Screen-Space Shadows on:**
![Screen-Space Shadows PS4](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_PS4/SSR_On.webp)

## 10. Screen-Space Reflections
This test offers two choices: you can pick either Pixel Projected Reflections or AMD's FX Stochastic Screen Space Reflection. We just made AMD's FX code cross-platform. It runs now on Windows, Linux, macOS, Switch, PS, and XBOX.

### Here are the screenshots of AMD's FX Stochastic Screen Space Reflections:

**Windows 11:**
![AMD FX Stochastic Screen Space Reflections](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Windows/StochasticScreenSpaceReflections.webp)

**PS5:**
![AMD FX Stochastic Screen Space Reflections](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_PS5/StochasticScreenSpaceReflections.webp)

**Switch 2:**
![AMD FX Stochastic Screen Space Reflections](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Switch2/StochasticScreenSpaceReflections.webp)

**Mac M1:**
![AMD FX Stochastic Screen Space Reflections](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_MacOS/StochasticScreenSpaceReflections.webp)

**XBOX One:**
![AMD FX Stochastic Screen Space Reflections](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_XboxOne/StochasticScreenSpaceReflections.webp)

**iPad Pro 12.9-inch (5th generation):**
![AMD FX Stochastic Screen Space Reflections](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_iOS/StochasticScreenSpaceReflections.webp)

### Here are the screenshots of Pixel Projected Reflections:

**Windows 11:**
![Pixel Projected Reflections](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Windows/PixelProjectedReflections.webp)

**PS5:**
![Pixel Projected Reflections](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_PS5/PixelProjectedReflections.webp)

**Switch 2:**
![Pixel Projected Reflections](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Switch2/PixelProjectedReflection.webp)

**Mac M1:**
![Pixel Projected Reflections](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_MacOS/PixelProjectedReflections.webp)

**XBOX One:**
![Pixel Projected Reflections](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_XboxOne/PixelProjectedReflections.webp)

**iPad Pro 12.9-inch (5th generation):**
![Pixel Projected Reflections](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_iOS/PixelProjectedReflections.webp)

## 11. Multi-GPU (Driver support only on PC Windows)
This unit test shows a typical VR Multi-GPU configuration. One eye is rendered by one GPU and the other eye is rendered by the other.

![Image of the Multi-GPU Unit test](https://github.com/ConfettiFX/The-Forge-Media/blob/master/Screenshots/11a_UnlinkedMultipleGPUs.PNG?raw=true )

### 11a. Unlinked multiple GPUs (Driver support only on PC Windows)
For professional visualization applications, we now support unlinked multiple GPUs. A new renderer API is added to enumerate available GPUs. Renderer creation is extended to allow explicit GPU selection using the enumerated GPU list. Multiple Renderers can be created this way. The resource loader interface has been extended to support multiple Renderers. It is initialized with the list of all Renderers created. To select which Renderer (GPU) resources are loaded on, the NodeIndex used in linked GPU configurations is reused for the same purpose. Resources cannot be shared on multiple Renderers; however, resources must be duplicated explicitly if needed. To retrieve generated content from one GPU to another (e.g. for presentation), a new resource loader operation is provided to schedule a transfer from a texture to a buffer. The target buffer should be mappable. This operation requires proper synchronization with the rendering work; a semaphore can be provided to the copy operation for that purpose. Available with Vulkan and D3D12. For other APIs, the enumeration API will not create a RendererContext, which indicates a lack of unlinked multi-GPU support.

![Image of the Unlinked Multiple GPUs Unit test](https://github.com/ConfettiFX/The-Forge-Media/blob/master/Screenshots/11a_UnlinkedMultipleGPUs.PNG?raw=true )

## 12. File System Test
This unit test showcases a cross-platform FileSystem C API supporting disk-based files, memory streams, and files in zip archives. The API can be viewed in [IFileSystem.h](/Common_3/OS/Interfaces/IFileSystem.h), and all of the example code has been updated to use the new API.
   * The API is based around `Path`s, where each `Path` represents an absolute, canonical path string on a particular file system. You can query information about the files at `Path`s, open files as `FileStream`s, and copy files between different `Path`s.
   * The concept of `FileSystemRoot`s has been replaced by `ResourceDirectory`s. `ResourceDirectory`s are predefined directories where resources are expected to exist, and there are convenience functions to open files in resource directories. If your resources don’t exist within the default directory for a particular resource type, you can call `fsSetPathForResourceDirectory` to relocate the resource directory; see the unit tests for sample code on how to do this.
   
![File System Unit Test](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_SteamDeck/12_ZipFileSystem.webp)

## 14. Wave Intrinsics
This unit test shows how to use the new wave intrinsics. Supporting Windows with DirectX 12 / Vulkan, Linux with Vulkan, and macOS / iOS.

![Image of the Wave Intrinsics unit test in The Forge](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Scarlett/UT14_FirstLastLane.webp)

## 15a. Visibility Buffer OIT
This unit test shows how to handle per-triangle order-independent transparency intuitively in the Visibility Buffer context. The main idea is that a per-pixel linked list of triangle IDs holds layers of transparency. This occupies less memory and is more efficient than storing per-pixel information.

We also added Variable Rate Shading to this unit test. This way, we have a better-looking test scene with St. Miguel.

VRS allows rendering parts of the render target at different resolutions based on the auto-generated VRS map, thus achieving higher performance with minimal quality loss. It is inspired by [Michael Drobot's SIGGRAPH 2020 talk](https://research.activision.com/publications/2020-09/software-based-variable-rate-shading-in-call-of-duty--modern-war)

The key idea behind the software-based approach is to render everything in 4xMS targets and use a stencil buffer as a VRS map. The VRS map is automatically generated based on the local image gradients.It could be used on a much wider range of platforms and devices than the hardware-based approach since hardware VRS support is either broken or not supported on many platforms. Because this software approach utilizes 2x2 tiles, we could also achieve higher image quality compared to hardware-based VRS.

Shading rate view based on the color per 2x2 pixel quad:
- White – 1 sample (top left, always shaded);
- Blue – 2 horizontal samples;
- Red – 2 vertical samples;
- Green – all 4 samples;

**PC:**
![VRS](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Windows/UT15.webp) 

**PC - UI Debug Output:**
![VRS](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Windows/UT15_DebugRTs.webp) 

**Android:**
![VRS](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Android/UT15.webp) 

**Android - Fullscreen Debug Output:**
![VRS](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Android/UT15_DebugRT.webp) 


Example 15a_VisibilityBufferOIT now has an additional option to toggle VRS: "Enable Variable Rate Shading"

The Debug view can now be toggled with the "Draw Debug Targets" option. This shows the auto-generated VRS map if VRS is enabled.

Limitations: 
* Relies on programmable sample locations support – not widely supported on Android devices.

Supported platforms:
PS4, PS5, all XBOXes, Nintendo Switch, Android (Galaxy S23 and higher), Windows (Vulkan/DX12), macOS/iOS.

## 16. Path Tracer - Ray Tracing
We switched to Ray Queries for the common Ray Tracing APIs on all the platforms we support. The current Ray Tracing APIs increase the necessary amount of memory substantially, decrease performance, and can't add much visually because the whole game has to run with a lower resolution, lower texture resolution, and lower graphics quality (to make up for this, upscalers were introduced that add new issues to the final image). 
Because Ray Tracing became a marketing term valuable to GPU manufacturers, some game developers now support Ray Tracing to help increase hardware sales. So we are going with the flow here by offering those APIs.

**iPad Pro (5th generation):**
![Ray Queries on macOS](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_iOS/16_Raytracing.webp)

**PS5**:
![Ray Queries on PS5](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_PS5/16_Raytracing.webp)

**Windows 10:**
![Ray Queries on Windows 10](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Windows/16_Raytracing.webp)

**XBOX One Series X:**
![Ray Queries on XBOX One Series X](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Scarlett/UT16.webp)

**iPhone 11:**
![Ray Queries on iOS](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_iOS/UT16_iPhone11.webp)

We do not have a denoiser for the Path Tracer.

## 19. C Hot Reloading
This unit test showcases an implementation of code hot reloading in C. We've used and adapted the following GitHub library for this: [cr](https://github.com/fungos/cr)
 
![C Code Hot Reloading Unit test](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Windows/19_CodeHotReload.webp)
 
 The test contains two projects:
- 19_CodeHotReload_Main: generates the executable. All code in this project can't be hot-reloaded. This is the project you should set as the startup project when running the program from an IDE.
- 19a_CodeHotReload_Game: for development platforms Windows/MacOS/Linux, it generates a dynamic library that is loaded by the Main project at runtime; when the dynamic library changes, the Main program reloads the new code. For Android/IOS/Quest/Consoles this project is compiled and linked statically.

How to use it: While the Main project is running, open 19_CodeHotReload_Game.cpp and make some changes; there are lines marked with `TRY_CODE_RELOAD` to make these changes easy. Once the file is saved, you can rebuild the project and see the changes happen automatically.
- Windows/Linux: Click on the UI "RebuildGame" button.
- MacOS: Command+B in XCode to rebuild.

Note: In this implementation, we can't call any functions from The Forge from the HotReloadable project (19a_CodeHotReload_Game). This is because we compile OS and Renderer as static libraries and linking them directly to the exe. Ideally, these projects should be compiled as dynamic libraries in order to expose their functionality to the exe and hot-reloadable DLL. The reason we didn't implement it in this way is that all our other projects are already set up to use static libraries.

## 21. Animation
This unit test shows a wide range of animation tasks. We used Ozz to achieve those. The following shots were taken on an Android phone. 

**Ozz Playback Animation:**

Here is how to playback a clip on a rig:

![Image of the Ozz Playback Animation](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Android/UT21_Standing.webp)

**Ozz Playback Blending:**

This option shows how to blend multiple clips and play them back on a rig:

![Image of the Ozz Playback Blending](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Android/UT21_Blending.webp)

**Ozz Partial Blending:**

This option shows how to blend clips having each only effect a certain portion of joints.

![Image of the Ozz Partial Blending](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Android/UT21_PartialBlending.webp)

**Ozz Additive Blending:**

This option shows how to introduce an additive clip onto another clip and play the result on a rig.

![Image of the Ozz Additive Blending](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Android/UT21_AdditiveBlending.webp)

**Ozz Baked Physics:**

This option shows how to use a scene of a physics interaction that has been baked into an animation and play it back on a rig.

![Image of the Ozz Baked Physics](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Android/UT21_BakedPhysics.webp)

**Ozz Multi Threading:**

This option shows how to animate multiple rigs simultaneously while using multi-threading for the animation updates:

![Image of the Ozz Multi Threading](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Android/UT21_Threading.webp)

## 28. Ozz Skinning
This unit test shows how to use skinning with Ozz

![Image of the Ozz Skinning unit test](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Android/UT28_Dancing.webp)

## 34. Input System
This unit test shows our cross-platform input library.

## 36. AlgorithmsAndContainers
This unit test is used to ensure the string, dynamic array, and hash map implementations are stable.

# Examples

## Triangle Visibility Buffer 1.0:
This is an implementation of the Triangle Visibility Buffer that utilizes indirect draw calls. An early version of this example was covered in various conference talks. For more information on how Triangle Visibility Buffer 1.0 was implemented in The Forge, please see our past blog entry [here](https://diaryofagraphicsprogrammer.blogspot.com/2018/03/triangle-visibility-buffer.html).

![Image of the Visibility Buffer](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_PS5/VisBuf_Alt.webp)

## Triangle Visibility Buffer 2.0:
This is a GPU-driven version of the Triangle Visibility Buffer. All the indirect draw calls are replaced by a large compute shader.
You can find more information about this in the following [talk](https://www.youtube.com/watch?v=kWLev9CoQdg) and in an article in *GPU Zen 3*. 

# Tools
Below are screenshots and descriptions of some of the tools we integrated.

## SAST Tools

[PVS-Studio](https://pvs-studio.com/pvs-studio/?utm_source=website&utm_medium=github&utm_campaign=open_source): static analyzer for C, C++, C#, and Java code.

## Shader Server
To enable re-compilation of shaders during run-time, we implemented a cross-platform shader server that allows you to recompile shaders by pressing CTRL-S or a button in a dedicated menu. You can find documentation on this in the Wiki in the FSL section.

## Ray Tracing Benchmark
Based on requests, we are providing a Ray Tracing Benchmark in 16_RayTracing. It allows you to compare the performance of two platforms: 
  * Windows with DirectX 12 DXR
  * Linux with Vulkan RTX

 We will extend this benchmark to the non-public platforms we support to compare PC performance with console performance. 
This benchmark comes with batch files for both platforms. Each run generates an HTML output file from the profiler that is integrated into TF. The default number of iterations is 64, but you can adjust that. There is a README file in the 16_RayTracing folder that describes the options.

Windows DirectX 12 DXR, GeForce RTX 2060, 3840x2160, NVIDIA Driver 610.62

![Windows DXR output of Ray Tracing Benchmark](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Windows/UT16_Benchmark.webp) 

## Microprofiler
We integrated the [Micro Profiler](https://github.com/zeux/microprofile) into our codebase by replacing the proprietary UI with Nuklear and simplifying the usage. Now, it is integrated much more tightly and consistently in our code base.

Here are screenshots of the Microprofiler running the Visibility Buffer on PC:

![MicroprofilerDetailed](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Windows/VisBuf_MicroProfilerDetailed.webp)

![MicroprofilerPlot](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Windows/VisBuf_MicroProfilerPlot.webp)

![MicroprofilerTimer](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Screenshots_Windows/VisBuf_MicroProfilerTimer.webp)

## Shader Translator
We provide a shader translator that translates one shader language -a superset of HLSL called Forge Shader Language (FLS)- to the target shader language of all our target platforms. This includes console and mobile platforms as well.
It is written in Python. We expect this shader translator to be a solution that is easier to maintain for smaller teams as it allows teams to add additional data to the shader source file with less effort. 

Such data could be: 
* A bucket classification or different shaders for different capability levels of the underlying platform.
* Descriptor memory requirements or resource memory requirements in general.
* Material information or just information to easier pre-compile pipelines.

The actual shader compilation will be done by the native compiler of the target platform.

 [See how to use the Shader Translator here.](https://codeberg.org/The-Forge/The-Forge/wiki/FSL-Programming-Guide)

## GPU Config System
This is a general system that can track GPU capabilities on all platforms and switch on and off features of a game for different platforms. 

## Summary

- [GPU Configuration system](#gpu-configuration-system)
  - [Hardware Capabilities](#hardware-capabilities)
  - [GPUPresetLevel](#gpupresetlevel)
  - [GPU Selection](#gpu-selection)
  - [Driver Rejection](#driver-rejection)
  - [User Extended Settings](#user-extended-settings)

- [Open questions](#open-questions)
- [List of available properties](#list-of-available-properties)

## GPU Configuration system

Our configuration system allows you to:

- Access an exhaustive list of **hardware features** and **capabilities** (see Vulkan hardware capability viewer) 
- Give a **performance rating** to each available GPU (office, low, medium, high, ultra) 
- **Choose a specific GPU** when multiple are available
- Turn on and off certain hardware features (ex: **turn off raytracing support** for a specific vendor)
- Disable certain GPU **depending on the current driver version**
- Set application settings based on the current hardware (ex: disable certain game mechanics if there are no proper support for advanced transparency)

What it is not:

- A full feature configuration system you see in most game, ex: **Graphic Settings** panel
- TheForge is mainly designed to deal with hardware features, this goal is not to manage the specific settings of your application.

![GPU Config System](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/gpuConf.webp)


### Hardware Capabilities

TheForge lets you access various structures that store hardware information about the current device your application is using:

- GPUSettings: storing various flags that indicate the hardware features supported across different platform (**mHDRSupported**, **mTessellationSupported**, **mRaytracingSupported**, **mROVsSupported**, **mTessellationSupported**, **mVRAM**, **mWaveOpsSupportFlags**, ...)
 - The quality index assigned to the GPU is set inside the **mGpuVendorPreset** attribute
- GPUCapBits: storing the list of all the available texture formats (**TinyImageFormat_R32G32_UINT**, **TinyImageFormat_R32_SFLOAT**, **TinyImageFormat_ASTC_8x8_SRGB**, ...)
- RendererContext: storing features and extensions specific to each graphic API
  - GPUInfo attribute can be used to access the native interface directly (**IDXGIAdapter**, **VkPhysicalDeviceProperties2**, **MTLDevice**, ...) 

### GPUPresetLevel

A performance index is assigned to the current GPU during the initialization phases in *initRenderer()*. This value comes from reading the gpu.data file in the **RD_GPU_CONFIG** directory, previously set via **fsSetPathForResourceDir()**.

This file contains a list of available models of graphics cards and their manufacturers. You can find the URLs that were used to create this database at the beginning of the file. Feel free to keep this list updated as needed. 

Consoles have been added inside this list:

- **Xbox:** the modelID is obtained by invoking **XsystemGetDevicetype**, which return a enum value you can find on the [msdn](https://learn.microsoft.com/en-us/gaming/gdk/_content/gc/reference/system/xsystem/enums/xsystemdevicetype) documentation page
- **Playstation:** use a proprietary platform macro to assign an identifier for each console
- **Nintendo Switch:** use the the model code of the nvidia tegra device
- **SteamDeck:** use the model code of the amd apu

If the model is missing, you can use the **DefaultPresetLevel** property to assign a GPUPresetLevel to any unknown device. This can be useful on a client machine or for testing your application's quality presets on multiple graphic cards.

`gpu.data`:

```
BEGIN_VENDOR_LIST;
intel; 0x163C, 0x8086, 0x8087;
nvidia; 0x10DE
amd; 0x1002, 0x1022;
qualcomm; 0x5143;
imagination technologies; 0x1010;
samsung; 0x144D;
arm; 0x13b5;
apple; 0x106b;
END_VENDOR_LIST;

BEGIN_DEFAULT_CONFIGURATION;
#if the current gpu doesn't exist in GPU_LIST use this instead
DefaultPresetLevel; Low;
END_DEFAULT_CONFIGURATION;

#VendorId; DeviceId; Classification; Name ; Revision ID (Can be null) ; Codename (can be null)
BEGIN_GPU_LIST;
# --- NVIDIA GPUs --- 
0x10de; 0x0045; Low; NVIDIA GeForce 6800 GT
0x10de; 0x0040; Low; NVIDIA GeForce 6800 Ultra
...
0x10de;	0x2860; Ultra; GeForce RTX 4070 Max-Q / Mobile;
0x10de;	0x2704; Ultra; GeForce RTX 4080;
...
# --- INTEL GPUs ---
0x8086; 0xA780; Medium; Intel(R) Xe Graphics
# --- XBOX ---
0x7a0d; 0x2; Low; Xbox One;
...
END_GPU_LIST;
```

### GPU Selection

When multiple devices are available, it's possible to write a set of rules which can be used to select one device against another. If there are no rules, the first GPU found will be used. Those rules are defined in the *gpu.cfg* file also located in the **RD_GPU_CONFIG** directory.

The rules are in **descending order**, and the first rule that gives a different result for two distinct GPUs will make the final decision.

Each GPU is compared one against one another in their discovery order, once one is rejected it will no longer be used as a potential candidate. The discovery order of the GPU can affect the final outcome (imagine a list of rock, paper and scissor, the last remaining candidate will vary depending in the order they are processed)

The possible syntaxes for a rule is:

- *\<property\>;*

```
BEGIN_GPU_SELECTION;
GpuPresetLevel;
DirectXFeatureLevel;
VRAM;
END_GPU_SELECTION;
```

This will choose the gpu with the greatest **GpuPresetLevel**, if they are both equal it will pick the one with the greatest **DirectXFeatureLevel** on windows and finally, if they all return the same, it will pick the one with the maximum amount of **VRAM**.

- *\<property\> \<comparator\> \<value\>**,** \<property\> \<comparator\> \<value\>, ... ;*
```
BEGIN_GPU_SELECTION;
DirectXFeatureLevel < 11;
deviceid == PreferredGPU;
# Intel vendor: 0x8086 && 0x8087 && 0x163C
VendorID != 0x8086, VendorID != 0x8087, VendorID != 0x163C;
END_GPU_SELECTION;
```

This will first eliminate the GPU if the **DirectXFeatureLevel** is lower than 11. Then it will use the special variable **PreferredGPU** to choose the GPU with the matching deviceid, if it is set correctly. Finally it will skip intel GPU.

You can combine those different syntaxes to create your own set of rules.

### Driver Rejection

If you want to reject a specific driver for a given manufacturer, you can add the following rule in **gpu.cfg**:

*\<vendorID\>; DriverVersion \<comparator\> \<driverVersion\>; \<reasonStr\>;*

```
BEGIN_DRIVER_REJECTION;
# amd: 0x1002, 0x1022
0x1002; DriverVersion <= 23.10.23.03; 09a unit test artefacts, pixelated and too bright, 15a flickers;
0x1022; DriverVersion <= 23.10.23.03; 09a unit test artefacts, pixelated and too bright, 15a flickers;
END_GPU_SETTINGS;
```

This will reject all AMD drivers prior to 23.10.23.03. You can use this to inform your users that they should update their graphic driver.

Driver convention name:

- **NVIDIA**: *\<Major\>.\<Minor\>* ex: **537.13**
- **AMD**: \<YEAR\>.\<MONTH\>.\<REVISION\> ex: **23.10.23.03**
- **Intel**:  we only use the **\<BUILD_NUMBER\>**, normally it's supposed to look like this *\<OS\>.0.\<BUILD_NUMBER\>* but Vulkan only return the last part, for instance, 31.0.101.5074 will become **101.5074** see [intel convention](https://www.intel.com/content/www/us/en/support/articles/000005654/graphics.html)

### Configuration Settings

It's possible to turn on and off certain hardware features and GPU properties using specific rules in **gpu.cfg**. This way you can disable functionalities on a specific set of devices. The rule syntax is the following:

*\<sourceProperty\>; \<compProperty\> \<comparator\> \<compValue\>, ... ; \<assignmentValue\>;*

```
BEGIN_GPU_SETTINGS;
# nvidia
maxRootSignatureDWORDS; vendorID == 0x10DE; 64;
# amd
maxRootSignatureDWORDS; vendorID == 0x1002; 13;
# disable tessellation support on arm system
tessellationsupported; vendorID == 0x13B5; 0;
END_GPU_SETTINGS;
```

This will set the maximum size of the **rootSignature** to 64x32bits DWORD on NVIDIA, and 13 on AMD graphic card. It will also disable the tessellation shader on the ARM system.

### User Extended Settings

It's possible to set application-wide settings using gpu.cfg. First you will need to register your settings by filling the **ExtendedSettings** attribute of your **RendererDesc** instance. You will have to provide a string literal and an integer variable to store the setting's value:

``` c++
const char* gSettingNames[];
struct ConfigSettings gGpuSettings;
    
ExtendedSettings extendedSettings = {};
extendedSettings.mNumSettings = ESettings::Count;
extendedSettings.pSettings = (uint32_t*)&gGpuSettings;
extendedSettings.ppSettingNames = gSettingNames;

RendererDesc settings;
memset(&settings, 0, sizeof(settings));
settings.pExtendedSettings = &extendedSettings;
```

Once it's done you can add your rules in gpu.cfg, the syntax is the following one:

*\<settingName\>; \<property\> \<comparator\> \<comparisonValue\>, ... ; \<assignmentValue\>*

```
BEGIN_USER_SETTINGS;
EnableAOIT; RasterOrderViewSupport == 1; 1;
END_USER_SETTINGS;
```

This will set the **EnableAOIT** variable to 1 if the hardware supports [razterizer order views](https://learn.microsoft.com/en-us/windows/win32/direct3d11/rasterizer-order-views).

### Open questions

- Should we use \<VendorID\> or their string literals?
- Should we add || and && operator for configuring rule?
  - currently "," can be used as && operator
- Should we add the possibility to configure which graphic API we want to use?

### List of available properties

| Property name                     | Read  | Write |
| --------------------------------- | ----- | ----- |
| allowbuffertextureinsameheap      | :white_check_mark: | :white_check_mark:      |
| builtindrawid                     | :white_check_mark: | :white_check_mark:      |
| cubemaptexturearraysupported      | :white_check_mark: | :white_check_mark:      |
| tessellationindirectdrawsupported | :white_check_mark: | :white_check_mark:      |
| isheadless                        | :white_check_mark: | :white_check_mark:      |
| deviceid                          | :white_check_mark: | :x:   |
| directxfeaturelevel               | :white_check_mark: | :white_check_mark:      |
| geometryshadersupported           | :white_check_mark: | :white_check_mark:      |
| gpupresetlevel                    | :white_check_mark: | :white_check_mark:      |
| graphicqueuesupported             | :white_check_mark: | :white_check_mark:      |
| hdrsupported                      | :white_check_mark: | :white_check_mark:      |
| dynamicrenderingenabled           | :white_check_mark: | :white_check_mark:      |
| indirectcommandbuffer             | :white_check_mark: | :white_check_mark:      |
| indirectrootconstant              | :white_check_mark: | :white_check_mark:      |
| maxboundtextures                  | :white_check_mark: | :white_check_mark:      |
| maxrootsignaturedwords            | :white_check_mark: | :white_check_mark:      |
| maxvertexinputbindings            | :white_check_mark: | :white_check_mark:      |
| multidrawindirect                 | :white_check_mark: | :white_check_mark:      |
| occlusionqueries                  | :white_check_mark: | :white_check_mark:      |
| pipelinestatsqueries              | :white_check_mark: | :white_check_mark:      |
| primitiveidsupported              | :white_check_mark: | :white_check_mark:      |
| rasterorderviewsupport            | :white_check_mark: | :white_check_mark:      |
| raytracingsupported               | :white_check_mark: | :white_check_mark:      |
| rayquerysupported                 | :white_check_mark: | :white_check_mark:      |
| raypipelinesupported              | :white_check_mark: | :white_check_mark:      |
| softwarevrssupported              | :white_check_mark: | :white_check_mark:      |
| tessellationsupported             | :white_check_mark: | :white_check_mark:      |
| timestampqueries                  | :white_check_mark: | :white_check_mark:      |
| uniformbufferalignment            | :white_check_mark: | :white_check_mark:      |
| uploadbuffertexturealignment      | :white_check_mark: | :white_check_mark:      |
| uploadbuffertexturerowalignment   | :white_check_mark: | :white_check_mark:      |
| vendorid                          | :white_check_mark: | :x:   |
| vram                              | :white_check_mark: | :white_check_mark:      |
| wavelanecount                     | :white_check_mark: | :white_check_mark:      |
| waveopssupport                    | :white_check_mark: | :white_check_mark:      |

---

# Releases / Maintenance
The Forge Interactive Inc. will prepare releases when all the platforms are stable and running and push them to this Codeberg repository. Up until a release, development will happen on an internal server. This is done to sync up the console, mobile, macOS, PC and other versions of the source code.

---

# Products

We would appreciate it if you could send us a link in case your product uses The Forge. 

Here are the products we’ve received so far or have contributed to: 

## *STAR WARS™: Bounty Hunter™*
Bounty Hunter was ported with the help of The Forge Framework to all the platforms mentioned in the trailer:

[![STAR WARS™: Bounty Hunter™](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Products/StarWars.webp)](https://www.youtube.com/watch?v=jiBmgse9GTc)

## BuildBox
The game engine BuildBox is now using The Forge (see the following link to go to the BigBox website): 

[![BuildBox](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Products/BuildBox.webp)](https://signup.buildbox.com/product/bb3)

## *Lethis*
The Game *Lethis Path of Progress* uses The Forge (see the following link to go to the Steam Store)

[![Lethis](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Products/Lethis.webp)](https://store.steampowered.com/app/359230/Lethis__Path_of_Progress/)

## Supergiant Games *Hades*
[Supergiant's Hades](https://www.supergiantgames.com/games/hades/) we worked with Supergiant Games from 2014 -2020. One of the on-going challenges was that their run-time was written in C#. We suggested helping them build a new cross-platform game engine in C/C++ from scratch with The Forge. The project started in April 2019 and the first version of this new engine launched in May 2020. Hades was then released for Microsoft Windows, macOS, and Nintendo Switch on September 17, 2020. The game can run on all platforms supported by The Forge.

Here is a screenshot of *Hades* running on Switch:

![Supergiant Hades](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Products/Supergiant_Hades.webp)


Here is an article by [Forbes](https://www.forbes.com/sites/davidthier/2020/09/27/you-need-to-play-the-game-at-the-top-of-the-nintendo-switch-charts/#6e9128ba2f80) about *Hades* being at the top of the Nintendo Switch Charts.
*Hades* is also a technology showcase for Intel's integrated GPUs on macOS and Windows. The target group of the game seems to often own those GPUs.

## Bethesda's Creation Engine
Bethesda based their rendering layer for their next-gen engine on The Forge. We helped integrate and optimize it. 

It always brings us pleasure to see The Forge running in AAA games like this:

[![Starfield](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Products/starfield-screenshot.webp)](https://www.youtube.com/watch?v=ZHZOTFMyMyM)

We added The Forge to the Creation Engine in 2019.

See more information about this game engine below:

[Todd Howard Teases Bethesda's New Game Engine Behind *The Elder Scrolls VI* and *Starfield*](https://www.thegamer.com/starfield-the-elder-scrolls-6-new-game-engine/)

[Bethesda's overhauling its engine for *Starfield* and *The Elder Scrolls VI*](https://www.gamesradar.com/bethesda-engine-starfield-elder-scrolls-6/)

## *No Man's Sky*
The Forge made an appearance during Apple’s Worldwide Developer Conference in 2022. We added The Forge to the game *No Man's Sky* from Hello Games to bring this game to macOS.

To see The Forge featured at WWDC 2022, please see the following link and jump to 1:22:40:
[![No Man's Sky on YouTube](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Products/NoMansSky.webp)](https://www.youtube.com/watch?v=q5D55G7Ejs8)

We helped to ship the macOS version of No Man's Sky.

## NeuroAnimation
NeuroAnimation uses The Forge - [NeuroAnimation](https://www.neuroanimation.com/) is a medical technology company. They have developed a physics-based video game therapy solution that is backed by leading edge neuroscience, powered by Artificial Intelligence and controlled by dynamic movement – all working in concert to stimulate vast improvement of cognitive and motor functions for patients with stroke and the aged.

The Forge provides the rendering layer for their application. Here is an interview our team did with their CEO & Founder, Omar Ahmad:
[Inside the Mind of Innovation: An Exclusive Interview with Omar Ahmad, CEO & Founder of NeuroAnimation](https://vimeo.com/1068293928)

## StarVR One SDK
The Forge was used to build the StarVR One SDK from 2016 - 2017:

<a href="https://www.starvr.com" target="_blank"><img src="https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Products/StarVR.webp" alt="StarVR" width="300" height="159" border="0" /></a>

## Torque 3D
The Forge Framework will be used as the rendering framework in Torque 3D:

<a href="https://torque3d.org/" target="_blank"><img src="https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Products/Torque-Logo_H.webp" 
alt="Torque 3D" width="417" height="106" border="0" /></a>

## *Star Wars Galaxies* Level Editor
SWB is an editor for the 2003 game *Star Wars Galaxies* that can edit terrains, scenes, particles and import/export models via FBX. The editor uses an engine called 'atlas' that will be made open source in the future. It focuses on making efficient use of the new graphics APIs (with help from The-Forge!), ease-of-use, and terrain rendering.

![SWB Level Editor](https://codeberg.org/The-Forge/The-Forge-Media/media/branch/master/Screenshots/Products/SWB.webp)

## Meta Application Framework
The Meta application framework uses TF as its rendering layer.

## Anduril
Anduril's internal code base is now using TF.

## Insomniac Games
Ephemeris runs in the Insomniac Game engine.

## Treyarch Engine
A dedicated version of our Android / Vulkan code base was fueling Call of Duty Warzone Mobile.

---

# Writing Guidelines
For contributions to The Forge we apply the following writing guidelines:
 * We limit all code to C++ 11 by setting the Clang and other compiler flags
 * We follow the [Orthodox C++ guidelines](https://bkaradzic.github.io/posts/orthodoxc++/) minus C++ 14 support (see above)
 * Please note that we are going to move towards C99 usage more and more because this language makes it easier to develop high-performance applications in team settings. With the increased call numbers of modern APIs and the always performance-detoriating C++ features, C++ is increasingly becoming a productivity and run-time performance challenge. C is also a better starting point for supporting many platforms compared to other languages like RUST.

---

# Support for Education 
Let us know if you are in need of educational support.

---

# Open-Source Libraries
The Forge utilizes the following Open-Source libraries:
* [Fontstash](https://github.com/memononen/fontstash)
* [Vectormath](https://github.com/glampert/vectormath)
* [Nothings](https://github.com/nothings/stb) single file libs 
  * [stb_image.h](https://github.com/nothings/stb/blob/master/stb_image.h)
  * [stb_image_resize.h](https://github.com/nothings/stb/blob/master/deprecated/stb_image_resize.h)
  * [stb_image_write.h](https://github.com/nothings/stb/blob/master/stb_image_write.h)
  * [stb_ds](https://github.com/nothings/stb/blob/master/stb_ds.h)
* [SPIRV_Cross](https://github.com/KhronosGroup/SPIRV-Cross)
* [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
* [D3D12 Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator)
* [WinPixEventRuntime](https://blogs.msdn.microsoft.com/pix/winpixeventruntime/)
* [Fluid Studios Memory Manager](http://www.paulnettle.com/)
* [volk Metaloader for Vulkan](https://github.com/zeux/volk)
* [DirectX Shader Compiler](https://github.com/Microsoft/DirectXShaderCompiler)
* [Ozz Animation System](https://github.com/guillaumeblanc/ozz-animation)
* [Lua Scripting System](https://www.lua.org/)
* [TressFX](https://github.com/GPUOpen-Effects/TressFX)
* [meshoptimizer](https://github.com/zeux/meshoptimizer)
* [TinyImageFormat](https://github.com/DeanoC/tiny_imageformat)
* [flecs](https://github.com/SanderMertens/flecs)
* [CPU Features](https://github.com/google/cpu_features)
* [HIDAPI](https://github.com/libusb/hidapi)
* [bstrlib](https://github.com/websnarf/bstrlib)
* [cr](https://github.com/fungos/cr)
* [Nuklear](https://github.com/immediate-mode-ui/nuklear)
