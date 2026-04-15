//
//  LogView.swift
//  AgentSandbox
//
//  Created by ConradSun on 2025/3/30.
//

import SwiftUI

/// 日志视图列宽配置（表头与行共享）
private enum LogColumn {
    static let typeWidth: CGFloat = 50
    static let timeWidth: CGFloat = 70
    static let pidWidth: CGFloat = 50
    static let processWidth: CGFloat = 100
    static let opWidth: CGFloat = 70
}

/// 审计日志视图：搜索过滤 + 实时滚动列表
struct LogView: View {
    @EnvironmentObject var vm: LogViewModel

    var body: some View {
        VStack(spacing: 0) {
            // Toolbar
            HStack(spacing: 12) {
                Image(systemName: "magnifyingglass")
                    .foregroundColor(.secondary)

                TextField(String(localized: "Search..."), text: $vm.searchText)
                    .textFieldStyle(.plain)

                Picker("", selection: $vm.filterType) {
                    Text(String(localized: "All")).tag(AuditLog.LogType?.none)
                    ForEach(AuditLog.LogType.allCases, id: \.self) { type in
                        Text(type.localizedString).tag(AuditLog.LogType?.some(type))
                    }
                }
                .frame(width: 100)

                Spacer()

                Text(String(localized: "\(vm.getFilteredLogs().count) logs"))
                    .foregroundColor(.secondary)

                Button(action: { vm.clear() }) {
                    Image(systemName: "trash")
                }
                .buttonStyle(.plain)
                .disabled(vm.logs.isEmpty)
            }
            .padding()
            .background(Color.primary.opacity(0.05))

            Divider()

            // Log list
            if vm.getFilteredLogs().isEmpty {
                VStack(spacing: 8) {
                    Image(systemName: "doc.text.magnifyingglass")
                        .font(.system(size: 48))
                        .foregroundColor(.secondary.opacity(0.5))
                    Text(String(localized: "No logs"))
                        .foregroundColor(.secondary)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                // 表头
                HStack(spacing: 8) {
                    Text("Type")
                        .font(.system(size: 10, weight: .semibold))
                        .foregroundColor(.secondary)
                        .frame(width: LogColumn.typeWidth, alignment: .leading)
                    Text("Time")
                        .font(.system(size: 10, weight: .semibold))
                        .foregroundColor(.secondary)
                        .frame(width: LogColumn.timeWidth, alignment: .leading)
                    Text("PID")
                        .font(.system(size: 10, weight: .semibold))
                        .foregroundColor(.secondary)
                        .frame(width: LogColumn.pidWidth, alignment: .trailing)
                    Text("Process")
                        .font(.system(size: 10, weight: .semibold))
                        .foregroundColor(.secondary)
                        .frame(width: LogColumn.processWidth, alignment: .leading)
                    Text("Operation")
                        .font(.system(size: 10, weight: .semibold))
                        .foregroundColor(.secondary)
                        .frame(width: LogColumn.opWidth, alignment: .leading)
                    Text("Detail")
                        .font(.system(size: 10, weight: .semibold))
                        .foregroundColor(.secondary)
                    Spacer()
                }
                .padding(.horizontal, 16)
                .padding(.vertical, 4)
                .background(Color.primary.opacity(0.03))
                
                Divider()
                
                ScrollViewReader { proxy in
                    ScrollView {
                        LazyVStack(spacing: 2) {
                            ForEach(vm.getFilteredLogs()) { log in
                                LogRowView(log: log, processName: vm.getProcessName(pid: log.processId))
                                    .id(log.id)
                            }
                        }
                        .padding(8)
                    }
                    .onChange(of: vm.logs.count) { newCount in
                        // 自动滚动到最新
                        if let lastLog = vm.logs.last {
                            proxy.scrollTo(lastLog.id, anchor: .bottom)
                        }
                    }
                }
            }
        }
    }
}

// MARK: - LogRowView

/// 日志行视图
struct LogRowView: View {
    /// 审计日志
    let log: AuditLog
    /// 进程名称
    let processName: String

    /// 全局共享的时间格式化器，避免每行重建
    private static let timeFormatter: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss"
        return f
    }()

    var body: some View {
        HStack(spacing: 8) {
            // Type icon + label
            HStack(spacing: 4) {
                Image(systemName: log.type.icon)
                    .font(.caption)
                    .foregroundColor(typeColor)
                Text(log.type.localizedString)
                    .font(.system(size: 10))
                    .foregroundColor(typeColor)
            }
            .frame(width: LogColumn.typeWidth, alignment: .leading)

            // Time
            Text(formatTime(log.timestamp))
                .font(.system(size: 11, design: .monospaced))
                .foregroundColor(.secondary)
                .frame(width: LogColumn.timeWidth, alignment: .leading)

            // PID
            Text("\(log.processId)")
                .font(.system(size: 11, design: .monospaced))
                .foregroundColor(.secondary)
                .frame(width: LogColumn.pidWidth, alignment: .trailing)

            // Process Name
            Text(processName)
                .font(.system(size: 11))
                .foregroundColor(.primary)
                .lineLimit(1)
                .frame(width: LogColumn.processWidth, alignment: .leading)

            // Operation
            Text(log.operation)
                .font(.system(size: 12, weight: .medium))
                .frame(width: LogColumn.opWidth, alignment: .leading)

            // Path/Detail
            Text(log.path ?? log.detail ?? "")
                .font(.system(size: 11, design: .monospaced))
                .foregroundColor(.secondary)
                .lineLimit(1)
                .truncationMode(.middle)

            Spacer()
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 6)
        .background(Color.primary.opacity(0.02))
        .cornerRadius(4)
    }

    /// 根据日志类型返回对应的颜色
    var typeColor: Color {
        switch log.type {
        case .file: return .green
        case .network: return .blue
        case .process: return .orange
        }
    }

    /// 格式化时间戳
    /// - Parameter date: 日期
    /// - Returns: 格式化后的时间字符串
    func formatTime(_ date: Date) -> String {
        Self.timeFormatter.string(from: date)
    }
}

#Preview {
    LogView()
        .environmentObject(LogViewModel())
}
