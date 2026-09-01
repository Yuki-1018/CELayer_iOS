# Compatibility matrix

| Area | Current implementation | Status |
|---|---|---|
| PE32 | ARM/Thumb validation, sections, IAT, name/ordinal imports, HIGHLOW relocation, exports and RT_STRING resources | Foundation |
| ARM | conditions, expanded ALU, multiply, branch/BL/BX, byte/word/halfword/signed and multiple transfers, SWI/API trap | Partial |
| Thumb | shifts/ALU/multiply, branch/BL/BX, byte/half/word load-store, PUSH/POP, SWI | Partial |
| Memory | checked 32-bit regions including the top page, protection, stack, heap and allocation | Foundation |
| CE ABI | native API-set call decoding, separate private traps, UserKData process/thread/TLS fields and system information | Partial |
| DLLs | case-insensitive companion lookup, recursive import/export binding and process-attach entry points | Partial |
| Kernel | typed file/event/GDI handles, TLS, time, timers, memory/string calls, sandboxed file and directory operations | Partial |
| Windowing | class registration, child coordinates, hit testing/capture, WM_COMMAND, paint/input queue and guest WndProc dispatch | Partial |
| GDI | DC/brush/pen selection, paint, fill, pixel, line/rectangle, bitmap text, control placeholders and Metal | Partial |
| App library | recursive folder/package import, list, launch, share, delete and Documents sharing | Implemented |
| iOS input | touch, floating game keys, WM_CHAR text field and hardware-key to VK translation | Implemented |
| Registry | HKCU/HKLM key handles, create/open/set/query/delete-value and sandbox persistence | Partial |
| Threads/processes | one emulated main thread | Planned |
| Shell/controls/dialogs | common AYGShell initialization, BUTTON/EDIT/STATIC basics and Common Controls init | Partial |
| Winsock/audio/COM/OLE | no host adapter yet | Planned |

“Foundation” and “Partial” do not imply application-level compatibility. The runtime is structured
for instructions, native API-set methods and DLL APIs to be added under regression tests. It does
not emulate an entire CE kernel and remains incomplete for applications that depend on threads,
winsock, audio, COM/OLE, DirectDraw, uncommon CPU instructions or undocumented OEM services.
