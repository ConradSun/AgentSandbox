//
//  SocketServer.swift
//  AgentSandbox
//
//  Created by ConradSun on 2025/3/30.
//

import Foundation

/// Unix Domain Socket Server，接收 dylib 发送的审计事件
class SocketServer: @unchecked Sendable {
    static let shared = SocketServer()

    private var serverFd: Int32 = -1
    private var receiveThread: Thread?
    private var isRunning = false
    private var isReady = false
    private let readySemaphore = DispatchSemaphore(value: 0)

    /// 收到事件时的回调：(type, operation, detail, timestamp, pid)
    var onEventReceived: ((Int, String, String, Double, Int32) -> Void)?

    private init() {}

    // MARK: - Public

    /// 启动 Socket Server，在后台线程监听连接
    func start() {
        guard !isRunning, serverFd < 0 else { return }

        unlink(SANDBOX_SOCK_PATH)

        serverFd = socket(AF_UNIX, SOCK_STREAM, 0)
        guard serverFd >= 0 else {
            Logger(.error, "Failed to create socket: \(errno)")
            return
        }

        var addr = sockaddr_un()
        addr.sun_family = sa_family_t(AF_UNIX)
        SANDBOX_SOCK_PATH.withCString { ptr in
            withUnsafeMutableBytes(of: &addr.sun_path) { dest in
                if let destPtr = dest.baseAddress {
                    strncpy(destPtr.assumingMemoryBound(to: CChar.self), ptr, dest.count - 1)
                }
            }
        }

        let bindResult: Int32 = withUnsafePointer(to: &addr) { ptr in
            ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sockaddrPtr in
                bind(serverFd, sockaddrPtr, socklen_t(MemoryLayout<sockaddr_un>.size))
            }
        }

        guard bindResult == 0 else {
            Logger(.error, "Failed to bind socket: \(errno)")
            close(serverFd)
            serverFd = -1
            return
        }

        chmod(SANDBOX_SOCK_PATH, 0o666)

        guard listen(serverFd, 10) == 0 else {
            Logger(.error, "Failed to listen: \(errno)")
            close(serverFd)
            serverFd = -1
            return
        }

        isRunning = true
        Logger(.info, "Socket server started: \(SANDBOX_SOCK_PATH)")

        receiveThread = Thread { [weak self] in
            self?.isReady = true
            self?.readySemaphore.signal()
            self?.acceptLoop()
        }
        receiveThread?.name = "SocketAcceptThread"
        receiveThread?.qualityOfService = .utility
        receiveThread?.start()
    }

    /// 等待 Server 就绪，超时返回 false
    /// - Parameter timeout: 超时时间（秒）
    /// - Returns: 是否成功就绪
    func waitForReady(timeout: TimeInterval = 5.0) -> Bool {
        readySemaphore.wait(timeout: .now() + timeout) == .success
    }

    /// 停止 Server
    func stop() {
        isRunning = false
        isReady = false
        // 关闭监听 fd 前先发送 shutdown，防止 accept() 永远阻塞
        if serverFd >= 0 {
            shutdown(serverFd, SHUT_RDWR)
            close(serverFd)
            serverFd = -1
        }
        unlink(SANDBOX_SOCK_PATH)
        Logger(.info, "Socket server stopped")
    }

    // MARK: - Private

    /// 循环接受客户端连接
    private func acceptLoop() {
        while isRunning {
            var clientAddr = sockaddr_un()
            var addrLen = socklen_t(MemoryLayout<sockaddr_un>.size)

            let clientFd = withUnsafeMutablePointer(to: &clientAddr) { ptr in
                ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sockaddrPtr in
                    accept(serverFd, sockaddrPtr, &addrLen)
                }
            }

            guard clientFd >= 0 else {
                if errno == EINTR { continue }
                Thread.sleep(forTimeInterval: 0.1)
                continue
            }

            Thread { [weak self] in
                self?.handleClient(clientFd)
            }.start()
        }
    }

    /// 读取单个客户端的数据，按行解析事件
    /// - Parameter clientFd: 客户端文件描述符
    private func handleClient(_ clientFd: Int32) {
        var buffer = [CChar](repeating: 0, count: Int(SANDBOX_MSG_MAX_LEN))

        while isRunning {
            var readFds = fd_set()
            var timeout = timeval(tv_sec: 1, tv_usec: 0)

            // 手动清零 + 设置 fd 位（Swift 无法直接用 FD_ZERO/FD_SET 宏）
            withUnsafeMutablePointer(to: &readFds) { ptr in
                memset(ptr, 0, MemoryLayout<fd_set>.size)
                ptr.withMemoryRebound(to: Int32.self, capacity: MemoryLayout<fd_set>.size / MemoryLayout<Int32>.size) { fdsPtr in
                    let word = Int(clientFd / 32)
                    let bit = Int32(1 << (clientFd % 32))
                    fdsPtr[word] |= bit
                }
            }

            let ret = select(clientFd + 1, &readFds, nil, nil, &timeout)
            if ret <= 0 { continue }

            let bytesRead = read(clientFd, &buffer, Int(SANDBOX_MSG_MAX_LEN))
            if bytesRead <= 0 { break }

            let data = Data(bytes: buffer, count: bytesRead)
            if let message = String(data: data, encoding: .utf8) {
                for line in message.components(separatedBy: "\n") where !line.isEmpty {
                    parseAndDispatch(line)
                }
            }
        }

        close(clientFd)
    }

    /// 解析消息行并回调主线程
    /// - Parameter message: 消息字符串
    private func parseAndDispatch(_ message: String) {
        // 格式：TYPE|pid|timestamp|operation|detail
        let parts = message.components(separatedBy: "|")
        guard parts.count >= 5,
              let pid = Int32(parts[1]),
              let timestamp = Double(parts[2]) else {
            Logger(.warning, "Invalid message format: \(message)")
            return
        }

        let type: Int
        switch parts[0] {
        case "FILE":    type = 1
        case "NETWORK": type = 2
        case "PROCESS": type = 3
        default:        type = 0
        }

        DispatchQueue.main.async { [weak self] in
            self?.onEventReceived?(type, parts[3], parts[4], timestamp, pid)
        }
    }
}
