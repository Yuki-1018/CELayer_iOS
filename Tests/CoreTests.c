#include "CELayer/CELayer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); abort(); \
} } while (0)

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }

static void test_memory(void) {
    CEVirtualMemory vm; CEVirtualMemoryInit(&vm); CEAddress a;
    CHECK(CEVirtualMemoryAllocate(&vm, 100, CE_PROT_READ | CE_PROT_WRITE, "test", &a) == CE_OK);
    CHECK((a & 4095u) == 0); CHECK(CEVirtualMemoryWriteU32(&vm, a + 8, 0x78563412u) == CE_OK);
    uint32_t value = 0; CHECK(CEVirtualMemoryReadU32(&vm, a + 8, &value) == CE_OK); CHECK(value == 0x78563412u);
    CHECK(CEVirtualMemoryProtect(&vm, a, 100, CE_PROT_READ) == CE_OK);
    CHECK(CEVirtualMemoryWriteU32(&vm, a, 1) == CE_ERROR_ACCESS_VIOLATION);
    CHECK(CEVirtualMemoryFree(&vm, a) == CE_OK);
    CHECK(CEVirtualMemoryReadU32(&vm, a, &value) == CE_ERROR_ACCESS_VIOLATION);
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

static void test_arm(void) {
    CEVirtualMemory vm; CEVirtualMemoryInit(&vm);
    CHECK(CEVirtualMemoryMap(&vm, 0x1000, 0x1000, CE_PROT_READ | CE_PROT_WRITE | CE_PROT_EXEC, "code") == CE_OK);
    uint32_t code[] = {0xe3a00005, 0xe2801007, 0xe2412002};
    CHECK(CEVirtualMemoryWrite(&vm, 0x1000, code, sizeof(code)) == CE_OK);
    CECPU cpu; CECPUInit(&cpu, &vm); CECPUSetEntry(&cpu, 0x1000, 0x2000, false);
    CHECK(CECPURun(&cpu, 3) == CE_OK); CHECK(cpu.r[0] == 5); CHECK(cpu.r[1] == 12); CHECK(cpu.r[2] == 10);
    CEVirtualMemoryDestroy(&vm);
}

static void test_thumb(void) {
    CEVirtualMemory vm; CEVirtualMemoryInit(&vm);
    CHECK(CEVirtualMemoryMap(&vm, 0x2000, 0x1000, CE_PROT_READ | CE_PROT_WRITE | CE_PROT_EXEC, "thumb") == CE_OK);
    uint16_t code[] = {0x2009, 0x3003, 0x3802};
    CHECK(CEVirtualMemoryWrite(&vm, 0x2000, code, sizeof(code)) == CE_OK);
    CECPU cpu; CECPUInit(&cpu, &vm); CECPUSetEntry(&cpu, 0x2001, 0x3000, true);
    CHECK(CECPURun(&cpu, 3) == CE_OK); CHECK(cpu.r[0] == 10);
    CEVirtualMemoryDestroy(&vm);
}

static void test_windows(void) {
    CEWindowServer ws; CHECK(CEWindowServerInit(&ws, 240, 320) == CE_OK);
    CEHandle w = CEWindowCreate(&ws, 0, 0, 0, 240, 300, 0x10000000, 0x1234, NULL); CHECK(w != 0);
    CEMessage in = {w, 0x100, 13, 0, 1}, out; CHECK(CEPostMessage(&ws, in) == CE_OK);
    CHECK(CEGetMessage(&ws, &out)); CHECK(out.wparam == 13); CHECK(CEGDIFillRect(&ws, 1, 1, 3, 3, 0xff112233) == CE_OK);
    CHECK(ws.framebuffer[241] == 0xff112233); CHECK(CEWindowDestroy(&ws, w) == CE_OK); CEWindowServerDestroy(&ws);
}

int main(void) {
    test_memory(); test_pe(); test_arm(); test_thumb(); test_windows();
    puts("CELayer core tests passed"); return 0;
}
