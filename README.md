# CELayer for iOS

CELayer is a clean-room Windows CE / Pocket PC compatibility-layer project for iOS. It contains a
portable C execution core and a SwiftUI/Metal host app. GitHub Actions builds the iOS target because
the local Linux environment does not require Swift or Xcode.

## Included in this milestone

- Defensive ARM PE32 parser, section mapper, `HIGHLOW` relocation and import resolver
- Checked 32-bit virtual memory and an ARM/Thumb interpreter foundation
- WinCE API traps, typed handles, sandboxed file/directory I/O, clock, events and memory/string calls
- Registered window classes, guest WndProc dispatch, message queue, BGRA GDI and Metal presentation
- Persistent HKCU/HKLM registry, wildcard file enumeration and basic AYGShell/common-control startup
- Built-in Pocket PC shell surface, hit-tested child controls and bitmap `TextOutW`/`DrawTextW`
- iOS launcher with import, launch, share, delete, diagnostics, touch and key input
- Strict portable-core tests and an iOS Simulator workflow

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

Import a legally obtained ARM Pocket PC executable in the launcher. Guest files live in the app's
`Documents/PocketPC` directory and are visible through iOS file sharing.

## Compatibility

This is an executable foundation, not yet a claim of complete Pocket PC 2003 binary compatibility.
The exact current and pending surface is in [Docs/COMPATIBILITY.md](Docs/COMPATIBILITY.md), with
component and safety details in [Docs/ARCHITECTURE.md](Docs/ARCHITECTURE.md).

Microsoft binaries and ROM assets are not included. Windows and Windows CE are trademarks of
Microsoft Corporation; this project is not affiliated with Microsoft.
