#ifndef CELAYER_CE_RUNTIME_H
#define CELAYER_CE_RUNTIME_H

#include "CEKernel.h"
#include "CEPELoader.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef enum CERuntimeState { CE_RUNTIME_IDLE, CE_RUNTIME_LOADED, CE_RUNTIME_RUNNING,
                              CE_RUNTIME_EXITED, CE_RUNTIME_FAULTED } CERuntimeState;
typedef struct CERuntime {
    CEVirtualMemory memory; CEPEImage image; CECPU cpu; CEKernel kernel; CEWindowServer windows;
    CERuntimeState state; CEStatus last_status; char diagnostic[256];
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

#if defined(__cplusplus)
}
#endif
#endif
