# AgentSandbox

中文 | [English](README.md)

> 让 AI Agent 的每一次操作都透明可见

---

## 简介

AgentSandbox 是 macOS 应用沙箱监控工具，通过动态库注入拦截应用的文件、网络和进程操作，实时审计行为。提供可视化界面展示审计日志和沙箱管理。

---

## 架构

```
┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
┃                    AgentSandbox.app                      ┃
┃  ┌─────────────────┐        ┌─────────────────┐        ┃
┃  │  📦 沙箱管理    │        │  📋 审计日志    │        ┃
┃  │                 │        │                 │        ┃
┃  │  • 拖放应用      │        │  • 实时事件     │        ┃
┃  │  • 注入 dylib   │        │  • 过滤搜索     │        ┃
┃  │  • 进程管理      │        │  • 进程名解析   │        ┃
┃  └─────────────────┘        └─────────────────┘        ┃
┃                                                          ┃
┃           SocketServer (Unix Domain Socket)             ┃
┗━━━━━━━━━━━━━━━━━━━━━━━┳━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
                          │
                          │  /tmp/sandbox_audit.sock
                          │
┏━━━━━━━━━━━━━━━━━━━━━━━┻━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
┃                   libsandbox.dylib (C)                   ┃
┃                                                          ┃
┃  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐   ┃
┃  │  hook_file   │  │ hook_network │  │ hook_process │   ┃
┃  │              │  │              │  │              │   ┃
┃  │  open/read/  │  │ connect/     │  │ execve/      │   ┃
┃  │  write/...   │  │ send/recv    │  │ fork/...     │   ┃
┃  └──────────────┘  └──────────────┘  └──────────────┘   ┃
┃                                                          ┃
┃              socket_client → Unix Domain Socket          ┃
┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
```

### 数据流

```
┌──────────────┐
│  拖入 .app   │
└──────┬───────┘
       │
       ▼
┌──────────────────────────┐
│ 复制到沙箱磁盘            │
│ /Volumes/AgentSandbox/    │
└──────┬───────────────────┘
       │
       ▼
┌──────────────────────────┐
│ 注入 dylib               │
│ (Mach-O LC_LOAD_DYLIB)   │
└──────┬───────────────────┘
       │
       ▼
┌──────────────────────────┐
│ 启动应用                 │
└──────┬───────────────────┘
       │
       ▼
┌──────────────────────────┐
│ Hook 拦截系统调用         │
│ • FILE    文件操作        │
│ • NETWORK 网络连接        │
│ • PROCESS 进程启动        │
└──────┬───────────────────┘
       │
       ▼
┌──────────────────────────┐
│ Unix Socket 发送事件      │
└──────┬───────────────────┘
       │
       ▼
┌──────────────────────────┐
│ UI 实时显示               │
└──────────────────────────┘
```

---

## 目录结构

```
AgentSandbox/
├── AgentSandbox/              # macOS App (SwiftUI)
│   ├── App.swift              # 应用入口
│   ├── Models/                # 数据模型
│   │   ├── AuditLog.swift     # 审计日志模型
│   │   └── SandboxApp.swift   # 沙箱应用模型
│   ├── ViewModels/            # 视图模型
│   │   ├── LogViewModel.swift
│   │   └── SandboxViewModel.swift
│   ├── Views/                 # 视图层
│   │   ├── ContentView.swift
│   │   ├── SandboxView.swift
│   │   └── LogView.swift
│   ├── Services/              # 服务层
│   │   ├── SocketServer.swift # Unix Socket 服务端
│   │   ├── SandboxService.swift
│   │   └── DiskManager.swift  # 磁盘镜像管理
│   └── Utils/                 # 工具类
│       ├── Logger.swift
│       └── Repack.swift       # Mach-O 注入工具
│
├── AgentHook/                 # 动态库 (纯 C)
│   ├── common.h               # 公共定义
│   ├── socket_client.h/c      # Unix Socket IPC 客户端
│   ├── hook_file.c            # 文件操作 Hook
│   ├── hook_network.c         # 网络操作 Hook
│   └── hook_process.c         # 进程操作 Hook
│
└── AgentCommon/               # 公共定义模块
    └── common.h               # IPC 常量和宏定义
```

---

## 使用说明

### 编译 dylib

```bash
cd AgentHook
clang -dynamiclib -o libsandbox.dylib \
  hook_file.c hook_network.c hook_process.c socket_client.c \
  -arch x86_64 -arch arm64

sudo cp libsandbox.dylib /usr/local/lib/
```

### 编译 App

```bash
xcodebuild -project AgentSandbox.xcodeproj \
  -scheme AgentSandbox -configuration Debug \
  ENABLE_APP_SANDBOX=NO CODE_SIGNING_ALLOWED=NO build
```

### 快速开始

1. 启动 `AgentSandbox.app`
2. 将目标 `.app` 拖入左侧区域
3. 应用自动注入 dylib 并启动
4. 切换到 **日志** Tab 查看实时事件

---

## IPC 消息格式

```
FILE|<pid>|<timestamp>|<operation>|<path>
NETWORK|<pid>|<timestamp>|<operation>|<address>
PROCESS|<pid>|<timestamp>|<operation>|<target>
```

**示例:**

```
FILE|12345|1711852800.123456|open|/Users/test/file.txt
NETWORK|12345|1711852800.234567|connect|127.0.0.1:8080
PROCESS|12345|1711852800.345678|execve|/bin/ls
```

---

## 技术特点

### 核心技术

- **动态库注入** - 通过 Mach-O 修改实现 dylib 注入
- **系统调用拦截** - DYLD_INTERPOSE 宏拦截系统调用
- **Unix Domain Socket** - 高效的 IPC 通信机制
- **Sparse Disk Image** - 独立的沙箱磁盘环境

### 设计约束

- dylib 在进程初始化极早期加载，**不能使用任何 Swift/ObjC/Foundation API**
- IPC 客户端使用纯 POSIX C，连接失败静默重试，不阻塞宿主进程
- App 端 Socket Server 使用后台 Thread，避免 Swift 6 actor 检查崩溃

### 性能优化

- 非阻塞 connect，避免冻结宿主进程
- 连接复用（TTL 5 秒），减少连接开销
- 发送超时控制（10ms），防止长时间阻塞
- 日志缓存（5000 条上限），避免内存溢出

---

## 开发计划

- [ ] 文件写入重定向（隔离到沙箱磁盘）
- [ ] 日志持久化与导出
- [ ] 菜单栏常驻模式
- [ ] 多应用同时监控
- [ ] 事件过滤规则配置
- [ ] 告警通知机制

---

## 许可证

MIT License

---

## 作者

**ConradSun**
- GitHub: [@ConradSun](https://github.com/ConradSun)
