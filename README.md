# CrossSCP

跨平台 SFTP 文件传输客户端，基于 C++ 原生核心 + Flutter UI + dart:ffi 桥接架构，性能对标 WinSCP。

## 特性

- **高性能传输** — C++ + libssh2 原生引擎，流式零拷贝，256KB 块 + 32 并发 SFTP 请求
- **全平台覆盖** — Windows / macOS / Linux / Android / iOS
- **双栏文件浏览器** — 本地 + 远程目录并列操作
- **完整文件操作** — 上传/下载、断点续传、删除、重命名、权限修改
- **站点管理** — 密码/私钥/无认证面板服全支持
- **Fluent Design** — Windows 风格专业桌面界面，内置简体中文

## 构建

```bash
# 安装依赖 (Arch Linux)
sudo pacman -S libssh2 openssl cmake clang pkgconf

# 编译原生核心库
./native/build_scripts/build_desktop.sh release

# Flutter 依赖 + 构建
flutter pub get
flutter build linux    # 或 windows / macos
```

## 架构

```
┌─────────────────────────────────────────┐
│  Flutter UI (fluent_ui + Material)      │
│  Site Manager · File Browser · Transfer │
├─────────────────────────────────────────┤
│  Dart FFI Bridge (Isolate 隔离)         │
│  scp_client.dart · scp_bindings.dart    │
├─────────────────────────────────────────┤
│  C++ Native Core (libcrossscp.so)       │
│  ssh_connection · sftp_transfer         │
│  256KB block · 32 concurrent · resume   │
├─────────────────────────────────────────┤
│  libssh2 + OpenSSL / mbedTLS            │
└─────────────────────────────────────────┘
```

## 技术栈

| 层 | 技术 |
|----|------|
| UI | Flutter 3.x + fluent_ui |
| 桥接 | dart:ffi + Isolate |
| 核心 | C++17 + libssh2 |
| 加密 | OpenSSL (桌面) / mbedTLS (移动) |
| 构建 | CMake + NDK (Android) + Xcode (iOS) |

## 许可证

[Apache License 2.0](LICENSE)

© 2026 CrossSCP Contributors
