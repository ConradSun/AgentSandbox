//
//  SandboxService.swift
//  AgentSandbox
//
//  Created by ConradSun on 2025/3/30.
//

import Foundation
import AppKit

/// 应用注入、启动与生命周期管理
@MainActor
class SandboxService {
    static let shared = SandboxService()

    private let fileManager = FileManager.default
    private let dylibPath = "/usr/local/lib/libsandbox.dylib"

    private init() {}

    // MARK: - Public

    /// 注入 dylib 并启动应用（已在沙箱中的应用直接启动）
    /// - Parameter app: 沙箱应用对象
    func injectAndLaunch(app: SandboxApp) async throws {
        if app.isInSandbox, let sandboxPath = app.sandboxPath {
            try await launchExisting(sandboxPath: sandboxPath, app: app)
            return
        }

        await updateAppStatus(app, status: .injecting, pid: nil)

        guard DiskManager.shared.isMounted || DiskManager.shared.mount() else {
            throw SandboxError.diskError
        }

        guard fileManager.fileExists(atPath: dylibPath) else {
            Logger(.error, "Dylib not found: \(dylibPath)")
            throw SandboxError.dylibNotFound
        }

        guard let source = app.bundlePath,
              let sandboxAppPath = DiskManager.shared.appPath(for: app.name) else {
            throw SandboxError.invalidApp
        }

        try copyApp(from: source, to: sandboxAppPath)

        let execPath = try getExecutablePath(from: sandboxAppPath)
        try injectDylib(execPath: execPath)

        // 等待文件系统同步
        try await Task.sleep(nanoseconds: 500_000_000)

        let pid = try await launch(execPath: execPath)
        app.sandboxPath = sandboxAppPath
        app.pid = pid
        app.status = .running

        Task { await self.watchProcess(pid: pid, app: app) }
    }

    /// 终止应用进程
    /// - Parameter app: 沙箱应用对象
    func stopApp(_ app: SandboxApp) {
        guard let pid = app.pid else { return }
        kill(pid, SIGTERM)
        app.status = .stopped
        app.pid = nil
        Logger(.info, "Stopped pid:\(pid)")
    }

    /// 从沙箱磁盘删除应用
    /// - Parameter app: 沙箱应用对象
    func deleteApp(_ app: SandboxApp) {
        guard let path = app.sandboxPath else { return }
        try? fileManager.removeItem(at: path)
        Logger(.info, "Deleted: \(path.lastPathComponent)")
    }

    // MARK: - Private

    /// 启动已在沙箱中的应用
    /// - Parameters:
    ///   - sandboxPath: 沙箱内应用路径
    ///   - app: 沙箱应用对象
    private func launchExisting(sandboxPath: URL, app: SandboxApp) async throws {
        guard DiskManager.shared.isMounted || DiskManager.shared.mount() else {
            throw SandboxError.diskError
        }
        let execPath = try getExecutablePath(from: sandboxPath)
        let pid = try await launch(execPath: execPath)
        app.pid = pid
        app.status = .running
        Task { await self.watchProcess(pid: pid, app: app) }
    }

    /// 复制 .app bundle 到沙箱目录
    /// - Parameters:
    ///   - source: 源应用路径
    ///   - dest: 目标应用路径
    private func copyApp(from source: URL, to dest: URL) throws {
        if fileManager.fileExists(atPath: dest.path) {
            try? fileManager.removeItem(at: dest)
        }
        try fileManager.copyItem(at: source, to: dest)
        Logger(.info, "Copied to: \(dest.lastPathComponent)")
    }

    /// 从 Info.plist 读取可执行文件路径
    /// - Parameter appURL: 应用路径
    /// - Returns: 可执行文件路径
    private func getExecutablePath(from appURL: URL) throws -> String {
        let plistURL = appURL.appendingPathComponent("Contents/Info.plist")
        guard let data = fileManager.contents(atPath: plistURL.path),
              let plist = try? PropertyListSerialization.propertyList(from: data, format: nil) as? [String: Any],
              let execName = plist["CFBundleExecutable"] as? String else {
            throw SandboxError.invalidApp
        }
        return appURL.appendingPathComponent("Contents/MacOS/\(execName)").path
    }

    /// 向可执行文件注入 dylib（修改 Mach-O LC_LOAD_DYLIB）
    /// - Parameter execPath: 可执行文件路径
    private func injectDylib(execPath: String) throws {
        let repack = BinaryRepack()
        guard repack.initWithFile(filePath: execPath, libPath: dylibPath),
              repack.repackBinary() else {
            throw SandboxError.injectFailed
        }
        Logger(.info, "Dylib injected")
    }

    /// 通过 NSWorkspace 启动 .app bundle，返回 PID
    /// - Parameter execPath: 可执行文件路径
    /// - Returns: 应用进程 ID
    private func launch(execPath: String) async throws -> Int32 {
        let appURL = URL(fileURLWithPath: execPath)
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .deletingLastPathComponent()

        let config = NSWorkspace.OpenConfiguration()
        config.createsNewApplicationInstance = true

        return try await withCheckedThrowingContinuation { continuation in
            NSWorkspace.shared.openApplication(at: appURL, configuration: config) { runningApp, error in
                if let error = error {
                    Logger(.error, "Launch failed: \(error)")
                    continuation.resume(throwing: SandboxError.launchFailed)
                } else if let runningApp = runningApp {
                    Logger(.info, "Launched: \(runningApp.localizedName ?? ""), pid:\(runningApp.processIdentifier)")
                    continuation.resume(returning: runningApp.processIdentifier)
                } else {
                    continuation.resume(throwing: SandboxError.launchFailed)
                }
            }
        }
    }

    /// 监控进程状态，退出时更新 app.status
    /// - Parameters:
    ///   - pid: 进程 ID
    ///   - app: 沙箱应用对象
    private func watchProcess(pid: Int32, app: SandboxApp) async {
        // 等待进程启动（最多 1.5s）
        for _ in 0..<3 {
            try? await Task.sleep(nanoseconds: 500_000_000)
            if kill(pid, 0) == 0 { break }
        }

        guard kill(pid, 0) == 0 else {
            await updateAppStatus(app, status: .failed, pid: nil)
            Logger(.error, "Process pid:\(pid) never started")
            return
        }

        while kill(pid, 0) == 0 {
            try? await Task.sleep(nanoseconds: 2_000_000_000)
        }

        await updateAppStatus(app, status: .stopped, pid: nil)
        Logger(.info, "Process pid:\(pid) exited")
    }

    /// 在 MainActor 上更新 app 状态
    /// - Parameters:
    ///   - app: 沙箱应用对象
    ///   - status: 新状态
    ///   - pid: 新进程 ID（传 nil 则不更新）
    @MainActor
    private func updateAppStatus(_ app: SandboxApp, status: SandboxApp.Status, pid: Int32?) {
        app.status = status
        app.pid = pid
    }
}

// MARK: - Error

/// 沙箱操作错误
enum SandboxError: LocalizedError {
    case invalidApp, diskError, dylibNotFound, injectFailed, launchFailed

    var errorDescription: String? {
        switch self {
        case .invalidApp:    return "Invalid application"
        case .diskError:     return "Sandbox disk error"
        case .dylibNotFound: return "libsandbox.dylib not found"
        case .injectFailed:  return "Dylib injection failed"
        case .launchFailed:  return "Application launch failed"
        }
    }
}
