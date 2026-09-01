#include "CELayer/CEKernel.h"
#include "CELayer/CEAPI.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct CEFindState { DIR *directory; char base[1400]; char pattern[260]; } CEFindState;
typedef struct CEFindData { uint32_t fields[11]; uint16_t filename[CE_MAX_PATH]; } CEFindData;

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
    kernel->text_color = 0xff000000u; kernel->background_color = 0xffffffffu;
    kernel->background_opaque = true;
    if (root) { strncpy(kernel->root_path, root, sizeof(kernel->root_path) - 1); }
    if (kernel->root_path[0]) {
        char path[1200]; snprintf(path, sizeof(path), "%s/.celayer-registry.bin", kernel->root_path);
        FILE *file = fopen(path, "rb");
        if (file) { uint32_t magic = 0, count = 0; if (fread(&magic, 4, 1, file) == 1 && fread(&count, 4, 1, file) == 1 && magic == 0x52474543u && count <= CE_MAX_REGISTRY_VALUES) {
                size_t read = fread(kernel->registry, sizeof(kernel->registry[0]), count, file);
                kernel->registry_count = read;
                for (size_t i = 0; i < read; ++i) {
                    kernel->registry[i].key[CE_ARRAY_COUNT(kernel->registry[i].key) - 1] = 0;
                    kernel->registry[i].name[CE_ARRAY_COUNT(kernel->registry[i].name) - 1] = 0;
                    if (kernel->registry[i].size > sizeof(kernel->registry[i].data)) {
                        kernel->registry_count = 0; break;
                    }
                }
            } fclose(file); }
    }
}
CEStatus CEKernelFlushRegistry(CEKernel *kernel) {
    if (!kernel || !kernel->root_path[0]) return CE_ERROR_INVALID_ARGUMENT;
    char path[1200]; snprintf(path, sizeof(path), "%s/.celayer-registry.bin", kernel->root_path);
    FILE *file = fopen(path, "wb"); if (!file) return CE_ERROR_IO;
    uint32_t magic = 0x52474543u, count = (uint32_t)kernel->registry_count;
    bool ok = fwrite(&magic, 4, 1, file) == 1 && fwrite(&count, 4, 1, file) == 1 &&
        fwrite(kernel->registry, sizeof(kernel->registry[0]), count, file) == count;
    if (fclose(file) != 0) ok = false;
    return ok ? CE_OK : CE_ERROR_IO;
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
    if (o->type == CE_OBJECT_FIND && o->value.find_state) {
        CEFindState *find = o->value.find_state;
        if (find->directory) closedir(find->directory);
        free(find);
    }
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
static uint32_t page_protection(CEProtection p) {
    if (p & CE_PROT_EXEC) return (p & CE_PROT_WRITE) ? 0x40u : 0x20u;
    return (p & CE_PROT_WRITE) ? 0x04u : 0x02u;
}
static CEProtection ce_protection(uint32_t p) {
    CEProtection result = CE_PROT_READ;
    if (p == 0x01u) return CE_PROT_NONE;
    if (p == 0x04u || p == 0x08u || p == 0x40u || p == 0x80u) result |= CE_PROT_WRITE;
    if (p >= 0x10u && p <= 0x80u) result |= CE_PROT_EXEC;
    return result;
}
static CEStatus write_system_time(CEKernel *k, CEAddress address, bool local) {
    time_t now = time(NULL); struct tm value; struct tm *converted = local ? localtime(&now) : gmtime(&now);
    if (!converted) return CE_ERROR_IO;
    value = *converted;
    uint16_t out[8] = {(uint16_t)(value.tm_year + 1900), (uint16_t)(value.tm_mon + 1),
        (uint16_t)value.tm_wday, (uint16_t)value.tm_mday, (uint16_t)value.tm_hour,
        (uint16_t)value.tm_min, (uint16_t)value.tm_sec, 0};
    return write_guest(k, address, out, sizeof(out));
}
static uint32_t colorref_to_bgra(uint32_t c) {
    uint32_t r = c & 0xffu, g = (c >> 8) & 0xffu, b = (c >> 16) & 0xffu;
    return 0xff000000u | (r << 16) | (g << 8) | b;
}
static uint32_t bgra_to_colorref(uint32_t c) {
    uint32_t r = (c >> 16) & 0xffu, g = (c >> 8) & 0xffu, b = c & 0xffu;
    return r | (g << 8) | (b << 16);
}
static void wide_copy(uint16_t *destination, size_t capacity, const uint16_t *source) {
    size_t i = 0; if (!capacity) return;
    while (i + 1 < capacity && source[i]) { destination[i] = source[i]; ++i; }
    destination[i] = 0;
}
static bool wide_equal(const uint16_t *a, const uint16_t *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static bool registry_path(CEKernel *k, CEHandle root, CEAddress subkey_address,
                          uint16_t *out, size_t capacity) {
    uint16_t subkey[128] = {0};
    if (subkey_address && CEVirtualMemoryReadUTF16(k->memory, subkey_address, subkey, CE_ARRAY_COUNT(subkey)) != CE_OK) return false;
    const uint16_t *base = NULL; static const uint16_t hkcu[] = {'H','K','C','U',0}, hklm[] = {'H','K','L','M',0};
    if (root == 0x80000001u) base = hkcu; else if (root == 0x80000002u) base = hklm;
    else if (root < CE_MAX_KERNEL_HANDLES && k->handles[root].type == CE_OBJECT_REGISTRY_KEY) base = k->handles[root].value.registry_key;
    if (!base) return false;
    wide_copy(out, capacity, base); size_t used = 0; while (used < capacity && out[used]) ++used;
    if (subkey[0] && used + 1 < capacity) { out[used++] = '\\'; wide_copy(out + used, capacity - used, subkey); }
    return true;
}
static CERegistryValue *registry_value(CEKernel *k, const uint16_t *key, const uint16_t *name) {
    for (size_t i = 0; i < k->registry_count; ++i)
        if (wide_equal(k->registry[i].key, key) && wide_equal(k->registry[i].name, name)) return &k->registry[i];
    return NULL;
}
static bool wildcard_match(const char *pattern, const char *value) {
    while (*pattern) {
        if (*pattern == '*') {
            ++pattern; if (!*pattern) return true;
            while (*value) { if (wildcard_match(pattern, value)) return true; ++value; }
            return false;
        }
        if (!*value || (*pattern != '?' &&
            tolower((unsigned char)*pattern) != tolower((unsigned char)*value))) return false;
        ++pattern; ++value;
    }
    return !*value;
}
static bool find_next(CEFindState *find, CEFindData *out) {
    struct dirent *entry;
    while ((entry = readdir(find->directory)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") ||
            !wildcard_match(find->pattern, entry->d_name)) continue;
        memset(out, 0, sizeof(*out)); char path[1700];
        snprintf(path, sizeof(path), "%s/%s", find->base, entry->d_name);
        struct stat st;
        if (stat(path, &st) == 0) {
            out->fields[0] = S_ISDIR(st.st_mode) ? 0x10u : 0x80u;
            out->fields[8] = (uint32_t)((uint64_t)st.st_size >> 32);
            out->fields[9] = (uint32_t)st.st_size;
        }
        size_t i = 0;
        for (; i + 1 < CE_ARRAY_COUNT(out->filename) && entry->d_name[i]; ++i)
            out->filename[i] = (uint8_t)entry->d_name[i];
        out->filename[i] = 0; return true;
    }
    return false;
}
CEStatus CEKernelDispatch(CEKernel *k, CECPU *cpu, uint16_t module, uint16_t fn) {
    if (!k || !cpu) return CE_ERROR_INVALID_ARGUMENT;
    if (module == CE_MODULE_INTERNAL && fn == CE_INTERNAL_RETURN_WNDPROC) {
        uint32_t target;
        CEStatus s = CEVirtualMemoryReadU32(k->memory, cpu->r[CE_REG_SP], &target);
        if (s != CE_OK) return s;
        cpu->r[CE_REG_SP] += 4; cpu->r[CE_REG_PC] = target & ~1u;
        if (target & 1u) cpu->cpsr |= CE_CPSR_T; else cpu->cpsr &= ~CE_CPSR_T;
        return CE_OK;
    }
    if (module == CE_MODULE_AYGSHELL) {
        if (fn >= CE_AYG_SH_INIT_DIALOG && fn <= CE_AYG_SH_HANDLE_WM_SETTING_CHANGE) {
            cpu->r[0] = 1; return CE_OK;
        }
    }
    if (module == CE_MODULE_COMMCTRL &&
        (fn == CE_COMMCTRL_INIT_COMMON_CONTROLS || fn == CE_COMMCTRL_INIT_COMMON_CONTROLS_EX)) {
        cpu->r[0] = 1; return CE_OK;
    }
    if (module == CE_MODULE_OLE32 && fn == CE_OLE_CO_INITIALIZE_EX) { cpu->r[0] = 0; return CE_OK; }
    if (module == CE_MODULE_OLE32 && fn == CE_OLE_CO_UNINITIALIZE) return CE_OK;
    if (module != CE_MODULE_COREDLL) { k->last_error = 120; cpu->r[0] = 0; return CE_ERROR_UNSUPPORTED; }
    switch (fn) {
    case CE_API_GET_LAST_ERROR: cpu->r[0] = k->last_error; return CE_OK;
    case CE_API_SET_LAST_ERROR: k->last_error = cpu->r[0]; return CE_OK;
    case CE_API_GET_TICK_COUNT: cpu->r[0] = (uint32_t)(CEClockMilliseconds() - k->boot_millis); return CE_OK;
    case CE_API_VIRTUAL_ALLOC: {
        CEAddress result; CEStatus s = CEVirtualMemoryAllocate(k->memory, cpu->r[1],
            CE_PROT_READ | CE_PROT_WRITE, "VirtualAlloc", &result);
        cpu->r[0] = s == CE_OK ? result : 0; if (s != CE_OK) k->last_error = 8; return CE_OK; }
    case CE_API_VIRTUAL_FREE: cpu->r[0] = CEVirtualMemoryFree(k->memory, cpu->r[0]) == CE_OK; return CE_OK;
    case CE_API_CLOSE_HANDLE: cpu->r[0] = CEKernelCloseHandle(k, cpu->r[0]) == CE_OK; return CE_OK;
    case CE_API_CREATE_FILE_W: {
        char path[1400]; if (!guest_path(k, cpu->r[0], path, sizeof(path))) { cpu->r[0] = UINT32_MAX; k->last_error = 123; return CE_OK; }
        const char *mode = (cpu->r[1] & 0x40000000u) ? ((cpu->r[1] & 0x80000000u) ? "w+b" : "wb") : "rb";
        FILE *f = fopen(path, mode); if (!f) { cpu->r[0] = UINT32_MAX; k->last_error = (uint32_t)errno; return CE_OK; }
        CEKernelObject o = {.type = CE_OBJECT_FILE}; o.value.file = f; cpu->r[0] = CEKernelAddHandle(k, o); return CE_OK; }
    case CE_API_READ_FILE: case CE_API_WRITE_FILE: {
        CEHandle h = cpu->r[0]; if (!h || h >= CE_MAX_KERNEL_HANDLES || k->handles[h].type != CE_OBJECT_FILE) { cpu->r[0] = 0; k->last_error = 6; return CE_OK; }
        uint32_t length = cpu->r[2], done = 0; uint8_t buffer[4096]; CEAddress ptr = cpu->r[1];
        while (done < length) { uint32_t chunk = length - done > sizeof(buffer) ? sizeof(buffer) : length - done; size_t n;
            if (fn == CE_API_READ_FILE) { n = fread(buffer, 1, chunk, k->handles[h].value.file); if (write_guest(k, ptr + done, buffer, n) != CE_OK) break; }
            else { if (CEVirtualMemoryRead(k->memory, ptr + done, buffer, chunk) != CE_OK) break; n = fwrite(buffer, 1, chunk, k->handles[h].value.file); }
            done += (uint32_t)n; if (n != chunk) break; }
        write_guest(k, cpu->r[3], &done, sizeof(done)); cpu->r[0] = done == length || fn == CE_API_READ_FILE; return CE_OK; }
    case CE_API_SET_FILE_POINTER: {
        CEHandle h = cpu->r[0];
        if (!h || h >= CE_MAX_KERNEL_HANDLES || k->handles[h].type != CE_OBJECT_FILE) { cpu->r[0] = UINT32_MAX; k->last_error = 6; return CE_OK; }
        int origin = cpu->r[3] == 0 ? SEEK_SET : (cpu->r[3] == 1 ? SEEK_CUR : SEEK_END);
        if (fseek(k->handles[h].value.file, (long)(int32_t)cpu->r[1], origin) != 0) { cpu->r[0] = UINT32_MAX; k->last_error = (uint32_t)errno; }
        else { long position = ftell(k->handles[h].value.file); cpu->r[0] = position < 0 ? UINT32_MAX : (uint32_t)position; }
        return CE_OK; }
    case CE_API_CREATE_EVENT_W: { CEKernelObject o = {.type = CE_OBJECT_EVENT}; o.value.event.manual = cpu->r[1] != 0; o.value.event.signaled = cpu->r[2] != 0; cpu->r[0] = CEKernelAddHandle(k, o); return CE_OK; }
    case CE_API_SET_EVENT: case CE_API_RESET_EVENT: { CEHandle h = cpu->r[0]; bool ok = h && h < CE_MAX_KERNEL_HANDLES && k->handles[h].type == CE_OBJECT_EVENT; if (ok) k->handles[h].value.event.signaled = fn == CE_API_SET_EVENT; cpu->r[0] = ok; return CE_OK; }
    case CE_API_POST_MESSAGE_W: { CEMessage m = {cpu->r[0], cpu->r[1], cpu->r[2], cpu->r[3], (uint32_t)(CEClockMilliseconds() - k->boot_millis)}; cpu->r[0] = CEPostMessage(k->windows, m) == CE_OK; return CE_OK; }
    case CE_API_GET_MESSAGE_W: case CE_API_PEEK_MESSAGE_W: { CEMessage m; bool ok = CEGetMessage(k->windows, &m); if (ok && write_guest(k, cpu->r[0], &m, sizeof(m)) != CE_OK) ok = false; cpu->r[0] = ok; return CE_OK; }
    case CE_API_CREATE_WINDOW_EX_W: {
        uint32_t stack[8]; uint16_t title[128] = {0};
        uint16_t class_name[64] = {0};
        if (CEVirtualMemoryRead(k->memory, cpu->r[CE_REG_SP], stack, sizeof(stack)) != CE_OK) { cpu->r[0] = 0; return CE_OK; }
        if (cpu->r[2]) (void)CEVirtualMemoryReadUTF16(k->memory, cpu->r[2], title, CE_ARRAY_COUNT(title));
        if (cpu->r[1]) (void)CEVirtualMemoryReadUTF16(k->memory, cpu->r[1], class_name, CE_ARRAY_COUNT(class_name));
        CEAddress wndproc = CEWindowClassProc(k->windows, class_name);
        cpu->r[0] = CEWindowCreate(k->windows, stack[4], (int32_t)stack[0], (int32_t)stack[1],
            (int32_t)stack[2], (int32_t)stack[3], cpu->r[3], wndproc, title);
        if (cpu->r[0]) { CEWindow *created = CEWindowFind(k->windows, cpu->r[0]); created->control_id = stack[5]; wide_copy(created->class_name, CE_ARRAY_COUNT(created->class_name), class_name);
            CEWindow *parent = CEWindowFind(k->windows, created->parent); if (parent) { created->x += parent->x; created->y += parent->y; }
            k->windows->focus = cpu->r[0]; CEMessage create = {cpu->r[0], 1, 0, stack[7], 0}; (void)CEPostMessage(k->windows, create);
            static const uint16_t button[] = {'B','U','T','T','O','N',0}, edit[] = {'E','D','I','T',0};
            if (wide_equal(class_name, button) || wide_equal(class_name, edit)) { uint32_t fill = wide_equal(class_name, edit) ? 0xffffffffu : 0xffc0c0c0u; (void)CEGDIFillRect(k->windows, created->x, created->y, created->x + created->width, created->y + created->height, fill); (void)CEGDIFillRect(k->windows, created->x, created->y, created->x + created->width, created->y + 1, 0xff404040u); (void)CEGDIFillRect(k->windows, created->x, created->y, created->x + 1, created->y + created->height, 0xff404040u); }
            size_t title_length = 0; while (title_length < CE_ARRAY_COUNT(title) && title[title_length]) ++title_length; if (title_length) (void)CEGDIDrawTextUTF16(k->windows, created->x + 3, created->y + 3, title, title_length, 0xff000000u, 0xffc0c0c0u, false);
        }
        return CE_OK; }
    case CE_API_DESTROY_WINDOW: cpu->r[0] = CEWindowDestroy(k->windows, cpu->r[0]) == CE_OK; return CE_OK;
    case CE_API_SHOW_WINDOW: { CEWindow *w = CEWindowFind(k->windows, cpu->r[0]); if (w) { w->visible = cpu->r[1] != 0; if (w->visible) { CEMessage paint = {w->handle, 0x000fu, 0, 0, 0}; (void)CEPostMessage(k->windows, paint); } } cpu->r[0] = w != NULL; return CE_OK; }
    case CE_API_INVALIDATE_RECT: { k->windows->generation++; CEMessage paint = {cpu->r[0], 0x000fu, 0, 0, 0}; (void)CEPostMessage(k->windows, paint); cpu->r[0] = 1; return CE_OK; }
    case CE_API_EXIT_PROCESS: cpu->halted = true; cpu->fault = CE_OK; return CE_OK;
    case CE_API_SLEEP: cpu->r[0] = 0; return CE_OK;
    case CE_API_GET_SYSTEM_TIME: return write_system_time(k, cpu->r[0], false);
    case CE_API_GET_LOCAL_TIME: return write_system_time(k, cpu->r[0], true);
    case CE_API_CREATE_DIRECTORY_W: case CE_API_REMOVE_DIRECTORY_W: case CE_API_DELETE_FILE_W: {
        char path[1400]; if (!guest_path(k, cpu->r[0], path, sizeof(path))) { cpu->r[0] = 0; return CE_OK; }
        int result = fn == CE_API_CREATE_DIRECTORY_W ? mkdir(path, 0755) :
            (fn == CE_API_REMOVE_DIRECTORY_W ? rmdir(path) : unlink(path));
        cpu->r[0] = result == 0; if (result) k->last_error = (uint32_t)errno; return CE_OK; }
    case CE_API_MOVE_FILE_W: {
        char from[1400], to[1400]; bool ok = guest_path(k, cpu->r[0], from, sizeof(from)) && guest_path(k, cpu->r[1], to, sizeof(to));
        cpu->r[0] = ok && rename(from, to) == 0; if (!cpu->r[0]) k->last_error = (uint32_t)errno; return CE_OK; }
    case CE_API_GET_FILE_ATTRIBUTES_W: {
        char path[1400]; struct stat st; if (!guest_path(k, cpu->r[0], path, sizeof(path)) || stat(path, &st)) { cpu->r[0] = UINT32_MAX; return CE_OK; }
        cpu->r[0] = S_ISDIR(st.st_mode) ? 0x10u : 0x80u; return CE_OK; }
    case CE_API_GET_FILE_SIZE: { CEHandle h = cpu->r[0]; if (!h || h >= CE_MAX_KERNEL_HANDLES || k->handles[h].type != CE_OBJECT_FILE) { cpu->r[0] = UINT32_MAX; return CE_OK; }
        long old = ftell(k->handles[h].value.file); fseek(k->handles[h].value.file, 0, SEEK_END); long end = ftell(k->handles[h].value.file); fseek(k->handles[h].value.file, old, SEEK_SET); cpu->r[0] = end < 0 ? UINT32_MAX : (uint32_t)end; return CE_OK; }
    case CE_API_FLUSH_FILE_BUFFERS: { CEHandle h = cpu->r[0]; bool ok = h && h < CE_MAX_KERNEL_HANDLES && k->handles[h].type == CE_OBJECT_FILE && fflush(k->handles[h].value.file) == 0; cpu->r[0] = ok; return CE_OK; }
    case CE_API_VIRTUAL_PROTECT: { CEProtection old = CE_PROT_NONE; CEStatus s = CEVirtualMemoryQuery(k->memory, cpu->r[0], &old, NULL); if (s == CE_OK) s = CEVirtualMemoryProtect(k->memory, cpu->r[0], cpu->r[1], ce_protection(cpu->r[2])); uint32_t old_page = page_protection(old); if (s == CE_OK) s = write_guest(k, cpu->r[3], &old_page, 4); cpu->r[0] = s == CE_OK; return CE_OK; }
    case CE_API_LSTRLEN_W: { uint32_t n = 0; uint16_t c; while (n < 0x100000u && CEVirtualMemoryReadU16(k->memory, cpu->r[0] + n * 2, &c) == CE_OK && c) ++n; cpu->r[0] = n; return CE_OK; }
    case CE_API_MEMCPY: { uint8_t buffer[4096]; uint32_t done = 0; while (done < cpu->r[2]) { uint32_t n = cpu->r[2] - done > sizeof(buffer) ? sizeof(buffer) : cpu->r[2] - done; if (CEVirtualMemoryRead(k->memory, cpu->r[1] + done, buffer, n) != CE_OK || CEVirtualMemoryWrite(k->memory, cpu->r[0] + done, buffer, n) != CE_OK) return CE_ERROR_ACCESS_VIOLATION; done += n; } return CE_OK; }
    case CE_API_MEMSET: { uint8_t buffer[4096]; memset(buffer, (uint8_t)cpu->r[1], sizeof(buffer)); uint32_t done = 0; while (done < cpu->r[2]) { uint32_t n = cpu->r[2] - done > sizeof(buffer) ? sizeof(buffer) : cpu->r[2] - done; if (CEVirtualMemoryWrite(k->memory, cpu->r[0] + done, buffer, n) != CE_OK) return CE_ERROR_ACCESS_VIOLATION; done += n; } return CE_OK; }
    case CE_API_LSTRCPY_W: { uint32_t i = 0; uint16_t c; do { if (CEVirtualMemoryReadU16(k->memory, cpu->r[1] + i * 2, &c) != CE_OK || CEVirtualMemoryWrite(k->memory, cpu->r[0] + i * 2, &c, 2) != CE_OK) return CE_ERROR_ACCESS_VIOLATION; ++i; } while (c && i < 0x100000u); return CE_OK; }
    case CE_API_REGISTER_CLASS_W: { uint32_t wc[10]; uint16_t name[64]; if (CEVirtualMemoryRead(k->memory, cpu->r[0], wc, sizeof(wc)) != CE_OK || CEVirtualMemoryReadUTF16(k->memory, wc[9], name, CE_ARRAY_COUNT(name)) != CE_OK) { cpu->r[0] = 0; return CE_OK; } cpu->r[0] = CEWindowRegisterClass(k->windows, name, wc[1], wc[0], wc[8]) == CE_OK ? 1u : 0u; return CE_OK; }
    case CE_API_GET_CLIENT_RECT: { CEWindow *w = CEWindowFind(k->windows, cpu->r[0]); int32_t rect[4] = {0, 0, w ? w->width : (int32_t)k->windows->width, w ? w->height : (int32_t)k->windows->height}; cpu->r[0] = write_guest(k, cpu->r[1], rect, sizeof(rect)) == CE_OK; return CE_OK; }
    case CE_API_GET_DC: cpu->r[0] = 1; return CE_OK;
    case CE_API_RELEASE_DC: cpu->r[0] = 1; return CE_OK;
    case CE_API_CREATE_SOLID_BRUSH: { CEKernelObject o = {.type = CE_OBJECT_BRUSH}; o.value.color = colorref_to_bgra(cpu->r[0]); cpu->r[0] = CEKernelAddHandle(k, o); return CE_OK; }
    case CE_API_DELETE_OBJECT: { CEHandle h = cpu->r[0]; bool valid = h && h < CE_MAX_KERNEL_HANDLES && k->handles[h].type == CE_OBJECT_BRUSH; cpu->r[0] = valid && CEKernelCloseHandle(k, h) == CE_OK; return CE_OK; }
    case CE_API_FILL_RECT: { int32_t rect[4]; CEHandle brush = cpu->r[2]; bool ok = brush < CE_MAX_KERNEL_HANDLES && k->handles[brush].type == CE_OBJECT_BRUSH && CEVirtualMemoryRead(k->memory, cpu->r[1], rect, sizeof(rect)) == CE_OK; if (ok) ok = CEGDIFillRect(k->windows, rect[0], rect[1], rect[2], rect[3], k->handles[brush].value.color) == CE_OK; cpu->r[0] = ok; return CE_OK; }
    case CE_API_SET_PIXEL: { uint32_t color = colorref_to_bgra(cpu->r[3]); cpu->r[0] = CEGDIFillRect(k->windows, (int32_t)cpu->r[1], (int32_t)cpu->r[2], (int32_t)cpu->r[1] + 1, (int32_t)cpu->r[2] + 1, color) == CE_OK ? cpu->r[3] : UINT32_MAX; return CE_OK; }
    case CE_API_DEF_WINDOW_PROC_W: cpu->r[0] = 0; return CE_OK;
    case CE_API_TRANSLATE_MESSAGE: cpu->r[0] = 1; return CE_OK;
    case CE_API_DISPATCH_MESSAGE_W: { CEMessage m; if (CEVirtualMemoryRead(k->memory, cpu->r[0], &m, sizeof(m)) != CE_OK) return CE_ERROR_ACCESS_VIOLATION; CEWindow *w = CEWindowFind(k->windows, m.hwnd); if (!w || !w->wndproc) { cpu->r[0] = 0; return CE_OK; }
        uint32_t resume = cpu->r[CE_REG_PC] | ((cpu->cpsr & CE_CPSR_T) ? 1u : 0u); cpu->r[CE_REG_SP] -= 4; CEStatus s = CEVirtualMemoryWriteU32(k->memory, cpu->r[CE_REG_SP], resume); if (s != CE_OK) return s;
        cpu->r[0] = m.hwnd; cpu->r[1] = m.message; cpu->r[2] = m.wparam; cpu->r[3] = m.lparam; cpu->r[CE_REG_PC] = w->wndproc & ~1u; cpu->r[CE_REG_LR] = CE_API_TRAP_ADDRESS(CE_MODULE_INTERNAL, CE_INTERNAL_RETURN_WNDPROC); if (w->wndproc & 1u) cpu->cpsr |= CE_CPSR_T; else cpu->cpsr &= ~CE_CPSR_T; return CE_OK; }
    case CE_API_REG_CREATE_KEY_EX_W: case CE_API_REG_OPEN_KEY_EX_W: { uint16_t path[128]; if (!registry_path(k, cpu->r[0], cpu->r[1], path, CE_ARRAY_COUNT(path))) { cpu->r[0] = 6; return CE_OK; }
        CEKernelObject object = {.type = CE_OBJECT_REGISTRY_KEY}; wide_copy(object.value.registry_key, CE_ARRAY_COUNT(object.value.registry_key), path); CEHandle handle = CEKernelAddHandle(k, object);
        uint32_t stack[5]; if (!handle || CEVirtualMemoryRead(k->memory, cpu->r[CE_REG_SP], stack, sizeof(stack)) != CE_OK) { cpu->r[0] = 8; return CE_OK; }
        CEAddress result_ptr = fn == CE_API_REG_CREATE_KEY_EX_W ? stack[3] : stack[0]; if (write_guest(k, result_ptr, &handle, 4) != CE_OK) { CEKernelCloseHandle(k, handle); cpu->r[0] = 87; return CE_OK; }
        if (fn == CE_API_REG_CREATE_KEY_EX_W && stack[4]) { uint32_t disposition = 1; (void)write_guest(k, stack[4], &disposition, 4); }
        cpu->r[0] = 0; return CE_OK; }
    case CE_API_REG_CLOSE_KEY: { CEHandle h = cpu->r[0]; if (h >= 0x80000000u) cpu->r[0] = 0; else { bool valid = h && h < CE_MAX_KERNEL_HANDLES && k->handles[h].type == CE_OBJECT_REGISTRY_KEY; cpu->r[0] = valid && CEKernelCloseHandle(k, h) == CE_OK ? 0u : 6u; } return CE_OK; }
    case CE_API_REG_SET_VALUE_EX_W: { CEHandle h = cpu->r[0]; uint16_t name[64] = {0}; uint32_t stack[2]; if (!h || h >= CE_MAX_KERNEL_HANDLES || k->handles[h].type != CE_OBJECT_REGISTRY_KEY || (cpu->r[1] && CEVirtualMemoryReadUTF16(k->memory, cpu->r[1], name, CE_ARRAY_COUNT(name)) != CE_OK) || CEVirtualMemoryRead(k->memory, cpu->r[CE_REG_SP], stack, sizeof(stack)) != CE_OK || stack[1] > sizeof(k->registry[0].data)) { cpu->r[0] = 87; return CE_OK; }
        CERegistryValue *value = registry_value(k, k->handles[h].value.registry_key, name); if (!value) { if (k->registry_count >= CE_MAX_REGISTRY_VALUES) { cpu->r[0] = 8; return CE_OK; } value = &k->registry[k->registry_count++]; memset(value, 0, sizeof(*value)); wide_copy(value->key, CE_ARRAY_COUNT(value->key), k->handles[h].value.registry_key); wide_copy(value->name, CE_ARRAY_COUNT(value->name), name); }
        value->type = cpu->r[3]; value->size = stack[1]; if (value->size && CEVirtualMemoryRead(k->memory, stack[0], value->data, value->size) != CE_OK) { cpu->r[0] = 87; return CE_OK; } cpu->r[0] = 0; return CE_OK; }
    case CE_API_REG_QUERY_VALUE_EX_W: { CEHandle h = cpu->r[0]; uint16_t name[64] = {0}; uint32_t stack[2]; if (!h || h >= CE_MAX_KERNEL_HANDLES || k->handles[h].type != CE_OBJECT_REGISTRY_KEY || (cpu->r[1] && CEVirtualMemoryReadUTF16(k->memory, cpu->r[1], name, CE_ARRAY_COUNT(name)) != CE_OK) || CEVirtualMemoryRead(k->memory, cpu->r[CE_REG_SP], stack, sizeof(stack)) != CE_OK) { cpu->r[0] = 87; return CE_OK; }
        CERegistryValue *value = registry_value(k, k->handles[h].value.registry_key, name); if (!value) { cpu->r[0] = 2; return CE_OK; } uint32_t capacity = 0; if (CEVirtualMemoryReadU32(k->memory, stack[1], &capacity) != CE_OK) { cpu->r[0] = 87; return CE_OK; } (void)write_guest(k, cpu->r[3], &value->type, 4); (void)write_guest(k, stack[1], &value->size, 4); if (capacity < value->size) { cpu->r[0] = 234; return CE_OK; } cpu->r[0] = write_guest(k, stack[0], value->data, value->size) == CE_OK ? 0u : 87u; return CE_OK; }
    case CE_API_REG_DELETE_VALUE_W: { CEHandle h = cpu->r[0]; uint16_t name[64] = {0}; if (!h || h >= CE_MAX_KERNEL_HANDLES || k->handles[h].type != CE_OBJECT_REGISTRY_KEY || CEVirtualMemoryReadUTF16(k->memory, cpu->r[1], name, CE_ARRAY_COUNT(name)) != CE_OK) { cpu->r[0] = 87; return CE_OK; } CERegistryValue *value = registry_value(k, k->handles[h].value.registry_key, name); if (!value) { cpu->r[0] = 2; return CE_OK; } size_t index = (size_t)(value - k->registry); memmove(&k->registry[index], &k->registry[index + 1], (k->registry_count - index - 1) * sizeof(k->registry[0])); k->registry_count--; cpu->r[0] = 0; return CE_OK; }
    case CE_API_TEXT_OUT_W: { uint32_t count; if (CEVirtualMemoryReadU32(k->memory, cpu->r[CE_REG_SP], &count) != CE_OK) return CE_ERROR_ACCESS_VIOLATION; if (count > 512) count = 512; uint16_t text[512]; if (count && CEVirtualMemoryRead(k->memory, cpu->r[3], text, count * 2u) != CE_OK) { cpu->r[0] = 0; return CE_OK; } cpu->r[0] = CEGDIDrawTextUTF16(k->windows, (int32_t)cpu->r[1], (int32_t)cpu->r[2], text, count, k->text_color, k->background_color, k->background_opaque) == CE_OK; return CE_OK; }
    case CE_API_DRAW_TEXT_W: { int32_t rect[4]; uint32_t count = cpu->r[2]; if ((int32_t)count < 0) { uint16_t c; count = 0; while (count < 512 && CEVirtualMemoryReadU16(k->memory, cpu->r[1] + count * 2, &c) == CE_OK && c) ++count; } if (count > 512) count = 512; uint16_t text[512]; if (CEVirtualMemoryRead(k->memory, cpu->r[3], rect, sizeof(rect)) != CE_OK || (count && CEVirtualMemoryRead(k->memory, cpu->r[1], text, count * 2u) != CE_OK)) { cpu->r[0] = 0; return CE_OK; } (void)CEGDIDrawTextUTF16(k->windows, rect[0], rect[1], text, count, k->text_color, k->background_color, k->background_opaque); cpu->r[0] = 8; return CE_OK; }
    case CE_API_SET_TEXT_COLOR: { uint32_t old = k->text_color; k->text_color = colorref_to_bgra(cpu->r[1]); cpu->r[0] = bgra_to_colorref(old); return CE_OK; }
    case CE_API_SET_BK_COLOR: { uint32_t old = k->background_color; k->background_color = colorref_to_bgra(cpu->r[1]); cpu->r[0] = bgra_to_colorref(old); return CE_OK; }
    case CE_API_SET_BK_MODE: { bool old = k->background_opaque; k->background_opaque = cpu->r[1] != 1u; cpu->r[0] = old ? 2u : 1u; return CE_OK; }
    case CE_API_FIND_FIRST_FILE_W: { char path[1600]; if (!guest_path(k, cpu->r[0], path, sizeof(path))) { cpu->r[0] = UINT32_MAX; return CE_OK; } char *slash = strrchr(path, '/'); if (!slash) { cpu->r[0] = UINT32_MAX; return CE_OK; }
        CEFindState *find = calloc(1, sizeof(*find)); if (!find) return CE_ERROR_OUT_OF_MEMORY; strncpy(find->pattern, slash + 1, sizeof(find->pattern) - 1); *slash = 0; strncpy(find->base, path, sizeof(find->base) - 1); find->directory = opendir(find->base); CEFindData data;
        if (!find->directory || !find_next(find, &data)) { if (find->directory) closedir(find->directory); free(find); cpu->r[0] = UINT32_MAX; return CE_OK; }
        CEKernelObject object = {.type = CE_OBJECT_FIND}; object.value.find_state = find; CEHandle handle = CEKernelAddHandle(k, object); if (!handle || write_guest(k, cpu->r[1], &data, sizeof(data)) != CE_OK) { if (handle) CEKernelCloseHandle(k, handle); else { closedir(find->directory); free(find); } cpu->r[0] = UINT32_MAX; return CE_OK; } cpu->r[0] = handle; return CE_OK; }
    case CE_API_FIND_NEXT_FILE_W: { CEHandle h = cpu->r[0]; CEFindData data; if (!h || h >= CE_MAX_KERNEL_HANDLES || k->handles[h].type != CE_OBJECT_FIND || !find_next(k->handles[h].value.find_state, &data)) { cpu->r[0] = 0; return CE_OK; } cpu->r[0] = write_guest(k, cpu->r[1], &data, sizeof(data)) == CE_OK; return CE_OK; }
    case CE_API_FIND_CLOSE: { CEHandle h = cpu->r[0]; bool valid = h && h < CE_MAX_KERNEL_HANDLES && k->handles[h].type == CE_OBJECT_FIND; cpu->r[0] = valid && CEKernelCloseHandle(k, h) == CE_OK; return CE_OK; }
    default: k->last_error = 120; cpu->r[0] = 0; return CE_ERROR_UNSUPPORTED;
    }
}
