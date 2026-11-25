# GPU Report

**GPU Report** — консольная утилита для Linux, которая обнаруживает графические
адаптеры, диагностирует аппаратное декодирование и кодирование видео,
показывает локализованный отчёт в ncursesw или обычном тексте, рекомендует
параметры FFmpeg и экспортирует независимый от языка `gpu_info.xml`.

Главный принцип — **достоверность важнее оптимистичных предположений**:

- `YES` — поддержка подтверждена источником диагностики.
- `NO` — поддержка проверена и отсутствует.
- `UNKNOWN` — поддержку не удалось подтвердить; он никогда не заменяется на `NO`.
- Наличие GPU, драйвера или энкодера в сборке FFmpeg не считается доказательством
  аппаратной поддержки кодека.

## Возможности

- Обнаружение Intel, AMD, NVIDIA и других PCI-адаптеров через sysfs/PCI.
- Корректная работа с гибридными и многопроцессорными GPU-конфигурациями.
- Название, Vendor/Device ID, PCI-путь, драйвер, версия драйвера, DRM-карты,
  render nodes, тип адаптера и статус обнаружения.
- Независимая диагностика декодирования и кодирования для H.264/AVC,
  HEVC/H.265, VP8, VP9, AV1, MPEG-2, VC-1 и JPEG/MJPEG.
- Профили, разрядность, форматы пикселей и источники подтверждения, если известны.
- Форматы из сборки FFmpeg выводятся отдельно как непроверенные для конкретного
  GPU и не считаются подтверждёнными возможностями оборудования.
- VA-API (`vainfo`) как авторитетный источник для Intel/AMD.
- Для NVIDIA — таблица поколений NVENC/NVDEC, `nvidia-smi` и имя PCI-чипа,
  без активного тестового кодирования; `Encoder/Decoder: N/A` имеет приоритет
  над общей таблицей поколения.
- Vulkan используется для идентификации и свидетельств о видео-расширениях.
- Рекомендации FFmpeg только для подтверждённых энкодеров.
- Шесть языков интерфейса: английский, русский, японский, корейский,
  упрощённый китайский и испанский.
- TUI на ncursesw, адаптивные текстовые таблицы и экспорт XML.
- Не требует root и не изменяет драйверы или системные настройки.

## Зависимости

Обязательные для сборки:

- компилятор C++17 (GCC или Clang);
- CMake 3.16+;
- pkg-config;
- заголовки ncursesw;
- заголовки libxml2.

Необязательные диагностические инструменты: `vainfo`, `vulkaninfo`, `ffmpeg`,
`nvidia-smi` и база `pci.ids` (обычно из пакета `hwdata`). Их отсутствие не
приводит к аварийному завершению.

## Сборка

### Arch Linux

```bash
sudo pacman -S --needed base-devel cmake pkgconf ncurses libxml2 hwdata
sudo pacman -S --needed ffmpeg libva-utils vulkan-tools   # необязательно
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

### Debian и Ubuntu

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libncursesw5-dev libxml2-dev
sudo apt install -y ffmpeg vainfo vulkan-tools hwdata   # необязательно
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

### Fedora

```bash
sudo dnf install -y gcc-c++ cmake pkgconf-pkg-config ncurses-devel libxml2-devel hwdata
sudo dnf install -y ffmpeg libva-utils vulkan-tools   # необязательно
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Установка:

```bash
sudo cmake --install build
```

## Использование

```bash
gpu-report                         # справка
gpu-report --help
gpu-report --version
gpu-report --list-gpu              # список без полной диагностики
gpu-report --get-report            # полный отчёт (TUI в интерактивном терминале)
gpu-report --get-report --no-tui   # обычный текст
```

Выбор GPU:

```bash
gpu-report --get-report --set-gpu 0
gpu-report --get-report --set-gpu 0000:01:00.0
gpu-report --get-report --set-gpu 10de:1341
```

Язык:

```bash
gpu-report --get-report --set-local=english
gpu-report --get-report --set-local=russian
gpu-report --get-report --set-local=japanese
gpu-report --get-report --set-local=korean
gpu-report --get-report --set-local=chinese
gpu-report --get-report --set-local=spanish
```

Приоритет: явный `--set-local`, затем `LC_ALL` / `LC_MESSAGES` / `LANG`,
затем английский. Неизвестный явный язык даёт предупреждение и английский
fallback.

Экспорт XML:

```bash
gpu-report --get-report --export gpu_info.xml
gpu-report --get-report --set-gpu 0 --export /tmp/gpu_info.xml
```

Экспорт пишет временный файл, повторно разбирает его через libxml2 и только
затем атомарно переименовывает. При ошибке предыдущий корректный файл не
повреждается.

## TUI

| Клавиша | Действие |
|---|---|
| Влево / Вправо, Tab | переключение GPU |
| Вверх / Вниз, PgUp / PgDn, Home / End | прокрутка |
| `2` или `d` | детали диагностики; повторное нажатие возвращает в обзор |
| `3` или `r` | полный отчёт; повторное нажатие возвращает в обзор |
| `1`, `o`, `b`, Backspace или Esc | вернуться в обзор |
| `e` | экспорт `gpu_info.xml` |
| `4`, `?` или F1 | справка; повторное нажатие возвращает в обзор |
| `q` | выход |

## Тесты

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Тесты покрывают локализацию, XML и UTF-8, обнаружение GPU, разбор аргументов,
формирование отчёта, таблицу NVIDIA и рекомендации FFmpeg.

## Документация

- Полное английское описание: [`../../README.en.md`](../../README.en.md)
- Схема XML: [`../XML_SCHEMA.md`](../XML_SCHEMA.md)
- Пример отчёта: [`../../examples/gpu_info.xml`](../../examples/gpu_info.xml)

## Лицензия

MIT. См. [`../../LICENSE`](../../LICENSE).
