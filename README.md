# Godot Engine

<p align="center">
  <a href="https://godotengine.org">
    <img src="misc/logo/logo_outlined.svg" width="400" alt="Godot Engine logo">
  </a>
</p>

## RTX fork: hardware ray tracing (4.8-dev)

This fork adds hardware ray traced effects to the Forward+ renderer. They replace
the screen-space versions when enabled:

- **Reflections** (replace SSR)
- **Global illumination** (replace SSIL), with temporal accumulation and a spatial denoiser
- **Shadows** for directional, omni and spot lights (contact shadows are traced against the TLAS)
- **Ambient occlusion** (ray traced, denoised)

It remains a rasterization renderer with ray tracing added on top.

### Enabling it

Set **Project Settings > Rendering > Ray Tracing > Enabled** (`rendering/ray_tracing/enabled`)
to `true` and restart the project. The renderer needs a Vulkan device with
`VK_KHR_ray_query` and `VK_KHR_buffer_device_address`. Without them, or with the
setting off, the engine uses its normal screen-space effects.

### Status

- The upstream CI workflows cover Linux, Windows, macOS, Android, iOS, Web and visionOS.
  The style check and the `Linux / Editor w/ Mono` job pass on the fork, including the godot-cpp
  build (which needed its archive step staged at a short path to avoid `Argument list too long`).
- Checked locally on Mesa lavapipe (software Vulkan) with the Khronos validation layer: the ray traced
  shaders compile and pass validation. Mesa's log reports `OpTypeRayQueryKHR is not supported yet`, so the
  ray query paths were not executed there and no rendered output has been checked.
- **Not yet verified on NVIDIA RTX or AMD RDNA2+ hardware.** Known gaps: skinned meshes
  are not in the acceleration structure, multiview renders without the ray traced shadow layer (shadow maps only),
  and GI disocclusion rejection is based on view depth only, so it has not been tested with a moving camera.

### Building

```sh
scons platform=linuxbsd target=editor dev_mode=no module_text_server_fb_enabled=yes accesskit=no -j8
```

### Releases

The `release.yml` workflow runs on `v*` tags and on manual dispatch with an existing tag.
Publishing creates a public GitHub release, so only dispatch it for a tag you intend to publish.

## 2D and 3D cross-platform game engine

**[Godot Engine](https://godotengine.org) is a feature-packed, cross-platform
game engine to create 2D and 3D games from a unified interface.** It provides a
comprehensive set of [common tools](https://godotengine.org/features), so that
users can focus on making games without having to reinvent the wheel. Games can
be exported with one click to a number of platforms, including the major desktop
platforms (Linux, macOS, Windows), mobile platforms (Android, iOS), as well as
Web-based platforms and [consoles](https://godotengine.org/consoles).

## Free, open source and community-driven

Godot is completely free and open source under the very permissive [MIT license](https://godotengine.org/license).
No strings attached, no royalties, nothing. The users' games are theirs, down
to the last line of engine code. Godot's development is fully independent and
community-driven, empowering users to help shape their engine to match their
expectations. It is supported by the [Godot Foundation](https://godot.foundation/)
not-for-profit.

Before being open sourced in [February 2014](https://github.com/godotengine/godot/commit/0b806ee0fc9097fa7bda7ac0109191c9c5e0a1ac),
Godot had been developed by [Juan Linietsky](https://github.com/reduz) and
[Ariel Manzur](https://github.com/punto-) for several years as an in-house
engine, used to publish several work-for-hire titles.

![Screenshot of a 3D scene in the Godot Engine editor](https://raw.githubusercontent.com/godotengine/godot-design/master/screenshots/editor_tps_demo_1920x1080.jpg)

## Getting the engine

### Binary downloads

Official binaries for the Godot editor and the export templates can be found
[on the Godot website](https://godotengine.org/download).

### Compiling from source

[See the official docs](https://docs.godotengine.org/en/latest/engine_details/development/compiling)
for compilation instructions for every supported platform.

## Community and contributing

Godot is not only an engine but an ever-growing community of users and engine
developers. The main community channels are listed [on the homepage](https://godotengine.org/community).

The best way to get in touch with the core engine developers is to join the
[Godot Contributors Chat](https://chat.godotengine.org).

To get started contributing to the project, see the [contributing guide](CONTRIBUTING.md).
This document also includes guidelines for reporting bugs.

## Documentation and demos

The official documentation is hosted on [Read the Docs](https://docs.godotengine.org).
It is maintained by the Godot community in its own [GitHub repository](https://github.com/godotengine/godot-docs).

The [class reference](https://docs.godotengine.org/en/latest/classes/)
is also accessible from the Godot editor.

We also maintain official demos in their own [GitHub repository](https://github.com/godotengine/godot-demo-projects)
as well as the [Asset Store](https://store.godotengine.org/).

There are also a number of other
[learning resources](https://docs.godotengine.org/en/latest/community/tutorials.html)
provided by the community, such as text and video tutorials, demos, etc.
Consult the [community channels](https://godotengine.org/community)
for more information.

[![Code Triagers Badge](https://www.codetriage.com/godotengine/godot/badges/users.svg)](https://www.codetriage.com/godotengine/godot)
[![Translate on Weblate](https://hosted.weblate.org/widgets/godot-engine/-/godot/svg-badge.svg)](https://hosted.weblate.org/engage/godot-engine/?utm_source=widget)
