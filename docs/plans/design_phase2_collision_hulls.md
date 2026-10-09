# Design Document: Phase 2 - Universal Collision Hull Generation & Model Collision Overhaul

## Overview

This document specifies the technical design for universal collision hull generation and UI overhaul for GoldSrc submodels in revamped newbspguy. It adapts the core algorithms from branch `origin/feat/bsp-model-hull-generation-overhaul` into main, refining recursion safety, plane reuse, hull bounding alignment, and seamless integration with the Blood Obsidian interface.

## System Architecture

### Component Map

| Component ID | Name | Type | Responsibility | Interfaces With |
|-------------|------|------|----------------|-----------------|
| COMP-HULL-1 | ClipnodeGenerator | Core Algorithm | Builds clipnode trees from polygon faces or BSP nodes | `Bsp.h`, `Bsp.cpp` |
| COMP-HULL-2 | HullSimplifier | Geometry Processor | Computes simplified convex volumes and merges co-planar clipnodes | `ClipnodeGenerator`, `Bsp.h` |
| COMP-HULL-3 | HullUIManager | UI Component | Manages hull selection modal, generation options, and model context actions | `Renderer.cpp`, ImGui |

## Data Flow Specifications

### Universal Hull Generation Pipeline

```
1. User selects a submodel (or faces belonging to a model)
2. User chooses "Create Hull" -> selects target hulls (1, 2, 3, or all)
3. ClipnodeGenerator extracts polygon faces of the model
4. Plane deduplication / plane finding finds or inserts required planes into LUMP_PLANES
5. Recursive BSP splitting partitions the convex/concave volume into inside (SOLID) and outside (EMPTY)
6. Generated clipnodes are appended to LUMP_CLIPNODES
7. Target model's headnodes[hullIndex] is set to root index of the new clipnodes
8. BspRenderer updates collision wireframe visualization
```

## Core Interfaces & Algorithm Contracts

### Recursion & Generation API
```cpp
// In Bsp.h / Bsp.cpp
int convert_nodes_to_clipnodes_recursive(int nodeIdx, int hull);
int generate_clipnodes_from_model_faces(int modelIdx, int hull);
bool simplify_model_hull(int modelIdx, int hull, float epsilon);
```

### Safety and Lump Overflow Protection
- Prior to appending clipnodes to `LUMP_CLIPNODES`, the generator validates that `numclipnodes + newClipnodesCount < MAX_MAP_CLIPNODES (32767)`.
- If limit would be exceeded, the operation aborts cleanly and displays an error alert without corrupting the existing lump data.

## Error Handling

- **Concave / Complex Non-manifold Meshes**: Uses robust plane splitting with fallback to bounding box convex envelope if face subdivision fails to resolve.
- **Empty Model**: Returns error status early if model has no faces, displaying "Model contains no geometry to generate collision for."
