#ifndef CELAYER_CE_CPU_H
#define CELAYER_CE_CPU_H

#include "CEVirtualMemory.h"

#if defined(__cplusplus)
extern "C" {
#endif

enum { CE_REG_SP = 13, CE_REG_LR = 14, CE_REG_PC = 15 };
#define CE_CPSR_N UINT32_C(0x80000000)
#define CE_CPSR_Z UINT32_C(0x40000000)
#define CE_CPSR_C UINT32_C(0x20000000)
#define CE_CPSR_V UINT32_C(0x10000000)
#define CE_CPSR_T UINT32_C(0x00000020)

struct CECPU;
typedef CEStatus (*CECPUTrapHandler)(void *context, struct CECPU *cpu, uint32_t trap);

typedef struct CECPU {
    uint32_t r[16];
    uint32_t cpsr;
    uint64_t instruction_count;
    CEVirtualMemory *memory;
    CECPUTrapHandler trap_handler;
    void *trap_context;
    bool halted;
    CEStatus fault;
} CECPU;

void CECPUInit(CECPU *cpu, CEVirtualMemory *memory);
void CECPUSetEntry(CECPU *cpu, CEAddress entry, CEAddress stack_top, bool thumb);
CEStatus CECPUStep(CECPU *cpu);
CEStatus CECPURun(CECPU *cpu, uint64_t instruction_budget);

#if defined(__cplusplus)
}
#endif
#endif
