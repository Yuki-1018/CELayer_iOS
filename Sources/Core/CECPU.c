#include "CELayer/CECPU.h"
#include "CELayer/CEAPI.h"

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
    if ((x & 0x0fc000f0u) == 0x00000090u) { /* MUL/MLA */
        unsigned rd = (x >> 16) & 0xfu, rn = (x >> 12) & 0xfu;
        uint32_t result = cpu->r[x & 0xfu] * cpu->r[(x >> 8) & 0xfu];
        if (x & (1u << 21)) result += cpu->r[rn];
        cpu->r[rd] = result; if (x & (1u << 20)) set_nz(cpu, result); return CE_OK;
    }
    if ((x & 0x0f000000u) == 0x0f000000u) {
        return cpu->trap_handler ? cpu->trap_handler(cpu->trap_context, cpu,
            CE_TRAP_SOFTWARE_INTERRUPT | (x & 0x00ffffffu))
                                 : CE_ERROR_UNSUPPORTED;
    }
    if ((x & 0x0e000000u) == 0x0a000000u) { /* B/BL */
        if (x & (1u << 24)) cpu->r[CE_REG_LR] = pc + 4;
        cpu->r[CE_REG_PC] = pc + 8 + (uint32_t)(sign_extend(x & 0xffffffu, 24) << 2);
        return CE_OK;
    }
    if ((x & 0x0e000090u) == 0x00000090u) { /* halfword and signed transfer */
        bool pre = x & (1u << 24), up = x & (1u << 23), immediate = x & (1u << 22);
        bool writeback = x & (1u << 21), load = x & (1u << 20);
        unsigned rn = (x >> 16) & 0xfu, rd = (x >> 12) & 0xfu, kind = (x >> 5) & 3u;
        uint32_t offset = immediate ? (((x >> 8) & 0xfu) << 4) | (x & 0xfu) : cpu->r[x & 0xfu];
        uint32_t base = cpu->r[rn], adjusted = up ? base + offset : base - offset;
        CEAddress address = pre ? adjusted : base;
        if (!load) {
            uint16_t value = (uint16_t)cpu->r[rd];
            if (kind != 1) return CE_ERROR_UNSUPPORTED;
            s = CEVirtualMemoryWrite(cpu->memory, address, &value, 2);
        } else if (kind == 1) { uint16_t value; s = CEVirtualMemoryReadU16(cpu->memory, address, &value); cpu->r[rd] = value; }
        else if (kind == 2) { uint8_t value; s = CEVirtualMemoryReadU8(cpu->memory, address, &value); cpu->r[rd] = (uint32_t)(int32_t)(int8_t)value; }
        else { uint16_t value; s = CEVirtualMemoryReadU16(cpu->memory, address, &value); cpu->r[rd] = (uint32_t)(int32_t)(int16_t)value; }
        if (!pre || writeback) cpu->r[rn] = adjusted;
        return s;
    }
    if ((x & 0x0e000000u) == 0x08000000u) { /* LDM/STM */
        bool pre = x & (1u << 24), up = x & (1u << 23), writeback = x & (1u << 21), load = x & (1u << 20);
        unsigned rn = (x >> 16) & 0xfu; uint16_t list = (uint16_t)x; unsigned count = 0;
        for (unsigned i = 0; i < 16; ++i) if (list & (1u << i)) ++count;
        if (!count) return CE_ERROR_UNSUPPORTED;
        uint32_t base = cpu->r[rn]; CEAddress address;
        if (up) address = base + (pre ? 4u : 0u);
        else address = base - count * 4u + (pre ? 0u : 4u);
        for (unsigned i = 0; i < 16; ++i) if (list & (1u << i)) {
            if (load) s = CEVirtualMemoryReadU32(cpu->memory, address, &cpu->r[i]);
            else s = CEVirtualMemoryWriteU32(cpu->memory, address, cpu->r[i]);
            if (s != CE_OK) return s;
            address += 4;
        }
        if (writeback) cpu->r[rn] = up ? base + count * 4u : base - count * 4u;
        if (load && (list & (1u << CE_REG_PC))) { uint32_t target = cpu->r[CE_REG_PC]; cpu->r[CE_REG_PC] = target & ~1u; if (target & 1u) cpu->cpsr |= CE_CPSR_T; }
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
          case 2: result = a - b; break; case 3: result = b - a; break; case 4: result = a + b; break;
          case 5: result = a + b + ((cpu->cpsr & CE_CPSR_C) ? 1u : 0u); break;
          case 6: result = a - b - ((cpu->cpsr & CE_CPSR_C) ? 0u : 1u); break;
          case 7: result = b - a - ((cpu->cpsr & CE_CPSR_C) ? 0u : 1u); break;
          case 8: result = a & b; write = false; break; case 9: result = a ^ b; write = false; break;
          case 10: result = a - b; write = false; break; case 11: result = a + b; write = false; break;
          case 12: result = a | b; break; case 13: result = b; break; case 14: result = a & ~b; break;
          case 15: result = ~b; break; default: return CE_ERROR_UNSUPPORTED; }
        if ((x & (1u << 20)) || !write) {
            if (opcode == 2 || opcode == 6 || opcode == 10) set_sub_flags(cpu, a, b, result);
            else if (opcode == 3 || opcode == 7) set_sub_flags(cpu, b, a, result);
            else if (opcode == 4 || opcode == 5 || opcode == 11) set_add_flags(cpu, a, b, result);
            else set_nz(cpu, result);
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
        cpu->trap_handler(cpu->trap_context, cpu, CE_TRAP_SOFTWARE_INTERRUPT | (x & 0xffu))
        : CE_ERROR_UNSUPPORTED;
    if ((x & 0xff87u) == 0x4700u) { uint32_t target = cpu->r[(x >> 3) & 0xfu];
        cpu->r[CE_REG_PC] = target & ~1u; if (target & 1u) cpu->cpsr |= CE_CPSR_T;
        else cpu->cpsr &= ~CE_CPSR_T;
        return CE_OK; }
    if ((x & 0xe000u) == 0x0000u && (x & 0x1800u) != 0x1800u) { /* immediate shifts */
        unsigned kind = (x >> 11) & 3u, amount = (x >> 6) & 0x1fu, rs = (x >> 3) & 7u, rd = x & 7u;
        uint32_t value = cpu->r[rs];
        if (kind == 0) cpu->r[rd] = value << amount;
        else if (kind == 1) cpu->r[rd] = amount ? value >> amount : 0;
        else cpu->r[rd] = amount ? (uint32_t)((int32_t)value >> amount) : (value & 0x80000000u ? UINT32_MAX : 0);
        set_nz(cpu, cpu->r[rd]); return CE_OK;
    }
    if ((x & 0xf800u) == 0x1800u) { /* add/subtract register or immediate */
        unsigned rd = x & 7u, rs = (x >> 3) & 7u; uint32_t a = cpu->r[rs];
        uint32_t b = (x & 0x0400u) ? ((x >> 6) & 7u) : cpu->r[(x >> 6) & 7u];
        if (x & 0x0200u) { cpu->r[rd] = a - b; set_sub_flags(cpu, a, b, cpu->r[rd]); }
        else { cpu->r[rd] = a + b; set_add_flags(cpu, a, b, cpu->r[rd]); }
        return CE_OK;
    }
    if ((x & 0xf800u) == 0x2000u) { unsigned rd = (x >> 8) & 7u; cpu->r[rd] = x & 0xffu; set_nz(cpu, cpu->r[rd]); return CE_OK; }
    if ((x & 0xf800u) == 0x2800u) { unsigned rn = (x >> 8) & 7u; uint32_t b = x & 0xffu;
        set_sub_flags(cpu, cpu->r[rn], b, cpu->r[rn] - b); return CE_OK; }
    if ((x & 0xf800u) == 0x3000u) { unsigned rd = (x >> 8) & 7u; uint32_t a = cpu->r[rd], b = x & 0xffu;
        cpu->r[rd] = a + b; set_add_flags(cpu, a, b, cpu->r[rd]); return CE_OK; }
    if ((x & 0xf800u) == 0x3800u) { unsigned rd = (x >> 8) & 7u; uint32_t a = cpu->r[rd], b = x & 0xffu;
        cpu->r[rd] = a - b; set_sub_flags(cpu, a, b, cpu->r[rd]); return CE_OK; }
    if ((x & 0xfc00u) == 0x4000u) { /* ALU */
        unsigned op = (x >> 6) & 0xfu, rd = x & 7u, rs = (x >> 3) & 7u; uint32_t a = cpu->r[rd], b = cpu->r[rs], result = 0; bool write = true;
        switch (op) { case 0: result = a & b; break; case 1: result = a ^ b; break;
          case 2: result = a << (b & 0xffu); break; case 3: result = b >= 32 ? 0 : a >> b; break;
          case 4: result = b >= 32 ? (a & 0x80000000u ? UINT32_MAX : 0) : (uint32_t)((int32_t)a >> b); break;
          case 5: result = a + b + ((cpu->cpsr & CE_CPSR_C) ? 1u : 0u); break;
          case 6: result = a - b - ((cpu->cpsr & CE_CPSR_C) ? 0u : 1u); break;
          case 7: result = ror32(a, b & 0xffu); break; case 8: result = a & b; write = false; break;
          case 9: result = 0u - b; break; case 10: result = a - b; write = false; break;
          case 11: result = a + b; write = false; break; case 12: result = a | b; break;
          case 13: result = a * b; break; case 14: result = a & ~b; break; default: result = ~b; break; }
        if (op == 6 || op == 9 || op == 10) set_sub_flags(cpu, op == 9 ? 0u : a, b, result);
        else if (op == 5 || op == 11) set_add_flags(cpu, a, b, result); else set_nz(cpu, result);
        if (write) cpu->r[rd] = result;
        return CE_OK;
    }
    if ((x & 0xfc00u) == 0x4400u) { /* high register operations */
        unsigned op = (x >> 8) & 3u, rd = (x & 7u) | ((x >> 4) & 8u), rs = (x >> 3) & 0xfu;
        if (op == 0) cpu->r[rd] += cpu->r[rs];
        else if (op == 1) set_sub_flags(cpu, cpu->r[rd], cpu->r[rs], cpu->r[rd] - cpu->r[rs]);
        else if (op == 2) cpu->r[rd] = cpu->r[rs];
        else { uint32_t target = cpu->r[rs]; cpu->r[CE_REG_PC] = target & ~1u; if (target & 1u) cpu->cpsr |= CE_CPSR_T; else cpu->cpsr &= ~CE_CPSR_T; }
        return CE_OK;
    }
    if ((x & 0xf800u) == 0x4800u) { unsigned rd = (x >> 8) & 7u;
        return CEVirtualMemoryReadU32(cpu->memory, ((pc + 4) & ~3u) + ((x & 0xffu) << 2), &cpu->r[rd]); }
    if ((x & 0xf000u) == 0x6000u) { bool load = x & 0x0800u; unsigned imm = ((x >> 6) & 0x1fu) << 2;
        unsigned rn = (x >> 3) & 7u, rd = x & 7u; CEAddress a = cpu->r[rn] + imm;
        return load ? CEVirtualMemoryReadU32(cpu->memory, a, &cpu->r[rd]) : CEVirtualMemoryWriteU32(cpu->memory, a, cpu->r[rd]); }
    if ((x & 0xf000u) == 0x7000u) { bool load = x & 0x0800u; unsigned imm = (x >> 6) & 0x1fu, rn = (x >> 3) & 7u, rd = x & 7u; CEAddress a = cpu->r[rn] + imm;
        if (load) { uint8_t value; s = CEVirtualMemoryReadU8(cpu->memory, a, &value); cpu->r[rd] = value; return s; }
        uint8_t value = (uint8_t)cpu->r[rd]; return CEVirtualMemoryWrite(cpu->memory, a, &value, 1); }
    if ((x & 0xf000u) == 0x8000u) { bool load = x & 0x0800u; unsigned imm = ((x >> 6) & 0x1fu) << 1, rn = (x >> 3) & 7u, rd = x & 7u; CEAddress a = cpu->r[rn] + imm;
        if (load) { uint16_t value; s = CEVirtualMemoryReadU16(cpu->memory, a, &value); cpu->r[rd] = value; return s; }
        uint16_t value = (uint16_t)cpu->r[rd]; return CEVirtualMemoryWrite(cpu->memory, a, &value, 2); }
    if ((x & 0xf000u) == 0x9000u) { bool load = x & 0x0800u; unsigned rd = (x >> 8) & 7u; CEAddress a = cpu->r[CE_REG_SP] + ((x & 0xffu) << 2);
        return load ? CEVirtualMemoryReadU32(cpu->memory, a, &cpu->r[rd]) : CEVirtualMemoryWriteU32(cpu->memory, a, cpu->r[rd]); }
    if ((x & 0xf000u) == 0xa000u) { unsigned rd = (x >> 8) & 7u; uint32_t base = (x & 0x0800u) ? cpu->r[CE_REG_SP] : ((pc + 4) & ~3u); cpu->r[rd] = base + ((x & 0xffu) << 2); return CE_OK; }
    if ((x & 0xff00u) == 0xb000u) { uint32_t amount = (x & 0x7fu) << 2; cpu->r[CE_REG_SP] = (x & 0x80u) ? cpu->r[CE_REG_SP] - amount : cpu->r[CE_REG_SP] + amount; return CE_OK; }
    if ((x & 0xfe00u) == 0xb400u) { bool load = x & 0x0800u; uint8_t list = x & 0xffu;
        if (!load) { if (x & 0x0100u) { s = push(cpu, cpu->r[CE_REG_LR]); if (s != CE_OK) return s; }
            for (int i = 7; i >= 0; --i) if (list & (1u << i)) { s = push(cpu, cpu->r[i]); if (s != CE_OK) return s; } }
        else { for (unsigned i = 0; i < 8; ++i) if (list & (1u << i)) { s = pop(cpu, &cpu->r[i]); if (s != CE_OK) return s; }
            if (x & 0x0100u) { uint32_t target; s = pop(cpu, &target); if (s != CE_OK) return s;
                cpu->r[CE_REG_PC] = target & ~1u; if (!(target & 1u)) cpu->cpsr &= ~CE_CPSR_T; } }
        return CE_OK;
    }
    if ((x & 0xf000u) == 0xc000u) { /* LDMIA/STMIA */
        bool load = x & 0x0800u; unsigned rb = (x >> 8) & 7u; uint8_t list = (uint8_t)x;
        CEAddress address = cpu->r[rb];
        for (unsigned i = 0; i < 8; ++i) if (list & (1u << i)) {
            if (load) s = CEVirtualMemoryReadU32(cpu->memory, address, &cpu->r[i]);
            else s = CEVirtualMemoryWriteU32(cpu->memory, address, cpu->r[i]);
            if (s != CE_OK) return s;
            address += 4;
        }
        cpu->r[rb] = address; return CE_OK;
    }
    if ((x & 0xf000u) == 0xd000u && (x & 0x0f00u) != 0x0f00u) {
        unsigned cond = (x >> 8) & 0xfu; if (condition(cpu, cond)) cpu->r[CE_REG_PC] = pc + 4 + (sign_extend(x & 0xffu, 8) << 1); return CE_OK;
    }
    if ((x & 0xf800u) == 0xe000u) { cpu->r[CE_REG_PC] = pc + 4 + (sign_extend(x & 0x7ffu, 11) << 1); return CE_OK; }
    if ((x & 0xf800u) == 0xf000u) { cpu->r[CE_REG_LR] = pc + 4 + (sign_extend(x & 0x7ffu, 11) << 12); return CE_OK; }
    if ((x & 0xf800u) == 0xf800u) { uint32_t target = cpu->r[CE_REG_LR] + ((x & 0x7ffu) << 1); cpu->r[CE_REG_LR] = (pc + 2) | 1u; cpu->r[CE_REG_PC] = target; return CE_OK; }
    return CE_ERROR_UNSUPPORTED;
}

CEStatus CECPUStep(CECPU *cpu) {
    if (!cpu || !cpu->memory) return CE_ERROR_INVALID_ARGUMENT;
    if (cpu->halted) return CE_ERROR_HALTED;
    uint32_t direct_pc = cpu->r[CE_REG_PC];
    if ((direct_pc & 0xff000000u) == 0xee000000u ||
        (direct_pc >= 0xf0008000u && direct_pc < 0xf0010000u)) {
        uint32_t trap;
        if ((direct_pc & 0xff000000u) == 0xee000000u)
            trap = (direct_pc & 0x00ffffffu) >> 2;
        else {
            uint32_t index = (0xf0010000u - direct_pc) >> 2;
            trap = CE_TRAP_NATIVE | ((index >> 8) << 16) | (index & 0xffu);
        }
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
