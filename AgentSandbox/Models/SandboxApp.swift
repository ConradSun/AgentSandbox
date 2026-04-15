//
//  SandboxApp.swift
//  AgentSandbox
//
//  Created by ConradSun on 2025/3/30.
//

import Foundation
import AppKit
import Combine

/// 沙箱应用数据模型
class SandboxApp: ObservableObject, Identifiable, @unchecked Sendable {
    let id = UUID()
    /// 应用名称
    let name: String
    /// 原始 .app 路径（从外部拖入时设置）
    let bundlePath: URL?
    /// 沙箱磁盘内的 .app 路径
    @Published var sandboxPath: URL?
    /// 应用图标
    let icon: NSImage?
    /// 应用运行状态
    @Published var status: Status = .pending
    /// 应用进程 ID
    @Published var pid: Int32?
    /// 启动时的可执行文件路径（用于校验 PID 是否被回收）
    var expectedExecPath: String?

    /// 应用是否已在沙箱中
    var isInSandbox: Bool { sandboxPath != nil }

    /// 初始化沙箱应用
    /// - Parameters:
    ///   - name: 应用名称
    ///   - bundlePath: 原始 .app 路径
    ///   - sandboxPath: 沙箱内 .app 路径
    ///   - icon: 应用图标
    init(name: String, bundlePath: URL?, sandboxPath: URL?, icon: NSImage?) {
        self.name = name
        self.bundlePath = bundlePath
        self.sandboxPath = sandboxPath
        self.icon = icon
    }

    /// 应用运行状态枚举
    enum Status: String {
        case pending, injecting, running, stopped, failed

        /// 本地化状态字符串
        var localizedString: String {
            switch self {
            case .pending:   return "Pending"
            case .injecting: return "Injecting..."
            case .running:   return "Running"
            case .stopped:   return "Stopped"
            case .failed:    return "Failed"
            }
        }
    }
}
