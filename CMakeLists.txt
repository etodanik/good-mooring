cmake_minimum_required(VERSION 3.25)
project(MooringSimulator VERSION 0.1 LANGUAGES C CXX OBJC OBJCXX)

if(NOT APPLE)
  message(FATAL_ERROR "The first platform checkpoint requires macOS and the Metal SDK.")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0)
set(CMAKE_XCODE_GENERATE_SCHEME ON)
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)

set(F Common_3)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(MOORING_ASSET_BUILD "${CMAKE_BINARY_DIR}/../")
add_custom_command(
  OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/${F}/Utilities/Math/MathLibrary.h"
         "${CMAKE_CURRENT_SOURCE_DIR}/${F}/Tools/ForgeShadingLanguage/includes/MathLibrary.h"
  COMMAND ${Python3_EXECUTABLE} ${F}/Tools/ForgeShadingLanguage/fsl.py -l ISPC -t MACOS -a ARM64
    -d "${MOORING_ASSET_BUILD}/shaders" -b "${MOORING_ASSET_BUILD}/shaders/bin"
    --ispc_headerDestinationCPU ${F}/Utilities/Math
    --ispc_headerDestinationShader ${F}/Tools/ForgeShadingLanguage/includes
    ${F}/Utilities/Math/MathShaders.list
  DEPENDS ${F}/Utilities/Math/MathShaders.list
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} VERBATIM)
add_custom_target(MooringMath DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${F}/Utilities/Math/MathLibrary.h")
set(FORGE_SOURCES
  ${F}/Graphics/GraphicsConfig.c
  ${F}/Graphics/Metal/MetalRenderer.mm
  ${F}/Graphics/Metal/MetalRaytracing.mm
  ${F}/OS/CPUConfig.cpp
  ${F}/OS/Darwin/CocoaFileSystem.mm
  ${F}/OS/Darwin/CocoaToolsFileSystem.mm
  ${F}/OS/Darwin/DarwinLog.c
  ${F}/OS/Darwin/DarwinThread.c
  ${F}/OS/Darwin/DarwinInput.mm
  ${F}/OS/Darwin/macOSBase.mm
  ${F}/OS/Darwin/macOSWindow.mm
  ${F}/OS/Darwin/macOSAppDelegate.m
  ${F}/OS/WindowSystem/WindowSystem.cpp
  ${F}/OS/Input/HID/HIDInput.cpp
  ${F}/OS/ThirdParty/OpenSource/hidapi/mac/hid.c
  ${F}/OS/ThirdParty/OpenSource/cpu_features/src/impl_aarch64_iOS.c
  ${F}/Utilities/MemoryTracking/MemoryTracking.c
  ${F}/Utilities/Log/Log.c
  ${F}/Utilities/Timer.c
  ${F}/Utilities/Math/StbDs.c
  ${F}/Utilities/Math/Algorithms.c
  ${F}/Utilities/Threading/ThreadSystem.c
  ${F}/Utilities/FileSystem/FileSystem.c
  ${F}/Utilities/FileSystem/UnixFileSystem.c
  ${F}/Utilities/FileSystem/ToolFileSystem.c
  ${F}/Utilities/Reflection/Serialization.c
  ${F}/Utilities/Reflection/DataTypeReflection.c
  ${F}/Utilities/NodeGraph/NodeGraph.c
  ${F}/Utilities/ThirdParty/OpenSource/bstrlib/bstrlib.c
  ${F}/Utilities/ThirdParty/OpenSource/lz4/lz4.c
  ${F}/Resources/ResourceLoader/ResourceLoader.cpp
  ${F}/Application/Camera.cpp
  ${F}/Application/Screenshot/Screenshot.cpp
  ${F}/Application/UI/UI.cpp
  ${F}/Application/Fonts/FontSystem.cpp
  ${F}/Application/Fonts/stbtt.cpp
  ${F}/Application/Profiler/ProfilerBase.cpp
  ${F}/Application/Profiler/GpuProfiler.cpp
  ${F}/Game/ThirdParty/OpenSource/flecs/flecs.c
)
file(GLOB ZSTD_SOURCES CONFIGURE_DEPENDS
  ${F}/Utilities/ThirdParty/OpenSource/zstd/common/*.c
  ${F}/Utilities/ThirdParty/OpenSource/zstd/decompress/*.c)
list(APPEND FORGE_SOURCES ${ZSTD_SOURCES})
foreach(source IN LISTS FORGE_SOURCES)
  if(source MATCHES "\\.cpp$")
    set_source_files_properties(${source} PROPERTIES LANGUAGE OBJCXX)
  elseif(source MATCHES "\\.c$")
    set_source_files_properties(${source} PROPERTIES LANGUAGE OBJC)
  endif()
endforeach()
add_library(Forge STATIC ${FORGE_SOURCES})
add_dependencies(Forge MooringMath)
target_include_directories(Forge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(Forge PUBLIC -include ${CMAKE_CURRENT_SOURCE_DIR}/Tools/Mooring/BuildConfig.h)
target_compile_options(Forge PRIVATE -fobjc-arc -Wno-deprecated-declarations -Wno-nullability-completeness -Wno-incompatible-pointer-types-discards-qualifiers)
target_compile_definitions(Forge PRIVATE ZSTD_DISABLE_ASM)
target_link_libraries(Forge PUBLIC
  "-framework Cocoa" "-framework Metal" "-framework MetalKit"
  "-framework MetalPerformanceShaders" "-framework IOKit" "-framework QuartzCore"
  "-framework CoreVideo" "-framework GameController" "-framework CoreHaptics"
  "-framework Carbon" "-framework Security")

set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
include(Tools/Mooring/Tracy.cmake)
add_subdirectory(${F}/Game/ThirdParty/OpenSource/Jolt/Build)

set(FONT_TOOL_SOURCES Tools/Mooring/FontTool.cpp ${F}/Tools/AssetPipeline/src/AssetPipeline_Fonts.cpp)
set_source_files_properties(${FONT_TOOL_SOURCES} PROPERTIES LANGUAGE OBJCXX)
add_executable(MooringFontTool ${FONT_TOOL_SOURCES})
target_link_libraries(MooringFontTool PRIVATE Forge)
target_compile_options(MooringFontTool PRIVATE -fobjc-arc)

set(MOORING_SOURCE Examples_3/Unit_Tests/src/40_MooringSimulator)
set(CORE_SOURCES ${MOORING_SOURCE}/Simulation/Simulation.cpp ${MOORING_SOURCE}/Simulation/Hydrodynamics.cpp
  ${MOORING_SOURCE}/Water/Ocean.cpp ${MOORING_SOURCE}/Simulation/Hydrostatics.cpp)
set_source_files_properties(${CORE_SOURCES} PROPERTIES LANGUAGE OBJCXX)
add_library(MooringCore STATIC ${CORE_SOURCES})
target_link_libraries(MooringCore PUBLIC Forge Jolt)
if(MOORING_TRACY)
  target_sources(MooringCore PRIVATE Tools/Mooring/TracyJolt.cpp)
  set_source_files_properties(Tools/Mooring/Tracy.cpp Tools/Mooring/TracyJolt.cpp PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(Jolt PUBLIC Tracy::TracyClient)
endif()
target_compile_options(MooringCore PRIVATE -fobjc-arc -fno-exceptions -fno-rtti)
set_source_files_properties(${MOORING_SOURCE}/Water/Ocean.cpp PROPERTIES COMPILE_OPTIONS "-O2")
set_source_files_properties(${MOORING_SOURCE}/Camera.cpp PROPERTIES LANGUAGE OBJCXX)
add_library(MooringCamera STATIC ${MOORING_SOURCE}/Camera.cpp)
target_link_libraries(MooringCamera PUBLIC MooringCore)
set_source_files_properties(${MOORING_SOURCE}/Tests/SimulationTests.cpp PROPERTIES LANGUAGE OBJCXX)
add_executable(MooringTests ${MOORING_SOURCE}/Tests/SimulationTests.cpp)
target_link_libraries(MooringTests PRIVATE MooringCamera)
target_compile_options(MooringTests PRIVATE -fobjc-arc)
set_source_files_properties(${MOORING_SOURCE}/Tests/OceanTests.cpp PROPERTIES LANGUAGE OBJCXX)
add_executable(MooringOceanTests ${MOORING_SOURCE}/Tests/OceanTests.cpp)
target_link_libraries(MooringOceanTests PRIVATE MooringCore)
target_compile_options(MooringOceanTests PRIVATE -fobjc-arc)
set_source_files_properties(${MOORING_SOURCE}/Tests/PhysicsTests.cpp PROPERTIES LANGUAGE OBJCXX)
add_executable(MooringPhysicsTests ${MOORING_SOURCE}/Tests/PhysicsTests.cpp)
target_link_libraries(MooringPhysicsTests PRIVATE MooringCore)
target_compile_options(MooringPhysicsTests PRIVATE -fobjc-arc)
add_custom_target(MooringResources
  COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/Tools/Mooring/build_resources.py"
    --root "${CMAKE_CURRENT_SOURCE_DIR}" --output "${MOORING_ASSET_BUILD}"
    --font-tool "$<TARGET_FILE:MooringFontTool>"
  DEPENDS MooringFontTool
  VERBATIM)
set(APP_SOURCES ${MOORING_SOURCE}/MooringApp.cpp ${MOORING_SOURCE}/PlatformProbe.cpp
  ${MOORING_SOURCE}/Scene.cpp ${MOORING_SOURCE}/Interaction.cpp ${MOORING_SOURCE}/Water/OceanRenderer.cpp
  ${MOORING_SOURCE}/Water/PhysicsConfig.cpp ${MOORING_SOURCE}/Water/SeaLab.cpp)
set_source_files_properties(${APP_SOURCES} PROPERTIES LANGUAGE OBJCXX)
add_executable(MooringSimulator MACOSX_BUNDLE ${APP_SOURCES})
add_dependencies(MooringSimulator MooringResources)
target_link_libraries(MooringSimulator PRIVATE MooringCamera)
target_compile_options(MooringSimulator PRIVATE -fobjc-arc -fno-exceptions -fno-rtti)
set_target_properties(MooringSimulator PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Tools/Mooring/Info.plist.in
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER com.mooring.simulator)
add_custom_command(TARGET MooringSimulator POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:MooringSimulator>/Resources/Shaders/MACOS"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/../shaders/bin/MACOS" "$<TARGET_BUNDLE_CONTENT_DIR:MooringSimulator>/Resources/Shaders/MACOS"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/${MOORING_SOURCE}/Assets" "$<TARGET_BUNDLE_CONTENT_DIR:MooringSimulator>/Resources"
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/../fonts/AtkinsonHyperlegible-Regular.msdf" "$<TARGET_BUNDLE_CONTENT_DIR:MooringSimulator>/Resources/Fonts")
