#include "CELayer/CELayer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); abort(); \
} } while (0)

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }
typedef struct TrapCapture { uint32_t number; } TrapCapture;
static CEStatus capture_trap(void *context, CECPU *cpu, uint32_t trap) {
    (void)cpu; ((TrapCapture *)context)->number = trap; return CE_OK;
}

static void test_memory(void) {
    CEVirtualMemory vm; CEVirtualMemoryInit(&vm); CEAddress a;
    CHECK(CEVirtualMemoryAllocate(&vm, 100, CE_PROT_READ | CE_PROT_WRITE, "test", &a) == CE_OK);
    CHECK((a & 4095u) == 0); CHECK(CEVirtualMemoryWriteU32(&vm, a + 8, 0x78563412u) == CE_OK);
    uint32_t value = 0; CHECK(CEVirtualMemoryReadU32(&vm, a + 8, &value) == CE_OK); CHECK(value == 0x78563412u);
    CHECK(CEVirtualMemoryProtect(&vm, a, 100, CE_PROT_READ) == CE_OK);
    CHECK(CEVirtualMemoryWriteU32(&vm, a, 1) == CE_ERROR_ACCESS_VIOLATION);
    CHECK(CEVirtualMemoryFree(&vm, a) == CE_OK);
    CHECK(CEVirtualMemoryReadU32(&vm, a, &value) == CE_ERROR_ACCESS_VIOLATION);
    CHECK(CEVirtualMemoryMap(&vm, 0xfffff000u, 4096, CE_PROT_READ | CE_PROT_WRITE, "top page") == CE_OK);
    CHECK(CEVirtualMemoryWriteU32(&vm, 0xfffffffcu, 0x12345678u) == CE_OK);
    CEVirtualMemoryDestroy(&vm);
}

static void test_pe(void) {
    uint8_t pe[1024]; memset(pe, 0, sizeof(pe)); put16(pe, 0x5a4d); put32(pe + 0x3c, 0x80);
    put32(pe + 0x80, 0x00004550); put16(pe + 0x84, CE_PE_MACHINE_ARM); put16(pe + 0x86, 1);
    put16(pe + 0x94, 224); uint8_t *o = pe + 0x98; put16(o, 0x10b); put32(o + 16, 0x1000);
    put32(o + 28, 0x00400000); put32(o + 56, 0x2000); put32(o + 60, 0x200); put16(o + 68, 9); put32(o + 92, 16);
    uint8_t *s = o + 224; memcpy(s, ".text", 5); put32(s + 8, 4); put32(s + 12, 0x1000);
    put32(s + 16, 4); put32(s + 20, 0x200); put32(s + 36, 0x60000020); put32(pe + 0x200, 0xe3a0002a);
    CEPEImage image; CHECK(CEPEParse(pe, sizeof(pe), &image) == CE_OK); CHECK(image.entry_rva == 0x1000);
    CEVirtualMemory vm; CEVirtualMemoryInit(&vm);
    CHECK(CEPEMap(pe, sizeof(pe), &image, &vm, NULL, NULL) == CE_OK);
    uint32_t insn; CHECK(CEVirtualMemoryReadU32(&vm, 0x00401000, &insn) == CE_OK); CHECK(insn == 0xe3a0002a);
    CEVirtualMemoryDestroy(&vm); pe[0] = 0; CHECK(CEPEParse(pe, sizeof(pe), &image) == CE_ERROR_BAD_FORMAT);
}

static void test_pe_exports(void) {
    CEVirtualMemory vm; CEVirtualMemoryInit(&vm);
    CHECK(CEVirtualMemoryMap(&vm, 0x5000, 0x1000, CE_PROT_READ | CE_PROT_WRITE, "exports") == CE_OK);
    CEPEImage image; memset(&image, 0, sizeof(image)); image.mapped_base = 0x5000;
    image.directories[0].rva = 0x100; image.directories[0].size = 0x100;
    CHECK(CEVirtualMemoryWriteU32(&vm, 0x5110, 1) == CE_OK);
    CHECK(CEVirtualMemoryWriteU32(&vm, 0x5114, 1) == CE_OK);
    CHECK(CEVirtualMemoryWriteU32(&vm, 0x5118, 1) == CE_OK);
    CHECK(CEVirtualMemoryWriteU32(&vm, 0x511c, 0x180) == CE_OK);
    CHECK(CEVirtualMemoryWriteU32(&vm, 0x5120, 0x184) == CE_OK);
    CHECK(CEVirtualMemoryWriteU32(&vm, 0x5124, 0x188) == CE_OK);
    CHECK(CEVirtualMemoryWriteU32(&vm, 0x5180, 0x300) == CE_OK);
    CHECK(CEVirtualMemoryWriteU32(&vm, 0x5184, 0x190) == CE_OK);
    uint16_t name_ordinal = 0; CHECK(CEVirtualMemoryWrite(&vm, 0x5188, &name_ordinal, 2) == CE_OK);
    const char name[] = "Foo"; CHECK(CEVirtualMemoryWrite(&vm, 0x5190, name, sizeof(name)) == CE_OK);
    CEAddress address = 0; CHECK(CEPELookupExport(&image, &vm, "Foo", 0, &address) == CE_OK);
    CHECK(address == 0x5300); CHECK(CEPELookupExport(&image, &vm, NULL, 1, &address) == CE_OK);
    CEVirtualMemoryDestroy(&vm);
}

static void test_arm(void) {
    CEVirtualMemory vm; CEVirtualMemoryInit(&vm);
    CHECK(CEVirtualMemoryMap(&vm, 0x1000, 0x1000, CE_PROT_READ | CE_PROT_WRITE | CE_PROT_EXEC, "code") == CE_OK);
    uint32_t code[] = {0xe3a00005, 0xe2801007, 0xe2412002, 0xe0030190};
    CHECK(CEVirtualMemoryWrite(&vm, 0x1000, code, sizeof(code)) == CE_OK);
    CECPU cpu; CECPUInit(&cpu, &vm); CECPUSetEntry(&cpu, 0x1000, 0x2000, false);
    CHECK(CECPURun(&cpu, 4) == CE_OK); CHECK(cpu.r[0] == 5); CHECK(cpu.r[1] == 12);
    CHECK(cpu.r[2] == 10); CHECK(cpu.r[3] == 60);
    CEVirtualMemoryDestroy(&vm);
}

static void test_thumb(void) {
    CEVirtualMemory vm; CEVirtualMemoryInit(&vm);
    CHECK(CEVirtualMemoryMap(&vm, 0x2000, 0x1000, CE_PROT_READ | CE_PROT_WRITE | CE_PROT_EXEC, "thumb") == CE_OK);
    uint16_t code[] = {0x2009, 0x3003, 0x3802, 0x2103, 0x4348};
    CHECK(CEVirtualMemoryWrite(&vm, 0x2000, code, sizeof(code)) == CE_OK);
    CECPU cpu; CECPUInit(&cpu, &vm); CECPUSetEntry(&cpu, 0x2001, 0x3000, true);
    CHECK(CECPURun(&cpu, 5) == CE_OK); CHECK(cpu.r[0] == 30); CHECK(cpu.r[1] == 3);
    CEVirtualMemoryDestroy(&vm);
}

static void test_api_trap(void) {
    CEVirtualMemory vm; CEVirtualMemoryInit(&vm); CECPU cpu; CECPUInit(&cpu, &vm); TrapCapture capture = {0};
    cpu.trap_handler = capture_trap; cpu.trap_context = &capture;
    cpu.r[CE_REG_PC] = CE_API_TRAP_ADDRESS(CE_MODULE_COREDLL, CE_API_FILL_RECT);
    cpu.r[CE_REG_LR] = 0x1001; CHECK(CECPUStep(&cpu) == CE_OK);
    CHECK(capture.number == ((CE_MODULE_COREDLL << 16) | CE_API_FILL_RECT));
    CHECK(cpu.r[CE_REG_PC] == 0x1000); CHECK((cpu.cpsr & CE_CPSR_T) != 0);
    capture.number = 0; cpu.r[CE_REG_PC] = 0xf000f7f8u; cpu.r[CE_REG_LR] = 0x1000;
    CHECK(CECPUStep(&cpu) == CE_OK);
    CHECK(capture.number == (CE_TRAP_NATIVE | (2u << 16) | 2u));
    CEVirtualMemoryDestroy(&vm);
}

static void test_windows(void) {
    CEWindowServer ws; CHECK(CEWindowServerInit(&ws, 240, 320) == CE_OK);
    CEHandle w = CEWindowCreate(&ws, 0, 0, 0, 240, 300, 0x10000000, 0x1234, NULL); CHECK(w != 0);
    CEMessage in = {w, 0x100, 13, 0, 1}, out; CHECK(CEPostMessage(&ws, in) == CE_OK);
    CHECK(CEGetMessage(&ws, &out)); CHECK(out.wparam == 13); CHECK(CEGDIFillRect(&ws, 1, 1, 3, 3, 0xff112233) == CE_OK);
    CHECK(ws.framebuffer[241] == 0xff112233);
    uint16_t text[] = {'A','1'}; CHECK(CEGDIDrawTextUTF16(&ws, 10, 10, text, 2, 0xffffffff, 0xff000000, true) == CE_OK);
    CHECK(ws.framebuffer[10 * 240 + 11] == 0xffffffff);
    CHECK(CEWindowDestroy(&ws, w) == CE_OK); CEWindowServerDestroy(&ws);
}

static void test_kernel_gwes(void) {
    CEVirtualMemory vm; CEVirtualMemoryInit(&vm);
    CHECK(CEVirtualMemoryMap(&vm, 0x1000, 0x4000,
        CE_PROT_READ | CE_PROT_WRITE | CE_PROT_EXEC, "kernel test") == CE_OK);
    CEWindowServer ws; CHECK(CEWindowServerInit(&ws, 240, 320) == CE_OK);
    CEKernel kernel; CEKernelInit(&kernel, &vm, &ws, NULL);
    CECPU cpu; CECPUInit(&cpu, &vm); cpu.r[CE_REG_SP] = 0x4f00;
    uint32_t tls_pointer = 0; CHECK(CEVirtualMemoryReadU32(&vm, 0xffffc800u, &tls_pointer) == CE_OK);
    CHECK(tls_pointer == kernel.tls_address);
    cpu.r[0] = 0; cpu.r[1] = 0;
    CHECK(CEKernelDispatch(&kernel, &cpu, CE_MODULE_COREDLL, CE_API_TLS_CALL) == CE_OK);
    uint32_t tls_index = cpu.r[0]; cpu.r[0] = tls_index; cpu.r[1] = 0xabcdef01u;
    CHECK(CEKernelDispatch(&kernel, &cpu, CE_MODULE_COREDLL, CE_API_TLS_SET_VALUE) == CE_OK);
    cpu.r[0] = tls_index; CHECK(CEKernelDispatch(&kernel, &cpu, CE_MODULE_COREDLL, CE_API_TLS_GET_VALUE) == CE_OK);
    CHECK(cpu.r[0] == 0xabcdef01u);

    cpu.r[0] = 0x1400;
    CHECK(CEKernelDispatch(&kernel, &cpu, CE_MODULE_COREDLL, CE_API_GET_SYSTEM_INFO) == CE_OK);
    uint16_t architecture = 0; uint32_t page_size = 0;
    CHECK(CEVirtualMemoryReadU16(&vm, 0x1400, &architecture) == CE_OK);
    CHECK(CEVirtualMemoryReadU32(&vm, 0x1404, &page_size) == CE_OK);
    CHECK(architecture == 5 && page_size == 4096);

    cpu.r[0] = 0x000000ffu;
    CHECK(CEKernelDispatch(&kernel, &cpu, CE_MODULE_COREDLL, CE_API_CREATE_SOLID_BRUSH) == CE_OK);
    CEHandle brush = cpu.r[0]; int32_t rect[4] = {2, 3, 5, 7};
    CHECK(CEVirtualMemoryWrite(&vm, 0x1100, rect, sizeof(rect)) == CE_OK);
    cpu.r[0] = 1; cpu.r[1] = 0x1100; cpu.r[2] = brush;
    CHECK(CEKernelDispatch(&kernel, &cpu, CE_MODULE_COREDLL, CE_API_FILL_RECT) == CE_OK);
    CHECK(cpu.r[0] == 1); CHECK(ws.framebuffer[3 * 240 + 2] == 0xffff0000u);

    uint16_t class_name[] = {'T','e','s','t',0}; uint32_t wc[10] = {0};
    wc[1] = 0x2001; wc[9] = 0x1200;
    CHECK(CEVirtualMemoryWrite(&vm, 0x1200, class_name, sizeof(class_name)) == CE_OK);
    CHECK(CEVirtualMemoryWrite(&vm, 0x1300, wc, sizeof(wc)) == CE_OK);
    cpu.r[0] = 0x1300;
    CHECK(CEKernelDispatch(&kernel, &cpu, CE_MODULE_COREDLL, CE_API_REGISTER_CLASS_W) == CE_OK);
    CHECK(cpu.r[0] == 1);

    uint32_t args[8] = {0, 0, 100, 120, 0, 0, 0, 0};
    CHECK(CEVirtualMemoryWrite(&vm, cpu.r[CE_REG_SP], args, sizeof(args)) == CE_OK);
    cpu.r[0] = 0; cpu.r[1] = 0x1200; cpu.r[2] = 0; cpu.r[3] = 0x10000000;
    CHECK(CEKernelDispatch(&kernel, &cpu, CE_MODULE_COREDLL, CE_API_CREATE_WINDOW_EX_W) == CE_OK);
    CHECK(cpu.r[0] != 0); CHECK(ws.window_count == 1); CHECK(ws.windows[0].wndproc == 0x2001);

    cpu.r[0] = 0x1400;
    CHECK(CEKernelDispatch(&kernel, &cpu, CE_MODULE_COREDLL, CE_API_GET_MESSAGE_W) == CE_OK);
    CHECK(cpu.r[0] == 1); cpu.r[0] = 0x1400; cpu.r[CE_REG_PC] = 0x1800; cpu.cpsr |= CE_CPSR_T;
    CHECK(CEKernelDispatch(&kernel, &cpu, CE_MODULE_COREDLL, CE_API_DISPATCH_MESSAGE_W) == CE_OK);
    CHECK(cpu.r[CE_REG_PC] == 0x2000);
    CHECK(cpu.r[CE_REG_LR] == CE_API_TRAP_ADDRESS(CE_MODULE_INTERNAL, CE_INTERNAL_RETURN_WNDPROC));

    uint16_t reg_key[] = {'S','o','f','t','w','a','r','e','\\','C','E','L','a','y','e','r',0};
    uint16_t reg_name[] = {'E','n','a','b','l','e','d',0}; uint32_t reg_stack[5] = {0, 0, 0, 0x1700, 0};
    CHECK(CEVirtualMemoryWrite(&vm, 0x1500, reg_key, sizeof(reg_key)) == CE_OK);
    CHECK(CEVirtualMemoryWrite(&vm, 0x1600, reg_name, sizeof(reg_name)) == CE_OK);
    CHECK(CEVirtualMemoryWrite(&vm, cpu.r[CE_REG_SP], reg_stack, sizeof(reg_stack)) == CE_OK);
    cpu.r[0] = 0x80000001u; cpu.r[1] = 0x1500;
    CHECK(CEKernelDispatch(&kernel, &cpu, CE_MODULE_COREDLL, CE_API_REG_CREATE_KEY_EX_W) == CE_OK);
    CHECK(cpu.r[0] == 0); CEHandle reg_handle; CHECK(CEVirtualMemoryReadU32(&vm, 0x1700, &reg_handle) == CE_OK);
    uint32_t reg_data = 1; uint32_t set_stack[2] = {0x1800, 4};
    CHECK(CEVirtualMemoryWriteU32(&vm, 0x1800, reg_data) == CE_OK);
    CHECK(CEVirtualMemoryWrite(&vm, cpu.r[CE_REG_SP], set_stack, sizeof(set_stack)) == CE_OK);
    cpu.r[0] = reg_handle; cpu.r[1] = 0x1600; cpu.r[3] = 4;
    CHECK(CEKernelDispatch(&kernel, &cpu, CE_MODULE_COREDLL, CE_API_REG_SET_VALUE_EX_W) == CE_OK);
    CHECK(cpu.r[0] == 0); CHECK(kernel.registry_count == 1);

    CHECK(CEKernelCloseHandle(&kernel, brush) == CE_OK);
    CHECK(CEKernelCloseHandle(&kernel, reg_handle) == CE_OK);
    CEWindowServerDestroy(&ws); CEVirtualMemoryDestroy(&vm);
}

int main(void) {
    test_memory(); test_pe(); test_pe_exports(); test_arm(); test_thumb(); test_api_trap(); test_windows(); test_kernel_gwes();
    puts("CELayer core tests passed"); return 0;
}
