#ifndef CELAYER_CE_PE_LOADER_H
#define CELAYER_CE_PE_LOADER_H

#include "CEVirtualMemory.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define CE_PE_MAX_SECTIONS 96u
#define CE_PE_DIRECTORY_COUNT 16u
#define CE_PE_MACHINE_ARM 0x01c0u
#define CE_PE_MACHINE_THUMB 0x01c2u

typedef struct CEPEDataDirectory { uint32_t rva, size; } CEPEDataDirectory;
typedef struct CEPESection {
    char name[9];
    uint32_t virtual_size, virtual_address, raw_size, raw_offset, characteristics;
} CEPESection;

typedef struct CEPEImage {
    uint16_t machine;
    uint16_t subsystem;
    uint32_t image_base, mapped_base, entry_rva, size_of_image, size_of_headers;
    CEPEDataDirectory directories[CE_PE_DIRECTORY_COUNT];
    CEPESection sections[CE_PE_MAX_SECTIONS];
    size_t section_count;
} CEPEImage;

typedef CEStatus (*CEPEImportResolver)(void *context, const char *module,
                                       const char *symbol, uint16_t ordinal,
                                       CEAddress *address);

CEStatus CEPEParse(const uint8_t *data, size_t size, CEPEImage *image);
CEStatus CEPEMap(const uint8_t *data, size_t size, CEPEImage *image,
                 CEVirtualMemory *memory, CEPEImportResolver resolver, void *context);
CEStatus CEPELookupExport(const CEPEImage *image, const CEVirtualMemory *memory,
                          const char *symbol, uint16_t ordinal, CEAddress *address);
CEStatus CEPELookupStringResource(const CEPEImage *image, const CEVirtualMemory *memory,
                                  uint32_t identifier, uint16_t *text, size_t capacity,
                                  size_t *length);

#if defined(__cplusplus)
}
#endif
#endif
