import Foundation
import UniformTypeIdentifiers

struct CEProgram: Identifiable, Hashable {
    let url: URL
    var id: URL { url }
    var name: String { url.deletingPathExtension().lastPathComponent }
    var size: Int64 { (try? url.resourceValues(forKeys: [.fileSizeKey]).fileSize).map(Int64.init) ?? 0 }
}

@MainActor
final class CELibrary: ObservableObject {
    @Published private(set) var programs: [CEProgram] = []
    @Published var error: String?
    let root: URL

    init() {
        root = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("PocketPC", isDirectory: true)
        try? FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        reload()
    }

    func reload() {
        let urls = (try? FileManager.default.contentsOfDirectory(at: root,
            includingPropertiesForKeys: [.fileSizeKey], options: [.skipsHiddenFiles])) ?? []
        programs = urls.filter { ["exe", "dll", "cab"].contains($0.pathExtension.lowercased()) }
            .map(CEProgram.init).sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
    }

    func importFile(_ source: URL) {
        let access = source.startAccessingSecurityScopedResource()
        defer { if access { source.stopAccessingSecurityScopedResource() } }
        let destination = uniqueURL(for: source.lastPathComponent)
        do { try FileManager.default.copyItem(at: source, to: destination); reload() }
        catch { self.error = error.localizedDescription }
    }

    func delete(_ program: CEProgram) {
        do { try FileManager.default.removeItem(at: program.url); reload() }
        catch { self.error = error.localizedDescription }
    }

    private func uniqueURL(for filename: String) -> URL {
        var result = root.appendingPathComponent(filename); var n = 2
        while FileManager.default.fileExists(atPath: result.path) {
            let base = (filename as NSString).deletingPathExtension
            let ext = (filename as NSString).pathExtension
            result = root.appendingPathComponent("\(base) \(n).\(ext)"); n += 1
        }
        return result
    }
}
