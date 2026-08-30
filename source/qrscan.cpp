#include "qrscan.h"

#include <3ds.h>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "quirc/quirc.h"
}

// -----------------------------------------------------------------------------
//  Outer-camera capture runs on its own thread (mirrors FBI's capturecam),
//  handing finished RGB565 frames to the render thread through a mutex-guarded
//  buffer. The render thread turns the newest frame into a citro2d texture and
//  feeds it to quirc.
// -----------------------------------------------------------------------------

namespace
{
    constexpr int CAM_W = 400;
    constexpr int CAM_H = 240;
    constexpr int TEX_W = 512;
    constexpr int TEX_H = 256;
    constexpr u32 FRAME_PIXELS = CAM_W * CAM_H;
    constexpr u32 FRAME_BYTES  = FRAME_PIXELS * sizeof(u16);

    // shared with the capture thread
    Thread      s_thread      = nullptr;
    Handle      s_cancelEvent = 0;
    Handle      s_mutex       = 0;
    u16*        s_shared      = nullptr;   // last completed frame (thread -> render)
    volatile u32 s_frameSeq   = 0;
    volatile bool s_camError  = false;

    // render-thread only
    u16*        s_local       = nullptr;
    u32         s_seenSeq     = 0;
    struct quirc* s_quirc     = nullptr;
    int         s_decodeThrottle = 0;

    C3D_Tex   s_tex;
    C2D_Image s_img;
    Tex3DS_SubTexture s_sub;
    bool      s_texReady = false;
    bool      s_texInit  = false;

    // 8x8 GPU tile swizzle for a 16bpp texture.
    inline u32 tiledIndex(u32 x, u32 y, u32 texW)
    {
        u32 within = ((x & 1)      | ((y & 1) << 1) |
                      ((x & 2) << 1) | ((y & 2) << 2) |
                      ((x & 4) << 2) | ((y & 4) << 3));
        return ((x >> 3) + (y >> 3) * (texW >> 3)) * 64 + within;
    }

    void camThread(void*)
    {
        Handle events[3] = {0};   // 0 cancel, 1 recv, 2 buffer error
        events[0] = s_cancelEvent;

        u16* buf = static_cast<u16*>(std::calloc(1, FRAME_BYTES));
        if (!buf) { s_camError = true; return; }

        Result res = camInit();
        if (R_FAILED(res)) { std::free(buf); s_camError = true; return; }

        u32 cam = SELECT_OUT1;
        bool ok =
            R_SUCCEEDED(CAMU_SetSize(cam, SIZE_CTR_TOP_LCD, CONTEXT_A)) &&
            R_SUCCEEDED(CAMU_SetOutputFormat(cam, OUTPUT_RGB_565, CONTEXT_A)) &&
            R_SUCCEEDED(CAMU_SetFrameRate(cam, FRAME_RATE_30)) &&
            R_SUCCEEDED(CAMU_SetNoiseFilter(cam, true)) &&
            R_SUCCEEDED(CAMU_SetAutoExposure(cam, true)) &&
            R_SUCCEEDED(CAMU_SetAutoWhiteBalance(cam, true)) &&
            R_SUCCEEDED(CAMU_Activate(cam));

        u32 transferUnit = 0;
        ok = ok &&
            R_SUCCEEDED(CAMU_GetBufferErrorInterruptEvent(&events[2], PORT_CAM1)) &&
            R_SUCCEEDED(CAMU_SetTrimming(PORT_CAM1, true)) &&
            R_SUCCEEDED(CAMU_SetTrimmingParamsCenter(PORT_CAM1, CAM_W, CAM_H, 400, 240)) &&
            R_SUCCEEDED(CAMU_GetMaxBytes(&transferUnit, CAM_W, CAM_H)) &&
            R_SUCCEEDED(CAMU_SetTransferBytes(PORT_CAM1, transferUnit, CAM_W, CAM_H)) &&
            R_SUCCEEDED(CAMU_ClearBuffer(PORT_CAM1)) &&
            R_SUCCEEDED(CAMU_SetReceiving(&events[1], buf, PORT_CAM1, FRAME_BYTES, (s16)transferUnit)) &&
            R_SUCCEEDED(CAMU_StartCapture(PORT_CAM1));

        if (!ok) s_camError = true;

        bool cancel = false;
        while (ok && !cancel)
        {
            s32 idx = 0;
            if (R_FAILED(svcWaitSynchronizationN(&idx, events, 3, false, U64_MAX)))
                break;

            if (idx == 0)                       // cancel
            {
                cancel = true;
            }
            else if (idx == 1)                  // frame received
            {
                svcCloseHandle(events[1]);
                events[1] = 0;

                svcWaitSynchronization(s_mutex, U64_MAX);
                std::memcpy(s_shared, buf, FRAME_BYTES);
                s_frameSeq++;
                svcReleaseMutex(s_mutex);

                if (R_FAILED(CAMU_SetReceiving(&events[1], buf, PORT_CAM1,
                                               FRAME_BYTES, (s16)transferUnit)))
                    break;
            }
            else                                // buffer error -> restart
            {
                if (events[1]) { svcCloseHandle(events[1]); events[1] = 0; }
                if (R_FAILED(CAMU_ClearBuffer(PORT_CAM1)) ||
                    R_FAILED(CAMU_SetReceiving(&events[1], buf, PORT_CAM1,
                                               FRAME_BYTES, (s16)transferUnit)) ||
                    R_FAILED(CAMU_StartCapture(PORT_CAM1)))
                    break;
            }
        }

        CAMU_StopCapture(PORT_CAM1);
        bool busy = true;
        while (R_SUCCEEDED(CAMU_IsBusy(&busy, PORT_CAM1)) && busy)
            svcSleepThread(1000000);
        CAMU_ClearBuffer(PORT_CAM1);
        CAMU_Activate(SELECT_NONE);
        camExit();

        if (events[1]) svcCloseHandle(events[1]);
        if (events[2]) svcCloseHandle(events[2]);
        std::free(buf);
    }

    void uploadPreview()
    {
        u16* dst = static_cast<u16*>(s_tex.data);
        for (int y = 0; y < CAM_H; ++y)
            for (int x = 0; x < CAM_W; ++x)
                dst[tiledIndex(x, y, TEX_W)] = s_local[y * CAM_W + x];
        C3D_TexFlush(&s_tex);
        s_texReady = true;
    }

    bool decodeFrame(std::string& payload)
    {
        int w = 0, h = 0;
        u8* gray = quirc_begin(s_quirc, &w, &h);
        for (int y = 0; y < h; ++y)
        {
            const u16* row = s_local + y * CAM_W;
            u8* out = gray + y * w;
            for (int x = 0; x < w; ++x)
            {
                u16 p = row[x];
                u8 r = ( p >> 11)        << 3;
                u8 g = ((p >> 5) & 0x3F) << 2;
                u8 b = ( p        & 0x1F) << 3;
                out[x] = static_cast<u8>((r * 77 + g * 151 + b * 28) >> 8);
            }
        }
        quirc_end(s_quirc);

        int n = quirc_count(s_quirc);
        for (int i = 0; i < n; ++i)
        {
            struct quirc_code code;
            struct quirc_data data;
            quirc_extract(s_quirc, i, &code);
            if (quirc_decode(&code, &data) != QUIRC_SUCCESS)
            {
                quirc_flip(&code);               // retry a mirrored code
                if (quirc_decode(&code, &data) != QUIRC_SUCCESS)
                    continue;
            }
            payload.assign(reinterpret_cast<const char*>(data.payload),
                           static_cast<size_t>(data.payload_len));
            return true;
        }
        return false;
    }
}

namespace qrscan
{

bool init()
{
    s_shared = static_cast<u16*>(std::calloc(1, FRAME_BYTES));
    s_local  = static_cast<u16*>(std::calloc(1, FRAME_BYTES));
    if (!s_shared || !s_local) return false;

    s_quirc = quirc_new();
    if (!s_quirc || quirc_resize(s_quirc, CAM_W, CAM_H) < 0) return false;

    if (!C3D_TexInit(&s_tex, TEX_W, TEX_H, GPU_RGB565)) return false;
    s_texInit = true;
    C3D_TexSetFilter(&s_tex, GPU_LINEAR, GPU_LINEAR);
    std::memset(s_tex.data, 0, s_tex.size);

    s_sub.width  = CAM_W;
    s_sub.height = CAM_H;
    s_sub.left   = 0.0f;
    s_sub.top    = 1.0f;
    s_sub.right  = (float)CAM_W / TEX_W;
    s_sub.bottom = 1.0f - (float)CAM_H / TEX_H;
    s_img.tex    = &s_tex;
    s_img.subtex = &s_sub;

    s_frameSeq = 0;
    s_seenSeq  = 0;
    s_camError = false;
    s_texReady = false;
    s_decodeThrottle = 0;

    if (R_FAILED(svcCreateEvent(&s_cancelEvent, RESET_STICKY))) return false;
    if (R_FAILED(svcCreateMutex(&s_mutex, false)))              return false;

    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    s_thread = threadCreate(camThread, nullptr, 0x10000, prio - 1, -1, false);
    return s_thread != nullptr;
}

void exit()
{
    if (s_thread)
    {
        svcSignalEvent(s_cancelEvent);
        threadJoin(s_thread, U64_MAX);
        threadFree(s_thread);
        s_thread = nullptr;
    }
    if (s_cancelEvent) { svcCloseHandle(s_cancelEvent); s_cancelEvent = 0; }
    if (s_mutex)       { svcCloseHandle(s_mutex);       s_mutex = 0; }

    if (s_texInit) { C3D_TexDelete(&s_tex); s_texInit = false; }
    s_texReady = false;

    if (s_quirc)  { quirc_destroy(s_quirc); s_quirc = nullptr; }
    if (s_shared) { std::free(s_shared); s_shared = nullptr; }
    if (s_local)  { std::free(s_local);  s_local  = nullptr; }
}

bool update(std::string& payload)
{
    if (s_frameSeq == s_seenSeq)
        return false;                       // no new frame since last call

    svcWaitSynchronization(s_mutex, U64_MAX);
    std::memcpy(s_local, s_shared, FRAME_BYTES);
    s_seenSeq = s_frameSeq;
    svcReleaseMutex(s_mutex);

    uploadPreview();

    if (++s_decodeThrottle >= 2)
    {
        s_decodeThrottle = 0;
        return decodeFrame(payload);
    }
    return false;
}

C2D_Image preview()   { return s_img; }
bool      hasPreview() { return s_texReady; }
bool      failed()     { return s_camError; }

} // namespace qrscan
