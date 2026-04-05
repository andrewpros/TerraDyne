# TerraDyne 0.3

TerraDyne is an MIT-licensed Unreal Engine 5.7 plugin that converts an authored Unreal Landscape into a persistent, networked, runtime-editable world without switching to voxels.

This release moves the public GitHub repo forward to the current runtime-world framework direction: authored landscape conversion remains the entry point, but the plugin now extends into persistent population state, procedural outskirts, gameplay hooks, and designer-facing world presets.

## Release Highlights

- Authored world conversion:
  landscape import, paint layer capture, placed foliage transfer, actor foliage transfer, save/load, and replicated terrain state.
- Runtime terrain editing:
  raise, lower, smooth, flatten, paint layers, undo/redo, and the existing TerraDyne tool panel.
- Persistent world population:
  register placed actors, place new runtime actors, persist destruction and harvest state, and restore regrowth-ready entries from saves.
- Procedural extension:
  seeded outskirts, biome overlays, runtime spawn rules, optional edge growth, and PCG-ready point export.
- Gameplay hooks:
  navmesh dirtying, AI spawn zone queries, build-permission checks, biome queries, Blueprint APIs, and terrain/foliage/population change delegates.
- Designer workflow:
  world presets, scene setup templates, a full feature showcase sequence, and packaged docs for the runtime-world layer.
- Hybrid rendering path:
  CPU-authoritative terrain with optional render-target-assisted visuals layered on top of the runtime system.

## Why TerraDyne

TerraDyne is aimed at survival, sandbox, and open-world projects that want to keep the authored Unreal Landscape workflow while gaining runtime persistence and terrain-driven world systems. Instead of replacing the authored world with a voxel stack, TerraDyne treats the original Landscape as the source world and converts it into a runtime-managed framework that can keep evolving during play.

## Requirements

- Unreal Engine `5.7`
- `GeometryScripting`
- `VirtualHeightfieldMesh`
- `Win64` is the currently packaged and validated target in this repository

## Installation

1. Copy the `TerraDyne` plugin folder into your project's `Plugins/` directory.
2. Enable `GeometryScripting` and `VirtualHeightfieldMesh`.
3. Regenerate project files.
4. Build the `Development Editor` target for your project.

## Quick Start

1. Add `BP_TerraDyneManager` to a level if you want direct runtime terrain control.
2. Add `ATerraDyneSceneSetup` if you want a template-driven bootstrap flow.
3. Choose a scene template:
   `Full Feature Showcase`, `Sandbox`, `Survival Framework`, or `Authored World Conversion`.
4. Import an authored Landscape or initialize a runtime world directly.
5. Press Play and use the TerraDyne panel to sculpt, paint, save/load, or run the showcase flow.

## Included Docs

- [`CHANGELOG.md`](CHANGELOG.md)
- [`DEMO.md`](DEMO.md)
- [`docs/runtime-world-framework.md`](docs/runtime-world-framework.md)

## Validation

- `TerraDyneEditor` builds cleanly on Unreal Engine 5.7.
- `Automation RunTests TerraDyne` passes with 16 TerraDyne tests.

## License

MIT. Code by Andras Gregori.
