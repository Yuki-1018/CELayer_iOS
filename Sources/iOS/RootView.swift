import SwiftUI
import UniformTypeIdentifiers

struct RootView: View {
    @EnvironmentObject private var library: CELibrary
    @StateObject private var runtime = CERuntimeModel()
    @State private var importing = false
    @State private var tab = 0

    var body: some View {
        TabView(selection: $tab) {
            NavigationStack {
                Group {
                    if library.programs.isEmpty { emptyLibrary }
                    else { programList }
                }
                .navigationTitle("CE Launcher")
                .toolbar { ToolbarItem(placement: .primaryAction) { importButton } }
            }
            .tabItem { Label("ライブラリ", systemImage: "square.grid.2x2") }.tag(0)

            NavigationStack { DeviceView(runtime: runtime) }
                .tabItem { Label("Pocket PC", systemImage: "iphone") }.tag(1)

            NavigationStack { SettingsView(library: library, runtime: runtime) }
                .tabItem { Label("情報", systemImage: "info.circle") }.tag(2)
        }
        .fileImporter(isPresented: $importing, allowedContentTypes: [.data], allowsMultipleSelection: false) { result in
            if case .success(let urls) = result, let url = urls.first { library.importFile(url) }
            if case .failure(let error) = result { library.error = error.localizedDescription }
        }
        .alert("CELayer", isPresented: Binding(get: { library.error != nil }, set: { if !$0 { library.error = nil } })) {
            Button("OK") { library.error = nil }
        } message: { Text(library.error ?? "") }
    }

    private var importButton: some View {
        Button { importing = true } label: { Label("読み込む", systemImage: "plus") }
    }

    private var emptyLibrary: some View {
        ContentUnavailableView("Windows CE アプリがありません", systemImage: "shippingbox",
            description: Text("ARM版 Pocket PC 2003 の .exe を読み込んでください。"))
            .overlay(alignment: .bottom) { Button("ファイルを読み込む") { importing = true }.buttonStyle(.borderedProminent).padding(40) }
    }

    private var programList: some View {
        List {
            Section("インストール済み") {
                ForEach(library.programs) { program in
                    Button { runtime.launch(program, root: library.root); tab = 1 } label: {
                        HStack(spacing: 14) {
                            Image(systemName: program.url.pathExtension.lowercased() == "exe" ? "app.dashed" : "doc.badge.gearshape")
                                .font(.title2).frame(width: 32).foregroundStyle(.blue)
                            VStack(alignment: .leading) {
                                Text(program.name).foregroundStyle(.primary)
                                Text("\(ByteCountFormatter.string(fromByteCount: program.size, countStyle: .file)) · \(program.url.pathExtension.uppercased())")
                                    .font(.caption).foregroundStyle(.secondary)
                            }
                            Spacer(); Image(systemName: "play.fill").foregroundStyle(.tint)
                        }.padding(.vertical, 4)
                    }
                    .swipeActions {
                        Button(role: .destructive) { library.delete(program) } label: { Label("削除", systemImage: "trash") }
                        ShareLink(item: program.url) { Label("共有", systemImage: "square.and.arrow.up") }.tint(.blue)
                    }
                }
            }
        }
    }
}

private struct DeviceView: View {
    @ObservedObject var runtime: CERuntimeModel

    var body: some View {
        VStack(spacing: 16) {
            ZStack {
                RoundedRectangle(cornerRadius: 28).fill(.black).shadow(radius: 8)
                if runtime.runtime != nil {
                    CEMetalDisplay(runtime: runtime)
                        .aspectRatio(240.0 / 320.0, contentMode: .fit)
                        .padding(14)
                        .clipShape(RoundedRectangle(cornerRadius: 15))
                } else {
                    VStack(spacing: 10) { Image(systemName: "power").font(.largeTitle); Text("ライブラリからアプリを起動") }.foregroundStyle(.gray)
                }
            }.aspectRatio(0.70, contentMode: .fit).padding(.horizontal)
            Text(runtime.diagnostic).font(.caption.monospaced()).foregroundStyle(.secondary).lineLimit(3)
            HStack {
                Button { runtime.key(0x25, down: true); runtime.key(0x25, down: false) } label: { Image(systemName: "chevron.left") }
                Button { runtime.key(0x0d, down: true); runtime.key(0x0d, down: false) } label: { Image(systemName: "return") }
                Button { runtime.key(0x1b, down: true); runtime.key(0x1b, down: false) } label: { Text("Esc") }
                Button("停止", role: .destructive) { runtime.stop() }.disabled(runtime.runtime == nil)
            }.buttonStyle(.bordered)
            Spacer(minLength: 0)
        }.padding(.top).navigationTitle("Pocket PC 2003").navigationBarTitleDisplayMode(.inline)
    }
}

private struct SettingsView: View {
    @ObservedObject var library: CELibrary
    @ObservedObject var runtime: CERuntimeModel
    var body: some View {
        List {
            Section("仮想端末") {
                LabeledContent("画面", value: "240 × 320 / 32-bit")
                LabeledContent("CPU", value: "ARMv4T interpreter")
                LabeledContent("ゲストルート", value: "Documents/PocketPC")
            }
            Section("実装状況") {
                Label("PE32・再配置・名前/序数インポート", systemImage: "checkmark.circle.fill")
                Label("ARM/Thumb・仮想メモリ・APIトラップ", systemImage: "checkmark.circle.fill")
                Label("ウィンドウ・メッセージ・GDI framebuffer", systemImage: "checkmark.circle.fill")
                Label("追加WinCE APIは継続実装中", systemImage: "wrench.and.screwdriver")
            }
            Section("診断") { Text(runtime.diagnostic).font(.caption.monospaced()).textSelection(.enabled) }
        }.navigationTitle("CELayerについて")
    }
}
