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

    /// pid -> 进程名缓存
    private var processNameCache: [Int32: String] = [:]

    /// 过滤后的日志列表
    var filteredLogs: [AuditLog] {
        var result = logs
        if let type = filterType {
            result = result.filter { $0.type == type }
        }
        if !searchText.isEmpty {
            let lower = searchText.lowercased()
            result = result.filter {
                $0.operation.localizedLowercase.contains(lower) ||
                ($0.path?.localizedLowercase.contains(lower) ?? false) ||
                $0.processName.localizedLowercase.contains(lower)
            }
        }
        return result
    }

    // MARK: - Public

    /// 追加一条日志，超过 5000 条时丢弃最旧的
    /// - Parameter log: 审计日志
    func addLog(_ log: AuditLog) {
        logs.append(log)
        if logs.count > 5000 {
            logs.removeFirst(logs.count - 5000)
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

        let name: String
        if let app = NSWorkspace.shared.runningApplications.first(where: { $0.processIdentifier == pid }) {
            name = app.localizedName ?? "pid:\(pid)"
        } else {
            name = getProcessNameFromPS(pid: pid)
        }

        processNameCache[pid] = name
        return name
    }

    /// 通过 ps 命令获取进程名
    /// - Parameter pid: 进程 ID
    /// - Returns: 进程名称
    private func getProcessNameFromPS(pid: Int32) -> String {
        let task = Process()
        task.executableURL = URL(fileURLWithPath: "/bin/ps")
        task.arguments = ["-p", "\(pid)", "-o", "comm="]

        let outputPipe = Pipe()
        let errorPipe = Pipe()
        task.standardOutput = outputPipe
        task.standardError = errorPipe

        do {
            try task.run()

            /* 在后台线程读取输出，避免管道阻塞 */
            let outputData = outputPipe.fileHandleForReading.readDataToEndOfFile()
            let output = String(data: outputData, encoding: .utf8)?
                .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""

            task.waitUntilExit()

            return output.isEmpty ? "pid:\(pid)" : output
        } catch {
            return "pid:\(pid)"
        }
    }
}
