import Foundation

@MainActor
final class CERuntimeModel: ObservableObject {
    @Published var diagnostic = "停止中"
    @Published var running = false
    @Published var frameGeneration: UInt32 = 0
    private(set) var runtime: UnsafeMutablePointer<CERuntime>?
    private var timer: Timer?

    func launch(_ program: CEProgram, root: URL) {
        stop()
        guard let data = try? Data(contentsOf: program.url) else { diagnostic = "ファイルを読めません"; return }
        runtime = root.path.withCString { CERuntimeCreate($0, 240, 320) }
        guard let runtime else { diagnostic = "仮想端末を作成できません"; return }
        let status: CEStatus = data.withUnsafeBytes { raw in
            CERuntimeLoadExecutable(runtime, raw.bindMemory(to: UInt8.self).baseAddress, raw.count)
        }
        diagnostic = String(cString: CERuntimeDiagnostic(runtime))
        guard status == CE_OK else { return }
        running = true
        timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 60.0, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.tick() }
        }
    }

    func stop() {
        timer?.invalidate(); timer = nil
        if let runtime { CERuntimeDestroy(runtime) }
        runtime = nil; running = false
    }

    private func tick() {
        guard let runtime else { return }
        _ = CERuntimeRunSlice(runtime, 50_000)
        var width: UInt32 = 0, height: UInt32 = 0, generation: UInt32 = 0
        _ = CERuntimeFramebuffer(runtime, &width, &height, &generation)
        frameGeneration = generation
        diagnostic = String(cString: CERuntimeDiagnostic(runtime))
    }

    func pointer(x: Int32, y: Int32, down: Bool) { if let runtime { CERuntimePostPointer(runtime, x, y, down) } }
    func key(_ virtualKey: UInt32, down: Bool) { if let runtime { CERuntimePostKey(runtime, virtualKey, down) } }
    deinit { timer?.invalidate(); if let runtime { CERuntimeDestroy(runtime) } }
}
