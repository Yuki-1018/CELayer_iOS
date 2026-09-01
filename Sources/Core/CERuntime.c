#include "CELayer/CERuntime.h"
#include "CELayer/CEAPI.h"

#include <dirent.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct APIName { const char *name; uint16_t id; } APIName;
static CEStatus resolve(void *context, const char *module, const char *symbol,
                        uint16_t ordinal, CEAddress *address);
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
    {"LoadLibraryW",CE_API_LOAD_LIBRARY_W},{"FreeLibrary",CE_API_FREE_LIBRARY},
    {"GetProcAddressW",CE_API_GET_PROC_ADDRESS},{"GetModuleHandleW",CE_API_GET_MODULE_HANDLE_W},
    {"TlsCall",CE_API_TLS_CALL},{"TlsAlloc",CE_API_TLS_ALLOC},{"TlsFree",CE_API_TLS_FREE},
    {"TlsGetValue",CE_API_TLS_GET_VALUE},{"TlsSetValue",CE_API_TLS_SET_VALUE},
    {"OutputDebugStringW",CE_API_OUTPUT_DEBUG_STRING_W},
    {"BeginPaint",CE_API_BEGIN_PAINT},{"EndPaint",CE_API_END_PAINT},
    {"GetStockObject",CE_API_GET_STOCK_OBJECT},{"SelectObject",CE_API_SELECT_OBJECT},
    {"CreatePen",CE_API_CREATE_PEN},{"Rectangle",CE_API_RECTANGLE},
    {"MoveToEx",CE_API_MOVE_TO_EX},{"LineTo",CE_API_LINE_TO},
    {"SetTimer",CE_API_SET_TIMER},{"KillTimer",CE_API_KILL_TIMER},
    {"GetKeyState",CE_API_GET_KEY_STATE},{"GetAsyncKeyState",CE_API_GET_ASYNC_KEY_STATE},
    {"GetSystemMetrics",CE_API_GET_SYSTEM_METRICS},{"GetDeviceCaps",CE_API_GET_DEVICE_CAPS},
    {"MessageBoxW",CE_API_MESSAGE_BOX_W},{"LoadStringW",CE_API_LOAD_STRING_W},
    {"GetSystemInfo",CE_API_GET_SYSTEM_INFO},
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
static bool ascii_equal_ignore_case(const char *a, const char *b) {
    while (*a && *b) { if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false; ++a; ++b; }
    return *a == *b;
}
static CERuntimeModule *find_runtime_module(CERuntime *r, const char *name) {
    for (size_t i = 0; i < r->module_count; ++i)
        if (r->modules[i].name[0] && ascii_equal_ignore_case(r->modules[i].name, name)) return &r->modules[i];
    return NULL;
}
static CEStatus load_dependency(CERuntime *r, const char *name, CERuntimeModule **result) {
    CERuntimeModule *existing = find_runtime_module(r, name);
    if (existing) { *result = existing; return CE_OK; }
    if (!name[0] || strchr(name, '/') || strchr(name, '\\') || r->module_count >= CE_RUNTIME_MAX_MODULES)
        return CE_ERROR_NOT_FOUND;
    DIR *directory = opendir(r->kernel.root_path); if (!directory) return CE_ERROR_NOT_FOUND;
    char matched[256] = {0}; struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) if (ascii_equal_ignore_case(entry->d_name, name)) {
        strncpy(matched, entry->d_name, sizeof(matched) - 1); break;
    }
    closedir(directory); if (!matched[0]) return CE_ERROR_NOT_FOUND;
    char path[1400]; snprintf(path, sizeof(path), "%s/%s", r->kernel.root_path, matched);
    FILE *file = fopen(path, "rb"); if (!file) return CE_ERROR_IO;
    if (fseek(file, 0, SEEK_END) || ftell(file) <= 0) { fclose(file); return CE_ERROR_IO; }
    long file_size = ftell(file); if (file_size > 128 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) { fclose(file); return CE_ERROR_LIMIT; }
    uint8_t *bytes = malloc((size_t)file_size); if (!bytes) { fclose(file); return CE_ERROR_OUT_OF_MEMORY; }
    if (fread(bytes, 1, (size_t)file_size, file) != (size_t)file_size) { free(bytes); fclose(file); return CE_ERROR_IO; }
    fclose(file);
    CERuntimeModule *slot = &r->modules[r->module_count++]; memset(slot, 0, sizeof(*slot));
    strncpy(slot->name, name, sizeof(slot->name) - 1); slot->loading = true;
    CEStatus status = CEPEMap(bytes, (size_t)file_size, &slot->image, &r->memory, resolve, r);
    free(bytes); slot->loading = false;
    if (status != CE_OK) { slot->name[0] = '\0'; return status; }
    *result = slot; return CE_OK;
}
static CEStatus resolve(void *context, const char *module, const char *symbol,
                        uint16_t ordinal, CEAddress *address) {
    CERuntime *r = context; uint16_t mod = module_id(module);
    if (!mod) {
        CERuntimeModule *dependency = NULL; CEStatus status = load_dependency(r, module, &dependency);
        if (status != CE_OK) { snprintf(r->diagnostic, sizeof(r->diagnostic), "Missing or invalid companion DLL: %s", module); return status; }
        status = CEPELookupExport(&dependency->image, &r->memory, symbol, ordinal, address);
        if (status != CE_OK) {
            if (symbol) snprintf(r->diagnostic, sizeof(r->diagnostic), "Missing export %s!%s", module, symbol);
            else snprintf(r->diagnostic, sizeof(r->diagnostic), "Missing export %s!#%u", module, ordinal);
        }
        return status;
    }
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
static bool runtime_module_name(CERuntime *r, CEAddress address, char *out, size_t capacity) {
    if (!address || !capacity) return false;
    uint16_t wide[128]; if (CEVirtualMemoryReadUTF16(&r->memory, address, wide, CE_ARRAY_COUNT(wide)) != CE_OK) return false;
    size_t used = 0;
    for (size_t i = 0; wide[i]; ++i) {
        if (wide[i] == '/' || wide[i] == '\\' || wide[i] == ':') { used = 0; continue; }
        if (wide[i] > 0x7fu || used + 1 >= capacity) return false;
        out[used++] = (char)tolower((unsigned char)wide[i]);
    }
    out[used] = '\0'; return used != 0 && strcmp(out, ".") && strcmp(out, "..");
}
static CEStatus dispatch_runtime_api(CERuntime *r, CECPU *cpu, uint16_t function) {
    if (function == CE_API_LOAD_STRING_W) {
        const CEPEImage *image = (!cpu->r[0] || cpu->r[0] == r->image.mapped_base) ? &r->image : NULL;
        for (size_t i = 0; !image && i < r->module_count; ++i)
            if (r->modules[i].name[0] && r->modules[i].image.mapped_base == cpu->r[0]) image = &r->modules[i].image;
        uint16_t text[512]; size_t length = 0; size_t capacity = cpu->r[3] < CE_ARRAY_COUNT(text) ? cpu->r[3] : CE_ARRAY_COUNT(text);
        if (!image || !capacity || CEPELookupStringResource(image, &r->memory, cpu->r[1], text, capacity, &length) != CE_OK ||
            CEVirtualMemoryWrite(&r->memory, cpu->r[2], text, (length + 1) * 2) != CE_OK) cpu->r[0] = 0;
        else cpu->r[0] = (uint32_t)length;
        return CE_OK;
    }
    if (function == CE_API_LOAD_LIBRARY_W || function == CE_API_GET_MODULE_HANDLE_W) {
        if (!cpu->r[0] && function == CE_API_GET_MODULE_HANDLE_W) { cpu->r[0] = r->image.mapped_base; return CE_OK; }
        char name[128]; if (!runtime_module_name(r, cpu->r[0], name, sizeof(name))) { cpu->r[0] = 0; return CE_OK; }
        CERuntimeModule *module = find_runtime_module(r, name); CEStatus status = CE_OK;
        if (!module && function == CE_API_LOAD_LIBRARY_W) status = load_dependency(r, name, &module);
        cpu->r[0] = status == CE_OK && module ? module->image.mapped_base : 0; return CE_OK;
    }
    if (function == CE_API_FREE_LIBRARY) { cpu->r[0] = 1; return CE_OK; }
    if (function == CE_API_GET_PROC_ADDRESS) {
        CERuntimeModule *module = NULL; for (size_t i = 0; i < r->module_count; ++i)
            if (r->modules[i].name[0] && r->modules[i].image.mapped_base == cpu->r[0]) { module = &r->modules[i]; break; }
        const CEPEImage *image = module ? &module->image : (cpu->r[0] == r->image.mapped_base ? &r->image : NULL);
        if (!image) { cpu->r[0] = 0; return CE_OK; }
        char symbol[192]; const char *name = NULL; uint16_t ordinal = 0;
        if (cpu->r[1] <= 0xffffu) ordinal = (uint16_t)cpu->r[1];
        else { size_t i = 0; uint8_t c = 0; do { if (i + 1 >= sizeof(symbol) || CEVirtualMemoryReadU8(&r->memory, cpu->r[1] + (CEAddress)i, &c) != CE_OK) { cpu->r[0] = 0; return CE_OK; } symbol[i++] = (char)c; } while (c); name = symbol; }
        CEAddress result = 0; if (CEPELookupExport(image, &r->memory, name, ordinal, &result) != CE_OK) result = 0;
        cpu->r[0] = result; return CE_OK;
    }
    return CE_ERROR_NOT_FOUND;
}
static CEStatus dispatch_coredll(CERuntime *r, CECPU *cpu, uint16_t function) {
    CEStatus status = dispatch_runtime_api(r, cpu, function);
    return status == CE_ERROR_NOT_FOUND ? CEKernelDispatch(&r->kernel, cpu, CE_MODULE_COREDLL, function) : status;
}
static CEStatus trap(void *context, CECPU *cpu, uint32_t number) {
    CERuntime *r = context;
    if (number & CE_TRAP_SOFTWARE_INTERRUPT) {
        /* CE's default SWI handler returns without dispatch; OEM SWIs remain observable. */
        snprintf(r->diagnostic, sizeof(r->diagnostic), "Ignored OEM SWI 0x%06x at PC=%08x",
                 number & 0x00ffffffu, cpu->r[CE_REG_PC]);
        return CE_OK;
    }
    if (number & CE_TRAP_NATIVE) {
        uint16_t api_set = (uint16_t)((number >> 16) & 0x7fffu), method = (uint16_t)number;
        uint16_t mapped = 0;
        if (api_set == 0) {
            if (method == 3) mapped = CE_API_VIRTUAL_ALLOC;
            else if (method == 4) mapped = CE_API_VIRTUAL_FREE;
            else if (method == 5) mapped = CE_API_VIRTUAL_PROTECT;
            else if (method == 8) mapped = CE_API_LOAD_LIBRARY_W;
            else if (method == 9) mapped = CE_API_FREE_LIBRARY;
            else if (method == 10) mapped = CE_API_GET_PROC_ADDRESS;
            else if (method == 13) mapped = CE_API_GET_TICK_COUNT;
            else if (method == 14) mapped = CE_API_OUTPUT_DEBUG_STRING_W;
            else if (method == 15) mapped = CE_API_TLS_CALL;
            else if (method == 16) mapped = CE_API_GET_SYSTEM_INFO;
        } else if (api_set == 2 && method == 2) mapped = CE_API_EXIT_PROCESS;
        if (mapped) return dispatch_coredll(r, cpu, mapped);
        snprintf(r->diagnostic, sizeof(r->diagnostic),
                 "Unsupported native WinCE syscall apiSet=%u method=%u at PC=%08x",
                 api_set, method, cpu->r[CE_REG_PC]);
        return CE_ERROR_UNSUPPORTED;
    }
    uint16_t module = (uint16_t)(number >> 16), fn = (uint16_t)number;
    if (module == CE_MODULE_INTERNAL && fn == CE_INTERNAL_RETURN_DLL) {
        r->initializer_complete = true; cpu->halted = true; cpu->fault = CE_OK; return CE_OK;
    }
    CEStatus s = module == CE_MODULE_COREDLL ? dispatch_coredll(r, cpu, fn)
                                             : CEKernelDispatch(&r->kernel, cpu, module, fn);
    if (s == CE_ERROR_UNSUPPORTED) {
        snprintf(r->diagnostic, sizeof(r->diagnostic), "Unsupported WinCE API module=%u function=%u at PC=%08x",
                 module, fn, cpu->r[CE_REG_PC]);
    }
    return s;
}
static CEStatus initialize_dll(CERuntime *r, CERuntimeModule *module, CEAddress stack_top) {
    if (!module->image.entry_rva) return CE_OK;
    r->initializer_complete = false;
    CEAddress entry = module->image.mapped_base + module->image.entry_rva;
    CECPUSetEntry(&r->cpu, entry, stack_top,
                  module->image.machine == CE_PE_MACHINE_THUMB || (entry & 1u));
    r->cpu.r[0] = module->image.mapped_base; r->cpu.r[1] = 1; r->cpu.r[2] = 0;
    r->cpu.r[CE_REG_LR] = CE_API_TRAP_ADDRESS(CE_MODULE_INTERNAL, CE_INTERNAL_RETURN_DLL);
    for (uint64_t i = 0; i < 500000 && !r->initializer_complete; ++i) {
        CEStatus status = CECPUStep(&r->cpu);
        if (r->cpu.halted && !r->initializer_complete) return status;
    }
    if (!r->initializer_complete) return CE_ERROR_LIMIT;
    r->cpu.halted = false; r->cpu.fault = CE_OK; return CE_OK;
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
    for (size_t i = r->module_count; i > 0; --i) {
        if (!r->modules[i - 1].name[0]) continue;
        s = initialize_dll(r, &r->modules[i - 1], stack + 1024u * 1024u);
        if (s != CE_OK) { r->state = CE_RUNTIME_FAULTED; r->last_status = s;
            snprintf(r->diagnostic, sizeof(r->diagnostic), "DLL initialization failed: %s (%s)", r->modules[i - 1].name, CEStatusString(s)); return s; }
    }
    CECPUSetEntry(&r->cpu, entry, stack + 1024u * 1024u,
                  r->image.machine == CE_PE_MACHINE_THUMB || (entry & 1u));
    r->state = CE_RUNTIME_LOADED; r->last_status = CE_OK;
    snprintf(r->diagnostic, sizeof(r->diagnostic), "Loaded ARM PE at 0x%08x", r->image.mapped_base); return CE_OK;
}
CEStatus CERuntimeRunSlice(CERuntime *r, uint64_t instructions) {
    if (!r || (r->state != CE_RUNTIME_LOADED && r->state != CE_RUNTIME_RUNNING)) return CE_ERROR_INVALID_ARGUMENT;
    r->state = CE_RUNTIME_RUNNING;
    uint64_t now = CEClockMilliseconds();
    for (size_t i = 0; i < CE_MAX_TIMERS; ++i) if (r->windows.timers[i].active && now >= r->windows.timers[i].next_fire) {
        CEMessage timer = {r->windows.timers[i].hwnd, 0x0113u, r->windows.timers[i].identifier, 0, (uint32_t)(now - r->kernel.boot_millis)};
        (void)CEPostMessage(&r->windows, timer); r->windows.timers[i].next_fire = now + r->windows.timers[i].interval;
    }
    CEStatus s = CECPURun(&r->cpu, instructions); r->last_status = s;
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
    if (key < CE_ARRAY_COUNT(r->windows.key_state)) r->windows.key_state[key] = down ? 1u : 0u;
    CEMessage m = {r->windows.focus, down ? 0x0100u : 0x0101u, key, 0,
        (uint32_t)(CEClockMilliseconds() - r->kernel.boot_millis)}; CEPostMessage(&r->windows, m);
}
void CERuntimePostCharacter(CERuntime *r, uint16_t character) {
    if (!r) return;
    CEMessage message = {r->windows.focus, 0x0102u, character, 0,
        (uint32_t)(CEClockMilliseconds() - r->kernel.boot_millis)};
    (void)CEPostMessage(&r->windows, message);
}
