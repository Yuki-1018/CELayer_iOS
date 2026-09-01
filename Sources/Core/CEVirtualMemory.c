#include "CELayer/CEVirtualMemory.h"

#include <stdlib.h>
#include <string.h>

static uint32_t align_page(uint32_t value) {
    return (value + CE_PAGE_SIZE - 1u) & ~(CE_PAGE_SIZE - 1u);
}

static bool ranges_overlap(CEAddress a, uint32_t as, CEAddress b, uint32_t bs) {
    uint64_t ae = (uint64_t)a + as;
    uint64_t be = (uint64_t)b + bs;
    return (uint64_t)a < be && (uint64_t)b < ae;
}

static CEMemoryRegion *find_region(CEVirtualMemory *vm, CEAddress address, size_t length) {
    uint64_t end = (uint64_t)address + length;
    for (size_t i = 0; i < vm->region_count; ++i) {
        CEMemoryRegion *r = &vm->regions[i];
        if (address >= r->base && end <= (uint64_t)r->base + r->size) return r;
    }
    return NULL;
}

static const CEMemoryRegion *find_region_const(const CEVirtualMemory *vm, CEAddress address,
                                                size_t length) {
    return find_region((CEVirtualMemory *)(uintptr_t)vm, address, length);
}

void CEVirtualMemoryInit(CEVirtualMemory *vm) {
    if (!vm) return;
    memset(vm, 0, sizeof(*vm));
    vm->allocation_cursor = 0x10000000u;
}

void CEVirtualMemoryDestroy(CEVirtualMemory *vm) {
    if (!vm) return;
    for (size_t i = 0; i < vm->region_count; ++i) free(vm->regions[i].bytes);
    CEVirtualMemoryInit(vm);
}

CEStatus CEVirtualMemoryMap(CEVirtualMemory *vm, CEAddress base, uint32_t size,
                            CEProtection protection, const char *name) {
    if (!vm || !size || (base & (CE_PAGE_SIZE - 1u))) return CE_ERROR_INVALID_ARGUMENT;
    size = align_page(size);
    if ((uint64_t)base + size > UINT32_MAX || vm->region_count >= CE_VM_MAX_REGIONS)
        return CE_ERROR_LIMIT;
    for (size_t i = 0; i < vm->region_count; ++i)
        if (ranges_overlap(base, size, vm->regions[i].base, vm->regions[i].size))
            return CE_ERROR_ADDRESS_CONFLICT;
    uint8_t *bytes = calloc(1, size);
    if (!bytes) return CE_ERROR_OUT_OF_MEMORY;
    CEMemoryRegion *r = &vm->regions[vm->region_count++];
    r->base = base;
    r->size = size;
    r->protection = protection;
    r->bytes = bytes;
    r->committed = true;
    if (name) {
        strncpy(r->name, name, sizeof(r->name) - 1u);
        r->name[sizeof(r->name) - 1u] = '\0';
    }
    if (base >= vm->allocation_cursor) vm->allocation_cursor = base + size;
    return CE_OK;
}

CEStatus CEVirtualMemoryAllocate(CEVirtualMemory *vm, uint32_t size,
                                 CEProtection protection, const char *name,
                                 CEAddress *address) {
    if (!vm || !address || !size) return CE_ERROR_INVALID_ARGUMENT;
    size = align_page(size);
    CEAddress candidate = align_page(vm->allocation_cursor);
    for (size_t attempts = 0; attempts <= vm->region_count; ++attempts) {
        bool conflict = false;
        for (size_t i = 0; i < vm->region_count; ++i) {
            CEMemoryRegion *r = &vm->regions[i];
            if (ranges_overlap(candidate, size, r->base, r->size)) {
                candidate = align_page(r->base + r->size);
                conflict = true;
                break;
            }
        }
        if (!conflict) {
            CEStatus status = CEVirtualMemoryMap(vm, candidate, size, protection, name);
            if (status == CE_OK) *address = candidate;
            return status;
        }
    }
    return CE_ERROR_ADDRESS_CONFLICT;
}

CEStatus CEVirtualMemoryProtect(CEVirtualMemory *vm, CEAddress address,
                                uint32_t size, CEProtection protection) {
    if (!vm || !size) return CE_ERROR_INVALID_ARGUMENT;
    CEMemoryRegion *r = find_region(vm, address, size);
    if (!r) return CE_ERROR_ACCESS_VIOLATION;
    r->protection = protection;
    return CE_OK;
}

CEStatus CEVirtualMemoryFree(CEVirtualMemory *vm, CEAddress address) {
    if (!vm || !address) return CE_ERROR_INVALID_ARGUMENT;
    for (size_t i = 0; i < vm->region_count; ++i) {
        if (vm->regions[i].base == address) {
            free(vm->regions[i].bytes);
            memmove(&vm->regions[i], &vm->regions[i + 1],
                    (vm->region_count - i - 1u) * sizeof(vm->regions[0]));
            vm->region_count--;
            return CE_OK;
        }
    }
    return CE_ERROR_NOT_FOUND;
}

CEStatus CEVirtualMemoryQuery(const CEVirtualMemory *vm, CEAddress address,
                              CEProtection *protection, uint32_t *remaining_size) {
    if (!vm) return CE_ERROR_INVALID_ARGUMENT;
    const CEMemoryRegion *r = find_region_const(vm, address, 1);
    if (!r) return CE_ERROR_NOT_FOUND;
    if (protection) *protection = r->protection;
    if (remaining_size) *remaining_size = r->size - (address - r->base);
    return CE_OK;
}

CEStatus CEVirtualMemoryRead(const CEVirtualMemory *vm, CEAddress address,
                             void *destination, size_t length) {
    if (!vm || (!destination && length)) return CE_ERROR_INVALID_ARGUMENT;
    if (!length) return CE_OK;
    const CEMemoryRegion *r = find_region_const(vm, address, length);
    if (!r || !(r->protection & CE_PROT_READ)) return CE_ERROR_ACCESS_VIOLATION;
    memcpy(destination, r->bytes + (address - r->base), length);
    return CE_OK;
}

CEStatus CEVirtualMemoryWrite(CEVirtualMemory *vm, CEAddress address,
                              const void *source, size_t length) {
    if (!vm || (!source && length)) return CE_ERROR_INVALID_ARGUMENT;
    if (!length) return CE_OK;
    CEMemoryRegion *r = find_region(vm, address, length);
    if (!r || !(r->protection & CE_PROT_WRITE)) return CE_ERROR_ACCESS_VIOLATION;
    memcpy(r->bytes + (address - r->base), source, length);
    return CE_OK;
}

CEStatus CEVirtualMemoryReadU8(const CEVirtualMemory *vm, CEAddress address, uint8_t *value) {
    return CEVirtualMemoryRead(vm, address, value, sizeof(*value));
}

CEStatus CEVirtualMemoryReadU16(const CEVirtualMemory *vm, CEAddress address, uint16_t *value) {
    uint8_t b[2]; CEStatus s = CEVirtualMemoryRead(vm, address, b, 2);
    if (s == CE_OK) *value = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
    return s;
}

CEStatus CEVirtualMemoryReadU32(const CEVirtualMemory *vm, CEAddress address, uint32_t *value) {
    uint8_t b[4]; CEStatus s = CEVirtualMemoryRead(vm, address, b, 4);
    if (s == CE_OK) *value = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
        ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return s;
}

CEStatus CEVirtualMemoryWriteU32(CEVirtualMemory *vm, CEAddress address, uint32_t value) {
    uint8_t b[4] = {(uint8_t)value, (uint8_t)(value >> 8),
                    (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    return CEVirtualMemoryWrite(vm, address, b, 4);
}

CEStatus CEVirtualMemoryReadUTF16(const CEVirtualMemory *vm, CEAddress address,
                                  uint16_t *destination, size_t capacity) {
    if (!destination || !capacity) return CE_ERROR_INVALID_ARGUMENT;
    for (size_t i = 0; i < capacity; ++i) {
        CEStatus s = CEVirtualMemoryReadU16(vm, address + (CEAddress)(i * 2u), &destination[i]);
        if (s != CE_OK) return s;
        if (!destination[i]) return CE_OK;
    }
    destination[capacity - 1u] = 0;
    return CE_ERROR_LIMIT;
}

const char *CEStatusString(CEStatus status) {
    static const char *names[] = {"ok", "invalid argument", "bad format", "unsupported",
        "out of memory", "address conflict", "access violation", "not found", "I/O error",
        "limit reached", "halted"};
    return (unsigned)status < CE_ARRAY_COUNT(names) ? names[status] : "unknown";
}
