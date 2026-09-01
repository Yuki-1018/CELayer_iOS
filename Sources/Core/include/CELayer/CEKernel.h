#ifndef CELAYER_CE_KERNEL_H
#define CELAYER_CE_KERNEL_H

#include "CECPU.h"
#include "CEWindowServer.h"
#include <stdio.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define CE_MAX_KERNEL_HANDLES 256u
#define CE_MAX_REGISTRY_VALUES 256u
typedef enum CEObjectType { CE_OBJECT_NONE, CE_OBJECT_FILE, CE_OBJECT_EVENT,
                            CE_OBJECT_WINDOW, CE_OBJECT_BRUSH, CE_OBJECT_REGISTRY_KEY,
                            CE_OBJECT_FIND, CE_OBJECT_PEN } CEObjectType;
typedef struct CEKernelObject { CEObjectType type; uint32_t references; union {
    FILE *file; struct { bool signaled, manual; } event; CEHandle window; uint32_t color;
    uint16_t registry_key[128]; void *find_state;
} value; } CEKernelObject;
typedef struct CERegistryValue { uint16_t key[128], name[64]; uint32_t type; uint8_t data[512]; uint32_t size; } CERegistryValue;
typedef struct CEKernel {
    CEVirtualMemory *memory; CEWindowServer *windows; CEKernelObject handles[CE_MAX_KERNEL_HANDLES];
    CERegistryValue registry[CE_MAX_REGISTRY_VALUES]; size_t registry_count;
    char root_path[1024]; uint32_t last_error; uint64_t boot_millis; CEAddress heap_cursor, heap_end;
    uint32_t text_color, background_color; bool background_opaque;
    CEAddress tls_address; uint64_t tls_used;
    uint32_t selected_color; int32_t pen_x, pen_y;
} CEKernel;

void CEKernelInit(CEKernel *kernel, CEVirtualMemory *memory, CEWindowServer *windows,
                  const char *root_path);
CEStatus CEKernelFlushRegistry(CEKernel *kernel);
CEHandle CEKernelAddHandle(CEKernel *kernel, CEKernelObject object);
CEStatus CEKernelCloseHandle(CEKernel *kernel, CEHandle handle);
CEStatus CEKernelDispatch(CEKernel *kernel, CECPU *cpu, uint16_t module, uint16_t function);
uint64_t CEClockMilliseconds(void);

#if defined(__cplusplus)
}
#endif
#endif
