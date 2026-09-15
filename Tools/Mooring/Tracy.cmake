option(MOORING_TRACY "Instrument Mooring, Forge and Jolt with Tracy" OFF)
option(MOORING_TRACY_FINE "Also instrument high-frequency water and hull queries" OFF)
set(MOORING_TRACY_STACK_DEPTH 8 CACHE STRING "Allocation callstack depth (0 disables allocation stacks)")
set(MOORING_TRACY_ZONE_STACK_DEPTH 0 CACHE STRING "CPU zone callstack depth (0 keeps source locations only)")

if(MOORING_TRACY)
    foreach(depth MOORING_TRACY_STACK_DEPTH MOORING_TRACY_ZONE_STACK_DEPTH)
        if(NOT "${${depth}}" MATCHES "^[0-9]+$" OR ${depth} GREATER 62)
            message(FATAL_ERROR "${depth} must be an integer from 0 to 62")
        endif()
    endforeach()
    include(FetchContent)
    set(TRACY_ENABLE ON CACHE BOOL "Enable Tracy" FORCE)
    option(TRACY_ON_DEMAND "Only record while a capture is connected" ON)
    option(TRACY_ONLY_LOCALHOST "Listen on localhost" ON)
    option(TRACY_NO_BROADCAST "Disable network discovery" ON)
    FetchContent_Declare(tracy
        URL https://github.com/wolfpld/tracy/archive/refs/tags/v0.14.1.tar.gz
        URL_HASH SHA256=bf4af567e9c7524d07f3caa745fad02fb33bd5694f11910750382d1efbb251c1
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(tracy)
    # Keep profiler transport fast even when investigating a Debug application.
    target_compile_options(TracyClient PRIVATE -O2)
    target_link_libraries(Forge PUBLIC Tracy::TracyClient)
    target_sources(Forge PRIVATE Tools/Mooring/Tracy.cpp Tools/Mooring/TracyMetal.mm)
    target_compile_definitions(Forge PUBLIC
        MOORING_TRACY_STACK_DEPTH=${MOORING_TRACY_STACK_DEPTH}
        MOORING_TRACY_ZONE_STACK_DEPTH=${MOORING_TRACY_ZONE_STACK_DEPTH}
        $<$<BOOL:${MOORING_TRACY_FINE}>:MOORING_TRACY_FINE>)
    target_compile_options(Forge PUBLIC -g -fno-omit-frame-pointer)
    set_source_files_properties(Tools/Mooring/TracyTests.cpp PROPERTIES LANGUAGE OBJCXX)
    add_executable(MooringTracyTests Tools/Mooring/TracyTests.cpp)
    target_link_libraries(MooringTracyTests PRIVATE Forge)
    target_compile_options(MooringTracyTests PRIVATE -fobjc-arc)
    set(CMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym")
    set(PROFILER_IN_DISTRIBUTION ON CACHE BOOL "Jolt external profiling in every configuration" FORCE)
    set(JPH_USE_EXTERNAL_PROFILE ON CACHE BOOL "Use Tracy for Jolt measurements" FORCE)
else()
    set(PROFILER_IN_DISTRIBUTION OFF CACHE BOOL "" FORCE)
    set(JPH_USE_EXTERNAL_PROFILE OFF CACHE BOOL "" FORCE)
endif()
