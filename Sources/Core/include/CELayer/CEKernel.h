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
typedef enum CEObjectType { CE_OBJECT_NONE, CE_OBJECT_FILE, CE_OBJECT_EVENT, CE_OBJECT_WINDOW } CEObjectType;
typedef struct CEKernelObject { CEObjectType type; uint32_t references; union { FILE *file; struct { bool signaled, manual; } event; CEHandle window; } value; } CEKernelObject;
typedef struct CERegistryValue { uint16_t key[128], name[64]; uint32_t type; uint8_t data[512]; uint32_t size; } CERegistryValue;
typedef struct CEKernel {
    CEVirtualMemory *memory; CEWindowServer *windows; CEKernelObject handles[CE_MAX_KERNEL_HANDLES];
    CERegistryValue registry[CE_MAX_REGISTRY_VALUES]; size_t registry_count;
    char root_path[1024]; uint32_t last_error; uint64_t boot_millis; CEAddress heap_cursor, heap_end;
} CEKernel;

void CEKernelInit(CEKernel *kernel, CEVirtualMemory *memory, CEWindowServer *windows,
                  const char *root_path);
CEHandle CEKernelAddHandle(CEKernel *kernel, CEKernelObject object);
CEStatus CEKernelCloseHandle(CEKernel *kernel, CEHandle handle);
CEStatus CEKernelDispatch(CEKernel *kernel, CECPU *cpu, uint16_t module, uint16_t function);
uint64_t CEClockMilliseconds(void);

#if defined(__cplusplus)
}
#endif
#endif
