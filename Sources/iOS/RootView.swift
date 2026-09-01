import SwiftUI
import UniformTypeIdentifiers

private enum CEImportKind { case files, folder }

struct RootView: View {
    @EnvironmentObject private var library: CELibrary
    @StateObject private var runtime = CERuntimeModel()
    @State private var importing = false
    @State private var importKind: CEImportKind = .folder
    @State private var tab = 0

    var body: some View {
        TabView(selection: $tab) {
            NavigationStack { applicationLibrary }
                .tabItem { Label("アプリ", systemImage: "app.badge") }.tag(0)
            NavigationStack { ExecutionView(runtime: runtime) }
                .tabItem { Label("実行画面", systemImage: "play.rectangle") }.tag(1)
            NavigationStack { SettingsView(library: library, runtime: runtime) }
                .tabItem { Label("設定", systemImage: "gearshape") }.tag(2)
        }
        .fileImporter(isPresented: $importing,
            allowedContentTypes: importKind == .folder ? [.folder] : [.data],
            allowsMultipleSelection: importKind == .files) { result in
            switch result {
            case .success(let urls):
                if importKind == .folder, let folder = urls.first { library.importFolder(folder) }
                else { library.importFiles(urls) }
            case .failure(let error): library.error = error.localizedDescription
            }
        }
        .alert("CELayer", isPresented: Binding(get: { library.error != nil },
            set: { if !$0 { library.error = nil } })) {
            Button("OK") { library.error = nil }
        } message: { Text(library.error ?? "") }
    }

    private var applicationLibrary: some View {
        Group {
            if library.programs.isEmpty {
                ContentUnavailableView("Windows CEアプリがありません", systemImage: "folder.badge.plus",
                    description: Text("EXE・DLL・画像・データを含むアプリフォルダを、そのまま読み込めます。"))
            } else {
                List {
                    Section("インストール済み") {
                        ForEach(library.programs) { program in
                            Button {
                                runtime.launch(program); tab = 1
                            } label: {
                                HStack(spacing: 14) {
                                    Image(systemName: "app.fill").font(.title2)
                                        .frame(width: 36, height: 36).foregroundStyle(.white)
                                        .background(.blue.gradient, in: RoundedRectangle(cornerRadius: 8))
                                    VStack(alignment: .leading, spacing: 3) {
                                        Text(program.name).foregroundStyle(.primary)
                                        Text("\(program.packageName) · \(ByteCountFormatter.string(fromByteCount: program.size, countStyle: .file))")
                                            .font(.caption).foregroundStyle(.secondary)
                                    }
                                    Spacer(); Image(systemName: "play.fill")
                                }.padding(.vertical, 4)
                            }
                            .swipeActions {
                                Button(role: .destructive) { library.delete(program) } label: {
                                    Label("パッケージ削除", systemImage: "trash")
                                }
                                ShareLink(item: program.url) { Label("EXE共有", systemImage: "square.and.arrow.up") }
                                    .tint(.blue)
                            }
                        }
                    }
                }
            }
        }
        .navigationTitle("CE Apps")
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Menu {
                    Button { importKind = .folder; importing = true } label: {
                        Label("アプリフォルダを読み込む", systemImage: "folder.badge.plus")
                    }
                    Button { importKind = .files; importing = true } label: {
                        Label("複数ファイルをまとめて読み込む", systemImage: "doc.on.doc")
                    }
                } label: { Label("読み込む", systemImage: "plus") }
            }
        }
    }
}

private struct ExecutionView: View {
    @ObservedObject var runtime: CERuntimeModel
    @State private var controlsVisible = true
    @State private var keyboardVisible = false
    @State private var diagnosticVisible = false
    @State private var keyboardText = ""

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()
            if runtime.runtime != nil {
                CEMetalDisplay(runtime: runtime)
                    .aspectRatio(240.0 / 320.0, contentMode: .fit)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                ContentUnavailableView("実行中のアプリはありません", systemImage: "play.rectangle",
                    description: Text("「アプリ」からEXEを選択してください。"))
                    .foregroundStyle(.white, .gray)
            }

            VStack {
                HStack {
                    if diagnosticVisible {
                        Text(runtime.diagnostic).font(.caption2.monospaced()).lineLimit(4)
                            .padding(9).background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 12))
                    }
                    Spacer(); floatingMenu
                }.padding(12)
                Spacer()
                if controlsVisible && runtime.runtime != nil { gameControls.padding(12) }
                if keyboardVisible && runtime.runtime != nil { characterInput.padding([.horizontal, .bottom], 12) }
            }
        }
        .navigationTitle(runtime.applicationName)
        .navigationBarTitleDisplayMode(.inline)
        .toolbarBackground(.black, for: .navigationBar)
        .toolbarColorScheme(.dark, for: .navigationBar)
    }

    private var floatingMenu: some View {
        Menu {
            Button { keyboardVisible.toggle() } label: {
                Label(keyboardVisible ? "文字入力を閉じる" : "文字入力", systemImage: "keyboard")
            }
            Button { controlsVisible.toggle() } label: {
                Label(controlsVisible ? "操作キーを隠す" : "操作キーを表示", systemImage: "gamecontroller")
            }
            Button { diagnosticVisible.toggle() } label: {
                Label("診断表示", systemImage: "waveform.path.ecg")
            }
            Divider()
            Button { runtime.togglePause() } label: {
                Label(runtime.paused ? "再開" : "一時停止", systemImage: runtime.paused ? "play" : "pause")
            }
            Button { runtime.restart() } label: { Label("再起動", systemImage: "arrow.clockwise") }
            Button(role: .destructive) { runtime.stop() } label: { Label("終了", systemImage: "xmark") }
        } label: {
            Image(systemName: "ellipsis").font(.title3.bold()).foregroundStyle(.primary)
                .frame(width: 48, height: 48).background(.ultraThinMaterial, in: Circle()).shadow(radius: 6)
        }
    }

    private var characterInput: some View {
        TextField("アプリへ文字を送る", text: $keyboardText)
            .textFieldStyle(.plain).padding(12)
            .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 14))
            .foregroundStyle(.primary)
            .onChange(of: keyboardText) { _, value in
                guard !value.isEmpty else { return }
                for scalar in value.unicodeScalars where scalar.value <= UInt32(UInt16.max) {
                    runtime.character(UInt16(scalar.value))
                }
                keyboardText = ""
            }
    }

    private var gameControls: some View {
        HStack(alignment: .bottom) {
            VStack(spacing: 2) {
                keyButton("chevron.up", 0x26)
                HStack(spacing: 2) { keyButton("chevron.left", 0x25); keyButton("chevron.down", 0x28); keyButton("chevron.right", 0x27) }
            }
            Spacer()
            VStack(spacing: 8) {
                HStack { textKey("Esc", 0x1b); textKey("Enter", 0x0d) }
                HStack { textKey("A", 0x41); textKey("B", 0x42); textKey("Space", 0x20) }
            }
        }.padding(10).background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 18))
    }

    private func keyButton(_ icon: String, _ key: UInt32) -> some View {
        Button { press(key) } label: { Image(systemName: icon).frame(width: 42, height: 34) }.buttonStyle(.bordered)
    }
    private func textKey(_ title: String, _ key: UInt32) -> some View {
        Button(title) { press(key) }.buttonStyle(.bordered)
    }
    private func press(_ key: UInt32) { runtime.key(key, down: true); runtime.key(key, down: false) }
}

private struct SettingsView: View {
    @ObservedObject var library: CELibrary
    @ObservedObject var runtime: CERuntimeModel
    var body: some View {
        List {
            Section("Wine型ランタイム") {
                LabeledContent("アプリ", value: "\(library.programs.count)")
                LabeledContent("CPU", value: "ARM/Thumb interpreter")
                LabeledContent("アプリ領域", value: "Documents/PocketPC/Applications")
            }
            Section("操作") {
                Text("実行画面右上のフローティングメニューから、文字入力、ゲームキー、一時停止、再起動、診断を操作できます。")
            }
            Section("診断") { Text(runtime.diagnostic).font(.caption.monospaced()).textSelection(.enabled) }
        }.navigationTitle("CELayer")
    }
}
