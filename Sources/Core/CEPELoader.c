#include "CELayer/CEPELoader.h"

#include <ctype.h>
#include <string.h>

static bool bounds(size_t offset, size_t length, size_t size) {
    return offset <= size && length <= size - offset;
}
static uint16_t u16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

CEStatus CEPEParse(const uint8_t *data, size_t size, CEPEImage *image) {
    if (!data || !image) return CE_ERROR_INVALID_ARGUMENT;
    memset(image, 0, sizeof(*image));
    if (!bounds(0, 0x40, size) || u16(data) != 0x5a4d) return CE_ERROR_BAD_FORMAT;
    uint32_t pe = u32(data + 0x3c);
    if (!bounds(pe, 24, size) || u32(data + pe) != 0x00004550u) return CE_ERROR_BAD_FORMAT;
    const uint8_t *coff = data + pe + 4;
    image->machine = u16(coff);
    uint16_t section_count = u16(coff + 2);
    uint16_t optional_size = u16(coff + 16);
    if ((image->machine != CE_PE_MACHINE_ARM && image->machine != CE_PE_MACHINE_THUMB) ||
        !section_count || section_count > CE_PE_MAX_SECTIONS || optional_size < 96)
        return CE_ERROR_UNSUPPORTED;
    size_t optional_offset = (size_t)pe + 24;
    if (!bounds(optional_offset, optional_size, size)) return CE_ERROR_BAD_FORMAT;
    const uint8_t *opt = data + optional_offset;
    if (u16(opt) != 0x10bu) return CE_ERROR_UNSUPPORTED;
    image->entry_rva = u32(opt + 16);
    image->image_base = u32(opt + 28);
    image->mapped_base = image->image_base;
    image->size_of_image = u32(opt + 56);
    image->size_of_headers = u32(opt + 60);
    image->subsystem = u16(opt + 68);
    if (!image->size_of_image || image->size_of_headers > image->size_of_image)
        return CE_ERROR_BAD_FORMAT;
    uint32_t directory_count = u32(opt + 92);
    if (directory_count > CE_PE_DIRECTORY_COUNT) directory_count = CE_PE_DIRECTORY_COUNT;
    for (uint32_t i = 0; i < directory_count; ++i) {
        if (96u + i * 8u + 8u > optional_size) break;
        image->directories[i].rva = u32(opt + 96u + i * 8u);
        image->directories[i].size = u32(opt + 100u + i * 8u);
    }
    size_t section_offset = optional_offset + optional_size;
    if (!bounds(section_offset, (size_t)section_count * 40u, size)) return CE_ERROR_BAD_FORMAT;
    image->section_count = section_count;
    for (size_t i = 0; i < section_count; ++i) {
        const uint8_t *s = data + section_offset + i * 40u;
        CEPESection *out = &image->sections[i];
        memcpy(out->name, s, 8); out->name[8] = '\0';
        out->virtual_size = u32(s + 8); out->virtual_address = u32(s + 12);
        out->raw_size = u32(s + 16); out->raw_offset = u32(s + 20);
        out->characteristics = u32(s + 36);
        if (out->raw_size && !bounds(out->raw_offset, out->raw_size, size)) return CE_ERROR_BAD_FORMAT;
        uint64_t mapped_end = (uint64_t)out->virtual_address +
            (out->virtual_size > out->raw_size ? out->virtual_size : out->raw_size);
        if (mapped_end > image->size_of_image) return CE_ERROR_BAD_FORMAT;
    }
    return CE_OK;
}

static CEStatus read_ascii(const CEVirtualMemory *vm, CEAddress address, char *out, size_t cap) {
    if (!cap) return CE_ERROR_INVALID_ARGUMENT;
    for (size_t i = 0; i < cap; ++i) {
        uint8_t c; CEStatus s = CEVirtualMemoryReadU8(vm, address + (CEAddress)i, &c);
        if (s != CE_OK) return s;
        out[i] = (char)c;
        if (!c) return CE_OK;
    }
    out[cap - 1] = '\0'; return CE_ERROR_LIMIT;
}

static CEStatus apply_relocations(CEPEImage *image, CEVirtualMemory *vm) {
    int64_t delta = (int64_t)image->mapped_base - image->image_base;
    if (!delta) return CE_OK;
    CEPEDataDirectory d = image->directories[5];
    if (!d.rva || d.size < 8) return CE_ERROR_ADDRESS_CONFLICT;
    uint32_t cursor = 0;
    while (cursor + 8 <= d.size) {
        uint32_t page, block_size;
        CEStatus s = CEVirtualMemoryReadU32(vm, image->mapped_base + d.rva + cursor, &page);
        if (s != CE_OK) return s;
        s = CEVirtualMemoryReadU32(vm, image->mapped_base + d.rva + cursor + 4, &block_size);
        if (s != CE_OK || block_size < 8 || cursor + block_size > d.size) return CE_ERROR_BAD_FORMAT;
        uint32_t count = (block_size - 8) / 2;
        for (uint32_t i = 0; i < count; ++i) {
            uint16_t item;
            s = CEVirtualMemoryReadU16(vm, image->mapped_base + d.rva + cursor + 8 + i * 2, &item);
            if (s != CE_OK) return s;
            uint16_t type = item >> 12; uint16_t offset = item & 0xfffu;
            if (type == 0) continue;
            if (type != 3) return CE_ERROR_UNSUPPORTED;
            CEAddress target = image->mapped_base + page + offset; uint32_t value;
            s = CEVirtualMemoryReadU32(vm, target, &value);
            if (s != CE_OK) return s;
            s = CEVirtualMemoryWriteU32(vm, target, (uint32_t)((int64_t)value + delta));
            if (s != CE_OK) return s;
        }
        cursor += block_size;
    }
    return CE_OK;
}

static CEStatus resolve_imports(CEPEImage *image, CEVirtualMemory *vm,
                                CEPEImportResolver resolver, void *context) {
    CEPEDataDirectory d = image->directories[1];
    if (!d.rva || !d.size) return CE_OK;
    if (!resolver) return CE_ERROR_NOT_FOUND;
    for (uint32_t off = 0; off + 20 <= d.size; off += 20) {
        CEAddress descriptor = image->mapped_base + d.rva + off;
        uint32_t original, name_rva, first;
        CEStatus s = CEVirtualMemoryReadU32(vm, descriptor, &original);
        if (s != CE_OK) return s;
        s = CEVirtualMemoryReadU32(vm, descriptor + 12, &name_rva); if (s != CE_OK) return s;
        s = CEVirtualMemoryReadU32(vm, descriptor + 16, &first); if (s != CE_OK) return s;
        if (!original && !name_rva && !first) return CE_OK;
        char module[128]; s = read_ascii(vm, image->mapped_base + name_rva, module, sizeof(module));
        if (s != CE_OK && s != CE_ERROR_LIMIT) return s;
        for (size_t i = 0; i < sizeof(module) && module[i]; ++i)
            module[i] = (char)tolower((unsigned char)module[i]);
        uint32_t thunk_rva = original ? original : first;
        for (uint32_t index = 0; ; ++index) {
            uint32_t thunk;
            s = CEVirtualMemoryReadU32(vm, image->mapped_base + thunk_rva + index * 4, &thunk);
            if (s != CE_OK) return s;
            if (!thunk) break;
            char symbol[192] = {0}; uint16_t ordinal = 0;
            if (thunk & 0x80000000u) ordinal = (uint16_t)(thunk & 0xffffu);
            else {
                s = read_ascii(vm, image->mapped_base + thunk + 2, symbol, sizeof(symbol));
                if (s != CE_OK && s != CE_ERROR_LIMIT) return s;
            }
            CEAddress resolved;
            s = resolver(context, module, symbol[0] ? symbol : NULL, ordinal, &resolved);
            if (s != CE_OK) return s;
            s = CEVirtualMemoryWriteU32(vm, image->mapped_base + first + index * 4, resolved);
            if (s != CE_OK) return s;
        }
    }
    return CE_ERROR_BAD_FORMAT;
}

CEStatus CEPEMap(const uint8_t *data, size_t size, CEPEImage *image,
                 CEVirtualMemory *memory, CEPEImportResolver resolver, void *context) {
    CEStatus status = CEPEParse(data, size, image);
    if (status != CE_OK) return status;
    status = CEVirtualMemoryMap(memory, image->image_base, image->size_of_image,
        CE_PROT_READ | CE_PROT_WRITE | CE_PROT_EXEC, "PE image");
    if (status == CE_ERROR_ADDRESS_CONFLICT) {
        CEAddress alternate;
        status = CEVirtualMemoryAllocate(memory, image->size_of_image,
            CE_PROT_READ | CE_PROT_WRITE | CE_PROT_EXEC, "PE image", &alternate);
        if (status == CE_OK) image->mapped_base = alternate;
    }
    if (status != CE_OK) return status;
    uint32_t headers = image->size_of_headers < size ? image->size_of_headers : (uint32_t)size;
    status = CEVirtualMemoryWrite(memory, image->mapped_base, data, headers);
    if (status != CE_OK) return status;
    for (size_t i = 0; i < image->section_count; ++i) {
        CEPESection *section = &image->sections[i];
        if (!section->raw_size) continue;
        status = CEVirtualMemoryWrite(memory, image->mapped_base + section->virtual_address,
                                      data + section->raw_offset, section->raw_size);
        if (status != CE_OK) return status;
    }
    status = apply_relocations(image, memory); if (status != CE_OK) return status;
    return resolve_imports(image, memory, resolver, context);
}
