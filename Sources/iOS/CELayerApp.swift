import SwiftUI

@main
struct CELayerApp: App {
    @StateObject private var library = CELibrary()

    var body: some Scene {
        WindowGroup { RootView().environmentObject(library) }
    }
}
