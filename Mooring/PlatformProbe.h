#pragma once
struct TFRenderer;
struct TFQueue;
// Startup-only readback verifies the restored shader ABI and texture upload.
bool runPlatformProbe(TFRenderer* renderer, TFQueue* queue);
