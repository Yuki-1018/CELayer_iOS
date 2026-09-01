# Compatibility matrix

| Area | Current implementation | Status |
|---|---|---|
| PE32 | ARM/Thumb validation, sections, IAT, name/ordinal imports, HIGHLOW relocation | Foundation |
| ARM | conditions, ALU subset, branch/BL/BX, basic LDR/STR, SWI/API trap | Partial |
| Thumb | immediate ALU, branch, BX, literal/word load-store, PUSH/POP, SWI | Partial |
| Memory | checked 32-bit regions, protection, stack and allocation | Foundation |
| Kernel | typed file/event handles, clock, allocation, basic file I/O | Partial |
| Windowing | window records, hierarchy fields, input state and message queue | Partial |
| GDI | 32-bit BGRA surface, fill, blit and Metal presentation | Partial |
| File app | import, list, launch, share, delete and Documents sharing | Implemented |
| Registry | storage model defined; persistence and API dispatch pending | Planned |
| Threads/processes | one emulated main thread | Planned |
| Shell/controls/dialogs | imports recognized, calls diagnosed unsupported | Planned |
| Winsock/audio/COM/OLE | no host adapter yet | Planned |

“Foundation” and “Partial” do not imply application-level compatibility. The runtime is structured
for instructions and APIs to be added under regression tests, but is not a complete Pocket PC 2003
implementation yet.
