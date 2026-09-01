#include "CELayer/CEWindowServer.h"

#include <stdlib.h>
#include <string.h>

CEStatus CEWindowServerInit(CEWindowServer *server, uint32_t width, uint32_t height) {
    if (!server || !width || !height || (uint64_t)width * height > SIZE_MAX / sizeof(uint32_t))
        return CE_ERROR_INVALID_ARGUMENT;
    memset(server, 0, sizeof(*server)); server->width = width; server->height = height;
    server->next_handle = 1; server->framebuffer = calloc((size_t)width * height, sizeof(uint32_t));
    return server->framebuffer ? CE_OK : CE_ERROR_OUT_OF_MEMORY;
}
void CEWindowServerDestroy(CEWindowServer *server) {
    if (!server) return;
    free(server->framebuffer); memset(server, 0, sizeof(*server));
}
CEHandle CEWindowCreate(CEWindowServer *server, CEHandle parent, int32_t x, int32_t y,
                        int32_t width, int32_t height, uint32_t style,
                        CEAddress wndproc, const uint16_t *title) {
    if (!server || width <= 0 || height <= 0 || server->window_count >= CE_MAX_WINDOWS) return 0;
    CEWindow *w = &server->windows[server->window_count++]; memset(w, 0, sizeof(*w));
    w->handle = server->next_handle++; w->parent = parent; w->x = x; w->y = y;
    w->width = width; w->height = height; w->style = style; w->wndproc = wndproc;
    w->visible = (style & 0x10000000u) != 0; w->enabled = true;
    if (title) { size_t i = 0; for (; i + 1 < CE_ARRAY_COUNT(w->title) && title[i]; ++i) w->title[i] = title[i]; w->title[i] = 0; }
    return w->handle;
}
CEStatus CEWindowDestroy(CEWindowServer *server, CEHandle handle) {
    if (!server || !handle) return CE_ERROR_INVALID_ARGUMENT;
    for (size_t i = 0; i < server->window_count; ++i) if (server->windows[i].handle == handle) {
        memmove(&server->windows[i], &server->windows[i + 1],
                (server->window_count - i - 1) * sizeof(server->windows[0]));
        server->window_count--; if (server->focus == handle) server->focus = 0;
        if (server->capture == handle) server->capture = 0;
        return CE_OK;
    }
    return CE_ERROR_NOT_FOUND;
}
CEStatus CEPostMessage(CEWindowServer *server, CEMessage message) {
    if (!server) return CE_ERROR_INVALID_ARGUMENT;
    if (server->queue_count == CE_MESSAGE_QUEUE_SIZE) return CE_ERROR_LIMIT;
    size_t tail = (server->queue_head + server->queue_count) % CE_MESSAGE_QUEUE_SIZE;
    server->messages[tail] = message; server->queue_count++; return CE_OK;
}
bool CEGetMessage(CEWindowServer *server, CEMessage *message) {
    if (!server || !message || !server->queue_count) return false;
    *message = server->messages[server->queue_head];
    server->queue_head = (server->queue_head + 1) % CE_MESSAGE_QUEUE_SIZE; server->queue_count--; return true;
}
CEStatus CEGDIFillRect(CEWindowServer *server, int32_t left, int32_t top,
                       int32_t right, int32_t bottom, uint32_t bgra) {
    if (!server || !server->framebuffer) return CE_ERROR_INVALID_ARGUMENT;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > (int32_t)server->width) right = (int32_t)server->width;
    if (bottom > (int32_t)server->height) bottom = (int32_t)server->height;
    if (left >= right || top >= bottom) return CE_OK;
    for (int32_t y = top; y < bottom; ++y)
        for (int32_t x = left; x < right; ++x) server->framebuffer[(size_t)y * server->width + x] = bgra;
    server->generation++; return CE_OK;
}
CEStatus CEGDIBitBlt(CEWindowServer *server, int32_t dx, int32_t dy, uint32_t width,
                     uint32_t height, const uint32_t *source, uint32_t source_stride) {
    if (!server || !source || source_stride < width) return CE_ERROR_INVALID_ARGUMENT;
    for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x) {
        int64_t tx = (int64_t)dx + x, ty = (int64_t)dy + y;
        if (tx >= 0 && ty >= 0 && tx < server->width && ty < server->height)
            server->framebuffer[(size_t)ty * server->width + (size_t)tx] = source[(size_t)y * source_stride + x];
    }
    server->generation++; return CE_OK;
}
