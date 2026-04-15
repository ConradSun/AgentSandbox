//
//  LogViewModel.swift
//  AgentSandbox
//
//  Created by ConradSun on 2025/3/30.
//

import Foundation
import AppKit
import Combine

/// 审计日志列表的状态管理
@MainActor
class LogViewModel: ObservableObject {
    /// 日志列表
    @Published var logs: [AuditLog] = []
    /// 日志类型过滤器
    @Published var filterType: AuditLog.LogType?
    /// 搜索文本
    @Published var searchText = ""
    /// 缓存的过滤结果
    @Published var filteredLogs: [AuditLog] = []

    /// pid -> 进程名缓存
    private var processNameCache: [Int32: String] = [:]
    /// 过滤脏标记
    private var filterDirty = true
    /// 过滤缓存取消订阅
    private var cancellables = Set<AnyCancellable>()

    init() {
        // 任一过滤条件变化时标记需要重算
        $logs
            .sink { [weak self] _ in self?.filterDirty = true }
            .store(in: &cancellables)
        $filterType
            .sink { [weak self] _ in self?.filterDirty = true }
            .store(in: &cancellables)
        $searchText
            .sink { [weak self] _ in self?.filterDirty = true }
            .store(in: &cancellables)
    }

    /// 获取过滤后的日志（惰性计算，脏时才重算）
    func getFilteredLogs() -> [AuditLog] {
        if filterDirty {
            var result = logs
            if let type = filterType {
                result = result.filter { $0.type == type }
            }
            if !searchText.isEmpty {
                let lower = searchText.lowercased()
                result = result.filter {
                    $0.operation.localizedLowercase.contains(lower) ||
                    ($0.path?.localizedLowercase.contains(lower) ?? false)
                }
            }
            filteredLogs = result
            filterDirty = false
        }
        return filteredLogs
    }

    // MARK: - Public

    /// 追加一条日志，超过 5000 条时丢弃最旧的
    /// - Parameter log: 审计日志
    func addLog(_ log: AuditLog) {
        logs.append(log)
        if logs.count > 5000 {
            logs.removeFirst(logs.count - 5000)
        }
        // 缓存未命中时异步查询进程名
        if processNameCache[log.processId] == nil {
            fetchProcessName(pid: log.processId)
        }
    }

    /// 清空所有日志
    func clear() {
        logs.removeAll()
    }

    /// 根据 PID 获取进程名，结果缓存避免重复查询
    /// - Parameter pid: 进程 ID
    /// - Returns: 进程名称
    func getProcessName(pid: Int32) -> String {
        if let cached = processNameCache[pid] { return cached }

        if let app = NSWorkspace.shared.runningApplications.first(where: { $0.processIdentifier == pid }) {
            let name = app.localizedName ?? "pid:\(pid)"
            processNameCache[pid] = name
            return name
        }

        // 已不在 runningApps 中，返回 pid 占位，下次查询用 ps 重试
        return "pid:\(pid)"
    }

    /// 通过 ps 命令异步获取进程名并缓存
    /// - Parameter pid: 进程 ID
    func fetchProcessName(pid: Int32) {
        // 已在缓存或已在查询中，跳过
        if processNameCache[pid] != nil { return }

        Task.detached(priority: .utility) { [weak self] in
            let name: String
            let task = Process()
            task.executableURL = URL(fileURLWithPath: "/bin/ps")
            task.arguments = ["-p", "\(pid)", "-o", "comm="]

            let outputPipe = Pipe()
            task.standardOutput = outputPipe
            task.standardError = FileHandle.nullDevice

            do {
                try task.run()
                let outputData = outputPipe.fileHandleForReading.readDataToEndOfFile()
                let output = String(data: outputData, encoding: .utf8)?
                    .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
                // ps -o comm= 可能返回完整路径，只取最后一段作为进程名
                if output.isEmpty {
                    name = "pid:\(pid)"
                } else {
                    name = URL(fileURLWithPath: output).deletingPathExtension().lastPathComponent
                }
            } catch {
                name = "pid:\(pid)"
            }

            await MainActor.run {
                self?.processNameCache[pid] = name
            }
        }
    }
}
