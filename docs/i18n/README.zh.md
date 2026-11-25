# GPU Report

**GPU Report** 是一个 Linux 命令行诊断工具。它检测显卡适配器，诊断硬件视频
解码/编码能力，使用 ncursesw 显示本地化 TUI 或纯文本报告，推荐 FFmpeg 设置，
并导出与界面语言无关的 `gpu_info.xml`。

核心原则是 **可靠性优先于乐观猜测**：

- `YES` — 已由诊断来源确认支持。
- `NO` — 已检查并确认不支持。
- `UNKNOWN` — 无法确认；绝不会被替换为 `NO`。
- GPU、驱动或 FFmpeg 构建中的编码器存在，并不证明该 GPU 支持相应编解码器。

## 功能

- 通过 sysfs/PCI 检测 Intel、AMD、NVIDIA 及其他显示适配器。
- 支持混合显卡和多 GPU 系统。
- 显示名称、Vendor/Device ID、PCI 路径、驱动、驱动版本、DRM 卡、
  render node、适配器类型和检测状态。
- 独立诊断 H.264/AVC、HEVC/H.265、VP8、VP9、AV1、MPEG-2、VC-1、
  JPEG/MJPEG 的解码和编码。
- 在已知时记录配置文件、位深、像素格式和确认来源。
- FFmpeg 构建公布的格式会单独标记为“未针对具体 GPU 验证”，不会并入已确认的
  硬件能力。
- Intel/AMD 使用 VA-API（`vainfo`）作为权威依据。
- NVIDIA 使用 NVENC/NVDEC 世代表、`nvidia-smi` 和 PCI 芯片名称，
  不执行实际编码测试；`Encoder/Decoder: N/A` 的硬件特定结果优先于世代表。
- Vulkan 仅用于识别和 Vulkan Video 扩展证据。
- 仅对已确认的编码器给出 FFmpeg 推荐。
- 六种界面语言：英语、俄语、日语、韩语、简体中文、西班牙语。
- ncursesw TUI、自适应文本表格和 XML 导出。
- 不需要 root 权限，不修改驱动或系统设置。

## 依赖

构建必需：

- C++17 编译器（GCC 或 Clang）
- CMake 3.16 或更高
- pkg-config
- ncursesw 开发文件
- libxml2 开发文件

可选诊断工具：`vainfo`、`vulkaninfo`、`ffmpeg`、`nvidia-smi`、
`pci.ids`（通常来自 `hwdata` 包）。缺失时程序不会崩溃。

## 构建

### Arch Linux

```bash
sudo pacman -S --needed base-devel cmake pkgconf ncurses libxml2 hwdata
sudo pacman -S --needed ffmpeg libva-utils vulkan-tools   # 可选
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

### Debian / Ubuntu

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libncursesw5-dev libxml2-dev
sudo apt install -y ffmpeg vainfo vulkan-tools hwdata   # 可选
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

### Fedora

```bash
sudo dnf install -y gcc-c++ cmake pkgconf-pkg-config ncurses-devel libxml2-devel hwdata
sudo dnf install -y ffmpeg libva-utils vulkan-tools   # 可选
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

安装：

```bash
sudo cmake --install build
```

## 用法

```bash
gpu-report                         # 帮助
gpu-report --help
gpu-report --version
gpu-report --list-gpu              # 列出适配器，不执行完整编解码诊断
gpu-report --get-report            # 完整诊断（交互终端中打开 TUI）
gpu-report --get-report --no-tui   # 纯文本
```

选择 GPU：

```bash
gpu-report --get-report --set-gpu 0
gpu-report --get-report --set-gpu 0000:01:00.0
gpu-report --get-report --set-gpu 10de:1341
```

语言：

```bash
gpu-report --get-report --set-local=english
gpu-report --get-report --set-local=russian
gpu-report --get-report --set-local=japanese
gpu-report --get-report --set-local=korean
gpu-report --get-report --set-local=chinese
gpu-report --get-report --set-local=spanish
```

优先级：显式 `--set-local`，然后 `LC_ALL` / `LC_MESSAGES` / `LANG`，
最后英语回退。不支持的显式语言会给出警告并回退到英语。

XML 导出：

```bash
gpu-report --get-report --export gpu_info.xml
gpu-report --get-report --set-gpu 0 --export /tmp/gpu_info.xml
```

导出时先写临时文件，用 libxml2 重新解析验证，然后原子重命名。失败时不会
破坏已有的有效文件。

## TUI 按键

| 按键 | 操作 |
|---|---|
| 左 / 右、Tab | 切换 GPU |
| 上 / 下、PgUp / PgDn、Home / End | 滚动 |
| `2` 或 `d` | 诊断详情；再次按下返回概览 |
| `3` 或 `r` | 完整报告；再次按下返回概览 |
| `1`、`o`、`b`、Backspace 或 Esc | 返回概览 |
| `e` | 导出 `gpu_info.xml` |
| `4`、`?` 或 F1 | 帮助；再次按下返回概览 |
| `q` | 退出 |

## 测试

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

测试覆盖本地化、XML 与 UTF-8、GPU 检测、参数解析、报告渲染、NVIDIA 能力表和
FFmpeg 推荐。

## 文档

- 完整英文文档：[`../../README.en.md`](../../README.en.md)
- XML 架构：[`../XML_SCHEMA.md`](../XML_SCHEMA.md)
- 示例：[`../../examples/gpu_info.xml`](../../examples/gpu_info.xml)

## 许可证

MIT。参见 [`../../LICENSE`](../../LICENSE)。
