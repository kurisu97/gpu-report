# GPU Report

**GPU Report** es una utilidad de línea de comandos para Linux que detecta
adaptadores gráficos, diagnostica la decodificación y codificación de vídeo por
hardware, muestra un informe localizado en ncursesw o texto plano, recomienda
ajustes de FFmpeg y exporta un inventario `gpu_info.xml` independiente del
idioma.

El principio principal es **la fiabilidad antes que las suposiciones
optimistas**:

- `YES` — la compatibilidad fue confirmada por una fuente de diagnóstico.
- `NO` — se comprobó y no hay compatibilidad.
- `UNKNOWN` — no se pudo verificar; nunca se sustituye por `NO`.
- La presencia de una GPU, un controlador o un codificador en la compilación de
  FFmpeg no demuestra que esa GPU admita el códec.

## Características

- Detecta adaptadores Intel, AMD, NVIDIA y otros mediante sysfs/PCI.
- Gestiona configuraciones híbridas y con varias GPU.
- Muestra nombre, Vendor/Device ID, ruta PCI, controlador, versión del
  controlador, tarjetas DRM, render nodes, tipo de adaptador y estado.
- Diagnostica por separado decodificación y codificación para H.264/AVC,
  HEVC/H.265, VP8, VP9, AV1, MPEG-2, VC-1 y JPEG/MJPEG.
- Registra perfiles, profundidad de bits, formatos de píxel y fuentes de
  confirmación cuando se conocen.
- Los formatos anunciados por la compilación de FFmpeg se muestran por separado
  como no verificados para la GPU y no se tratan como capacidades confirmadas.
- Usa VA-API (`vainfo`) como evidencia autoritativa para Intel/AMD.
- Para NVIDIA usa la tabla de generaciones NVENC/NVDEC, `nvidia-smi` y el
  nombre del chip PCI, sin ejecutar una prueba real de codificación; un resultado
  `Encoder/Decoder: N/A` prevalece sobre la tabla general de la generación.
- Vulkan se usa solo para identificación y evidencia de extensiones de vídeo.
- Las recomendaciones de FFmpeg solo se generan para codificadores confirmados.
- Seis idiomas de interfaz: inglés, ruso, japonés, coreano, chino simplificado
  y español.
- TUI ncursesw, tablas de texto adaptables y exportación XML.
- No requiere root y no modifica controladores ni ajustes del sistema.

## Dependencias

Para compilar:

- compilador C++17 (GCC o Clang)
- CMake 3.16 o superior
- pkg-config
- archivos de desarrollo de ncursesw
- archivos de desarrollo de libxml2

Herramientas opcionales: `vainfo`, `vulkaninfo`, `ffmpeg`, `nvidia-smi` y
`pci.ids` (normalmente del paquete `hwdata`). Su ausencia no provoca un fallo.

## Compilación

### Arch Linux

```bash
sudo pacman -S --needed base-devel cmake pkgconf ncurses libxml2 hwdata
sudo pacman -S --needed ffmpeg libva-utils vulkan-tools   # opcional
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

### Debian y Ubuntu

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libncursesw5-dev libxml2-dev
sudo apt install -y ffmpeg vainfo vulkan-tools hwdata   # opcional
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

### Fedora

```bash
sudo dnf install -y gcc-c++ cmake pkgconf-pkg-config ncurses-devel libxml2-devel hwdata
sudo dnf install -y ffmpeg libva-utils vulkan-tools   # opcional
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Instalación:

```bash
sudo cmake --install build
```

## Uso

```bash
gpu-report                         # ayuda
gpu-report --help
gpu-report --version
gpu-report --list-gpu              # lista sin diagnóstico completo de códecs
gpu-report --get-report            # diagnóstico completo (TUI en terminal interactiva)
gpu-report --get-report --no-tui   # texto plano
```

Selección de GPU:

```bash
gpu-report --get-report --set-gpu 0
gpu-report --get-report --set-gpu 0000:01:00.0
gpu-report --get-report --set-gpu 10de:1341
```

Idioma:

```bash
gpu-report --get-report --set-local=english
gpu-report --get-report --set-local=russian
gpu-report --get-report --set-local=japanese
gpu-report --get-report --set-local=korean
gpu-report --get-report --set-local=chinese
gpu-report --get-report --set-local=spanish
```

Prioridad: `--set-local` explícito, después `LC_ALL` / `LC_MESSAGES` / `LANG`
y por último inglés. Un idioma explícito no compatible muestra una advertencia
y vuelve al inglés.

Exportación XML:

```bash
gpu-report --get-report --export gpu_info.xml
gpu-report --get-report --set-gpu 0 --export /tmp/gpu_info.xml
```

La exportación escribe un archivo temporal, lo vuelve a analizar con libxml2 y
solo entonces lo renombra de forma atómica. Si falla, el archivo válido anterior
no se daña.

## Teclas de la TUI

| Tecla | Acción |
|---|---|
| Izquierda / Derecha, Tab | cambiar de GPU |
| Arriba / Abajo, PgUp / PgDn, Home / End | desplazarse |
| `2` o `d` | detalles del diagnóstico; repetir vuelve al resumen |
| `3` o `r` | informe completo; repetir vuelve al resumen |
| `1`, `o`, `b`, Backspace o Esc | volver al resumen |
| `e` | exportar `gpu_info.xml` |
| `4`, `?` o F1 | ayuda; repetir vuelve al resumen |
| `q` | salir |

## Pruebas

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Cubren localización, XML y UTF-8, detección de GPU, análisis de argumentos,
renderizado de informes, la tabla NVIDIA y recomendaciones de FFmpeg.

## Documentación

- Documentación completa en inglés: [`../../README.en.md`](../../README.en.md)
- Esquema XML: [`../XML_SCHEMA.md`](../XML_SCHEMA.md)
- Ejemplo: [`../../examples/gpu_info.xml`](../../examples/gpu_info.xml)

## Licencia

MIT. Consulte [`../../LICENSE`](../../LICENSE).
