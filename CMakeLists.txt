cmake_minimum_required(VERSION 3.25)

project(MooringSimulator VERSION 0.1 LANGUAGES C CXX OBJC OBJCXX)

if(NOT APPLE)
    message(FATAL_ERROR "The first platform checkpoint requires macOS and the Metal SDK.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0)
set(CMAKE_XCODE_GENERATE_SCHEME ON)
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)

set(COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Common")
set(GAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Mooring")
set(TOOLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Tools/Mooring")
set(MOORING_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")

option(MOORING_PROFILE "Enable Tracy and expose the performance profiling run target" OFF)
set(MOORING_PROFILE_FRAMES 1800 CACHE STRING "Number of beauty frames captured by MooringProfile")
set(MOORING_QA_SECONDS 12 CACHE STRING "Simulated seconds used by MooringQAMotion")
set(MOORING_RUN_ARGS "" CACHE STRING "Additional arguments passed to MooringRun")

foreach(value MOORING_PROFILE_FRAMES MOORING_QA_SECONDS)
    if(NOT "${${value}}" MATCHES "^[0-9]+$")
        message(FATAL_ERROR "${value} must be a positive integer")
    endif()
endforeach()
if(MOORING_PROFILE_FRAMES LESS 300 OR MOORING_PROFILE_FRAMES GREATER 18000)
    message(FATAL_ERROR "MOORING_PROFILE_FRAMES must be between 300 and 18000")
endif()
if(MOORING_QA_SECONDS LESS 1 OR MOORING_QA_SECONDS GREATER 60)
    message(FATAL_ERROR "MOORING_QA_SECONDS must be between 1 and 60")
endif()

# A single option makes the profiling build reproducible. Tracy.cmake still
# exposes MOORING_TRACY for callers that need instrumentation without the
# profile run target.
if(MOORING_PROFILE)
    set(MOORING_TRACY ON CACHE BOOL "Instrument Mooring, Forge and Jolt with Tracy" FORCE)
    if(NOT CMAKE_GENERATOR MATCHES "Xcode" AND NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
    endif()
    set(CMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym")
    set(CMAKE_XCODE_ATTRIBUTE_GCC_OPTIMIZATION_LEVEL 3)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Forge runtime ----------------------------------------------------------------
set(MATH_LIBRARY "${COMMON_DIR}/Utilities/Math/MathLibrary.h")
set(MATH_SHADER_LIBRARY "${COMMON_DIR}/Tools/ForgeShadingLanguage/includes/MathLibrary.h")
add_custom_command(
    OUTPUT "${MATH_LIBRARY}" "${MATH_SHADER_LIBRARY}"
    COMMAND ${Python3_EXECUTABLE} "${COMMON_DIR}/Tools/ForgeShadingLanguage/fsl.py"
        -l ISPC -t MACOS -a ARM64
        -d "${MOORING_GENERATED_DIR}/shaders"
        -b "${MOORING_GENERATED_DIR}/shaders/bin"
        --ispc_headerDestinationCPU "${COMMON_DIR}/Utilities/Math"
        --ispc_headerDestinationShader "${COMMON_DIR}/Tools/ForgeShadingLanguage/includes"
        "${COMMON_DIR}/Utilities/Math/MathShaders.list"
    DEPENDS "${COMMON_DIR}/Utilities/Math/MathShaders.list"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM)
add_custom_target(MooringMath DEPENDS "${MATH_LIBRARY}" "${MATH_SHADER_LIBRARY}")

set(FORGE_SOURCES
    "${COMMON_DIR}/Graphics/GraphicsConfig.c"
    "${COMMON_DIR}/Graphics/Metal/MetalRenderer.mm"
    "${COMMON_DIR}/Graphics/Metal/MetalRaytracing.mm"
    "${COMMON_DIR}/OS/CPUConfig.cpp"
    "${COMMON_DIR}/OS/Darwin/CocoaFileSystem.mm"
    "${COMMON_DIR}/OS/Darwin/CocoaToolsFileSystem.mm"
    "${COMMON_DIR}/OS/Darwin/DarwinLog.c"
    "${COMMON_DIR}/OS/Darwin/DarwinThread.c"
    "${COMMON_DIR}/OS/Darwin/DarwinInput.mm"
    "${COMMON_DIR}/OS/Darwin/macOSBase.mm"
    "${COMMON_DIR}/OS/Darwin/macOSWindow.mm"
    "${COMMON_DIR}/OS/Darwin/macOSAppDelegate.m"
    "${COMMON_DIR}/OS/WindowSystem/WindowSystem.cpp"
    "${COMMON_DIR}/OS/Input/HID/HIDInput.cpp"
    "${COMMON_DIR}/OS/ThirdParty/OpenSource/hidapi/mac/hid.c"
    "${COMMON_DIR}/OS/ThirdParty/OpenSource/cpu_features/src/impl_aarch64_iOS.c"
    "${COMMON_DIR}/Utilities/MemoryTracking/MemoryTracking.c"
    "${COMMON_DIR}/Utilities/Log/Log.c"
    "${COMMON_DIR}/Utilities/Timer.c"
    "${COMMON_DIR}/Utilities/Math/StbDs.c"
    "${COMMON_DIR}/Utilities/Math/Algorithms.c"
    "${COMMON_DIR}/Utilities/Threading/ThreadSystem.c"
    "${COMMON_DIR}/Utilities/FileSystem/FileSystem.c"
    "${COMMON_DIR}/Utilities/FileSystem/UnixFileSystem.c"
    "${COMMON_DIR}/Utilities/FileSystem/ToolFileSystem.c"
    "${COMMON_DIR}/Utilities/Reflection/Serialization.c"
    "${COMMON_DIR}/Utilities/Reflection/DataTypeReflection.c"
    "${COMMON_DIR}/Utilities/NodeGraph/NodeGraph.c"
    "${COMMON_DIR}/Utilities/ThirdParty/OpenSource/bstrlib/bstrlib.c"
    "${COMMON_DIR}/Utilities/ThirdParty/OpenSource/lz4/lz4.c"
    "${COMMON_DIR}/Resources/ResourceLoader/ResourceLoader.cpp"
    "${COMMON_DIR}/Tools/ReloadServer/ReloadClient.cpp"
    "${COMMON_DIR}/Tools/Network/Network.c"
    "${COMMON_DIR}/Application/Camera.cpp"
    "${COMMON_DIR}/Application/Screenshot/Screenshot.cpp"
    "${COMMON_DIR}/Application/UI/UI.cpp"
    "${COMMON_DIR}/Application/Fonts/FontSystem.cpp"
    "${COMMON_DIR}/Application/Fonts/stbtt.cpp"
    "${COMMON_DIR}/Application/Profiler/ProfilerBase.cpp"
    "${COMMON_DIR}/Application/Profiler/GpuProfiler.cpp"
    "${COMMON_DIR}/Game/ThirdParty/OpenSource/flecs/flecs.c")
file(GLOB ZSTD_SOURCES CONFIGURE_DEPENDS
    "${COMMON_DIR}/Utilities/ThirdParty/OpenSource/zstd/common/*.c"
    "${COMMON_DIR}/Utilities/ThirdParty/OpenSource/zstd/decompress/*.c")
list(APPEND FORGE_SOURCES ${ZSTD_SOURCES})

foreach(source IN LISTS FORGE_SOURCES)
    if(source MATCHES "\\.cpp$")
        set_source_files_properties("${source}" PROPERTIES LANGUAGE OBJCXX)
    elseif(source MATCHES "\\.c$")
        set_source_files_properties("${source}" PROPERTIES LANGUAGE OBJC)
    endif()
endforeach()

add_library(Forge STATIC ${FORGE_SOURCES})
add_dependencies(Forge MooringMath)
target_include_directories(Forge PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(Forge PUBLIC -include "${TOOLS_DIR}/BuildConfig.h")
target_compile_options(Forge PRIVATE
    -fobjc-arc
    -Wno-deprecated-declarations
    -Wno-nullability-completeness
    -Wno-incompatible-pointer-types-discards-qualifiers)
target_compile_definitions(Forge PRIVATE ZSTD_DISABLE_ASM)
target_compile_definitions(Forge PUBLIC $<$<CONFIG:Debug>:MOORING_SHADER_LAB>)
target_link_libraries(Forge PUBLIC
    "-framework Cocoa"
    "-framework Metal"
    "-framework MetalKit"
    "-framework MetalPerformanceShaders"
    "-framework IOKit"
    "-framework QuartzCore"
    "-framework CoreVideo"
    "-framework GameController"
    "-framework CoreHaptics"
    "-framework Carbon"
    "-framework Security")

# Disable Forge's sample applications. Mooring owns the executable and its
# QA/test entry points now.
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
include("${TOOLS_DIR}/Tracy.cmake")
add_subdirectory("${COMMON_DIR}/Game/ThirdParty/OpenSource/Jolt/Build")

# Project tools and simulation libraries --------------------------------------
set(FONT_TOOL_SOURCES
    "${TOOLS_DIR}/FontTool.cpp"
    "${COMMON_DIR}/Tools/AssetPipeline/src/AssetPipeline_Fonts.cpp")
set_source_files_properties(${FONT_TOOL_SOURCES} PROPERTIES LANGUAGE OBJCXX)
add_executable(MooringFontTool ${FONT_TOOL_SOURCES})
target_link_libraries(MooringFontTool PRIVATE Forge)
target_compile_options(MooringFontTool PRIVATE -fobjc-arc)

set(CORE_SOURCES
    "${GAME_DIR}/Simulation/Simulation.cpp"
    "${GAME_DIR}/Simulation/Hydrodynamics.cpp"
    "${GAME_DIR}/Water/Ocean.cpp"
    "${GAME_DIR}/Simulation/Hydrostatics.cpp")
set_source_files_properties(${CORE_SOURCES} PROPERTIES LANGUAGE OBJCXX)
add_library(MooringCore STATIC ${CORE_SOURCES})
target_link_libraries(MooringCore PUBLIC Forge Jolt)
if(MOORING_TRACY)
    target_sources(MooringCore PRIVATE "${TOOLS_DIR}/TracyJolt.cpp")
    set_source_files_properties("${TOOLS_DIR}/Tracy.cpp" "${TOOLS_DIR}/TracyJolt.cpp" PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(Jolt PUBLIC Tracy::TracyClient)
endif()
target_compile_options(MooringCore PRIVATE -fobjc-arc -fno-exceptions -fno-rtti)
set_source_files_properties("${GAME_DIR}/Water/Ocean.cpp" PROPERTIES COMPILE_OPTIONS "-O2")

set_source_files_properties("${GAME_DIR}/Camera.cpp" PROPERTIES LANGUAGE OBJCXX)
add_library(MooringCamera STATIC "${GAME_DIR}/Camera.cpp")
target_link_libraries(MooringCamera PUBLIC MooringCore)

function(add_mooring_test target source library)
    set_source_files_properties("${source}" PROPERTIES LANGUAGE OBJCXX)
    add_executable("${target}" "${source}")
    target_link_libraries("${target}" PRIVATE "${library}")
    target_compile_options("${target}" PRIVATE -fobjc-arc)
endfunction()

add_mooring_test(MooringTests "${GAME_DIR}/Tests/SimulationTests.cpp" MooringCamera)
add_mooring_test(MooringOceanTests "${GAME_DIR}/Tests/OceanTests.cpp" MooringCore)
add_mooring_test(MooringPhysicsTests "${GAME_DIR}/Tests/PhysicsTests.cpp" MooringCore)

# Generated resources ----------------------------------------------------------
file(GLOB_RECURSE MOORING_SHADER_SOURCES CONFIGURE_DEPENDS
    "${GAME_DIR}/Shaders/*"
    "${COMMON_DIR}/Application/UI/Shaders/FSL/*"
    "${COMMON_DIR}/Application/Fonts/Shaders/FSL/*"
    "${COMMON_DIR}/Application/Screenshot/Shaders/FSL/*")
# FSL's incremental cache does not track compiler flags. Keep each build
# configuration's resources separate so Debug always includes shader sources.
set(MOORING_RESOURCE_DIR "${MOORING_GENERATED_DIR}/$<CONFIG>")
set(MOORING_RESOURCE_STAMP "${MOORING_RESOURCE_DIR}/resources.stamp")
set(MOORING_FONT_SOURCE "${GAME_DIR}/Assets/Fonts/Inter-Regular.ttf")
add_custom_command(
    OUTPUT "${MOORING_RESOURCE_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MOORING_RESOURCE_DIR}"
    COMMAND ${Python3_EXECUTABLE} "${TOOLS_DIR}/build_resources.py"
        --root "${CMAKE_CURRENT_SOURCE_DIR}"
        --output "${MOORING_RESOURCE_DIR}"
        --configuration "$<CONFIG>"
        --font-tool "$<TARGET_FILE:MooringFontTool>"
    COMMAND ${CMAKE_COMMAND} -E touch "${MOORING_RESOURCE_STAMP}"
    DEPENDS MooringFontTool
        "${TOOLS_DIR}/build_resources.py"
        "${TOOLS_DIR}/shader_previews.py"
        "${MOORING_FONT_SOURCE}"
        ${MOORING_SHADER_SOURCES}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM)
add_custom_target(MooringResources DEPENDS "${MOORING_RESOURCE_STAMP}")

# Application ------------------------------------------------------------------
set(APP_SOURCES
    "${GAME_DIR}/MooringApp.cpp"
    "${GAME_DIR}/PlatformProbe.cpp"
    "${GAME_DIR}/Scene.cpp"
    "${GAME_DIR}/Interaction.cpp"
    "${GAME_DIR}/Water/OceanRenderer.cpp"
    "${GAME_DIR}/Water/PhysicsConfig.cpp"
    "${GAME_DIR}/Water/SeaLab.cpp"
    "${GAME_DIR}/Water/ShaderLab.cpp")
set_source_files_properties(${APP_SOURCES} PROPERTIES LANGUAGE OBJCXX)
add_executable(MooringSimulator MACOSX_BUNDLE ${APP_SOURCES})
add_dependencies(MooringSimulator MooringResources)
target_link_libraries(MooringSimulator PRIVATE MooringCamera)
target_compile_options(MooringSimulator PRIVATE -fobjc-arc -fno-exceptions -fno-rtti)
target_compile_definitions(MooringSimulator PRIVATE
    MOORING_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    MOORING_PYTHON="${Python3_EXECUTABLE}")
set_target_properties(MooringSimulator PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${TOOLS_DIR}/Info.plist.in"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER com.mooring.simulator
    XCODE_SCHEME_ENABLE_GPU_FRAME_CAPTURE_MODE Metal)
# Ninja/Make must run the bundle copy step after a shader-only rebuild too.
set_property(TARGET MooringSimulator APPEND PROPERTY LINK_DEPENDS "${MOORING_RESOURCE_STAMP}")
add_custom_command(TARGET MooringSimulator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf
        "$<TARGET_BUNDLE_CONTENT_DIR:MooringSimulator>/Resources/Shaders"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_BUNDLE_CONTENT_DIR:MooringSimulator>/Resources/Shaders/MACOS"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${MOORING_RESOURCE_DIR}/shaders/bin"
        "$<TARGET_BUNDLE_CONTENT_DIR:MooringSimulator>/Resources/Shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${GAME_DIR}/Assets"
        "$<TARGET_BUNDLE_CONTENT_DIR:MooringSimulator>/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${MOORING_RESOURCE_DIR}/fonts/Inter-Regular.msdf"
        "$<TARGET_BUNDLE_CONTENT_DIR:MooringSimulator>/Resources/Fonts/Inter-Regular.msdf")

# CTest and command-line test targets -----------------------------------------
include(CTest)
if(BUILD_TESTING)
    add_test(NAME Mooring.Simulation COMMAND "$<TARGET_FILE:MooringTests>")
    add_test(NAME Mooring.Ocean COMMAND "$<TARGET_FILE:MooringOceanTests>")
    add_test(NAME Mooring.Physics COMMAND "$<TARGET_FILE:MooringPhysicsTests>")
    set_tests_properties(
        Mooring.Simulation Mooring.Ocean Mooring.Physics
        PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" LABELS "mooring;regression")
    if(TARGET MooringTracyTests)
        add_test(NAME Mooring.Tracy COMMAND "$<TARGET_FILE:MooringTracyTests>")
        set_tests_properties(Mooring.Tracy PROPERTIES LABELS "mooring;profiling")
    endif()
endif()

set(MOORING_TEST_TARGETS MooringTests MooringOceanTests MooringPhysicsTests)
if(TARGET MooringTracyTests)
    list(APPEND MOORING_TEST_TARGETS MooringTracyTests)
endif()
add_custom_target(MooringTestSuite
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/test-results"
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${CMAKE_BINARY_DIR}" --output-on-failure -C "$<CONFIG>"
    DEPENDS ${MOORING_TEST_TARGETS}
    USES_TERMINAL)
add_custom_target(MooringPhysicsCalibration
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/test-results"
    COMMAND "$<TARGET_FILE:MooringPhysicsTests>"
        "${CMAKE_BINARY_DIR}/test-results/physics-calibration.csv"
    DEPENDS MooringPhysicsTests
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    USES_TERMINAL)

# QA and profiling run targets -------------------------------------------------
set(MOORING_APP_BINARY "$<TARGET_BUNDLE_DIR:MooringSimulator>/Contents/MacOS/MooringSimulator")
set(MOORING_RUNTIME_DIR "$<TARGET_FILE_DIR:MooringSimulator>")
set(MOORING_QA_DIR "$<TARGET_FILE_DIR:MooringSimulator>/QA")

function(add_mooring_run_target target)
    add_custom_target("${target}"
        COMMAND ${CMAKE_COMMAND} -E chdir "${MOORING_RUNTIME_DIR}" "${MOORING_APP_BINARY}" ${ARGN}
        DEPENDS MooringSimulator
        USES_TERMINAL)
endfunction()

separate_arguments(MOORING_RUN_ARGS_LIST NATIVE_COMMAND "${MOORING_RUN_ARGS}")
add_mooring_run_target(MooringRun ${MOORING_RUN_ARGS_LIST})

function(add_mooring_qa_target target preset option)
    add_custom_target("${target}"
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${MOORING_QA_DIR}"
        COMMAND ${CMAKE_COMMAND} -E chdir "${MOORING_RUNTIME_DIR}" "${MOORING_APP_BINARY}" "${option}"
        COMMAND ${Python3_EXECUTABLE} "${TOOLS_DIR}/check_water_captures.py"
            "${MOORING_QA_DIR}" --preset "${preset}"
        DEPENDS MooringSimulator
        USES_TERMINAL)
endfunction()

add_mooring_qa_target(MooringQA balanced --water-qa)
add_mooring_qa_target(MooringQAUltra ultra --water-qa-ultra)
add_custom_target(MooringQAMotion
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${MOORING_QA_DIR}"
    COMMAND ${CMAKE_COMMAND} -E chdir "${MOORING_RUNTIME_DIR}" "${MOORING_APP_BINARY}"
        --water-qa-ultra --water-qa-scene=9 --water-qa-motion --water-qa-seconds=${MOORING_QA_SECONDS}
    COMMAND ${Python3_EXECUTABLE} "${TOOLS_DIR}/check_water_captures.py"
        "${MOORING_QA_DIR}" --preset ultra
    DEPENDS MooringSimulator
    USES_TERMINAL)
add_custom_target(MooringQAResize
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${MOORING_QA_DIR}"
    COMMAND ${CMAKE_COMMAND} -E chdir "${MOORING_RUNTIME_DIR}" "${MOORING_APP_BINARY}" --water-qa-resize
    DEPENDS MooringSimulator
    USES_TERMINAL)
add_custom_target(MooringQAReload
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${MOORING_QA_DIR}"
    COMMAND ${CMAKE_COMMAND} -E chdir "${MOORING_RUNTIME_DIR}" "${MOORING_APP_BINARY}" --water-qa-reload
    DEPENDS MooringSimulator
    USES_TERMINAL)

if(MOORING_PROFILE)
    add_custom_target(MooringProfile
        COMMAND ${CMAKE_COMMAND} -E chdir "${MOORING_RUNTIME_DIR}" "${MOORING_APP_BINARY}"
            --tracy-wait --water-qa-ultra --water-qa-scene=2 --water-profile
            --water-benchmark=${MOORING_PROFILE_FRAMES} --benchmark-unthrottled
        DEPENDS MooringSimulator
        USES_TERMINAL)
else()
    add_custom_target(MooringProfile
        COMMAND ${CMAKE_COMMAND} -E echo
            "MooringProfile requires a configure pass with -DMOORING_PROFILE=ON"
        COMMAND ${CMAKE_COMMAND} -E false
        USES_TERMINAL)
endif()

add_custom_target(MooringClean
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${MOORING_GENERATED_DIR}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${MOORING_QA_DIR}"
    COMMAND ${CMAKE_COMMAND} -E echo "Removed generated Mooring resources and QA captures.")
