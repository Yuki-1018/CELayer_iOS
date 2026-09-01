# Compatibility matrix

| Area | Current implementation | Status |
|---|---|---|
| PE32 | ARM/Thumb validation, sections, IAT, name/ordinal imports, HIGHLOW relocation | Foundation |
| ARM | conditions, expanded ALU, multiply, branch/BL/BX, byte/word/halfword/signed and multiple transfers, SWI/API trap | Partial |
| Thumb | shifts/ALU/multiply, branch/BL/BX, byte/half/word load-store, PUSH/POP, SWI | Partial |
| Memory | checked 32-bit regions, protection, stack and allocation | Foundation |
| Kernel | typed file/event/brush handles, time, memory/string calls, sandboxed file and directory operations | Partial |
| Windowing | class registration, child coordinates, hit testing/capture, WM_COMMAND, paint/input queue and guest WndProc dispatch | Partial |
| GDI | DC/brushes, FillRect/SetPixel, ASCII bitmap TextOut/DrawText, built-in control placeholders and Metal | Partial |
| File app | import, list, launch, share, delete and Documents sharing | Implemented |
| Registry | HKCU/HKLM key handles, create/open/set/query/delete-value and sandbox persistence | Partial |
| Threads/processes | one emulated main thread | Planned |
| Shell/controls/dialogs | common AYGShell initialization, BUTTON/EDIT/STATIC basics and Common Controls init | Partial |
| Winsock/audio/COM/OLE | no host adapter yet | Planned |

“Foundation” and “Partial” do not imply application-level compatibility. The runtime is structured
for instructions and APIs to be added under regression tests, but is not a complete Pocket PC 2003
implementation yet.
