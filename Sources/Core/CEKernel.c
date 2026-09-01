#include "CELayer/CEKernel.h"

#include <errno.h>
#include <string.h>
#include <time.h>

enum { MOD_COREDLL = 1, MOD_AYGSHELL, MOD_COMMCTRL, MOD_COMMDLG, MOD_WINSOCK, MOD_OLE32 };
enum { FN_GET_LAST_ERROR = 1, FN_SET_LAST_ERROR, FN_GET_TICK_COUNT, FN_VIRTUAL_ALLOC,
       FN_VIRTUAL_FREE, FN_CLOSE_HANDLE, FN_CREATE_FILE_W, FN_READ_FILE, FN_WRITE_FILE,
       FN_SET_FILE_POINTER, FN_CREATE_EVENT_W, FN_SET_EVENT, FN_RESET_EVENT,
       FN_POST_MESSAGE_W, FN_GET_MESSAGE_W, FN_CREATE_WINDOW_EX_W, FN_DESTROY_WINDOW,
       FN_SHOW_WINDOW, FN_INVALIDATE_RECT, FN_EXIT_PROCESS };

uint64_t CEClockMilliseconds(void) {
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    timespec_get(&ts, TIME_UTC);
#endif
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}
void CEKernelInit(CEKernel *kernel, CEVirtualMemory *memory, CEWindowServer *windows, const char *root) {
    memset(kernel, 0, sizeof(*kernel)); kernel->memory = memory; kernel->windows = windows;
    kernel->boot_millis = CEClockMilliseconds();
    if (root) { strncpy(kernel->root_path, root, sizeof(kernel->root_path) - 1); }
}
CEHandle CEKernelAddHandle(CEKernel *kernel, CEKernelObject object) {
    for (CEHandle i = 1; i < CE_MAX_KERNEL_HANDLES; ++i) if (kernel->handles[i].type == CE_OBJECT_NONE) {
        kernel->handles[i] = object; kernel->handles[i].references = 1; return i;
    }
    kernel->last_error = 4; return 0;
}
CEStatus CEKernelCloseHandle(CEKernel *kernel, CEHandle handle) {
    if (!kernel || !handle || handle >= CE_MAX_KERNEL_HANDLES || kernel->handles[handle].type == CE_OBJECT_NONE)
        return CE_ERROR_INVALID_ARGUMENT;
    CEKernelObject *o = &kernel->handles[handle];
    if (--o->references) return CE_OK;
    if (o->type == CE_OBJECT_FILE && o->value.file) fclose(o->value.file);
    memset(o, 0, sizeof(*o)); return CE_OK;
}
static bool guest_path(CEKernel *k, CEAddress address, char *out, size_t cap) {
    uint16_t wide[CE_MAX_PATH];
    if (CEVirtualMemoryReadUTF16(k->memory, address, wide, CE_ARRAY_COUNT(wide)) != CE_OK) return false;
    size_t used = strlen(k->root_path); if (used + 2 >= cap) return false;
    memcpy(out, k->root_path, used); if (used && out[used - 1] != '/') out[used++] = '/';
    size_t segment = 0;
    for (size_t i = 0; wide[i] && used + 1 < cap; ++i) {
        uint16_t c = wide[i]; if (c == '\\') c = '/'; if (c == ':' || c > 0x7f) c = '_';
        if (c == '/') { if (segment == 2 && out[used - 1] == '.' && out[used - 2] == '.') return false; segment = 0; }
        else segment++;
        out[used++] = (char)c;
    }
    if (segment == 2 && used >= 2 && out[used - 1] == '.' && out[used - 2] == '.') return false;
    out[used] = '\0'; return true;
}
static CEStatus write_guest(CEKernel *k, CEAddress a, const void *p, size_t n) {
    return a ? CEVirtualMemoryWrite(k->memory, a, p, n) : CE_OK;
}
CEStatus CEKernelDispatch(CEKernel *k, CECPU *cpu, uint16_t module, uint16_t fn) {
    if (!k || !cpu) return CE_ERROR_INVALID_ARGUMENT;
    if (module != MOD_COREDLL) { k->last_error = 120; cpu->r[0] = 0; return CE_ERROR_UNSUPPORTED; }
    switch (fn) {
    case FN_GET_LAST_ERROR: cpu->r[0] = k->last_error; return CE_OK;
    case FN_SET_LAST_ERROR: k->last_error = cpu->r[0]; return CE_OK;
    case FN_GET_TICK_COUNT: cpu->r[0] = (uint32_t)(CEClockMilliseconds() - k->boot_millis); return CE_OK;
    case FN_VIRTUAL_ALLOC: {
        CEAddress result; CEStatus s = CEVirtualMemoryAllocate(k->memory, cpu->r[1],
            CE_PROT_READ | CE_PROT_WRITE, "VirtualAlloc", &result);
        cpu->r[0] = s == CE_OK ? result : 0; if (s != CE_OK) k->last_error = 8; return CE_OK; }
    case FN_VIRTUAL_FREE: cpu->r[0] = CEVirtualMemoryFree(k->memory, cpu->r[0]) == CE_OK; return CE_OK;
    case FN_CLOSE_HANDLE: cpu->r[0] = CEKernelCloseHandle(k, cpu->r[0]) == CE_OK; return CE_OK;
    case FN_CREATE_FILE_W: {
        char path[1400]; if (!guest_path(k, cpu->r[0], path, sizeof(path))) { cpu->r[0] = UINT32_MAX; k->last_error = 123; return CE_OK; }
        const char *mode = (cpu->r[1] & 0x40000000u) ? ((cpu->r[1] & 0x80000000u) ? "w+b" : "wb") : "rb";
        FILE *f = fopen(path, mode); if (!f) { cpu->r[0] = UINT32_MAX; k->last_error = (uint32_t)errno; return CE_OK; }
        CEKernelObject o = {.type = CE_OBJECT_FILE}; o.value.file = f; cpu->r[0] = CEKernelAddHandle(k, o); return CE_OK; }
    case FN_READ_FILE: case FN_WRITE_FILE: {
        CEHandle h = cpu->r[0]; if (!h || h >= CE_MAX_KERNEL_HANDLES || k->handles[h].type != CE_OBJECT_FILE) { cpu->r[0] = 0; k->last_error = 6; return CE_OK; }
        uint32_t length = cpu->r[2], done = 0; uint8_t buffer[4096]; CEAddress ptr = cpu->r[1];
        while (done < length) { uint32_t chunk = length - done > sizeof(buffer) ? sizeof(buffer) : length - done; size_t n;
            if (fn == FN_READ_FILE) { n = fread(buffer, 1, chunk, k->handles[h].value.file); if (write_guest(k, ptr + done, buffer, n) != CE_OK) break; }
            else { if (CEVirtualMemoryRead(k->memory, ptr + done, buffer, chunk) != CE_OK) break; n = fwrite(buffer, 1, chunk, k->handles[h].value.file); }
            done += (uint32_t)n; if (n != chunk) break; }
        write_guest(k, cpu->r[3], &done, sizeof(done)); cpu->r[0] = done == length || fn == FN_READ_FILE; return CE_OK; }
    case FN_SET_FILE_POINTER: {
        CEHandle h = cpu->r[0];
        if (!h || h >= CE_MAX_KERNEL_HANDLES || k->handles[h].type != CE_OBJECT_FILE) { cpu->r[0] = UINT32_MAX; k->last_error = 6; return CE_OK; }
        int origin = cpu->r[3] == 0 ? SEEK_SET : (cpu->r[3] == 1 ? SEEK_CUR : SEEK_END);
        if (fseek(k->handles[h].value.file, (long)(int32_t)cpu->r[1], origin) != 0) { cpu->r[0] = UINT32_MAX; k->last_error = (uint32_t)errno; }
        else { long position = ftell(k->handles[h].value.file); cpu->r[0] = position < 0 ? UINT32_MAX : (uint32_t)position; }
        return CE_OK; }
    case FN_CREATE_EVENT_W: { CEKernelObject o = {.type = CE_OBJECT_EVENT}; o.value.event.manual = cpu->r[1] != 0; o.value.event.signaled = cpu->r[2] != 0; cpu->r[0] = CEKernelAddHandle(k, o); return CE_OK; }
    case FN_SET_EVENT: case FN_RESET_EVENT: { CEHandle h = cpu->r[0]; bool ok = h && h < CE_MAX_KERNEL_HANDLES && k->handles[h].type == CE_OBJECT_EVENT; if (ok) k->handles[h].value.event.signaled = fn == FN_SET_EVENT; cpu->r[0] = ok; return CE_OK; }
    case FN_POST_MESSAGE_W: { CEMessage m = {cpu->r[0], cpu->r[1], cpu->r[2], cpu->r[3], (uint32_t)(CEClockMilliseconds() - k->boot_millis)}; cpu->r[0] = CEPostMessage(k->windows, m) == CE_OK; return CE_OK; }
    case FN_GET_MESSAGE_W: { CEMessage m; bool ok = CEGetMessage(k->windows, &m); if (ok && write_guest(k, cpu->r[0], &m, sizeof(m)) != CE_OK) ok = false; cpu->r[0] = ok; return CE_OK; }
    case FN_CREATE_WINDOW_EX_W: {
        uint32_t stack[8]; uint16_t title[128] = {0};
        if (CEVirtualMemoryRead(k->memory, cpu->r[CE_REG_SP], stack, sizeof(stack)) != CE_OK) { cpu->r[0] = 0; return CE_OK; }
        if (cpu->r[2]) (void)CEVirtualMemoryReadUTF16(k->memory, cpu->r[2], title, CE_ARRAY_COUNT(title));
        cpu->r[0] = CEWindowCreate(k->windows, stack[4], (int32_t)stack[0], (int32_t)stack[1],
            (int32_t)stack[2], (int32_t)stack[3], cpu->r[3], 0, title);
        return CE_OK; }
    case FN_DESTROY_WINDOW: cpu->r[0] = CEWindowDestroy(k->windows, cpu->r[0]) == CE_OK; return CE_OK;
    case FN_SHOW_WINDOW: for (size_t i = 0; i < k->windows->window_count; ++i) if (k->windows->windows[i].handle == cpu->r[0]) { k->windows->windows[i].visible = cpu->r[1] != 0; cpu->r[0] = 1; return CE_OK; } cpu->r[0] = 0; return CE_OK;
    case FN_INVALIDATE_RECT: k->windows->generation++; cpu->r[0] = 1; return CE_OK;
    case FN_EXIT_PROCESS: cpu->halted = true; cpu->fault = CE_OK; return CE_OK;
    default: k->last_error = 120; cpu->r[0] = 0; return CE_ERROR_UNSUPPORTED;
    }
}
