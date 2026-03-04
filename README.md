# 🌍 TerraDyne: Dynamic Landscape System
![image](https://img.shields.io/badge/-Unreal%20Engine-313131?style=for-the-badge&logo=unreal-engine&logoColor=blue) ![image](https://img.shields.io/badge/C%2B%2B-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=blue) ![image](https://img.shields.io/badge/Python-FFD43B?style=for-the-badge&logo=python&logoColor=blue) ![image](https://img.shields.io/badge/json-5E5C5C?style=for-the-badge&logo=json&logoColor=white) ![image](https://img.shields.io/badge/MIT-green?style=for-the-badge) ![alt text](https://img.shields.io/github/stars/gregorik/InstantOrganicCaves) ![alt text](https://img.shields.io/badge/Support-Patreon-red) [![YouTube](https://img.shields.io/badge/YouTube-Subscribe-red?style=flat&logo=youtube)](https://www.youtube.com/@agregori) [![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/C0C616ULD4)

**TerraDyne** is an experimental, high-performance, native C++ landscape plugin for **Unreal Engine 5.7**. It decouples physics from visuals to allow for infinite real-time runtime modification—digging, raising, and painting—while maintaining cinematic visual fidelity using Virtual Heightfield Mesh (VHFM) displacement. As of early 2026, I opened a new, advanced 0.3+ branch distributed on Fab.

> **Dependencies:** Geometry Scripting, Virtual Heightfield Mesh
<br>

[Watch an early video here.](https://www.youtube.com/watch?v=T4J53XLgaDI)

<br>

![TerraDyne Banner (WIP)](https://via.placeholder.com/1000x200?text=TerraDyne+Dynamic+Terrain+System)

| <i><b>Comparison | <i><b>GitHub version (0.1+ MIT)           | <i>[FAB edition (0.3+ Closed)](https://www.fab.com/listings/20f72d71-9737-4ac1-bce5-034eca166529)</b></i>                |
|:---|:---|:---|
| **Version** | Core | Fully featured + updated |
| **Distribution** | Source only | Binaries, vetted by Epic |
| **Engine support** | UE 5.7.0 | UE 5.6 - 5.7.3+ |
| **Chunk-based / Layered height system** | Included | Included |
| **Dynamic Mesh Generation** | Included | Included |
| **Material System** | Included | Included |
| **Runtime Sculpting Tools** | Included | Included |
| **Brush Parameters** | Included | Included | 
| **Native Slate editor panel** | Included | Included |
| **Real-time Preview** | n/a | Included |
| **GPU Acceleration (experimental)** | n/a | Included |
| **4 Weight Layers (RGBA)** | n/a | Included | 
| **Live GPU Upload** | n/a | Included |
| **Material Integration** | n/a | Included |
| **Weight data serialization** | n/a | Included |
| **HISM-Based vegetation rendering** | n/a | Included |
| **Grass Profile Data Asset** | n/a | Included |
| **Background Generation (Async task)** | n/a | Included |
| **Smart Regeneration: Debounced rebuild** | n/a | Included |
| **Persistence (Save/Load system)** | n/a | Included |
| **Undo/Redo System** | n/a | Included |
| **Multiplayer Support** | n/a | Included |
| **Updates** | n/a | Regular, vetted by Epic |
| **Quality Assurance** | GitHub Issues | Vetted by Epic, tested by author |
| **Support** | GitHub Issues | Forum & Email |
<br><br>

## ✨ Key Features

*   **Runtime Voxel-Like Editing:** Deform terrain in real-time using high-performance CPU-side arrays synced to GPU textures. Dig craters, build mounds, or tunnel (via masking).
*   **Non-Nanite Tessellation:** Utilizes **Virtual Heightfield Mesh (VHFM)** to render millions of polygons with view-dependent displacement, bypassing the removal of tessellation in UE5.
*   **Geometric Resampling Importer:** "Nuclear Option" import tool that physically scans existing UE Landscapes and converts them to TerraDyne Chunks, independent of internal data formats.
*   **Hybrid Physics Architecture:**
    *   **Visuals:** Handled by GPU (Render Targets -> VHFM).
    *   **Physics:** Handled by Async `UDynamicMesh` (Geometry Script) for accurate collision.
*   **Seamless Normals:** Custom HLSL shader logic eliminates visual seams between chunks by calculating normals from the global heightmap rather than local vertex geometry.
*   **Async Serialization:** Save and Load terrain state to disk on a background thread with zero game-thread hitching.
*   **"Self-Healing" Sandbox:** Chunks auto-initialize default data if placed manually, ensuring gameplay logic (projectiles, raycasts) never fails against uninitialized memory.

---

## 🚀 Installation

1.  **Requirements:** Unreal Engine **5.7** or higher.
2.  **Plugin Setup:**
    *   Copy the `TerraDyne` folder into your project's `Plugins/` directory.
    *   If the `Plugins` directory does not exist, create it in your project root.
3.  **Enable Dependencies:**
    Open your `.uproject` file or go to **Edit > Plugins** and ensure the following are enabled:
    *   `GeometryScripting`
    *   `VirtualHeightfieldMesh`
4.  **Compile:**
    *   Right-click your `.uproject` -> **Generate Visual Studio Project Files**.
    *   Open the Solution (`.sln`) and build the **Development Editor** configuration.

---

## 🎮 Quick Start (The Demo)

TerraDyne includes a "Zero-Config" demo mode to test the tech immediately.

1.  Create a **New Level** (Basic).
2.  Place **`BP_TerraDyneManager`** (from `Content/TerraDyne/Blueprints/`) at `0, 0, 0`.
3.  Place **`TerraDyneOrchestrator`** (C++ Actor) at `0, 0, 3000`.
4.  **Press Play**.

**What happens?**
*   The Manager detects no landscape exists and auto-spawns a **Sandbox Chunk (1km)**.
*   The Orchestrator spawns physics projectiles.
*   In theory, projectiles impact the ground, creating **Deformation (Craters)** and **Painting (Magma)**.
*   In theory, physics meshes update instantly; projectiles roll into the craters they just created.

---

## 🛠️ Usage Guide

### 1. The Manager (`BP_TerraDyneManager`)
This is the brain of the system.
*   **Chunk Class:** Ensure this is set to `BP_TerraDyneChunk`.
*   **Master Material:** Ensure this is set to `M_TerraDyne_Master`.
*   **Brush Materials:** Assign `M_HeightBrush` and `M_WeightBrush` in the Tools category to enable drawing.

### 2. Importing a Landscape
To convert a standard Epic Landscape into Dynamic Chunks:
1.  Place `BP_TerraDyneManager` in the level.
2.  Select the Manager.
3.  In the Details Panel, find **TerraDyne | Tools**.
4.  Set **Auto Import At Runtime** to `True` (simplest method).
    *   *Alternatively:* Call the `ImportFromLandscape` function via Blueprint/Blutility if you exposed the button manually.
5.  **Hide** your original Landscape.
6.  **Play**. The system will raycast-scan your landscape and generate matching dynamic chunks.

### 3. Runtime Interaction (C++)
To dig a hole from C++ code (e.g., from a weapon or vehicle):

```cpp
// 1. Get the Subsystem
if (UTerraDyneSubsystem* Sys = GetWorld()->GetSubsystem<UTerraDyneSubsystem>())
{
    // 2. Get the Manager
    if (ATerraDyneManager* Manager = Sys->GetTerrainManager())
    {
        // 3. Apply Brush
        // Location (World), Radius, Strength (Negative=Dig), bIsHole (Visibility mask)
        Manager->ApplyGlobalBrush(HitLocation, 500.0f, -100.0f, false);
    }
}
```

---

## 🎨 Materials & Visuals

### The Master Material (`M_TerraDyne_Master`)
TerraDyne does not use standard mesh normals. It uses a **3-Tap Texture Normal** calculation to ensure lighting flows perfectly across chunk borders.

*   **HeightMap:** The R16f Render Target driving displacement.
*   **WeightMap:** RGBA texture for layer blending.
    *   **R:** Layer 0 (Base) / Blend
    *   **G:** Layer 1 (e.g., Magma/Snow)
*   **ZScale:** Controls vertical displacement intensity.

### Reactive Layers (Magma Effect)
The demo setup uses **Layer 1 (Green Channel)** as an Emissive Layer.
*   When `ApplyPaintBrush` is called with `LayerIndex = 1`, the material reads the Green channel and multiplies it by a generic `MagmaColor` vector, piping it into Emissive.

---

## 📐 Architecture Overview

| Class | Role | Path |
| :--- | :--- | :--- |
| **`ATerraDyneManager`** | Orchestrator. Handles global brush dispatch and landscape import. | `Core/` |
| **`ATerraDyneChunk`** | The Tile. Holds the `DynamicMesh` (Physics) and `VHFM` (Visuals). | `World/` |
| **`UTerraDyneSubsystem`** | Global Registry. Allows actors to find the Manager without `GetAllActorsOfClass`. | `Core/` |
| **`FTerraDyneAsyncSaver`** | Background Worker. Zlib compresses float arrays to disk. | `IO/` |
| **`AMedMeshProjectile`** | Demo Actor. Validates physics sync by rolling down generated slopes. | `World/` |

---

## ⚠️ Troubleshooting

**Q: Example orbs bounce without making a hole.**
*   *Cause:* The Chunk did not initialize its data cache or tool materials are missing.
*   *Fix:* Ensure `BP_TerraDyneManager` has `M_HeightBrush` and `M_WeightBrush` assigned in the "Tools" details category. The Self-Healing logic should handle the rest.

**Q: I see holes, but they don't glow.**
*   *Cause:* Material parameter mismatch.
*   *Fix:* Open `M_TerraDyne_Master`. Ensure the logic flow is: `WeightMap` -> `ComponentMask(G)` -> `Multiply(Orange)` -> `Emissive`.

**Q: Import buttons are missing.**
*   *Cause:* UE5 Hot Reload bug.
*   *Fix:* Close Editor. Delete `Binaries/` and `Intermediate/` folders. Rebuild Solution. Re-open Editor.

---

## 📝 License

MIT. Code by Andras Gregori.
