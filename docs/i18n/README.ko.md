# GPU Report

**GPU Report**는 Linux용 명령줄 진단 도구입니다. 그래픽 어댑터를 감지하고
하드웨어 동영상 디코드/인코드 지원을 진단하며, ncursesw 기반의 현지화된
TUI 또는 일반 텍스트 보고서를 표시합니다. FFmpeg 설정을 추천하고 언어에
독립적인 `gpu_info.xml`을 내보냅니다.

핵심 원칙은 **낙관적인 추측보다 신뢰성**입니다.

- `YES` — 진단 소스로 하드웨어 지원이 확인됨.
- `NO` — 확인 결과 지원되지 않음.
- `UNKNOWN` — 확인할 수 없음. `NO`로 대체하지 않습니다.
- GPU, 드라이버, FFmpeg 빌드의 인코더 존재만으로 해당 GPU의 코덱 지원을
  증명하지 않습니다.

## 기능

- sysfs/PCI를 통한 Intel, AMD, NVIDIA 및 기타 디스플레이 어댑터 감지.
- 하이브리드 및 다중 GPU 구성 처리.
- 이름, Vendor/Device ID, PCI 경로, 드라이버, 드라이버 버전, DRM 카드,
  렌더 노드, 어댑터 유형, 감지 상태 표시.
- H.264/AVC, HEVC/H.265, VP8, VP9, AV1, MPEG-2, VC-1, JPEG/MJPEG의
  디코드와 인코드를 독립적으로 진단.
- 알려진 경우 프로필, 비트 깊이, 픽셀 포맷, 확인 소스 기록.
- FFmpeg 빌드가 제공하는 포맷은 해당 GPU에서 확인되지 않은 정보로 별도 표시하며,
  확인된 하드웨어 기능으로 취급하지 않음.
- Intel/AMD는 VA-API(`vainfo`)를 권위 있는 근거로 사용.
- NVIDIA는 NVENC/NVDEC 세대 표, `nvidia-smi`, PCI 칩 이름을 사용하며
  실제 인코드 테스트는 수행하지 않음. `Encoder/Decoder: N/A` 결과는 세대 표보다
  우선함.
- Vulkan은 식별 및 Vulkan Video 확장 근거로만 사용.
- FFmpeg 추천은 확인된 인코더에만 적용.
- 6개 인터페이스 언어: 영어, 러시아어, 일본어, 한국어, 중국어(간체), 스페인어.
- ncursesw TUI, 터미널 너비에 맞는 텍스트 표, XML 내보내기.
- root 권한이 필요 없고 드라이버나 시스템 설정을 변경하지 않음.

## 의존성

빌드 필수:

- C++17 컴파일러(GCC 또는 Clang)
- CMake 3.16 이상
- pkg-config
- ncursesw 개발 파일
- libxml2 개발 파일

선택 진단 도구: `vainfo`, `vulkaninfo`, `ffmpeg`, `nvidia-smi`,
`pci.ids`(보통 `hwdata` 패키지). 없어도 비정상 종료하지 않습니다.

## 빌드

### Arch Linux

```bash
sudo pacman -S --needed base-devel cmake pkgconf ncurses libxml2 hwdata
sudo pacman -S --needed ffmpeg libva-utils vulkan-tools   # 선택
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

### Debian / Ubuntu

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libncursesw5-dev libxml2-dev
sudo apt install -y ffmpeg vainfo vulkan-tools hwdata   # 선택
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

### Fedora

```bash
sudo dnf install -y gcc-c++ cmake pkgconf-pkg-config ncurses-devel libxml2-devel hwdata
sudo dnf install -y ffmpeg libva-utils vulkan-tools   # 선택
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

설치:

```bash
sudo cmake --install build
```

## 사용법

```bash
gpu-report                         # 도움말
gpu-report --help
gpu-report --version
gpu-report --list-gpu              # 전체 코덱 진단 없이 목록
gpu-report --get-report            # 전체 진단(대화형 터미널에서는 TUI)
gpu-report --get-report --no-tui   # 일반 텍스트
```

GPU 선택:

```bash
gpu-report --get-report --set-gpu 0
gpu-report --get-report --set-gpu 0000:01:00.0
gpu-report --get-report --set-gpu 10de:1341
```

언어:

```bash
gpu-report --get-report --set-local=english
gpu-report --get-report --set-local=russian
gpu-report --get-report --set-local=japanese
gpu-report --get-report --set-local=korean
gpu-report --get-report --set-local=chinese
gpu-report --get-report --set-local=spanish
```

우선순위: 명시적 `--set-local`, `LC_ALL` / `LC_MESSAGES` / `LANG`,
영어 폴백. 지원하지 않는 명시 언어는 경고 후 영어로 대체됩니다.

XML 내보내기:

```bash
gpu-report --get-report --export gpu_info.xml
gpu-report --get-report --set-gpu 0 --export /tmp/gpu_info.xml
```

임시 파일에 쓴 뒤 libxml2로 다시 파싱해 검증하고 원자적으로 이름을
변경합니다. 실패해도 기존의 정상 파일은 손상되지 않습니다.

## TUI 키

| 키 | 동작 |
|---|---|
| 왼쪽 / 오른쪽, Tab | GPU 전환 |
| 위 / 아래, PgUp / PgDn, Home / End | 스크롤 |
| `2` 또는 `d` | 진단 세부 정보. 다시 누르면 개요로 돌아감 |
| `3` 또는 `r` | 전체 보고서. 다시 누르면 개요로 돌아감 |
| `1`, `o`, `b`, Backspace 또는 Esc | 개요로 돌아가기 |
| `e` | `gpu_info.xml` 내보내기 |
| `4`, `?` 또는 F1 | 도움말. 다시 누르면 개요로 돌아감 |
| `q` | 종료 |

## 테스트

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

현지화, XML 및 UTF-8, GPU 감지, 인수 분석, 보고서 렌더링, NVIDIA 기능표,
FFmpeg 추천을 검증합니다.

## 문서

- 전체 영어 문서: [`../../README.en.md`](../../README.en.md)
- XML 스키마: [`../XML_SCHEMA.md`](../XML_SCHEMA.md)
- 예제: [`../../examples/gpu_info.xml`](../../examples/gpu_info.xml)

## 라이선스

MIT. [`../../LICENSE`](../../LICENSE)를 참조하세요.
