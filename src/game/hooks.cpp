// Hooks for default.xbe (MVP 06 NCAA Baseball, XDK 5849). A strong definition replaces the
// recompiled function at that address; call __imp__sub_XXXXXXXX to reach the original.
#include <cstring>

#include "core/log.h"
#include "xdk/hle.h"

// D3DDevice_SetBackBufferScale(float x, float y), stdcall. xbe_symbols has no signature for it,
// so the game's own copy ran: it scales by the multisample factors at device+0x196C, stores the
// result at +0x96C/+0x970 and rebuilds the render-target state through 0x57ADA0, which reads
// render-target pointers the host D3D never fills in (device+0x1A14/0x1A18) and faulted at boot.
// The host renderer sizes its targets itself and keeps the back-buffer scale at 1, so only the
// request is recorded.
X86_FUNC(sub_00586810) {
    (void)base;
    static uint32_t logged;
    float x, y;
    uint32_t xs = ::kernel::R32(ctx.esp + 4), ys = ::kernel::R32(ctx.esp + 8);
    std::memcpy(&x, &xs, 4);
    std::memcpy(&y, &ys, 4);
    if (logged++ < 4) LOGI("SetBackBufferScale(%g, %g) ignored, scale stays 1", x, y);
    ::kernel::Return(ctx, 0, 2);
}

