//
//  AuditLog.swift
//  AgentSandbox
//
//  Created by ConradSun on 2025/3/30.
//

import Foundation

/// 审计日志数据模型
struct AuditLog: Identifiable {
    let id = UUID()
    /// 日志时间戳
    let timestamp: Date
    /// 进程 ID
    let processId: Int32
    /// 进程名称
    let processName: String
    /// 日志类型（文件、网络、进程）
    let type: LogType
    /// 操作类型
    let operation: String
    /// 操作路径
    let path: String?
    /// 操作详情
    let detail: String?
    /// 操作结果代码
    let result: Int32

    /// 日志类型枚举
    enum LogType: String, CaseIterable {
        case file, network, process

        /// 本地化日志类型字符串
        var localizedString: String {
            switch self {
            case .file:    return "File"
            case .network: return "Network"
            case .process: return "Process"
            }
        }

        /// 日志类型对应的 SF Symbol 图标
        var icon: String {
            switch self {
            case .file:    return "doc.fill"
            case .network: return "network"
            case .process: return "cpu"
            }
        }
    }
}
