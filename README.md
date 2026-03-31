# AgentSandbox

[中文文档](README_CN.md) | English

> Make every AI Agent operation visible

---

## Introduction

AgentSandbox monitors macOS app sandboxes by intercepting file, network, and process operations through dynamic library injection. It provides real-time audit logs and sandbox management with a visual interface.

---

## Architecture

```
┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
┃                    AgentSandbox.app                      ┃
┃  ┌─────────────────┐        ┌─────────────────┐        ┃
┃  │  📦 Sandbox     │        │  📋 Logs        │        ┃
┃  │                 │        │                 │        ┃
┃  │  • Drag & Drop  │        │  • Real-time    │        ┃
┃  │  • Inject dylib │        │  • Filter       │        ┃
┃  │  • Manage Apps  │        │  • Search       │        ┃
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

### Data Flow

```
┌──────────────┐
│  Drop .app   │
└──────┬───────┘
       │
       ▼
┌──────────────────────────┐
│ Copy to Sandbox Disk     │
│ /Volumes/AgentSandbox/    │
└──────┬───────────────────┘
       │
       ▼
┌──────────────────────────┐
│ Inject dylib             │
│ (Mach-O LC_LOAD_DYLIB)   │
└──────┬───────────────────┘
       │
       ▼
┌──────────────────────────┐
│ Launch Application       │
└──────┬───────────────────┘
       │
       ▼
┌──────────────────────────┐
│ Hook System Calls         │
│ • FILE    Operations      │
│ • NETWORK Connections     │
│ • PROCESS Spawns          │
└──────┬───────────────────┘
       │
       ▼
┌──────────────────────────┐
│ Unix Socket Events        │
└──────┬───────────────────┘
       │
       ▼
┌──────────────────────────┐
│ UI Real-time Display      │
└──────────────────────────┘
```

---

## Project Structure

```
AgentSandbox/
├── AgentSandbox/              # macOS App (SwiftUI)
│   ├── App.swift              # App Entry
│   ├── Models/                # Data Models
│   │   ├── AuditLog.swift     # Audit Log Model
│   │   └── SandboxApp.swift   # Sandbox App Model
│   ├── ViewModels/            # View Models
│   │   ├── LogViewModel.swift
│   │   └── SandboxViewModel.swift
│   ├── Views/                 # Views
│   │   ├── ContentView.swift
│   │   ├── SandboxView.swift
│   │   └── LogView.swift
│   ├── Services/              # Services
│   │   ├── SocketServer.swift # Unix Socket Server
│   │   ├── SandboxService.swift
│   │   └── DiskManager.swift  # Disk Image Manager
│   └── Utils/                 # Utilities
│       ├── Logger.swift
│       └── Repack.swift       # Mach-O Injector
│
├── AgentHook/                 # Dynamic Library (Pure C)
│   ├── common.h               # Common Definitions
│   ├── socket_client.h/c      # Unix Socket IPC Client
│   ├── hook_file.c            # File Operations Hook
│   ├── hook_network.c         # Network Operations Hook
│   └── hook_process.c         # Process Operations Hook
│
└── AgentCommon/               # Common Module
    └── common.h               # IPC Constants & Macros
```

---

## Usage

### Build dylib

```bash
cd AgentHook
clang -dynamiclib -o libsandbox.dylib \
  hook_file.c hook_network.c hook_process.c socket_client.c \
  -arch x86_64 -arch arm64

sudo cp libsandbox.dylib /usr/local/lib/
```

### Build App

```bash
xcodebuild -project AgentSandbox.xcodeproj \
  -scheme AgentSandbox -configuration Debug \
  ENABLE_APP_SANDBOX=NO CODE_SIGNING_ALLOWED=NO build
```

### Quick Start

1. Launch `AgentSandbox.app`
2. Drop target `.app` to left panel
3. App auto-injects dylib and launches
4. Switch to **Logs** Tab to view real-time events

---

## IPC Message Format

```
FILE|<pid>|<timestamp>|<operation>|<path>
NETWORK|<pid>|<timestamp>|<operation>|<address>
PROCESS|<pid>|<timestamp>|<operation>|<target>
```

**Example:**

```
FILE|12345|1711852800.123456|open|/Users/test/file.txt
NETWORK|12345|1711852800.234567|connect|127.0.0.1:8080
PROCESS|12345|1711852800.345678|execve|/bin/ls
```

---

## Technical Features

### Core Technologies

- **Dynamic Library Injection** - Mach-O modification for dylib injection
- **System Call Interception** - DYLD_INTERPOSE macro for hooking
- **Unix Domain Socket** - Efficient IPC mechanism
- **Sparse Disk Image** - Isolated sandbox environment

### Design Constraints

- dylib loads extremely early (before Swift/ObjC/Foundation)
- IPC client uses pure POSIX C, silent retry on failure
- Socket Server uses background Thread to avoid Swift 6 actor issues

### Performance Optimizations

- Non-blocking connect to avoid freezing host process
- Connection reuse (5s TTL) to reduce overhead
- Send timeout (10ms) to prevent blocking
- Log cache (5000 limit) to avoid memory overflow

---

## Roadmap

- [ ] File write redirection (isolate to sandbox disk)
- [ ] Log persistence & export
- [ ] Menu bar resident mode
- [ ] Multi-app simultaneous monitoring
- [ ] Event filtering rules
- [ ] Alert notifications

---

## License

MIT License

---

## Author

**ConradSun**
- GitHub: [@ConradSun](https://github.com/ConradSun)
