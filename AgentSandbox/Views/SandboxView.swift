//
//  SandboxView.swift
//  AgentSandbox
//
//  Created by ConradSun on 2025/3/30.
//

import SwiftUI
import UniformTypeIdentifiers

/// 沙箱管理视图：拖放区 + 应用列表
struct SandboxView: View {
    @EnvironmentObject var vm: SandboxViewModel

    var body: some View {
        HSplitView {
            // Left: Drop zone
            DropZoneView()
                .frame(minWidth: 200, maxWidth: 300)

            // Right: App list
            AppListView()
        }
        .alert(String(localized: "Error"), isPresented: Binding(
            get: { vm.lastError != nil },
            set: { if !$0 { vm.lastError = nil } }
        )) {
            Button(String(localized: "OK"), role: .cancel) { vm.lastError = nil }
        } message: {
            Text(vm.lastError ?? "")
        }
    }
}

// MARK: - DropZoneView

/// 应用拖放区视图
struct DropZoneView: View {
    @EnvironmentObject var vm: SandboxViewModel
    @State private var isTargeted = false

    var body: some View {
        VStack(spacing: 16) {
            Image(systemName: "plus.circle.dashed")
                .font(.system(size: 48))
                .foregroundColor(isTargeted ? .blue : .secondary)

            Text(String(localized: "Drop .app here"))
                .font(.headline)
                .foregroundColor(.secondary)

            Text(String(localized: "App will be injected and run"))
                .font(.caption)
                .foregroundColor(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(
            RoundedRectangle(cornerRadius: 12)
                .strokeBorder(style: StrokeStyle(lineWidth: 2, dash: [8]))
                .foregroundColor(isTargeted ? .blue : .secondary.opacity(0.5))
        )
        .padding()
        .onDrop(of: [UTType.fileURL], isTargeted: $isTargeted) { providers in
            handleDrop(providers)
        }
    }

    /// 处理拖放事件
    /// - Parameter providers: 拖放项提供者
    /// - Returns: 是否成功处理
    private func handleDrop(_ providers: [NSItemProvider]) -> Bool {
        guard let provider = providers.first else { return false }

        provider.loadItem(forTypeIdentifier: UTType.fileURL.identifier, options: nil) { item, _ in
            guard let data = item as? Data,
                  let url = URL(dataRepresentation: data, relativeTo: nil) else { return }

            DispatchQueue.main.async {
                vm.addApp(from: url)
            }
        }
        return true
    }
}

// MARK: - AppListView

/// 应用列表视图
struct AppListView: View {
    @EnvironmentObject var vm: SandboxViewModel

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Text(String(localized: "Sandbox Apps"))
                    .font(.headline)
                Spacer()
                Text(String(localized: "\(vm.apps.count) items"))
                    .foregroundColor(.secondary)
            }
            .padding()

            Divider()

            if vm.apps.isEmpty {
                VStack(spacing: 8) {
                    Image(systemName: "tray")
                        .font(.system(size: 40))
                        .foregroundColor(.secondary.opacity(0.5))
                    Text(String(localized: "No apps"))
                        .foregroundColor(.secondary)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                ScrollView {
                    LazyVStack(spacing: 8) {
                        ForEach(vm.apps) { app in
                            AppRowView(app: app)
                        }
                    }
                    .padding()
                }
            }
        }
    }
}

// MARK: - AppRowView

/// 应用行视图
struct AppRowView: View {
    @EnvironmentObject var vm: SandboxViewModel
    @ObservedObject var app: SandboxApp

    var body: some View {
        HStack(spacing: 12) {
            if let icon = app.icon {
                Image(nsImage: icon)
                    .resizable()
                    .frame(width: 40, height: 40)
            } else {
                Image(systemName: "app.fill")
                    .font(.system(size: 32))
                    .foregroundColor(.secondary)
            }

            VStack(alignment: .leading, spacing: 2) {
                Text(app.name)
                    .font(.subheadline)
                    .fontWeight(.medium)

                HStack(spacing: 4) {
                    Circle()
                        .fill(statusColor)
                        .frame(width: 6, height: 6)
                    Text(app.status.localizedString)
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }

            Spacer()

            Button(action: {
                if app.status == .running {
                    vm.stopApp(app)
                } else {
                    Task {
                        await vm.injectApp(app)
                    }
                }
            }) {
                Image(systemName: app.status == .running ? "stop.fill" : "play.fill")
                    .foregroundColor(app.status == .running ? .red : .green)
            }
            .buttonStyle(.plain)

            Button(action: { vm.removeApp(app) }) {
                Image(systemName: "xmark")
                    .foregroundColor(.secondary)
            }
            .buttonStyle(.plain)
        }
        .padding(12)
        .background(Color.primary.opacity(0.05))
        .cornerRadius(8)
    }

    /// 根据应用状态返回对应的颜色
    var statusColor: Color {
        switch app.status {
        case .running: return .green
        case .failed: return .red
        case .pending, .stopped: return .gray
        case .injecting: return .orange
        }
    }
}

#Preview {
    SandboxView()
        .environmentObject(SandboxViewModel())
}
