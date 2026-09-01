import MetalKit
import SwiftUI

struct CEMetalDisplay: UIViewRepresentable {
    @ObservedObject var runtime: CERuntimeModel

    func makeCoordinator() -> Renderer { Renderer(runtime: runtime) }
    func makeUIView(context: Context) -> TouchMetalView {
        let view = TouchMetalView(frame: .zero, device: MTLCreateSystemDefaultDevice())
        view.colorPixelFormat = .bgra8Unorm; view.framebufferOnly = true; view.enableSetNeedsDisplay = false
        view.isPaused = false; view.preferredFramesPerSecond = 60; view.delegate = context.coordinator
        view.onPointer = { [weak runtime] x, y, down in runtime?.pointer(x: x, y: y, down: down) }
        view.onKey = { [weak runtime] key, down in runtime?.key(key, down: down) }
        context.coordinator.configure(view)
        return view
    }
    func updateUIView(_ view: TouchMetalView, context: Context) { context.coordinator.runtime = runtime }

    final class Renderer: NSObject, MTKViewDelegate {
        var runtime: CERuntimeModel
        private var queue: MTLCommandQueue?
        private var pipeline: MTLRenderPipelineState?
        private var texture: MTLTexture?
        init(runtime: CERuntimeModel) { self.runtime = runtime }

        func configure(_ view: MTKView) {
            guard let device = view.device else { return }; queue = device.makeCommandQueue()
            let source = """
            #include <metal_stdlib>
            using namespace metal;
            struct V { float4 p [[position]]; float2 uv; };
            vertex V vmain(uint id [[vertex_id]]) { float2 p[3]={{-1,-1},{3,-1},{-1,3}}; V o; o.p=float4(p[id],0,1); o.uv=float2((p[id].x+1)/2,1-(p[id].y+1)/2); return o; }
            fragment float4 fmain(V in [[stage_in]], texture2d<float> t [[texture(0)]]) { constexpr sampler s(filter::nearest, address::clamp_to_edge); return t.sample(s,in.uv); }
            """
            guard let library = try? device.makeLibrary(source: source, options: nil) else { return }
            let descriptor = MTLRenderPipelineDescriptor(); descriptor.vertexFunction = library.makeFunction(name: "vmain")
            descriptor.fragmentFunction = library.makeFunction(name: "fmain"); descriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat
            pipeline = try? device.makeRenderPipelineState(descriptor: descriptor)
            let td = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .bgra8Unorm, width: 240, height: 320, mipmapped: false)
            td.usage = [.shaderRead]; texture = device.makeTexture(descriptor: td)
        }
        func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}
        func draw(in view: MTKView) {
            guard let core = runtime.runtime, let texture, let pipeline, let drawable = view.currentDrawable,
                  let pass = view.currentRenderPassDescriptor, let queue else { return }
            var w: UInt32 = 0, h: UInt32 = 0, g: UInt32 = 0
            if let pixels = CERuntimeFramebuffer(core, &w, &h, &g) {
                texture.replace(region: MTLRegionMake2D(0, 0, Int(w), Int(h)), mipmapLevel: 0,
                    withBytes: pixels, bytesPerRow: Int(w) * MemoryLayout<UInt32>.size)
            }
            guard let command = queue.makeCommandBuffer(), let encoder = command.makeRenderCommandEncoder(descriptor: pass) else { return }
            encoder.setRenderPipelineState(pipeline); encoder.setFragmentTexture(texture, index: 0)
            encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3); encoder.endEncoding()
            command.present(drawable); command.commit()
        }
    }
}

final class TouchMetalView: MTKView {
    var onPointer: ((Int32, Int32, Bool) -> Void)?
    var onKey: ((UInt32, Bool) -> Void)?
    override var canBecomeFirstResponder: Bool { true }
    private func point(_ touch: UITouch) -> (Int32, Int32) {
        let p = touch.location(in: self)
        return (Int32(max(0, min(239, p.x / max(bounds.width, 1) * 240))),
                Int32(max(0, min(319, p.y / max(bounds.height, 1) * 320))))
    }
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) { becomeFirstResponder(); if let t = touches.first { let p = point(t); onPointer?(p.0, p.1, true) } }
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) { if let t = touches.first { let p = point(t); onPointer?(p.0, p.1, true) } }
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) { if let t = touches.first { let p = point(t); onPointer?(p.0, p.1, false) } }
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) { touchesEnded(touches, with: event) }
    override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        var handled = false
        for press in presses { if let key = press.key, let vk = virtualKey(key.keyCode.rawValue) { onKey?(vk, true); handled = true } }
        if !handled { super.pressesBegan(presses, with: event) }
    }
    override func pressesEnded(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        var handled = false
        for press in presses { if let key = press.key, let vk = virtualKey(key.keyCode.rawValue) { onKey?(vk, false); handled = true } }
        if !handled { super.pressesEnded(presses, with: event) }
    }
    private func virtualKey(_ usage: Int) -> UInt32? {
        if (4...29).contains(usage) { return UInt32(0x41 + usage - 4) }
        if (30...38).contains(usage) { return UInt32(0x31 + usage - 30) }
        if usage == 39 { return 0x30 }
        switch usage {
        case 40: return 0x0d
        case 41: return 0x1b
        case 42: return 0x08
        case 43: return 0x09
        case 44: return 0x20
        case 79: return 0x27
        case 80: return 0x25
        case 81: return 0x28
        case 82: return 0x26
        default: return nil
        }
    }
}
