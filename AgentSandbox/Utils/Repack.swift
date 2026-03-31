//
//  BinaryRepack.swift
//  AgentSandbox
//
//  Created by ConradSun on 2025/3/30.
//  Mach-O 重打包 - 添加 LC_LOAD_DYLIB
//  参考 FishHook 实现
//

import Foundation
import MachO

@available(macOS 10.15, *)
class BinaryRepack {
    private let pathPadding = 8
    private var machOData = Data()
    private var binaryPath = String()
    private var dylibPath = String()
    
    // MARK: - Public

    /// 初始化二进制重打包器
    /// - Parameters:
    ///   - filePath: 目标可执行文件路径
    ///   - libPath: 要注入的 dylib 路径
    /// - Returns: 是否成功初始化
    func initWithFile(filePath: String, libPath: String) -> Bool {
        if !FileManager.default.isExecutableFile(atPath: filePath) {
            Logger(.error, "File is not executable: \(filePath)")
            return false
        }
        guard let data = FileManager.default.contents(atPath: filePath) else {
            Logger(.error, "Failed to read file: \(filePath)")
            return false
        }
        
        binaryPath = filePath
        dylibPath = libPath
        machOData = data
        return true
    }
    
    /// 重打包二进制文件 - 添加 LC_LOAD_DYLIB 命令
    /// - Returns: 是否成功重打包
    func repackBinary() -> Bool {
        if machOData.isEmpty {
            return false
        }
        
        return machOData.withUnsafeBytes { pointer in
            guard let header = pointer.bindMemory(to: fat_header.self).baseAddress else {
                Logger(.error, "Failed to get fat header pointer")
                return false
            }
            
            var result = false
            var offset = MemoryLayout<fat_header>.size
            let archNum = _OSSwapInt32(header.pointee.nfat_arch)
            
            switch header.pointee.magic {
            case FAT_MAGIC, FAT_CIGAM:
                if archNum == 0 {
                    Logger(.error, "Invalid FAT MachO format")
                    return false
                }
                
                for i in 0..<archNum {
                    if i > 0 {
                        offset += MemoryLayout<fat_arch>.size
                    }
                    result = processFatMachO(offset: offset)
                    if !result {
                        return false
                    }
                }
                
            case MH_MAGIC_64, MH_CIGAM_64, MH_MAGIC, MH_CIGAM:
                result = processThinMachO(offset: 0)
                
            default:
                Logger(.error, "Unknown MachO format: \(String(header.pointee.magic, radix: 16))")
                return false
            }
            
            signAdhoc()
            return result
        }
    }
    
    // MARK: - Private

    /// 对二进制文件进行 Ad-hoc 代码签名
    private func signAdhoc() {
        let task = Process()
        task.executableURL = URL(fileURLWithPath: "/usr/bin/codesign")
        task.arguments = ["-f", "-s", "-", binaryPath]
        try? task.run()
    }
    
    /// 获取段命令结构
    /// - Parameter data: 二进制数据
    /// - Returns: 段命令结构
    private func getSegmentCommand(data: Data) -> segment_command_64? {
        return data.withUnsafeBytes { pointer in
            guard let segCmd = pointer.bindMemory(to: segment_command_64.self).baseAddress else {
                Logger(.error, "Failed to get segment command pointer")
                return nil
            }
            return segCmd.pointee
        }
    }
    
    /// 获取节命令结构
    /// - Parameter data: 二进制数据
    /// - Returns: 节命令结构
    private func getSectionCommand(data: Data) -> section_64? {
        return data.withUnsafeBytes { pointer in
            guard let sectCmd = pointer.bindMemory(to: section_64.self).baseAddress else {
                Logger(.error, "Failed to get section pointer")
                return nil
            }
            return sectCmd.pointee
        }
    }
    
    /// 检查是否有足够的空间注入 dylib 命令
    /// - Parameters:
    ///   - header: Mach-O 头
    ///   - offset: 偏移量
    ///   - is64bit: 是否为 64 位
    /// - Returns: 是否有足够空间
    private func isSpaceEnough(header: mach_header, offset: Int, is64bit: Bool) -> Bool {
        let pathSize = (dylibPath.count & ~(pathPadding - 1)) + pathPadding
        let injectSpace = MemoryLayout<dylib_command>.size + pathSize
        let headerSize = is64bit ? MemoryLayout<mach_header_64>.size : MemoryLayout<mach_header>.size
        
        var segOffset = offset
        for _ in 0..<header.ncmds {
            let segData = machOData.subdata(in: segOffset..<segOffset+MemoryLayout<segment_command_64>.size)
            guard let segCmd = getSegmentCommand(data: segData) else {
                Logger(.error, "Failed to get segment command")
                return false
            }
            
            var segName = segCmd.segname
            if strncmp(&segName.0, "__TEXT", 15) == 0 {
                for i in 0..<segCmd.nsects {
                    let sectOffset = segOffset + MemoryLayout<segment_command_64>.size + MemoryLayout<section_64>.size * Int(i)
                    let sectData = machOData.subdata(in: sectOffset..<sectOffset+MemoryLayout<section_64>.size)
                    guard let sectCmd = getSectionCommand(data: sectData) else {
                        Logger(.error, "Failed to get section")
                        return false
                    }
                    
                    var sectName = sectCmd.sectname
                    if strncmp(&sectName.0, "__text", 15) == 0 {
                        let space = sectCmd.offset - header.sizeofcmds - UInt32(headerSize)
                        Logger(.info, "Available space: \(space) bytes, needed: \(injectSpace) bytes")
                        return space >= injectSpace
                    }
                }
            }
            
            segOffset += Int(segCmd.cmdsize)
        }
        return false
    }
    
    /// 注入 dylib 命令到二进制文件
    /// - Parameters:
    ///   - header: Mach-O 头
    ///   - offset: 偏移量
    ///   - is64bit: 是否为 64 位
    /// - Returns: 是否成功注入
    private func injectDylib(header: mach_header, offset: UInt64, is64bit: Bool) -> Bool {
        guard let fileHandle = FileHandle(forWritingAtPath: binaryPath) else {
            Logger(.error, "Failed to create handler for binary file")
            return false
        }
        
        let pathSize = (dylibPath.count & ~(pathPadding - 1)) + pathPadding
        let cmdSize = MemoryLayout<dylib_command>.size + pathSize
        var cmdOffset: UInt64 = 0
        var dylibCmd = dylib_command()
        
        if is64bit {
            cmdOffset = offset + UInt64(MemoryLayout<mach_header_64>.size)
        } else {
            cmdOffset = offset + UInt64(MemoryLayout<mach_header>.size)
        }
                
        if !isSpaceEnough(header: header, offset: Int(cmdOffset), is64bit: is64bit) {
            Logger(.error, "No space for adding command")
            return false
        }
        
        Logger(.info, "Space check passed, writing...")
        
        dylibCmd.cmd = UInt32(LC_LOAD_DYLIB)
        dylibCmd.cmdsize = UInt32(cmdSize)
        dylibCmd.dylib.name = lc_str(offset: UInt32(MemoryLayout<dylib_command>.size))
        
        try? fileHandle.seek(toOffset: cmdOffset + UInt64(header.sizeofcmds))
        fileHandle.write(Data(bytes: &dylibCmd, count: MemoryLayout<dylib_command>.size))
        fileHandle.write(dylibPath.data(using: .utf8)!)
        
        var newHeader = header
        newHeader.ncmds = newHeader.ncmds + 1
        newHeader.sizeofcmds = newHeader.sizeofcmds + UInt32(cmdSize)
        try? fileHandle.seek(toOffset: offset)
        fileHandle.write(Data(bytes: &newHeader, count: MemoryLayout<mach_header>.size))
        
        try? fileHandle.close()
        
        Logger(.info, "Injection complete")
        return true
    }
    
    /// 处理瘦 Mach-O 文件
    /// - Parameter offset: 文件偏移量
    /// - Returns: 是否成功处理
    private func processThinMachO(offset: Int) -> Bool {
        let thinData = machOData.subdata(in: offset..<offset+MemoryLayout<mach_header>.size)
        return thinData.withUnsafeBytes { pointer in
            guard let header = pointer.bindMemory(to: mach_header.self).baseAddress else {
                Logger(.error, "Failed to get mach header pointer")
                return false
            }
            
            switch header.pointee.magic {
            case MH_MAGIC_64, MH_CIGAM_64:
                Logger(.info, "64-bit Mach-O detected")
                return injectDylib(header: header.pointee, offset: UInt64(offset), is64bit: true)
            case MH_MAGIC, MH_CIGAM:
                Logger(.info, "32-bit Mach-O detected")
                return injectDylib(header: header.pointee, offset: UInt64(offset), is64bit: false)
            default:
                Logger(.error, "Unknown MachO format: \(String(header.pointee.magic, radix: 16))")
                return false
            }
        }
    }
    
    /// 处理 FAT Mach-O 文件
    /// - Parameter offset: 文件偏移量
    /// - Returns: 是否成功处理
    private func processFatMachO(offset: Int) -> Bool {
        let fatData = machOData.subdata(in: offset..<offset+MemoryLayout<fat_arch>.size)
        return fatData.withUnsafeBytes { pointer in
            guard let arch = pointer.bindMemory(to: fat_arch.self).baseAddress else {
                Logger(.error, "Failed to get fat arch pointer")
                return false
            }
            
            let archOffset = _OSSwapInt32(arch.pointee.offset)
            return processThinMachO(offset: Int(archOffset))
        }
    }
}
