# GPU Report XML schema (`gpu_info.xml`)

This document describes the XML produced by `gpu-report --get-report --export
<path>` (or by pressing `e` in the TUI). The format is intended for other
programs that need the diagnostic result without re-running GPU detection.

The current schema version is **1.0**. The root element carries
`schema_version="1.0"`. Consumers should:

- accept unknown elements and attributes and ignore them;
- require only the elements they actually need;
- treat missing optional values as unknown, not as negative evidence;
- use `schema_version` for major compatibility decisions.

The file is UTF-8, and all text is XML-escaped by libxml2. The program writes a
temporary file, parses it again to verify well-formedness and the root element,
and only then renames it into place.

## Example

```xml
<?xml version="1.0" encoding="UTF-8"?>
<gpu_inventory schema_version="1.0" active_probe="none">
  <generated_at>2026-11-25T08:55:54Z</generated_at>
  <locale>en</locale>

  <program name="gpu-report" version="1.0.0" schema_version="1.0"/>

  <host>
    <hostname>example-host</hostname>
    <kernel>6.12.1</kernel>
    <architecture>x86_64</architecture>
    <distribution>Example Linux</distribution>
  </host>

  <interfaces>
    <interface name="sysfs" key="diag.interface.sysfs" status="available">
      <detail>2 display device(s) found</detail>
    </interface>
  </interfaces>

  <gpu>
    <name>Intel HD Graphics 4600</name>
    <name_source>Vulkan</name_source>
    <vendor>Intel</vendor>
    <vendor_name>Intel Corporation</vendor_name>
    <pci_device_name>4th Gen Core Processor Integrated Graphics Controller</pci_device_name>
    <vendor_id>0x8086</vendor_id>
    <device_id>0x0416</device_id>
    <driver>i915</driver>
    <driver_version>6.12.1</driver_version>
    <driver_version_source>module</driver_version_source>
    <pci_path>0000:00:02.0</pci_path>

    <adapter_type>integrated</adapter_type>
    <integrated supported="true"/>
    <status>detected</status>
    <vulkan_device>Intel(R) HD Graphics 4600 (HSW GT2)</vulkan_device>

    <render_nodes>
      <node>/dev/dri/renderD128</node>
    </render_nodes>
    <drm_cards>
      <card>/dev/dri/card1</card>
    </drm_cards>

    <interfaces>
      <interface name="VA-API" key="diag.interface.vaapi" status="available">
        <detail>/dev/dri/renderD128: Driver version: Intel i965 driver ...</detail>
      </interface>
    </interfaces>

    <capabilities>
      <codec name="H.264/AVC">
        <decode supported="true"/>
        <encode supported="true"/>
        <profiles>
          <profile>Constrained Baseline</profile>
          <profile>Main</profile>
          <profile>High</profile>
        </profiles>
        <bit_depths>
          <bit_depth>8</bit_depth>
        </bit_depths>
        <pixel_formats>
          <pixel_format>nv12</pixel_format>
        </pixel_formats>
        <sources>
          <decode_sources>
            <source>VA-API (vainfo, /dev/dri/renderD128, ...)</source>
          </decode_sources>
          <encode_sources>
            <source>VA-API (vainfo, /dev/dri/renderD128, ...)</source>
          </encode_sources>
        </sources>
      </codec>

      <codec name="HEVC/H.265">
        <decode supported="unknown"/>
        <encode supported="unknown"/>
      </codec>
    </capabilities>

    <hw_encoders>
      <encoder name="h264_vaapi" api="VA-API" codec="H.264/AVC"
               available_in_ffmpeg="true">
        <pixel_formats source="ffmpeg_build" per_gpu_verified="false">
          <pixel_format>nv12</pixel_format>
        </pixel_formats>
      </encoder>
    </hw_encoders>
    <hw_decoders>
      <decoder name="vaapi" api="VA-API" codec="H.264/AVC"
               available_in_ffmpeg="true"/>
    </hw_decoders>

    <encoding_recommendations>
      <preset resolution="1920x1080" fps="60" codec="H.264/AVC"
              bitrate="8-12M" encoder="h264_vaapi">
        <command>ffmpeg -vaapi_device /dev/dri/renderD128 -i input.mp4 -vf 'format=nv12,hwupload' -c:v h264_vaapi -b:v 10M output.mp4</command>
      </preset>
    </encoding_recommendations>
  </gpu>
</gpu_inventory>
```

## Root element

### `<gpu_inventory>`

| Attribute | Type | Meaning |
|---|---|---|
| `schema_version` | string | Schema version, currently `1.0`. |
| `active_probe` | string | `none` when no test encode/decode was executed. Future versions may report other values. |

Children:

| Element | Cardinality | Meaning |
|---|---|---|
| `generated_at` | 1 | UTC timestamp in ISO 8601 form, for example `2026-11-25T08:55:54Z`. |
| `locale` | 1 | Interface locale code (`en`, `ru`, `ja`, `ko`, `zh`, `es`). This is metadata only; it does not change any machine value. |
| `program` | 1 | Program identity and versions. |
| `host` | 1 | Host and operating system information. |
| `interfaces` | 1 | Global diagnostic checks. |
| `gpu` | 0..N | One element per diagnosed adapter. |

## `<program>`

| Attribute | Meaning |
|---|---|
| `name` | Program name, currently `gpu-report`. |
| `version` | Program version. |
| `schema_version` | Schema version written by the program. |

## `<host>`

| Element | Meaning |
|---|---|
| `hostname` | Host name. |
| `kernel` | Kernel release. |
| `architecture` | Machine architecture, for example `x86_64`. |
| `distribution` | Distribution description when available. |

Values are informational and may be empty.

## `<interfaces>` and `<interface>`

The root has a global `<interfaces>` block. Each `<gpu>` also has its own
`<interfaces>` block for adapter-specific checks.

Each `<interface>` describes one diagnostic interface:

| Attribute | Meaning |
|---|---|
| `name` | Stable technical name, for example `VA-API`, `Vulkan`, `FFmpeg`, `nvidia-smi`. |
| `key` | Localization key used by the UI. Third-party consumers should use `name`, not `key`. |
| `status` | Machine-readable check status. |

Status values:

| Status | Meaning |
|---|---|
| `available` | The interface was found and usable. |
| `unavailable` | The interface or tool is missing or could not be used. |
| `error` | The interface was present but failed. |
| `timeout` | The check exceeded its time limit. |
| `skipped` | The check was deliberately not run. |
| `not-applicable` | The interface does not apply to this adapter. |

`<detail>` is a human-readable technical explanation. It is not a machine
status and may contain tool output. Do not parse it when a structured field is
available.

## `<gpu>`

One `<gpu>` element per detected display adapter.

### Identity and driver fields

| Element | Meaning |
|---|---|
| `name` | Best available adapter name. |
| `name_source` | Source of the name, for example `pci.ids`, `Vulkan`, or `generic`. |
| `vendor` | Normalized vendor name: `Intel`, `AMD`, `NVIDIA`, `Other`, or `Unknown`. |
| `vendor_name` | Vendor name from the PCI database, when known. |
| `pci_device_name` | Device name from `pci.ids`, when known. |
| `vendor_id` | PCI vendor ID as `0xXXXX`. |
| `device_id` | PCI device ID as `0xXXXX`. |
| `driver` | Kernel driver name, for example `i915`, `amdgpu`, `nvidia`. |
| `driver_version` | Driver version when available. |
| `driver_version_source` | Where the version came from, when known. |
| `pci_path` | PCI bus path, for example `0000:00:02.0`. |

### Adapter classification

| Element | Meaning |
|---|---|
| `adapter_type` | `integrated`, `discrete`, or `unknown`. |
| `integrated` | Element with attribute `supported`: `true`, `false`, or `unknown`. |
| `status` | Detection status: `detected`, `no_driver`, `no_drm_node`, or `error`. |
| `vulkan_device` | Vulkan device name when it was matched. |

`adapter_type` is a convenience value derived from `integrated`; consumers
should prefer the `integrated` tri-state if they need machine logic.

### Device nodes

```xml
<render_nodes>
  <node>/dev/dri/renderD128</node>
</render_nodes>
<drm_cards>
  <card>/dev/dri/card1</card>
</drm_cards>
```

Both containers may be empty.

## `<capabilities>` and `<codec>`

The mandatory codecs are always represented, even when support is unknown:

`H.264/AVC`, `HEVC/H.265`, `VP8`, `VP9`, `AV1`, `MPEG-2`, `VC-1`,
`JPEG/MJPEG`.

Each codec has:

```xml
<codec name="H.264/AVC">
  <decode supported="true"/>
  <encode supported="true"/>
  ...
</codec>
```

The `supported` attribute is the only authoritative tri-state:

| Value | Meaning |
|---|---|
| `true` | Support confirmed (`YES`). |
| `false` | Support checked and absent (`NO`). |
| `unknown` | Not verified. This must not be interpreted as `false`. |

Decode and encode are independent. A codec may be `true` for decode and
`false` for encode.

Optional child elements:

| Element | Meaning |
|---|---|
| `profiles/profile` | Profiles confirmed by the evidence source. |
| `bit_depths/bit_depth` | Supported bit depths as decimal integers (for example `8`, `10`). |
| `pixel_formats/pixel_format` | Pixel formats confirmed for this GPU by the hardware evidence source. |
| `sources/decode_sources/source` | Evidence strings for the decode result. |
| `sources/encode_sources/source` | Evidence strings for the encode result. |

Evidence strings are technical and language-independent. They may name a tool,
render node, driver version, or NVIDIA generation table. Evidence can support a
negative result as well as a positive one; always read it together with the
`supported` attribute.

Pixel formats merely advertised by the installed FFmpeg build are **not** put
in this capability block because that would overstate support on older GPUs.
They are recorded separately under the matching `<encoder>` as build-level
metadata.

## `<hw_encoders>` and `<hw_decoders>`

```xml
<hw_encoders>
  <encoder name="h264_vaapi" api="VA-API" codec="H.264/AVC"
           available_in_ffmpeg="true">
    <pixel_formats source="ffmpeg_build" per_gpu_verified="false">
      <pixel_format>nv12</pixel_format>
    </pixel_formats>
  </encoder>
</hw_encoders>
<hw_decoders>
  <decoder name="vaapi" api="VA-API" codec="H.264/AVC"
           available_in_ffmpeg="true"/>
</hw_decoders>
```

| Attribute | Meaning |
|---|---|
| `name` | FFmpeg encoder/decoder name, for example `h264_vaapi`, `h264_nvenc`, `h264_cuvid`. |
| `api` | Hardware API: `VA-API`, `NVENC`, `NVDEC`, `QSV`, `AMF`, or `V4L2`. |
| `codec` | Standard codec name. |
| `available_in_ffmpeg` | `true` or `false`. This describes the FFmpeg build, not proof of per-GPU support. |

These lists are generated only for codecs confirmed on the adapter, but
`available_in_ffmpeg` remains a build-level fact.

An encoder can contain an optional `<pixel_formats>` block. Its attributes are:

| Attribute | Meaning |
|---|---|
| `source` | `ffmpeg_build`: values came from `ffmpeg -h encoder=<name>`. |
| `per_gpu_verified` | Always `false` for this block; the values were not actively verified on this GPU. |

Each `<pixel_format>` is therefore useful for inspecting the installed FFmpeg
build, but must not be interpreted as a hardware capability of the selected
adapter. Decoder entries currently do not contain this optional block.

## `<encoding_recommendations>` and `<preset>`

```xml
<encoding_recommendations>
  <preset resolution="1920x1080" fps="60" codec="H.264/AVC"
          bitrate="8-12M" encoder="h264_vaapi">
    <command>ffmpeg ...</command>
  </preset>
</encoding_recommendations>
```

| Attribute | Meaning |
|---|---|
| `resolution` | `WIDTHxHEIGHT`. |
| `fps` | Integer frame rate. |
| `codec` | Standard codec name. |
| `bitrate` | Recommended starting range, for example `8-12M`. |
| `encoder` | FFmpeg hardware encoder name. |

`<command>` is a complete example command using the adapter's render node or
hardware API. A command is emitted only when the codec and encoder were
confirmed. Bitrate ranges are heuristic starting points, not hardware limits.

## Compatibility and extension rules

- New optional elements and attributes may be added within schema `1.x`.
- Existing element names, attribute names, and value meanings will not change
  incompatibly within `1.x`.
- Consumers should ignore unknown elements and attributes.
- Consumers should not require `<encoding_recommendations>` or optional
  profile/format/source blocks.
- A missing `<gpu>` means no adapter was detected, not that the host has no GPU
  hardware in every possible sense.
- A missing optional value means unknown, not `false`.
- `UNKNOWN`/`unknown` is a first-class value and must never be converted to
  `NO`/`false` by a consumer.

## Validation

The exporter validates the generated file with libxml2 before replacing the
destination. A consumer can perform a basic validation by checking:

1. The file is well-formed XML and UTF-8.
2. The root element is `<gpu_inventory>`.
3. `schema_version` is present.
4. Every `<codec>` has `name`, `<decode supported>`, and `<encode supported>`.
5. Every `supported` attribute is exactly `true`, `false`, or `unknown`.

The program's own test suite includes XML structure, escaping, UTF-8
sanitization, and atomic-write error tests.
