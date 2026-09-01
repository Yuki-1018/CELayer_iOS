#ifndef CELAYER_CE_VIRTUAL_MEMORY_H
#define CELAYER_CE_VIRTUAL_MEMORY_H

#include "CECommon.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define CE_VM_MAX_REGIONS 512u

typedef struct CEMemoryRegion {
    CEAddress base;
    uint32_t size;
    CEProtection protection;
    uint8_t *bytes;
    bool committed;
    char name[32];
} CEMemoryRegion;

typedef struct CEVirtualMemory {
    CEMemoryRegion regions[CE_VM_MAX_REGIONS];
    size_t region_count;
    CEAddress allocation_cursor;
} CEVirtualMemory;

void CEVirtualMemoryInit(CEVirtualMemory *vm);
void CEVirtualMemoryDestroy(CEVirtualMemory *vm);
CEStatus CEVirtualMemoryMap(CEVirtualMemory *vm, CEAddress base, uint32_t size,
                            CEProtection protection, const char *name);
CEStatus CEVirtualMemoryAllocate(CEVirtualMemory *vm, uint32_t size,
                                 CEProtection protection, const char *name,
                                 CEAddress *address);
CEStatus CEVirtualMemoryProtect(CEVirtualMemory *vm, CEAddress address,
                                uint32_t size, CEProtection protection);
CEStatus CEVirtualMemoryFree(CEVirtualMemory *vm, CEAddress address);
CEStatus CEVirtualMemoryRead(const CEVirtualMemory *vm, CEAddress address,
                             void *destination, size_t length);
CEStatus CEVirtualMemoryWrite(CEVirtualMemory *vm, CEAddress address,
                              const void *source, size_t length);
CEStatus CEVirtualMemoryReadU8(const CEVirtualMemory *vm, CEAddress address, uint8_t *value);
CEStatus CEVirtualMemoryReadU16(const CEVirtualMemory *vm, CEAddress address, uint16_t *value);
CEStatus CEVirtualMemoryReadU32(const CEVirtualMemory *vm, CEAddress address, uint32_t *value);
CEStatus CEVirtualMemoryWriteU32(CEVirtualMemory *vm, CEAddress address, uint32_t value);
CEStatus CEVirtualMemoryReadUTF16(const CEVirtualMemory *vm, CEAddress address,
                                  uint16_t *destination, size_t capacity);

#if defined(__cplusplus)
}
#endif
#endif
