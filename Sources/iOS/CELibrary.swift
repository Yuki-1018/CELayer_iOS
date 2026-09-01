import Foundation
import UniformTypeIdentifiers

struct CEProgram: Identifiable, Hashable {
    let url: URL
    let packageURL: URL
    var id: URL { url }
    var name: String { url.deletingPathExtension().lastPathComponent }
    var size: Int64 { (try? url.resourceValues(forKeys: [.fileSizeKey]).fileSize).map(Int64.init) ?? 0 }
    var packageName: String { packageURL.lastPathComponent }
    var workingDirectory: URL { url.deletingLastPathComponent() }
}

@MainActor
final class CELibrary: ObservableObject {
    @Published private(set) var programs: [CEProgram] = []
    @Published var error: String?
    let root: URL
    let applicationsRoot: URL

    init() {
        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        let prefix = documents.appendingPathComponent("PocketPC", isDirectory: true)
        root = prefix
        applicationsRoot = prefix.appendingPathComponent("Applications", isDirectory: true)
        try? FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        try? FileManager.default.createDirectory(at: applicationsRoot, withIntermediateDirectories: true)
        reload()
    }

    func reload() {
        guard let enumerator = FileManager.default.enumerator(at: root,
            includingPropertiesForKeys: [.isRegularFileKey, .fileSizeKey], options: [.skipsHiddenFiles]) else {
            programs = []; return
        }
        var found: [CEProgram] = []
        for case let url as URL in enumerator where url.pathExtension.lowercased() == "exe" {
            found.append(CEProgram(url: url, packageURL: packageRoot(containing: url)))
        }
        programs = found.sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
    }

    func importFiles(_ sources: [URL]) {
        guard !sources.isEmpty else { return }
        let preferredName = sources.first(where: { $0.pathExtension.lowercased() == "exe" })?
            .deletingPathExtension().lastPathComponent ?? "Imported App"
        let package = uniqueURL(in: applicationsRoot, filename: preferredName)
        do {
            try FileManager.default.createDirectory(at: package, withIntermediateDirectories: true)
            for source in sources {
                let access = source.startAccessingSecurityScopedResource()
                defer { if access { source.stopAccessingSecurityScopedResource() } }
                try FileManager.default.copyItem(at: source,
                    to: uniqueURL(in: package, filename: source.lastPathComponent))
            }
            reload()
        } catch { self.error = error.localizedDescription }
    }

    func importFolder(_ source: URL) {
        let access = source.startAccessingSecurityScopedResource()
        defer { if access { source.stopAccessingSecurityScopedResource() } }
        let destination = uniqueURL(in: applicationsRoot, filename: source.lastPathComponent)
        do { try FileManager.default.copyItem(at: source, to: destination); reload() }
        catch { self.error = error.localizedDescription }
    }

    func delete(_ program: CEProgram) {
        do { try FileManager.default.removeItem(at: program.packageURL); reload() }
        catch { self.error = error.localizedDescription }
    }

    private func packageRoot(containing file: URL) -> URL {
        let relative = file.path.replacingOccurrences(of: applicationsRoot.path + "/", with: "")
        guard !relative.hasPrefix("/") else { return file }
        let component = relative.split(separator: "/").first.map(String.init)
        return component.map { applicationsRoot.appendingPathComponent($0, isDirectory: true) }
            ?? file.deletingLastPathComponent()
    }

    private func uniqueURL(in directory: URL, filename: String) -> URL {
        var result = directory.appendingPathComponent(filename); var n = 2
        while FileManager.default.fileExists(atPath: result.path) {
            let base = (filename as NSString).deletingPathExtension
            let ext = (filename as NSString).pathExtension
            let suffix = ext.isEmpty ? "\(base) \(n)" : "\(base) \(n).\(ext)"
            result = directory.appendingPathComponent(suffix); n += 1
        }
        return result
    }
}
