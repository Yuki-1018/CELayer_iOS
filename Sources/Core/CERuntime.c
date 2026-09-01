#include "CELayer/CERuntime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MOD_COREDLL = 1, MOD_AYGSHELL, MOD_COMMCTRL, MOD_COMMDLG, MOD_WINSOCK, MOD_OLE32 };
typedef struct APIName { const char *name; uint16_t id; } APIName;
static const APIName core_names[] = {
    {"GetLastError",1},{"SetLastError",2},{"GetTickCount",3},{"VirtualAlloc",4},
    {"VirtualFree",5},{"CloseHandle",6},{"CreateFileW",7},{"ReadFile",8},{"WriteFile",9},
    {"SetFilePointer",10},{"CreateEventW",11},{"SetEvent",12},{"ResetEvent",13},
    {"PostMessageW",14},{"GetMessageW",15},{"CreateWindowExW",16},{"DestroyWindow",17},
    {"ShowWindow",18},{"InvalidateRect",19},{"ExitProcess",20},
    {"LocalAlloc",4},{"HeapAlloc",4},{"GlobalAlloc",4}
};
static uint16_t module_id(const char *module) {
    if (!strcmp(module, "coredll.dll") || !strcmp(module, "coredll")) return MOD_COREDLL;
    if (!strcmp(module, "aygshell.dll")) return MOD_AYGSHELL;
    if (!strcmp(module, "commctrl.dll")) return MOD_COMMCTRL;
    if (!strcmp(module, "commdlg.dll")) return MOD_COMMDLG;
    if (!strcmp(module, "winsock.dll") || !strcmp(module, "ws2.dll")) return MOD_WINSOCK;
    if (!strcmp(module, "ole32.dll")) return MOD_OLE32;
    return 0;
}
static CEStatus resolve(void *context, const char *module, const char *symbol,
                        uint16_t ordinal, CEAddress *address) {
    CERuntime *r = context; uint16_t mod = module_id(module);
    if (!mod) { snprintf(r->diagnostic, sizeof(r->diagnostic), "Missing module: %s", module); return CE_ERROR_NOT_FOUND; }
    uint16_t fn = ordinal;
    if (symbol && mod == MOD_COREDLL) {
        fn = 0xffff;
        for (size_t i = 0; i < CE_ARRAY_COUNT(core_names); ++i)
            if (!strcmp(symbol, core_names[i].name)) { fn = core_names[i].id; break; }
    } else if (symbol) fn = 0xffff;
    *address = 0xf0000000u | ((uint32_t)mod << 16) | fn;
    return CE_OK;
}
static CEStatus trap(void *context, CECPU *cpu, uint32_t number) {
    CERuntime *r = context; uint16_t module = (uint16_t)(number >> 16), fn = (uint16_t)number;
    CEStatus s = CEKernelDispatch(&r->kernel, cpu, module, fn);
    if (s == CE_ERROR_UNSUPPORTED) {
        snprintf(r->diagnostic, sizeof(r->diagnostic), "Unsupported WinCE API module=%u function=%u at PC=%08x",
                 module, fn, cpu->r[CE_REG_PC]);
    }
    return s;
}
CERuntime *CERuntimeCreate(const char *root_path, uint32_t width, uint32_t height) {
    CERuntime *r = calloc(1, sizeof(*r)); if (!r) return NULL;
    CEVirtualMemoryInit(&r->memory);
    if (CEWindowServerInit(&r->windows, width, height) != CE_OK) { free(r); return NULL; }
    CEKernelInit(&r->kernel, &r->memory, &r->windows, root_path);
    CECPUInit(&r->cpu, &r->memory); r->cpu.trap_handler = trap; r->cpu.trap_context = r;
    r->state = CE_RUNTIME_IDLE; strcpy(r->diagnostic, "Ready"); return r;
}
void CERuntimeDestroy(CERuntime *r) {
    if (!r) return;
    for (CEHandle h = 1; h < CE_MAX_KERNEL_HANDLES; ++h)
        if (r->kernel.handles[h].type != CE_OBJECT_NONE) CEKernelCloseHandle(&r->kernel, h);
    CEWindowServerDestroy(&r->windows); CEVirtualMemoryDestroy(&r->memory); free(r);
}
CEStatus CERuntimeLoadExecutable(CERuntime *r, const uint8_t *bytes, size_t length) {
    if (!r || !bytes) return CE_ERROR_INVALID_ARGUMENT;
    CEStatus s = CEPEMap(bytes, length, &r->image, &r->memory, resolve, r);
    if (s != CE_OK) { r->state = CE_RUNTIME_FAULTED; r->last_status = s;
        if (!r->diagnostic[0] || !strcmp(r->diagnostic, "Ready"))
            snprintf(r->diagnostic, sizeof(r->diagnostic), "PE load failed: %s", CEStatusString(s));
        return s;
    }
    CEAddress stack; s = CEVirtualMemoryAllocate(&r->memory, 1024u * 1024u,
        CE_PROT_READ | CE_PROT_WRITE, "main stack", &stack);
    if (s != CE_OK) return s;
    CEAddress entry = r->image.mapped_base + r->image.entry_rva;
    CECPUSetEntry(&r->cpu, entry, stack + 1024u * 1024u, (entry & 1u) != 0);
    r->state = CE_RUNTIME_LOADED; r->last_status = CE_OK;
    snprintf(r->diagnostic, sizeof(r->diagnostic), "Loaded ARM PE at 0x%08x", r->image.mapped_base); return CE_OK;
}
CEStatus CERuntimeRunSlice(CERuntime *r, uint64_t instructions) {
    if (!r || (r->state != CE_RUNTIME_LOADED && r->state != CE_RUNTIME_RUNNING)) return CE_ERROR_INVALID_ARGUMENT;
    r->state = CE_RUNTIME_RUNNING; CEStatus s = CECPURun(&r->cpu, instructions); r->last_status = s;
    if (r->cpu.halted) { r->state = s == CE_OK ? CE_RUNTIME_EXITED : CE_RUNTIME_FAULTED;
        if (s != CE_OK) snprintf(r->diagnostic, sizeof(r->diagnostic), "CPU fault: %s at 0x%08x", CEStatusString(s), r->cpu.r[CE_REG_PC]); }
    return s;
}
const uint32_t *CERuntimeFramebuffer(const CERuntime *r, uint32_t *width, uint32_t *height, uint32_t *generation) {
    if (!r) return NULL;
    if (width) *width = r->windows.width;
    if (height) *height = r->windows.height;
    if (generation) *generation = r->windows.generation;
    return r->windows.framebuffer;
}
const char *CERuntimeDiagnostic(const CERuntime *r) { return r ? r->diagnostic : "No runtime"; }
void CERuntimePostPointer(CERuntime *r, int32_t x, int32_t y, bool down) {
    if (!r) return;
    CEMessage m = {r->windows.capture ? r->windows.capture : r->windows.focus,
        down ? 0x0201u : 0x0202u, down ? 1u : 0u, ((uint32_t)(uint16_t)y << 16) | (uint16_t)x,
        (uint32_t)(CEClockMilliseconds() - r->kernel.boot_millis)}; CEPostMessage(&r->windows, m);
}
void CERuntimePostKey(CERuntime *r, uint32_t key, bool down) {
    if (!r) return;
    CEMessage m = {r->windows.focus, down ? 0x0100u : 0x0101u, key, 0,
        (uint32_t)(CEClockMilliseconds() - r->kernel.boot_millis)}; CEPostMessage(&r->windows, m);
}
