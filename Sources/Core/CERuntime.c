#include "CELayer/CERuntime.h"
#include "CELayer/CEAPI.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct APIName { const char *name; uint16_t id; } APIName;
static const APIName core_names[] = {
    {"GetLastError",CE_API_GET_LAST_ERROR},{"SetLastError",CE_API_SET_LAST_ERROR},
    {"GetTickCount",CE_API_GET_TICK_COUNT},{"VirtualAlloc",CE_API_VIRTUAL_ALLOC},
    {"VirtualFree",CE_API_VIRTUAL_FREE},{"CloseHandle",CE_API_CLOSE_HANDLE},
    {"CreateFileW",CE_API_CREATE_FILE_W},{"ReadFile",CE_API_READ_FILE},
    {"WriteFile",CE_API_WRITE_FILE},{"SetFilePointer",CE_API_SET_FILE_POINTER},
    {"CreateEventW",CE_API_CREATE_EVENT_W},{"SetEvent",CE_API_SET_EVENT},
    {"ResetEvent",CE_API_RESET_EVENT},{"PostMessageW",CE_API_POST_MESSAGE_W},
    {"GetMessageW",CE_API_GET_MESSAGE_W},{"CreateWindowExW",CE_API_CREATE_WINDOW_EX_W},
    {"DestroyWindow",CE_API_DESTROY_WINDOW},{"ShowWindow",CE_API_SHOW_WINDOW},
    {"InvalidateRect",CE_API_INVALIDATE_RECT},{"ExitProcess",CE_API_EXIT_PROCESS},
    {"Sleep",CE_API_SLEEP},{"GetSystemTime",CE_API_GET_SYSTEM_TIME},
    {"GetLocalTime",CE_API_GET_LOCAL_TIME},{"CreateDirectoryW",CE_API_CREATE_DIRECTORY_W},
    {"RemoveDirectoryW",CE_API_REMOVE_DIRECTORY_W},{"DeleteFileW",CE_API_DELETE_FILE_W},
    {"MoveFileW",CE_API_MOVE_FILE_W},{"GetFileAttributesW",CE_API_GET_FILE_ATTRIBUTES_W},
    {"GetFileSize",CE_API_GET_FILE_SIZE},{"FlushFileBuffers",CE_API_FLUSH_FILE_BUFFERS},
    {"VirtualProtect",CE_API_VIRTUAL_PROTECT},{"lstrlenW",CE_API_LSTRLEN_W},
    {"memcpy",CE_API_MEMCPY},{"memset",CE_API_MEMSET},{"lstrcpyW",CE_API_LSTRCPY_W},
    {"PeekMessageW",CE_API_PEEK_MESSAGE_W},{"RegisterClassW",CE_API_REGISTER_CLASS_W},
    {"GetClientRect",CE_API_GET_CLIENT_RECT},{"GetDC",CE_API_GET_DC},
    {"ReleaseDC",CE_API_RELEASE_DC},{"CreateSolidBrush",CE_API_CREATE_SOLID_BRUSH},
    {"DeleteObject",CE_API_DELETE_OBJECT},{"FillRect",CE_API_FILL_RECT},
    {"SetPixel",CE_API_SET_PIXEL},{"DefWindowProcW",CE_API_DEF_WINDOW_PROC_W},
    {"TranslateMessage",CE_API_TRANSLATE_MESSAGE},{"DispatchMessageW",CE_API_DISPATCH_MESSAGE_W},
    {"RegCreateKeyExW",CE_API_REG_CREATE_KEY_EX_W},{"RegOpenKeyExW",CE_API_REG_OPEN_KEY_EX_W},
    {"RegCloseKey",CE_API_REG_CLOSE_KEY},{"RegSetValueExW",CE_API_REG_SET_VALUE_EX_W},
    {"RegQueryValueExW",CE_API_REG_QUERY_VALUE_EX_W},{"RegDeleteValueW",CE_API_REG_DELETE_VALUE_W},
    {"TextOutW",CE_API_TEXT_OUT_W},{"DrawTextW",CE_API_DRAW_TEXT_W},
    {"SetTextColor",CE_API_SET_TEXT_COLOR},{"SetBkColor",CE_API_SET_BK_COLOR},
    {"SetBkMode",CE_API_SET_BK_MODE},
    {"FindFirstFileW",CE_API_FIND_FIRST_FILE_W},{"FindNextFileW",CE_API_FIND_NEXT_FILE_W},
    {"FindClose",CE_API_FIND_CLOSE},
    {"LocalAlloc",CE_API_VIRTUAL_ALLOC},{"GlobalAlloc",CE_API_VIRTUAL_ALLOC}
};
static const APIName ayg_names[] = {
    {"SHInitDialog",CE_AYG_SH_INIT_DIALOG},{"SHCreateMenuBar",CE_AYG_SH_CREATE_MENU_BAR},
    {"SHFullScreen",CE_AYG_SH_FULL_SCREEN},{"SHDoneButton",CE_AYG_SH_DONE_BUTTON},
    {"SHSipPreference",CE_AYG_SH_SIP_PREFERENCE},{"SHHandleWMActivate",CE_AYG_SH_HANDLE_WM_ACTIVATE},
    {"SHHandleWMSettingChange",CE_AYG_SH_HANDLE_WM_SETTING_CHANGE}
};
static const APIName control_names[] = {
    {"InitCommonControls",CE_COMMCTRL_INIT_COMMON_CONTROLS},
    {"InitCommonControlsEx",CE_COMMCTRL_INIT_COMMON_CONTROLS_EX}
};
static const APIName ole_names[] = {
    {"CoInitializeEx",CE_OLE_CO_INITIALIZE_EX},{"CoUninitialize",CE_OLE_CO_UNINITIALIZE}
};
static uint16_t module_id(const char *module) {
    if (!strcmp(module, "coredll.dll") || !strcmp(module, "coredll")) return CE_MODULE_COREDLL;
    if (!strcmp(module, "aygshell.dll")) return CE_MODULE_AYGSHELL;
    if (!strcmp(module, "commctrl.dll")) return CE_MODULE_COMMCTRL;
    if (!strcmp(module, "commdlg.dll")) return CE_MODULE_COMMDLG;
    if (!strcmp(module, "winsock.dll") || !strcmp(module, "ws2.dll")) return CE_MODULE_WINSOCK;
    if (!strcmp(module, "ole32.dll")) return CE_MODULE_OLE32;
    return 0;
}
static CEStatus resolve(void *context, const char *module, const char *symbol,
                        uint16_t ordinal, CEAddress *address) {
    CERuntime *r = context; uint16_t mod = module_id(module);
    if (!mod) { snprintf(r->diagnostic, sizeof(r->diagnostic), "Missing module: %s", module); return CE_ERROR_NOT_FOUND; }
    uint16_t fn = ordinal;
    if (symbol) {
        const APIName *names = NULL; size_t count = 0;
        if (mod == CE_MODULE_COREDLL) { names = core_names; count = CE_ARRAY_COUNT(core_names); }
        else if (mod == CE_MODULE_AYGSHELL) { names = ayg_names; count = CE_ARRAY_COUNT(ayg_names); }
        else if (mod == CE_MODULE_COMMCTRL) { names = control_names; count = CE_ARRAY_COUNT(control_names); }
        else if (mod == CE_MODULE_OLE32) { names = ole_names; count = CE_ARRAY_COUNT(ole_names); }
        fn = 0xffff;
        for (size_t i = 0; i < count; ++i) if (!strcmp(symbol, names[i].name)) { fn = names[i].id; break; }
    }
    *address = CE_API_TRAP_ADDRESS(mod, fn);
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
    (void)CEGDIFillRect(&r->windows, 0, 0, (int32_t)width, (int32_t)height, 0xffd8e7ecu);
    (void)CEGDIFillRect(&r->windows, 0, 0, (int32_t)width, 24, 0xff315f8cu);
    (void)CEGDIFillRect(&r->windows, 0, (int32_t)height - 24, (int32_t)width,
                        (int32_t)height, 0xffc0c0c0u);
    static const uint16_t shell_title[] = {'C','E','L','A','Y','E','R',' ','P','O','C','K','E','T',' ','P','C'};
    (void)CEGDIDrawTextUTF16(&r->windows, 5, 8, shell_title, CE_ARRAY_COUNT(shell_title),
                             0xffffffffu, 0xff315f8cu, false);
    CEKernelInit(&r->kernel, &r->memory, &r->windows, root_path);
    CECPUInit(&r->cpu, &r->memory); r->cpu.trap_handler = trap; r->cpu.trap_context = r;
    r->state = CE_RUNTIME_IDLE; strcpy(r->diagnostic, "Ready"); return r;
}
void CERuntimeDestroy(CERuntime *r) {
    if (!r) return;
    (void)CEKernelFlushRegistry(&r->kernel);
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
    CEWindow *target = NULL;
    if (r->windows.capture) target = CEWindowFind(&r->windows, r->windows.capture);
    if (!target) for (size_t i = r->windows.window_count; i > 0; --i) { CEWindow *candidate = &r->windows.windows[i - 1]; if (candidate->visible && candidate->enabled && x >= candidate->x && y >= candidate->y && x < candidate->x + candidate->width && y < candidate->y + candidate->height) { target = candidate; break; } }
    if (down && target) { r->windows.capture = target->handle; r->windows.focus = target->handle; }
    int32_t local_x = target ? x - target->x : x, local_y = target ? y - target->y : y;
    CEMessage m = {target ? target->handle : r->windows.focus,
        down ? 0x0201u : 0x0202u, down ? 1u : 0u, ((uint32_t)(uint16_t)local_y << 16) | (uint16_t)local_x,
        (uint32_t)(CEClockMilliseconds() - r->kernel.boot_millis)}; CEPostMessage(&r->windows, m);
    if (!down && target) {
        static const uint16_t button[] = {'B','U','T','T','O','N',0}; bool is_button = true;
        for (size_t i = 0; button[i] || target->class_name[i]; ++i) if (button[i] != target->class_name[i]) { is_button = false; break; }
        if (is_button && target->parent) { CEMessage command = {target->parent, 0x0111u, target->control_id & 0xffffu, target->handle, m.time}; (void)CEPostMessage(&r->windows, command); }
        r->windows.capture = 0;
    }
}
void CERuntimePostKey(CERuntime *r, uint32_t key, bool down) {
    if (!r) return;
    CEMessage m = {r->windows.focus, down ? 0x0100u : 0x0101u, key, 0,
        (uint32_t)(CEClockMilliseconds() - r->kernel.boot_millis)}; CEPostMessage(&r->windows, m);
}
