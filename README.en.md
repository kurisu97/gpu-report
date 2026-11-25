# GPU Report

**GPU Report** is a Linux command-line utility that detects graphics adapters,
diagnoses hardware video decode/encode support, shows a localized ncursesw
interface or a plain-text report, recommends FFmpeg settings, and exports a
language-independent `gpu_info.xml` inventory.

## Multilanguage Support
Supports Enlgish, Japanese, Chinese, Korean, Russian, Spanish

## Correct Result
The guiding principle is **reliability over optimistic guesses**:

- `YES` means hardware support was confirmed by a diagnostic source.
- `NO` means support was checked and is absent.
- `UNKNOWN` means support could not be verified. It is never silently replaced
  with `NO`.
- Presence of a GPU, a kernel driver, or an encoder in the FFmpeg build is not
  treated as proof that the GPU supports a codec.

## Features

- Detects Intel, AMD, NVIDIA, and other PCI display adapters through sysfs/PCI.
- Handles hybrid and multi-GPU systems, for example Intel + NVIDIA.
- Reports name, vendor/device IDs, PCI path, kernel driver, driver version,
  DRM cards, render nodes, adapter type, and detection status.
- Checks the mandatory codecs independently for decode and encode:
  H.264/AVC, HEVC/H.265, VP8, VP9, AV1, MPEG-2, VC-1, JPEG/MJPEG.
- Records profiles, bit depths, pixel formats, and confirmation sources when
  available.
- Uses VA-API (`vainfo`) as authoritative evidence for Intel/AMD.
- Uses NVIDIA generation/NVENC/NVDEC evidence plus `nvidia-smi` and PCI names
  for NVIDIA adapters, without running an encode/decode probe.
- Uses Vulkan for identification and video-extension evidence only.
- Produces FFmpeg recommendations only for confirmed encoders.
- Provides six interface languages: English, Russian, Japanese, Korean,
  Simplified Chinese, and Spanish.
- Supports a Unicode ncursesw TUI, adaptive plain-text tables, and XML export.
- Requires no root privileges and does not modify drivers or system settings.

## Dependencies

Required build dependencies:

- C++17 compiler (GCC or Clang)
- CMake 3.16 or newer
- pkg-config
- ncursesw development files
- libxml2 development files

Optional runtime diagnostic tools:

- `vainfo` from libva-utils (VA-API for Intel/AMD)
- `vulkaninfo` from vulkan-tools (identification and Vulkan video extensions)
- `ffmpeg` (encoder/decoder availability and pixel formats)
- `nvidia-smi` from the NVIDIA driver (NVIDIA identification and architecture)
- `pci.ids`, usually provided by `hwdata`, for device names

Missing optional tools are reported as unavailable; they do not abort the
program.

## Quick build and install

The root `Makefile` provides short commands while retaining CMake as the
underlying build system:

```bash
make build
make install
```

`make install` defaults to `~/.local`, so it does not require root. Ensure
`~/.local/bin` is in `PATH`. For a system-wide installation:

```bash
sudo make install PREFIX=/usr/local
```

Tests and cleanup are also available through `make test` and `make clean`.
Use `make help` to list configurable variables.

## Building

### Arch Linux

```bash
sudo pacman -S --needed base-devel cmake pkgconf ncurses libxml2 hwdata
# Optional diagnostics:
sudo pacman -S --needed ffmpeg libva-utils vulkan-tools

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

### Debian and Ubuntu

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libncursesw5-dev libxml2-dev
# Optional diagnostics:
sudo apt install -y ffmpeg vainfo vulkan-tools hwdata

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

On Debian/Ubuntu the `nvidia-smi` binary is installed with the proprietary
NVIDIA driver package, not with a separate generic package.

### Fedora

```bash
sudo dnf install -y gcc-c++ cmake pkgconf-pkg-config ncurses-devel libxml2-devel hwdata
# Optional diagnostics:
sudo dnf install -y ffmpeg libva-utils vulkan-tools

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

On Fedora, `ffmpeg` may require RPM Fusion. NVIDIA packages vary by repository;
install the driver package appropriate for your system to obtain `nvidia-smi`.

### Install

```bash
sudo cmake --install build
```

The binary is installed as `gpu-report`, locale files under
`share/gpu-report/locales`, and documentation under the standard CMake
documentation directory.

The program also finds locales next to the executable, in the current
`locales` directory, in system data directories, and through the
`GPU_REPORT_LOCALE_DIR` environment variable.

## Usage

Running without arguments prints help:

```bash
gpu-report
```

### Actions

```bash
# Show help
gpu-report --help

# Show version
gpu-report --version

# List adapters without full codec diagnostics
gpu-report --list-gpu

# Run full diagnostics and show the report
gpu-report --get-report
```

In an interactive terminal, `--get-report` opens the TUI. Use `--no-tui` to
force plain text. `--tui` forces the TUI when appropriate.

### Selecting a GPU

```bash
# By index from --list-gpu
gpu-report --get-report --set-gpu 0

# By PCI bus path
gpu-report --get-report --set-gpu 0000:01:00.0

# By PCI vendor:device IDs (hex)
gpu-report --get-report --set-gpu 10de:1341
```

If the selector is invalid or no adapter matches, the program prints an error
and the available adapter list.

### Interface language

```bash
gpu-report --get-report --set-local=english
gpu-report --get-report --set-local=russian
gpu-report --get-report --set-local=japanese
gpu-report --get-report --set-local=korean
gpu-report --get-report --set-local=chinese
gpu-report --get-report --set-local=spanish
```

Language priority:

1. Explicit `--set-local=...`
2. `LC_ALL`, then `LC_MESSAGES`, then `LANG`
3. English fallback

Unsupported explicit languages produce a warning and fall back to English.
Chinese uses Simplified Chinese (`zh_CN` conventions) unless another variant is
explicitly requested.

### Colors and terminal output

```bash
gpu-report --get-report --no-tui --color=always
gpu-report --get-report --no-tui --color=never
gpu-report --get-report --no-tui --color=auto
```

`auto` enables ANSI colors only on a TTY with a non-`dumb` terminal. Codec
statuses are green for `YES`, red/gray for `NO`, and yellow for `UNKNOWN`.
Plain text always remains readable without colors.

### XML export

```bash
gpu-report --get-report --export gpu_info.xml
gpu-report --get-report --set-gpu 0 --export /tmp/gpu_info.xml
gpu-report --get-report --set-local=russian --export gpu_info.xml
```

Export writes a temporary file, re-parses it with libxml2, and only then
atomically renames it to the requested path. A failed write or validation
leaves any previous valid file untouched.

The XML contains machine-readable values and is independent of the interface
language. See [`docs/XML_SCHEMA.md`](docs/XML_SCHEMA.md) and
[`examples/gpu_info.xml`](examples/gpu_info.xml).

### TUI keys

| Key | Action |
|---|---|
| Left / Right or Tab | Switch GPU |
| Up / Down, PgUp / PgDn, Home / End | Scroll |
| `2` or `d` | Diagnostic details; press again to return to Overview |
| `3` or `r` | Full report; press again to return to Overview |
| `1`, `o`, `b`, Backspace, or Esc | Return to Overview |
| `e` | Export `gpu_info.xml` |
| `4`, `?`, or F1 | Help; press again to return to Overview |
| `q` | Quit |

When the terminal is too small, the TUI displays a compact message and keeps
running; resize the terminal to restore the full view.

## Diagnostic methodology

### Intel and AMD

`vainfo --display drm --device <render node>` is used per render node. If the
command succeeds and its output is parsed, the reported VA profiles are treated
as authoritative: a missing profile means `NO`. If VA-API cannot be initialized
or the output cannot be parsed, the corresponding capabilities remain
`UNKNOWN`. The iHD driver may fail to initialize on older hardware; falling
back to i965 is normal and is not treated as a diagnostic failure.

### NVIDIA

NVIDIA capabilities are inferred from the GPU generation/chip family
(`GM108`, `GP106`, `TU117`, `GA104`, and so on), the NVENC/NVDEC generation
table (with exact exceptions from NVIDIA's
[Video Encode and Decode Support Matrix](https://developer.nvidia.com/video-encode-decode-gpu-support-matrix)),
`nvidia-smi`, the PCI device name, and the driver version. This is
explicitly reported as table-based evidence. No active encode or decode test is
performed. Hardware-specific `nvidia-smi` engine results take precedence over
the broad generation table: for example, `Encoder: N/A` or `Decoder: N/A`
prevents an engine-less SKU from inheriting capabilities from another GPU in
the same architecture family. If the chip or a varying per-SKU feature cannot
be identified reliably, affected entries remain `UNKNOWN`.

### Vulkan

Vulkan is used to improve adapter names, adapter type, and to record
`VK_KHR_video_*` extension evidence. The absence of Vulkan video extensions does
not prove absence of hardware codec support.

### FFmpeg

FFmpeg is inspected for available encoders, decoders, hwaccels, and encoder
pixel formats. This describes the FFmpeg build, not per-GPU hardware support.
Build-advertised formats are reported separately and marked as unverified for
the selected GPU; they are never merged into its confirmed codec capability.
Recommendations are emitted only when the codec is confirmed for the adapter and
the matching hardware encoder exists in the FFmpeg build.

### Bitrate recommendations

The six H.264 baseline presets from the task are used as the reference:

| Resolution | FPS | H.264 bitrate |
|---|---:|---:|
| 1280x720 | 30 | 3–5 Mbps |
| 1920x1080 | 30 | 5–8 Mbps |
| 1920x1080 | 60 | 8–12 Mbps |
| 2560x1440 | 60 | 16–24 Mbps |
| 3840x2160 | 30 | 25–45 Mbps |
| 3840x2160 | 60 | 45–70 Mbps |

Ranges for other codecs are derived with documented heuristic multipliers
(HEVC ≈ 0.65, VP9 ≈ 0.75, AV1 ≈ 0.55, VP8 ≈ 1.20, MPEG-2 ≈ 1.60, VC-1 ≈ 1.40).
They are starting points, not hardware limits.

## Safety and reliability

- No root privileges are required for normal operation.
- The program does not load/unload drivers or change system settings.
- It does not install software.
- It does not run test encodes or decodes.
- External tools are invoked without a shell, so user input is never
  concatenated into shell commands.
- Missing tools, missing DRM nodes, parse failures, timeouts, and filesystem
  errors are handled and reported.
- `UNKNOWN` is preserved end to end in the report and XML.

## Testing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

The test suite covers:

- locale loading, key completeness, language resolution, and fallback
- XML structure, escaping, UTF-8 sanitization, validation, and error handling
- PCI database parsing, GPU detection invariants, tri-state values
- CLI parsing, option conflicts, and localized help/version text
- FFmpeg recommendation selection, encoder confirmation, presets, and commands
- GPU/report rendering, UNKNOWN preservation, and adaptive table widths
- NVIDIA generation-table behavior and direct `nvidia-smi` engine overrides

## Project structure

```text
.
├── Makefile
├── CMakeLists.txt
├── README.md          Russian GitHub overview
├── README.en.md       detailed English documentation
├── gitrd.md           source Russian overview
├── LICENSE
├── docs/
│   ├── XML_SCHEMA.md
│   └── i18n/          localized README files
├── examples/
│   └── gpu_info.xml
├── locales/
│   ├── en.xml
│   ├── ru.xml
│   ├── ja.xml
│   ├── ko.xml
│   ├── zh.xml
│   └── es.xml
├── src/
│   ├── main.cpp
│   ├── cli/            command-line parsing
│   ├── core/           process, sysfs, strings, system information
│   ├── diagnostics/    VA-API, NVENC/NVDEC, Vulkan, FFmpeg, orchestration
│   ├── gpu/            PCI database, GPU model, detector, codecs
│   ├── localization/   language resolution and XML locale loader
│   ├── report/         reports and FFmpeg recommendations
│   ├── tui/            ncursesw interface
│   └── xml/            libxml2 export and validation
└── tests/
```

## Localized documentation

- [Русский обзор для GitHub](gitrd.md)
- [Русский](docs/i18n/README.ru.md)
- [日本語](docs/i18n/README.ja.md)
- [한국어](docs/i18n/README.ko.md)
- [简体中文](docs/i18n/README.zh.md)
- [Español](docs/i18n/README.es.md)

## Adding a language

1. Copy `locales/en.xml` to `locales/<code>.xml`.
2. Keep every `key` unchanged and translate only the text.
3. Add the language to `Language`, `languageCode()`, and language resolution in
   `src/localization/`.
4. Rebuild and run `ctest`.

Machine values such as `YES`, `NO`, `UNKNOWN`, codec names, XML tags, FFmpeg
parameters, and API names must not be translated inside XML.

## License

MIT. See [`LICENSE`](LICENSE).
