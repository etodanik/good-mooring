# Forge Integration Changes

Changes to `Direct3D12MemoryAllocator.h` to hook into Forge infrastructure instead of STL.
Same pattern as `TFVulkanMemoryAllocatorConfig.h` for VMA.

## `D3D12MA_ASSERT` -> `ASSERT()`

Swapped `assert()` + `<cassert>` for Forge's `ASSERT()` from `ILog.h`.

## Default allocator -> `tf_memalign` / `tf_free`

`DefaultAllocate` and `DefaultFree` are the fallback path when the caller passes
`pAllocationCallbacks = NULL`. All internal allocations route through them, so swapping
to `tf_memalign` / `tf_free` (from `IMemory.h`) gets everything under Forge's memory
tracking for free. `<malloc.h>` removed.

## `Mutex` -> `TFMutex`

The `D3D12MA_MUTEX` wrapper used `std::mutex` internally. Replaced with `TFMutex` from
`IThread.h` (`initMutex` / `exitMutex` / `acquireMutex` / `releaseMutex`).
`<mutex>` removed.

## `RWMutex` -> `TFMutex`

The `D3D12MA_RW_MUTEX` wrapper used `SRWLOCK` on Windows and `std::shared_timed_mutex`
elsewhere. Forge has no RW mutex, so both sides just use a plain `TFMutex` — same as
`TFVmaRWMutex` in `TFVulkanMemoryAllocatorConfig.h`. Readers block each other but that's
fine, consistent with the rest of the codebase. Collapsed the `#ifdef _WIN32` split,
removed `<shared_mutex>`.

## Xbox GDK compatibility block

Added an `#if defined(XBOX)` block near the top of the header (before the
`D3D12MA_D3D12_HEADERS_ALREADY_INCLUDED` check) that handles three GDK quirks:

- `D3D12MA_D3D12_HEADERS_ALREADY_INCLUDED` — GDK's d3d12.h/dxgi are already in scope;
  skip the PC SDK includes.
- `__ID3D12Device1_INTERFACE_DEFINED__` — the GDK defines `D3D12_RESIDENCY_PRIORITY` but
  not this guard, causing D3D12MA's fallback typedef to collide. Declaring the guard
  suppresses D3D12MA's copy; `ID3D12Device1` is present in the GDK so the related
  code paths stay enabled.
- `D3D12MA_OPTIONS16_SUPPORTED 0` / `D3D12MA_TIGHT_ALIGNMENT_SUPPORTED 0` —
  `D3D12_FEATURE_DATA_D3D12_OPTIONS16` and `D3D12_FEATURE_DATA_TIGHT_ALIGNMENT` are
  PC-only types absent from the GDK. The SDK-version auto-detection would enable them,
  so they're forced off.
