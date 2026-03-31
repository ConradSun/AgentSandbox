//
//  Logger.swift
//  AgentSandbox
//
//  Created by ConradSun on 2025/3/30.
//

import Foundation

/// 日志级别枚举
enum LogLevel: Sendable {
    case error, warning, info, debug

    /// 日志级别前缀
    var prefix: String {
        switch self {
        case .error:   return "ERROR"
        case .warning: return "WARN"
        case .info:    return "INFO"
        case .debug:   return "DEBUG"
        }
    }
}

/// 全局日志函数，可在任意线程调用
/// - Parameters:
///   - level: 日志级别
///   - message: 日志消息（可变参数）
///   - file: 调用文件路径（自动捕获）
///   - line: 调用行号（自动捕获）
func Logger(_ level: LogLevel, _ message: Any..., file: String = #file, line: Int = #line) {
    let currentLevel: LogLevel = .info

    let order: [LogLevel] = [.error, .warning, .info, .debug]
    guard let msgIdx = order.firstIndex(of: level),
          let curIdx = order.firstIndex(of: currentLevel),
          msgIdx <= curIdx else { return }

    let fileName = (file as NSString).lastPathComponent
    let msg = message.map { "\($0)" }.joined(separator: " ")
    print("[\(level.prefix)] \(fileName):\(line) \(msg)")
}
