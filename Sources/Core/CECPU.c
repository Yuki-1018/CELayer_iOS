#include "CELayer/CECPU.h"

#include <string.h>

static uint32_t ror32(uint32_t v, unsigned n) { n &= 31u; return n ? (v >> n) | (v << (32u - n)) : v; }
static int32_t sign_extend(uint32_t value, unsigned bits) {
    uint32_t sign = 1u << (bits - 1u); return (int32_t)((value ^ sign) - sign);
}
static void set_nz(CECPU *cpu, uint32_t value) {
    cpu->cpsr &= ~(CE_CPSR_N | CE_CPSR_Z);
    if (!value) cpu->cpsr |= CE_CPSR_Z;
    if (value & 0x80000000u) cpu->cpsr |= CE_CPSR_N;
}
static void set_add_flags(CECPU *cpu, uint32_t a, uint32_t b, uint32_t result) {
    set_nz(cpu, result); cpu->cpsr &= ~(CE_CPSR_C | CE_CPSR_V);
    if ((uint64_t)a + b > UINT32_MAX) cpu->cpsr |= CE_CPSR_C;
    if ((~(a ^ b) & (a ^ result)) & 0x80000000u) cpu->cpsr |= CE_CPSR_V;
}
static void set_sub_flags(CECPU *cpu, uint32_t a, uint32_t b, uint32_t result) {
    set_nz(cpu, result); cpu->cpsr &= ~(CE_CPSR_C | CE_CPSR_V);
    if (a >= b) cpu->cpsr |= CE_CPSR_C;
    if (((a ^ b) & (a ^ result)) & 0x80000000u) cpu->cpsr |= CE_CPSR_V;
}
static bool condition(const CECPU *cpu, unsigned c) {
    bool n = (cpu->cpsr & CE_CPSR_N) != 0, z = (cpu->cpsr & CE_CPSR_Z) != 0;
    bool carry = (cpu->cpsr & CE_CPSR_C) != 0, v = (cpu->cpsr & CE_CPSR_V) != 0;
    switch (c) { case 0: return z; case 1: return !z; case 2: return carry; case 3: return !carry;
      case 4: return n; case 5: return !n; case 6: return v; case 7: return !v;
      case 8: return carry && !z; case 9: return !carry || z; case 10: return n == v;
      case 11: return n != v; case 12: return !z && n == v; case 13: return z || n != v;
      case 14: return true; default: return false; }
}

void CECPUInit(CECPU *cpu, CEVirtualMemory *memory) {
    if (!cpu) return;
    memset(cpu, 0, sizeof(*cpu)); cpu->memory = memory; cpu->fault = CE_OK;
}
void CECPUSetEntry(CECPU *cpu, CEAddress entry, CEAddress stack_top, bool thumb) {
    cpu->r[CE_REG_PC] = entry & ~1u; cpu->r[CE_REG_SP] = stack_top & ~7u;
    if (thumb || (entry & 1u)) cpu->cpsr |= CE_CPSR_T; else cpu->cpsr &= ~CE_CPSR_T;
    cpu->halted = false; cpu->fault = CE_OK;
}

static uint32_t arm_operand2(CECPU *cpu, uint32_t insn) {
    if (insn & (1u << 25)) return ror32(insn & 0xffu, ((insn >> 8) & 0xfu) * 2u);
    uint32_t value = cpu->r[insn & 0xfu]; unsigned type = (insn >> 5) & 3u;
    unsigned amount = (insn & (1u << 4)) ? (cpu->r[(insn >> 8) & 0xfu] & 0xffu) : ((insn >> 7) & 0x1fu);
    if (!amount) return value;
    if (type == 0) return value << amount;
    if (type == 1) return value >> amount;
    if (type == 2) return (uint32_t)((int32_t)value >> amount);
    return ror32(value, amount);
}

static CEStatus step_arm(CECPU *cpu) {
    CEAddress pc = cpu->r[CE_REG_PC]; uint32_t x;
    CEStatus s = CEVirtualMemoryReadU32(cpu->memory, pc, &x); if (s != CE_OK) return s;
    cpu->r[CE_REG_PC] = pc + 4;
    if (!condition(cpu, x >> 28)) return CE_OK;
    if ((x & 0x0ffffff0u) == 0x012fff10u) { /* BX */
        uint32_t target = cpu->r[x & 0xfu];
        cpu->r[CE_REG_PC] = target & ~1u;
        if (target & 1u) cpu->cpsr |= CE_CPSR_T; else cpu->cpsr &= ~CE_CPSR_T;
        return CE_OK;
    }
    if ((x & 0x0f000000u) == 0x0f000000u) {
        return cpu->trap_handler ? cpu->trap_handler(cpu->trap_context, cpu, x & 0x00ffffffu)
                                 : CE_ERROR_UNSUPPORTED;
    }
    if ((x & 0x0e000000u) == 0x0a000000u) { /* B/BL */
        if (x & (1u << 24)) cpu->r[CE_REG_LR] = pc + 4;
        cpu->r[CE_REG_PC] = pc + 8 + (uint32_t)(sign_extend(x & 0xffffffu, 24) << 2);
        return CE_OK;
    }
    if ((x & 0x0c000000u) == 0x04000000u) { /* LDR/STR */
        bool pre = x & (1u << 24), up = x & (1u << 23), byte = x & (1u << 22);
        bool writeback = x & (1u << 21), load = x & (1u << 20);
        unsigned rn = (x >> 16) & 0xfu, rd = (x >> 12) & 0xfu;
        uint32_t offset = (x & (1u << 25)) ? arm_operand2(cpu, x & ~(1u << 25)) : (x & 0xfffu);
        uint32_t base = rn == CE_REG_PC ? pc + 8 : cpu->r[rn];
        uint32_t adjusted = up ? base + offset : base - offset; uint32_t address = pre ? adjusted : base;
        if (load) {
            if (byte) { uint8_t v; s = CEVirtualMemoryReadU8(cpu->memory, address, &v); cpu->r[rd] = v; }
            else s = CEVirtualMemoryReadU32(cpu->memory, address, &cpu->r[rd]);
        } else if (byte) { uint8_t v = (uint8_t)cpu->r[rd]; s = CEVirtualMemoryWrite(cpu->memory, address, &v, 1); }
        else s = CEVirtualMemoryWriteU32(cpu->memory, address, cpu->r[rd]);
        if (!pre || writeback) cpu->r[rn] = adjusted;
        return s;
    }
    if ((x & 0x0c000000u) == 0) { /* data processing */
        unsigned opcode = (x >> 21) & 0xfu, rn = (x >> 16) & 0xfu, rd = (x >> 12) & 0xfu;
        uint32_t a = cpu->r[rn], b = arm_operand2(cpu, x), result = 0; bool write = true;
        switch (opcode) { case 0: result = a & b; break; case 1: result = a ^ b; break;
          case 2: result = a - b; break; case 4: result = a + b; break; case 10: result = a - b; write = false; break;
          case 12: result = a | b; break; case 13: result = b; break; case 14: result = a & ~b; break;
          case 15: result = ~b; break; default: return CE_ERROR_UNSUPPORTED; }
        if ((x & (1u << 20)) || !write) {
            if (opcode == 2 || opcode == 10) set_sub_flags(cpu, a, b, result);
            else if (opcode == 4) set_add_flags(cpu, a, b, result); else set_nz(cpu, result);
        }
        if (write) cpu->r[rd] = result;
        return CE_OK;
    }
    return CE_ERROR_UNSUPPORTED;
}

static CEStatus push(CECPU *cpu, uint32_t value) {
    cpu->r[CE_REG_SP] -= 4; return CEVirtualMemoryWriteU32(cpu->memory, cpu->r[CE_REG_SP], value);
}
static CEStatus pop(CECPU *cpu, uint32_t *value) {
    CEStatus s = CEVirtualMemoryReadU32(cpu->memory, cpu->r[CE_REG_SP], value);
    if (s == CE_OK) cpu->r[CE_REG_SP] += 4;
    return s;
}
static CEStatus step_thumb(CECPU *cpu) {
    CEAddress pc = cpu->r[CE_REG_PC]; uint16_t x;
    CEStatus s = CEVirtualMemoryReadU16(cpu->memory, pc, &x); if (s != CE_OK) return s;
    cpu->r[CE_REG_PC] = pc + 2;
    if ((x & 0xff00u) == 0xdf00u) return cpu->trap_handler ?
        cpu->trap_handler(cpu->trap_context, cpu, x & 0xffu) : CE_ERROR_UNSUPPORTED;
    if ((x & 0xff87u) == 0x4700u) { uint32_t target = cpu->r[(x >> 3) & 0xfu];
        cpu->r[CE_REG_PC] = target & ~1u; if (target & 1u) cpu->cpsr |= CE_CPSR_T;
        else cpu->cpsr &= ~CE_CPSR_T;
        return CE_OK; }
    if ((x & 0xf800u) == 0x2000u) { unsigned rd = (x >> 8) & 7u; cpu->r[rd] = x & 0xffu; set_nz(cpu, cpu->r[rd]); return CE_OK; }
    if ((x & 0xf800u) == 0x2800u) { unsigned rn = (x >> 8) & 7u; uint32_t b = x & 0xffu;
        set_sub_flags(cpu, cpu->r[rn], b, cpu->r[rn] - b); return CE_OK; }
    if ((x & 0xf800u) == 0x3000u) { unsigned rd = (x >> 8) & 7u; uint32_t a = cpu->r[rd], b = x & 0xffu;
        cpu->r[rd] = a + b; set_add_flags(cpu, a, b, cpu->r[rd]); return CE_OK; }
    if ((x & 0xf800u) == 0x3800u) { unsigned rd = (x >> 8) & 7u; uint32_t a = cpu->r[rd], b = x & 0xffu;
        cpu->r[rd] = a - b; set_sub_flags(cpu, a, b, cpu->r[rd]); return CE_OK; }
    if ((x & 0xf800u) == 0x4800u) { unsigned rd = (x >> 8) & 7u;
        return CEVirtualMemoryReadU32(cpu->memory, ((pc + 4) & ~3u) + ((x & 0xffu) << 2), &cpu->r[rd]); }
    if ((x & 0xf000u) == 0x6000u) { bool load = x & 0x0800u; unsigned imm = ((x >> 6) & 0x1fu) << 2;
        unsigned rn = (x >> 3) & 7u, rd = x & 7u; CEAddress a = cpu->r[rn] + imm;
        return load ? CEVirtualMemoryReadU32(cpu->memory, a, &cpu->r[rd]) : CEVirtualMemoryWriteU32(cpu->memory, a, cpu->r[rd]); }
    if ((x & 0xfe00u) == 0xb400u) { bool load = x & 0x0800u; uint8_t list = x & 0xffu;
        if (!load) { if (x & 0x0100u) { s = push(cpu, cpu->r[CE_REG_LR]); if (s != CE_OK) return s; }
            for (int i = 7; i >= 0; --i) if (list & (1u << i)) { s = push(cpu, cpu->r[i]); if (s != CE_OK) return s; } }
        else { for (unsigned i = 0; i < 8; ++i) if (list & (1u << i)) { s = pop(cpu, &cpu->r[i]); if (s != CE_OK) return s; }
            if (x & 0x0100u) { uint32_t target; s = pop(cpu, &target); if (s != CE_OK) return s;
                cpu->r[CE_REG_PC] = target & ~1u; if (!(target & 1u)) cpu->cpsr &= ~CE_CPSR_T; } }
        return CE_OK;
    }
    if ((x & 0xf000u) == 0xd000u && (x & 0x0f00u) != 0x0f00u) {
        unsigned cond = (x >> 8) & 0xfu; if (condition(cpu, cond)) cpu->r[CE_REG_PC] = pc + 4 + (sign_extend(x & 0xffu, 8) << 1); return CE_OK;
    }
    if ((x & 0xf800u) == 0xe000u) { cpu->r[CE_REG_PC] = pc + 4 + (sign_extend(x & 0x7ffu, 11) << 1); return CE_OK; }
    return CE_ERROR_UNSUPPORTED;
}

CEStatus CECPUStep(CECPU *cpu) {
    if (!cpu || !cpu->memory) return CE_ERROR_INVALID_ARGUMENT;
    if (cpu->halted) return CE_ERROR_HALTED;
    if ((cpu->r[CE_REG_PC] & 0xf0000000u) == 0xf0000000u) {
        uint32_t trap = cpu->r[CE_REG_PC] & 0x0fffffffu;
        cpu->r[CE_REG_PC] = cpu->r[CE_REG_LR] & ~1u;
        if (cpu->r[CE_REG_LR] & 1u) cpu->cpsr |= CE_CPSR_T;
        else cpu->cpsr &= ~CE_CPSR_T;
        CEStatus direct = cpu->trap_handler ? cpu->trap_handler(cpu->trap_context, cpu, trap)
                                           : CE_ERROR_UNSUPPORTED;
        cpu->instruction_count++;
        if (direct != CE_OK && direct != CE_ERROR_UNSUPPORTED) {
            cpu->fault = direct; cpu->halted = true;
        }
        return direct;
    }
    CEStatus s = (cpu->cpsr & CE_CPSR_T) ? step_thumb(cpu) : step_arm(cpu);
    cpu->instruction_count++;
    if (s != CE_OK) { cpu->fault = s; cpu->halted = true; }
    return s;
}
CEStatus CECPURun(CECPU *cpu, uint64_t instruction_budget) {
    if (!cpu || !instruction_budget) return CE_ERROR_INVALID_ARGUMENT;
    for (uint64_t i = 0; i < instruction_budget && !cpu->halted; ++i) {
        CEStatus s = CECPUStep(cpu); if (s != CE_OK) return s;
    }
    return cpu->halted ? cpu->fault : CE_OK;
}
