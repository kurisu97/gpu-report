# GPU Report

**GPU Report** は Linux 用のコマンドライン診断ツールです。グラフィック
アダプターを検出し、ハードウェア動画デコード／エンコード対応を診断し、
ncursesw のローカライズ TUI またはプレーンテキストで表示します。FFmpeg の
推奨設定を示し、言語に依存しない `gpu_info.xml` を出力します。

基本方針は **楽観的な推測より信頼性を優先する** ことです。

- `YES` — 診断ソースによりハードウェア対応が確認された状態。
- `NO` — 確認した結果、対応していない状態。
- `UNKNOWN` — 確認できない状態。`NO` に置き換えることはありません。
- GPU、ドライバー、FFmpeg ビルド内のエンコーダーの存在だけでは、
  その GPU のコーデック対応の証明にはなりません。

## 機能

- sysfs/PCI による Intel、AMD、NVIDIA、その他のディスプレイアダプター検出。
- ハイブリッド構成や複数 GPU 構成に対応。
- 名前、Vendor/Device ID、PCI パス、ドライバー、ドライバーバージョン、
  DRM カード、レンダーノード、アダプター種別、検出状態を表示。
- H.264/AVC、HEVC/H.265、VP8、VP9、AV1、MPEG-2、VC-1、JPEG/MJPEG の
  デコードとエンコードを独立して診断。
- 判明している場合はプロファイル、ビット深度、ピクセルフォーマット、
  確認ソースを記録。
- FFmpeg ビルドが公開するフォーマットは GPU ごとに未確認の情報として分離し、
  確認済みハードウェア機能には含めません。
- Intel/AMD では VA-API（`vainfo`）を主要な根拠として使用。
- NVIDIA では NVENC/NVDEC 世代表、`nvidia-smi`、PCI チップ名を使用し、
  実際のエンコードテストは行いません。`Encoder/Decoder: N/A` は世代表より
  優先されます。
- Vulkan は識別と Vulkan Video 拡張の証拠としてのみ使用。
- FFmpeg の推奨は確認済みのエンコーダーのみを対象。
- 6 言語対応: 英語、ロシア語、日本語、韓国語、簡体字中国語、スペイン語。
- ncursesw TUI、端末幅に適応するテキスト表、XML エクスポート。
- root 権限不要。ドライバーやシステム設定は変更しません。

## 依存関係

ビルドに必要:

- C++17 コンパイラー（GCC または Clang）
- CMake 3.16 以上
- pkg-config
- ncursesw 開発ファイル
- libxml2 開発ファイル

任意の診断ツール: `vainfo`、`vulkaninfo`、`ffmpeg`、`nvidia-smi`、
`pci.ids`（通常 `hwdata` パッケージ）。なくても異常終了しません。

## ビルド

### Arch Linux

```bash
sudo pacman -S --needed base-devel cmake pkgconf ncurses libxml2 hwdata
sudo pacman -S --needed ffmpeg libva-utils vulkan-tools   # 任意
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

### Debian / Ubuntu

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libncursesw5-dev libxml2-dev
sudo apt install -y ffmpeg vainfo vulkan-tools hwdata   # 任意
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

### Fedora

```bash
sudo dnf install -y gcc-c++ cmake pkgconf-pkg-config ncurses-devel libxml2-devel hwdata
sudo dnf install -y ffmpeg libva-utils vulkan-tools   # 任意
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

インストール:

```bash
sudo cmake --install build
```

## 使い方

```bash
gpu-report                         # ヘルプ
gpu-report --help
gpu-report --version
gpu-report --list-gpu              # コーデック診断なしの一覧
gpu-report --get-report            # 完全な診断（対話端末では TUI）
gpu-report --get-report --no-tui   # プレーンテキスト
```

GPU の選択:

```bash
gpu-report --get-report --set-gpu 0
gpu-report --get-report --set-gpu 0000:01:00.0
gpu-report --get-report --set-gpu 10de:1341
```

言語:

```bash
gpu-report --get-report --set-local=english
gpu-report --get-report --set-local=russian
gpu-report --get-report --set-local=japanese
gpu-report --get-report --set-local=korean
gpu-report --get-report --set-local=chinese
gpu-report --get-report --set-local=spanish
```

優先順位は、明示的な `--set-local`、`LC_ALL` / `LC_MESSAGES` / `LANG`、
英語フォールバックの順です。未対応の明示言語は警告を出して英語になります。

XML エクスポート:

```bash
gpu-report --get-report --export gpu_info.xml
gpu-report --get-report --set-gpu 0 --export /tmp/gpu_info.xml
```

一時ファイルへ書き込み、libxml2 で再解析して検証した後、原子的に
リネームします。失敗しても既存の有効なファイルは破壊されません。

## TUI キー

| キー | 動作 |
|---|---|
| 左 / 右、Tab | GPU 切り替え |
| 上 / 下、PgUp / PgDn、Home / End | スクロール |
| `2` または `d` | 診断の詳細。もう一度押すと概要へ戻る |
| `3` または `r` | 完全なレポート。もう一度押すと概要へ戻る |
| `1`、`o`、`b`、Backspace または Esc | 概要へ戻る |
| `e` | `gpu_info.xml` をエクスポート |
| `4`、`?` または F1 | ヘルプ。もう一度押すと概要へ戻る |
| `q` | 終了 |

## テスト

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

ローカライズ、XML と UTF-8、GPU 検出、引数解析、レポート描画、NVIDIA 機能表、
FFmpeg 推奨を検証します。

## ドキュメント

- 完全な英語版: [`../../README.en.md`](../../README.en.md)
- XML スキーマ: [`../XML_SCHEMA.md`](../XML_SCHEMA.md)
- サンプル: [`../../examples/gpu_info.xml`](../../examples/gpu_info.xml)

## ライセンス

MIT。[`../../LICENSE`](../../LICENSE) を参照してください。
