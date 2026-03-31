//
//  SandboxViewModel.swift
//  AgentSandbox
//
//  Created by ConradSun on 2025/3/30.
//

import Foundation
import AppKit
import Combine

/// 沙箱应用列表的状态管理
@MainActor
class SandboxViewModel: ObservableObject {
    /// 应用列表
    @Published var apps: [SandboxApp] = []
    /// 最后一次错误信息
    @Published var lastError: String?

    init() {
        scanSandboxApps()
    }

    // MARK: - Public

    /// 从外部拖入 .app，触发注入流程
    /// - Parameter url: 应用文件 URL
    func addApp(from url: URL) {
        guard url.pathExtension == "app" else {
            lastError = "Invalid .app file"
            return
        }

        guard !apps.contains(where: { $0.name == url.deletingPathExtension().lastPathComponent }) else {
            lastError = "App already in sandbox"
            return
        }

        let name = url.deletingPathExtension().lastPathComponent
        let icon = NSWorkspace.shared.icon(forFile: url.path)
        let app = SandboxApp(name: name, bundlePath: url, sandboxPath: nil, icon: icon)
        apps.append(app)

        Task { await injectApp(app) }
    }

    /// 注入并启动应用
    /// - Parameter app: 沙箱应用对象
    func injectApp(_ app: SandboxApp) async {
        do {
            try await SandboxService.shared.injectAndLaunch(app: app)
            Logger(.info, "Launched: \(app.name)")
        } catch {
            Logger(.error, "Launch failed: \(error.localizedDescription)")
            lastError = error.localizedDescription
            app.status = .failed
        }
    }

    /// 停止应用
    /// - Parameter app: 沙箱应用对象
    func stopApp(_ app: SandboxApp) {
        SandboxService.shared.stopApp(app)
    }

    /// 从列表移除并删除沙箱文件
    /// - Parameter app: 沙箱应用对象
    func removeApp(_ app: SandboxApp) {
        SandboxService.shared.stopApp(app)
        SandboxService.shared.deleteApp(app)
        apps.removeAll { $0.id == app.id }
    }

    // MARK: - Private

    /// 检查进程是否正在运行
    /// - Parameter pid: 进程 ID
    /// - Returns: 进程是否正在运行
    private func isProcessRunning(_ pid: Int32) -> Bool {
        guard pid > 0 else { return false }
        return kill(pid, 0) == 0 && errno != ESRCH
    }

    /// 扫描沙箱磁盘中已有的应用
    private func scanSandboxApps() {
        guard DiskManager.shared.isMounted, let appsDir = DiskManager.shared.appsDir else { return }

        let contents = try? FileManager.default.contentsOfDirectory(at: appsDir, includingPropertiesForKeys: nil)
        for url in contents ?? [] where url.pathExtension == "app" {
            let name = url.deletingPathExtension().lastPathComponent
            let icon = NSWorkspace.shared.icon(forFile: url.path)
            let app = SandboxApp(name: name, bundlePath: nil, sandboxPath: url, icon: icon)
            app.status = isProcessRunning(app.pid ?? -1) ? .running : .stopped
            apps.append(app)
        }
    }
}
