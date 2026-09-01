#ifndef CELAYER_CE_RUNTIME_H
#define CELAYER_CE_RUNTIME_H

#include "CEKernel.h"
#include "CEPELoader.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef enum CERuntimeState { CE_RUNTIME_IDLE, CE_RUNTIME_LOADED, CE_RUNTIME_RUNNING,
                              CE_RUNTIME_EXITED, CE_RUNTIME_FAULTED } CERuntimeState;
#define CE_RUNTIME_MAX_MODULES 64u
typedef struct CERuntimeModule { char name[128]; CEPEImage image; bool loading; } CERuntimeModule;
typedef struct CERuntime {
    CEVirtualMemory memory; CEPEImage image; CECPU cpu; CEKernel kernel; CEWindowServer windows;
    CERuntimeState state; CEStatus last_status; char diagnostic[256];
    CERuntimeModule modules[CE_RUNTIME_MAX_MODULES]; size_t module_count;
    bool initializer_complete;
} CERuntime;

CERuntime *CERuntimeCreate(const char *root_path, uint32_t width, uint32_t height);
void CERuntimeDestroy(CERuntime *runtime);
CEStatus CERuntimeLoadExecutable(CERuntime *runtime, const uint8_t *bytes, size_t length);
CEStatus CERuntimeRunSlice(CERuntime *runtime, uint64_t instructions);
const uint32_t *CERuntimeFramebuffer(const CERuntime *runtime, uint32_t *width,
                                     uint32_t *height, uint32_t *generation);
const char *CERuntimeDiagnostic(const CERuntime *runtime);
void CERuntimePostPointer(CERuntime *runtime, int32_t x, int32_t y, bool down);
void CERuntimePostKey(CERuntime *runtime, uint32_t virtual_key, bool down);
void CERuntimePostCharacter(CERuntime *runtime, uint16_t character);

#if defined(__cplusplus)
}
#endif
#endif
