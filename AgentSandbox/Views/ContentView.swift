//
//  ContentView.swift
//  AgentSandbox
//
//  Created by ConradSun on 2025/3/30.
//

import SwiftUI

/// 根视图，包含 Sandbox / Logs 两个 Tab
struct ContentView: View {
    @EnvironmentObject var sandboxVM: SandboxViewModel
    @EnvironmentObject var logVM: LogViewModel
    @State private var selectedTab = 0

    var body: some View {
        TabView(selection: $selectedTab) {
            SandboxView()
                .tabItem {
                    Label(String(localized: "Sandbox"), systemImage: "shield.fill")
                }
                .tag(0)

            LogView()
                .tabItem {
                    Label(String(localized: "Logs"), systemImage: "list.bullet.rectangle")
                }
                .tag(1)
        }
    }
}

#Preview {
    ContentView()
        .environmentObject(SandboxViewModel())
        .environmentObject(LogViewModel())
}
