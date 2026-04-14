//
//  DiskManager.swift
//  AgentSandbox
//
//  Created by ConradSun on 2025/3/30.
//

import Foundation

/// 磁盘配置常量
private struct DiskConfig {
    static let volumesPath = "/Volumes"
}

/// 沙箱 Sparse Disk Image 管理
class DiskManager: @unchecked Sendable {
    static let shared = DiskManager()

    private let fileManager = FileManager.default
    private let diskName = "AgentSandbox"
    private let diskSizeGB = 10

    private(set) var diskPath: URL?
    private(set) var isMounted = false

    /// 沙箱内应用目录
    var appsDir: URL?  { diskPath?.appendingPathComponent("Apps") }
    /// 沙箱内文件目录
    var filesDir: URL? { diskPath?.appendingPathComponent("Files") }
    /// 沙箱内日志目录
    var logsDir: URL?  { diskPath?.appendingPathComponent("Logs") }

    private init() {
        // 启动时主动挂载磁盘，确保扫描到沙箱内已有应用
        if !mount() {
            Logger(.error, "Failed to mount sandbox disk at startup")
        }
    }

    // MARK: - Public

    /// 挂载沙箱磁盘，不存在则先创建
    @discardableResult
    func mount() -> Bool {
        // volume 已存在（上次已挂载），直接复用
        let volumePath = URL(fileURLWithPath: "\(DiskConfig.volumesPath)/\(diskName)")
        if fileManager.fileExists(atPath: volumePath.path) {
            diskPath = volumePath
            isMounted = true
            createDirectoryStructure()
            Logger(.info, "Disk already mounted: \(volumePath.path)")
            return true
        }

        guard let dmgPath = getDMGPath() else {
            Logger(.error, "Failed to get DMG path")
            return false
        }

        if !fileManager.fileExists(atPath: dmgPath.path) {
            guard createSparseDMG(at: dmgPath) else { return false }
        }

        let task = Process()
        task.executableURL = URL(fileURLWithPath: "/usr/bin/hdiutil")
        task.arguments = ["attach", dmgPath.path, "-nobrowse", "-mountpoint", "\(DiskConfig.volumesPath)/\(diskName)"]

        do {
            try task.run()
            task.waitUntilExit()
            if task.terminationStatus == 0 {
                diskPath = URL(fileURLWithPath: "\(DiskConfig.volumesPath)/\(diskName)")
                isMounted = true
                createDirectoryStructure()
                Logger(.info, "Disk mounted: \(DiskConfig.volumesPath)/\(diskName)")
                return true
            }
        } catch {
            Logger(.error, "Mount failed: \(error)")
        }
        return false
    }

    /// 卸载沙箱磁盘
    @discardableResult
    func unmount() -> Bool {
        guard isMounted, let path = diskPath else { return true }

        let task = Process()
        task.executableURL = URL(fileURLWithPath: "/usr/bin/hdiutil")
        task.arguments = ["detach", path.path, "-force"]

        do {
            try task.run()
            task.waitUntilExit()
            if task.terminationStatus == 0 {
                diskPath = nil
                isMounted = false
                return true
            }
        } catch {
            Logger(.error, "Unmount failed: \(error)")
        }
        return false
    }

    /// 返回指定应用在沙箱内的路径
    /// - Parameter name: 应用名称
    /// - Returns: 沙箱内应用路径
    func appPath(for name: String) -> URL? {
        appsDir?.appendingPathComponent("\(name).app")
    }

    // MARK: - Private

    /// 获取 DMG 文件路径
    /// - Returns: DMG 文件路径，如果无法获取应用支持目录则返回 nil
    private func getDMGPath() -> URL? {
        guard let appSupport = fileManager.urls(for: .applicationSupportDirectory, in: .userDomainMask).first else {
            Logger(.error, "Failed to get Application Support directory")
            return nil
        }
        return appSupport.appendingPathComponent("AgentSandbox/\(diskName).sparseimage")
    }

    /// 创建 Sparse DMG 文件
    /// - Parameter path: DMG 文件路径
    /// - Returns: 是否成功创建
    private func createSparseDMG(at path: URL) -> Bool {
        let parentDir = path.deletingLastPathComponent()
        do {
            try fileManager.createDirectory(at: parentDir, withIntermediateDirectories: true)
        } catch {
            Logger(.error, "Failed to create parent directory: \(error)")
            return false
        }
        
        do {
            try fileManager.removeItem(at: path)
        } catch {
            // 文件不存在，忽略错误
        }

        let task = Process()
        task.executableURL = URL(fileURLWithPath: "/usr/bin/hdiutil")
        task.arguments = [
            "create", "-size", "\(diskSizeGB)g",
            "-type", "SPARSE", "-fs", "APFS",
            "-volname", diskName, path.path
        ]

        do {
            try task.run()
            task.waitUntilExit()
            if task.terminationStatus == 0 {
                Logger(.info, "Created sparse DMG: \(path.path)")
                return true
            }
        } catch {
            Logger(.error, "Create DMG failed: \(error)")
        }
        return false
    }

    /// 创建沙箱目录结构
    private func createDirectoryStructure() {
        guard let diskPath = diskPath else { return }

        for dir in ["Apps", "Files", "Logs"] {
            try? fileManager.createDirectory(
                at: diskPath.appendingPathComponent(dir),
                withIntermediateDirectories: true
            )
        }

        if let filesDir = filesDir {
            for dir in ["Desktop", "Documents", "Downloads", "Pictures", "Movies", "Music"] {
                try? fileManager.createDirectory(
                    at: filesDir.appendingPathComponent(dir),
                    withIntermediateDirectories: true
                )
            }
        }
    }
}
