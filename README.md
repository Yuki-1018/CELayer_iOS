# CELayer for iOS

CELayer is a clean-room, Wine-style Windows CE application compatibility-layer project for iOS. It
runs each guest application in a private prefix instead of reproducing the Pocket PC shell. The
portable C execution core is hosted by a SwiftUI/Metal app; GitHub Actions builds the iOS target
because the local Linux environment does not include Swift or Xcode.

## Included in this milestone

- Defensive ARM PE32 parser, section mapper, `HIGHLOW` relocation and import resolver
- Checked 32-bit virtual memory and an ARM/Thumb interpreter foundation
- WinCE native API-set trap decoding, private host API traps, UserKData/TLS, typed handles,
  sandboxed file/directory I/O, clock, timers, events and memory/string calls
- Companion-DLL mapping, export lookup, DLL entry-point startup and PE string-resource lookup
- Registered window classes, guest WndProc dispatch, message queue, BGRA GDI and Metal presentation
- Persistent HKCU/HKLM registry, wildcard file enumeration and basic AYGShell/common-control startup
- Hit-tested child controls, bitmap text, lines, pens, rectangles and basic paint APIs
- Folder/package import that keeps EXE, DLL, images and data together
- Full-canvas runner with a floating menu, software game keys, text input, touch and hardware keys
- Strict portable-core tests and a manually triggered unsigned-device IPA workflow

## Portable core

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Unsigned IPA

Run the **Build unsigned IPA** workflow manually from GitHub Actions. It performs strict core tests,
builds a Release `iphoneos` app with all code-signing disabled, packages `Payload/CELayer.app`, and
uploads `CELayer-unsigned.ipa` as the `CELayer-unsigned-ipa` artifact for 30 days.

The IPA intentionally has no signature. iOS will not install it through normal App Store or ad-hoc
installation paths until it is signed by the installer or deployment environment.

For a local macOS build with Xcode and [XcodeGen](https://github.com/yonaskolb/XcodeGen):

```sh
xcodegen generate
xcodebuild -project CELayer.xcodeproj -scheme CELayer -configuration Release \
  -sdk iphoneos -destination 'generic/platform=iOS' \
  CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO build
```

Import the complete folder of a legally obtained ARM Windows CE application. The launcher preserves
the directory layout so relative resources and companion DLLs remain beside the EXE. For providers
that cannot select a folder, select the EXE and its related files together. Guest packages live in
`Documents/PocketPC/Applications` and are visible through iOS file sharing.

## Compatibility

This is an executable foundation, not yet a claim of complete Windows CE binary compatibility.
The exact current and pending surface is in [Docs/COMPATIBILITY.md](Docs/COMPATIBILITY.md), with
component and safety details in [Docs/ARCHITECTURE.md](Docs/ARCHITECTURE.md).

Microsoft binaries and ROM assets are not included. Windows and Windows CE are trademarks of
Microsoft Corporation; this project is not affiliated with Microsoft.
