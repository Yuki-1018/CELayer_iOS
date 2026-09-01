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
static bool wide_equal(const uint16_t *a, const uint16_t *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
CEStatus CEWindowRegisterClass(CEWindowServer *server, const uint16_t *name,
                               CEAddress wndproc, uint32_t style, CEHandle background) {
    if (!server || !name || !*name || !wndproc) return CE_ERROR_INVALID_ARGUMENT;
    for (size_t i = 0; i < server->class_count; ++i)
        if (wide_equal(server->classes[i].name, name)) return CE_ERROR_ADDRESS_CONFLICT;
    if (server->class_count >= CE_MAX_WINDOW_CLASSES) return CE_ERROR_LIMIT;
    CEWindowClass *c = &server->classes[server->class_count++];
    memset(c, 0, sizeof(*c)); c->wndproc = wndproc; c->style = style; c->background = background;
    size_t i = 0; for (; i + 1 < CE_ARRAY_COUNT(c->name) && name[i]; ++i) c->name[i] = name[i];
    return CE_OK;
}
CEAddress CEWindowClassProc(const CEWindowServer *server, const uint16_t *name) {
    if (!server || !name) return 0;
    for (size_t i = 0; i < server->class_count; ++i)
        if (wide_equal(server->classes[i].name, name)) return server->classes[i].wndproc;
    return 0;
}
CEWindow *CEWindowFind(CEWindowServer *server, CEHandle handle) {
    if (!server) return NULL;
    for (size_t i = 0; i < server->window_count; ++i)
        if (server->windows[i].handle == handle) return &server->windows[i];
    return NULL;
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

static const uint8_t glyphs[36][7] = {
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14},{14,17,17,15,1,1,14},
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{15,16,16,16,16,16,15},
    {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
    {15,16,16,19,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
    {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
};
static const uint8_t *glyph_for(uint16_t character) {
    if (character >= '0' && character <= '9') return glyphs[character - '0'];
    if (character >= 'a' && character <= 'z') character = (uint16_t)(character - 'a' + 'A');
    if (character >= 'A' && character <= 'Z') return glyphs[10 + character - 'A'];
    return NULL;
}
CEStatus CEGDIDrawTextUTF16(CEWindowServer *server, int32_t x, int32_t y,
                            const uint16_t *text, size_t length,
                            uint32_t foreground, uint32_t background, bool opaque) {
    if (!server || !text) return CE_ERROR_INVALID_ARGUMENT;
    int32_t cursor = x;
    for (size_t i = 0; i < length; ++i, cursor += 6) {
        if (text[i] == '\n') { y += 8; cursor = x - 6; continue; }
        const uint8_t *glyph = glyph_for(text[i]);
        for (int row = 0; row < 8; ++row) for (int column = 0; column < 6; ++column) {
            int32_t px = cursor + column, py = y + row;
            if (px < 0 || py < 0 || px >= (int32_t)server->width || py >= (int32_t)server->height) continue;
            bool set = glyph && row < 7 && column < 5 && (glyph[row] & (1u << (4 - column)));
            if (set || opaque) server->framebuffer[(size_t)py * server->width + (size_t)px] = set ? foreground : background;
        }
    }
    server->generation++; return CE_OK;
}
CEStatus CEGDIDrawLine(CEWindowServer *server, int32_t x0, int32_t y0,
                       int32_t x1, int32_t y1, uint32_t color) {
    if (!server || !server->framebuffer) return CE_ERROR_INVALID_ARGUMENT;
    int32_t dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int32_t dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, error = dx + dy;
    for (;;) {
        if (x0 >= 0 && y0 >= 0 && x0 < (int32_t)server->width && y0 < (int32_t)server->height)
            server->framebuffer[(size_t)y0 * server->width + (size_t)x0] = color;
        if (x0 == x1 && y0 == y1) break;
        int32_t twice = error * 2;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
    server->generation++; return CE_OK;
}
