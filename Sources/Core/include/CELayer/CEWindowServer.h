#ifndef CELAYER_CE_WINDOW_SERVER_H
#define CELAYER_CE_WINDOW_SERVER_H

#include "CECommon.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define CE_MAX_WINDOWS 128u
#define CE_MESSAGE_QUEUE_SIZE 512u
typedef struct CEMessage { CEHandle hwnd; uint32_t message, wparam, lparam, time; } CEMessage;
typedef struct CEWindow {
    CEHandle handle, parent; int32_t x, y, width, height; uint32_t style;
    CEAddress wndproc; bool visible, enabled; uint16_t title[128];
} CEWindow;
typedef struct CEWindowServer {
    CEWindow windows[CE_MAX_WINDOWS]; size_t window_count; CEHandle next_handle, focus, capture;
    CEMessage messages[CE_MESSAGE_QUEUE_SIZE]; size_t queue_head, queue_count;
    uint32_t *framebuffer; uint32_t width, height, generation;
} CEWindowServer;

CEStatus CEWindowServerInit(CEWindowServer *server, uint32_t width, uint32_t height);
void CEWindowServerDestroy(CEWindowServer *server);
CEHandle CEWindowCreate(CEWindowServer *server, CEHandle parent, int32_t x, int32_t y,
                        int32_t width, int32_t height, uint32_t style,
                        CEAddress wndproc, const uint16_t *title);
CEStatus CEWindowDestroy(CEWindowServer *server, CEHandle handle);
CEStatus CEPostMessage(CEWindowServer *server, CEMessage message);
bool CEGetMessage(CEWindowServer *server, CEMessage *message);
CEStatus CEGDIFillRect(CEWindowServer *server, int32_t left, int32_t top,
                       int32_t right, int32_t bottom, uint32_t bgra);
CEStatus CEGDIBitBlt(CEWindowServer *server, int32_t dx, int32_t dy, uint32_t width,
                     uint32_t height, const uint32_t *source, uint32_t source_stride);

#if defined(__cplusplus)
}
#endif
#endif
