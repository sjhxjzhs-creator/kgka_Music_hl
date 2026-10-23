# kgka_Music_hl — Windows x86_64 Build

## 运行要求

- Windows 10/11 x86_64
- 无需安装 Flutter 或 Dart

## 运行方式

1. 解压 `kgka_Music_hl_windows.zip`
2. 双击 `kgka_music.exe`

## 文件说明

```
kgka_music.exe        主程序（Flutter Embedder Runner）
flutter_engine.dll    Flutter Engine（Python Embedder）
libgcc_s_seh-1.dll    MinGW 运行时
libstdc++-6.dll       MinGW C++ 运行时
libwinpthread-1.dll   MinGW 线程运行时
data/
  icudtl.dat          ICU 数据
  flutter_assets/
    kernel_blob.bin    Dart 内核（JIT）
    isolate_snapshot_data
    vm_snapshot_data
    AssetManifest.bin / FontManifest.json / NOTICES.Z
    fonts/  shaders/  lib/  packages/  native_assets/
```

## 技术架构

- **Runner**：最小 C++ Win32 程序，使用 Flutter Embedder C API
- **渲染**：软件渲染（`SoftwareSurfacePresent` 回调 → `StretchDIBits`）
- **引擎链接**：动态加载（`LoadLibrary` + `GetProcAddress`，不静态链接）
- **交叉编译**：Linux ARM64 上 `x86_64-w64-mingw32-g++`（MinGW）
- **Flutter 模式**：Debug（`kernel_blob.bin` JIT 解释执行）
- **运行时路径**：exe 所在目录的 `data/flutter_assets` 与 `data/icudtl.dat`

## 构建命令

```sh
# 1. 解析 Dart 依赖
flutter pub get

# 2. 编译 Dart → kernel_blob.bin（平台无关）
flutter build bundle --debug

# 3. 交叉编译 C++ Runner → .exe
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -DUNICODE -D_UNICODE \
  -I windows_engine/ -o kgka_music.exe runner_embedder.cpp \
  -mwindows -luser32 -lgdi32 -lkernel32
```

## 文件清单（产物目录）

| 文件 | 大小 | 说明 |
|------|------|------|
| `kgka_music.exe` | 268K | Win32 Runner |
| `flutter_engine.dll` | 37M | Flutter Engine |
| `libgcc_s_seh-1.dll` | 772K | MinGW 运行时 |
| `libstdc++-6.dll` | 26M | MinGW C++ 运行时 |
| `libwinpthread-1.dll` | 317K | MinGW 线程 RTL |
| `data/icudtl.dat` | 845K | ICU 数据 |
| `data/flutter_assets/` | ~98M | Dart kernel + assets + fonts + shaders |