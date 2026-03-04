import Cocoa
import SwiftUI
import Combine

enum AppState: Equatable {
    case idle
    case recording
    case transcribing
    case refining
    case done
    case error(String)
}

class StatusWindowController: NSWindowController {
    static let shared = StatusWindowController()
    private var cancellables = Set<AnyCancellable>()
    
    init() {
        let panel = NSPanel(
            contentRect: NSRect(x: 0, y: 0, width: 250, height: 60),
            styleMask: [.nonactivatingPanel, .fullSizeContentView, .hudWindow],
            backing: .buffered,
            defer: false
        )
        panel.level = .floating
        panel.collectionBehavior = [.canJoinAllSpaces, .stationary]
        panel.hasShadow = true
        panel.isMovableByWindowBackground = true
        panel.backgroundColor = .clear
        panel.isOpaque = false
        panel.center()
        
        super.init(window: panel)
        self.window?.contentView = NSHostingView(rootView: StatusView())
        
        ViewModel.shared.$state
            .receive(on: RunLoop.main)
            .sink { [weak self] state in
                self?.updateWindowVisibility(for: state)
            }
            .store(in: &cancellables)
    }
    
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }
    
    private func updateWindowVisibility(for state: AppState) {
        if case .idle = state {
            NSAnimationContext.runAnimationGroup { context in
                context.duration = 0.5
                window?.animator().alphaValue = 0
            } completionHandler: {
                self.window?.orderOut(nil)
            }
        } else {
            window?.makeKeyAndOrderFront(nil)
            NSAnimationContext.runAnimationGroup { context in
                context.duration = 0.3
                window?.animator().alphaValue = 1.0
            }
        }
    }
}

struct WaveView: View {
    var levels: [Float]
    
    var body: some View {
        HStack(spacing: 4) {
            ForEach(0..<levels.count, id: \.self) { index in
                RoundedRectangle(cornerRadius: 2)
                    .fill(Color.white)
                    .frame(width: 4, height: CGFloat(max(4, levels[index] * 24)))
                    .animation(.linear(duration: 0.05), value: levels[index])
            }
        }
        .frame(height: 24)
    }
}

struct StatusView: View {
    @ObservedObject var viewModel = ViewModel.shared
    @State private var pulse = false
    
    var body: some View {
        HStack {
            switch viewModel.state {
            case .idle:
                EmptyView()
            case .recording:
                Circle().fill(Color.red).frame(width: 15, height: 15)
                    .scaleEffect(pulse ? 1.2 : 0.8)
                    .onAppear {
                        withAnimation(.easeInOut(duration: 0.8).repeatForever(autoreverses: true)) {
                            pulse = true
                        }
                    }
                    .onDisappear { pulse = false }
                
                WaveView(levels: viewModel.audioLevels)
                    .padding(.leading, 8)
                
            case .transcribing:
                ProgressView()
                    .progressViewStyle(CircularProgressViewStyle(tint: .white))
                Image(systemName: "text.bubble")
                    .foregroundColor(.white)
                    .font(.system(size: 18))
            case .refining:
                ProgressView()
                    .progressViewStyle(CircularProgressViewStyle(tint: .white))
                Image(systemName: "wand.and.stars")
                    .foregroundColor(.white)
                    .font(.system(size: 18))
            case .done:
                Image(systemName: "checkmark.circle.fill")
                    .foregroundColor(.green)
                    .font(.system(size: 24))
            case .error:
                Image(systemName: "exclamationmark.triangle.fill")
                    .foregroundColor(.yellow)
            }
        }
        .padding()
        .background(Color.black.opacity(0.8))
        .cornerRadius(12)
    }
}
