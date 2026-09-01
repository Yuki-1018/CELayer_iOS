# CELayer architecture

CELayer is a clean-room, user-mode compatibility runtime. It contains no Microsoft ROM files,
headers, binaries, fonts, or copyrighted assets. Applications and redistributable DLLs must be
supplied by the user under their applicable licences.

## Execution path

1. `CEPELoader` validates an ARM/Thumb PE32 image, maps sections, applies `HIGHLOW` relocations,
   walks imports, and writes API trap addresses into the IAT.
2. `CECPU` executes guest ARMv4T/Thumb instructions from `CEVirtualMemory`. Calls into the reserved
   `0xF...` range transfer to `CEKernel` and return through the emulated link register.
3. `CEKernel` translates supported calls into sandboxed host operations. Handles never expose host
   pointers to guest code.
4. `CEWindowServer` owns window objects, input state, a message FIFO, and a BGRA framebuffer. Swift
   uploads the framebuffer to a Metal texture.
5. `CERuntime` owns the complete lifetime and is the C interface used by SwiftUI.

## Safety and sandboxing

- Guest addresses are 32-bit integers and are never cast to host pointers.
- Regions are page-aligned and protection/bounds checked on every interpreter access.
- The guest filesystem is rooted in `Documents/PocketPC`; separators are normalized and `..`
  components are rejected.
- Unsupported APIs return an observable error and diagnostic instead of fabricated success.

## Work required for broad Pocket PC 2003 compatibility

- Complete ARMv4/v5TE and Thumb-1, exception/alignment behavior, and a deterministic scheduler.
- Delay imports, TLS, forwarded exports, DLL graphs, resources, and CE-specific module imports.
- Full coredll/GWES/GDI, common controls, shell, Winsock, multimedia, COM/OLE, notifications,
  databases, and device shims.
- DIB/palette/ROP support, text and fonts, regions, menus, dialogs, controls, IME/SIP and audio.
- Persistent registry hives and conformance tests against legal reference software and devices.

These are requirements, not claims about the current milestone.
