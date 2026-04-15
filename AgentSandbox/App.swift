//
//  App.swift
//  AgentSandbox
//
//  Created by ConradSun on 2025/3/30.
//

import SwiftUI

/// 应用入口，初始化 Socket Server 并注入事件回调
@main
struct AgentSandboxApp: App {
    @StateObject private var sandboxVM = SandboxViewModel()
    @StateObject private var logVM = LogViewModel()

    init() {
        SocketServer.shared.start()

        if !SocketServer.shared.waitForReady(timeout: 5.0) {
            Logger(.error, "Socket server failed to start")
        }

        SocketServer.shared.onEventReceived = { type, operation, detail, timestamp, pid in
            let logType: AuditLog.LogType
            switch type {
            case 1:  logType = .file
            case 2:  logType = .network
            case 3:  logType = .process
            default: logType = .file
            }

            let log = AuditLog(
                timestamp: Date(timeIntervalSince1970: timestamp),
                processId: pid,
                type: logType,
                operation: operation,
                path: detail,
                detail: nil,
                result: 0
            )

            NotificationCenter.default.post(
                name: .sandboxEventReceived,
                object: nil,
                userInfo: ["log": log]
            )
        }
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(sandboxVM)
                .environmentObject(logVM)
                .onReceive(NotificationCenter.default.publisher(for: .sandboxEventReceived)) { notification in
                    if let log = notification.userInfo?["log"] as? AuditLog {
                        logVM.addLog(log)
                    }
                }
        }
        .windowStyle(.hiddenTitleBar)
    }
}

// MARK: - Notification

extension Notification.Name {
    /// 沙箱事件接收通知
    static let sandboxEventReceived = Notification.Name("sandboxEventReceived")
}
