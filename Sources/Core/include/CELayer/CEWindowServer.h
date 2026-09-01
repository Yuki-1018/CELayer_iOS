#ifndef CELAYER_CE_WINDOW_SERVER_H
#define CELAYER_CE_WINDOW_SERVER_H

#include "CECommon.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define CE_MAX_WINDOWS 128u
#define CE_MAX_WINDOW_CLASSES 64u
#define CE_MESSAGE_QUEUE_SIZE 512u
#define CE_MAX_TIMERS 64u
typedef struct CEMessage { CEHandle hwnd; uint32_t message, wparam, lparam, time; } CEMessage;
typedef struct CEWindow {
    CEHandle handle, parent; int32_t x, y, width, height; uint32_t style;
    CEAddress wndproc; bool visible, enabled; uint16_t title[128], class_name[64];
    uint32_t control_id;
} CEWindow;
typedef struct CEWindowClass {
    uint16_t name[64]; CEAddress wndproc; uint32_t style; CEHandle background;
} CEWindowClass;
typedef struct CEWindowTimer {
    CEHandle hwnd; uint32_t identifier, interval; uint64_t next_fire; bool active;
} CEWindowTimer;
typedef struct CEWindowServer {
    CEWindow windows[CE_MAX_WINDOWS]; size_t window_count; CEHandle next_handle, focus, capture;
    CEWindowClass classes[CE_MAX_WINDOW_CLASSES]; size_t class_count;
    CEMessage messages[CE_MESSAGE_QUEUE_SIZE]; size_t queue_head, queue_count;
    CEWindowTimer timers[CE_MAX_TIMERS]; uint8_t key_state[256];
    uint32_t *framebuffer; uint32_t width, height, generation;
} CEWindowServer;

CEStatus CEWindowServerInit(CEWindowServer *server, uint32_t width, uint32_t height);
void CEWindowServerDestroy(CEWindowServer *server);
CEHandle CEWindowCreate(CEWindowServer *server, CEHandle parent, int32_t x, int32_t y,
                        int32_t width, int32_t height, uint32_t style,
                        CEAddress wndproc, const uint16_t *title);
CEStatus CEWindowDestroy(CEWindowServer *server, CEHandle handle);
CEStatus CEWindowRegisterClass(CEWindowServer *server, const uint16_t *name,
                               CEAddress wndproc, uint32_t style, CEHandle background);
CEAddress CEWindowClassProc(const CEWindowServer *server, const uint16_t *name);
CEWindow *CEWindowFind(CEWindowServer *server, CEHandle handle);
CEStatus CEPostMessage(CEWindowServer *server, CEMessage message);
bool CEGetMessage(CEWindowServer *server, CEMessage *message);
CEStatus CEGDIFillRect(CEWindowServer *server, int32_t left, int32_t top,
                       int32_t right, int32_t bottom, uint32_t bgra);
CEStatus CEGDIBitBlt(CEWindowServer *server, int32_t dx, int32_t dy, uint32_t width,
                     uint32_t height, const uint32_t *source, uint32_t source_stride);
CEStatus CEGDIDrawTextUTF16(CEWindowServer *server, int32_t x, int32_t y,
                            const uint16_t *text, size_t length,
                            uint32_t foreground, uint32_t background, bool opaque);
CEStatus CEGDIDrawLine(CEWindowServer *server, int32_t x0, int32_t y0,
                       int32_t x1, int32_t y1, uint32_t color);

#if defined(__cplusplus)
}
#endif
#endif
