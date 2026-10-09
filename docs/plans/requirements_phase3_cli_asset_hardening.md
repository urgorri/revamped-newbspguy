# Requirements Document: Phase 3 - CLI Alignment, Asset Resolution & WIP Feature Hardening

## Introduction

This phase focuses on eliminating CLI discrepancies between implementation and documentation, removing hardcoded development paths in asset loading, and stabilizing experimental tools. Specifically:
1. The README documents vertical map merging via `verticalMerge` and `verticalGap`, while the engine CLI parses `-overlapgap`. Both syntaxes must be cleanly supported.
2. The CLI table in `README.md` is missing recent commands (`modent`, `screenshot`, etc.).
3. `Sprite.cpp` contains hardcoded paths to local development environments (e.g. `d:\SteamLibrary`), causing failures when loading sprite files across machines.
4. The `exportobj` tool and related experimental tools need clean CLI/GUI integration and stabilization.

## Glossary

- **Overlap Gap / Vertical Merge**: Map merging mode where BSPs are stacked vertically with a user-specified separation distance.
- **CLI Alias**: Command-line flags that route to identical internal command structures.
- **Sprite Loader**: Engine component (`Sprite.cpp`) responsible for parsing and rendering GoldSrc `.spr` billboard assets.
- **OBJ Export**: Serializer in `ExportObj.cpp` converting BSP geometry to Wavefront OBJ format.

## Requirements

### Requirement 1: CLI Syntax Alignment & Alias Support

**User Story:** As an automation script author or command-line user, I want to use either `verticalMerge` / `verticalGap` or `-overlapgap`, so that scripts work seamlessly regardless of whether they follow the README or traditional CLI flags.

#### Acceptance Criteria
1. WHEN `--verticalMerge` and `--verticalGap <dist>` are passed on the command line, THE CLI parser SHALL map them to the vertical merge operation with the designated distance.
2. WHEN `-overlapgap <dist>` is passed on the command line, THE CLI parser SHALL continue to function identically for backwards compatibility.
3. THE `README.md` CLI documentation table SHALL list both aliases with comprehensive parameter descriptions.

### Requirement 2: CLI Documentation Completeness

**User Story:** As a user reading `README.md`, I want documentation for all supported CLI operations, including `modent` (entity import/export) and `screenshot`, so that I have complete reference material.

#### Acceptance Criteria
1. THE `README.md` command-line table SHALL include entries for `modent` and `screenshot` with usage examples and descriptions.
2. All CLI flag examples in `README.md` SHALL match the exact flags supported by `CommandLine.cpp`.

### Requirement 3: Asset Path Sanitization in Sprite Loading

**User Story:** As a user, I want sprite preview and loading to locate assets through configured game/mod search paths, so that the editor does not attempt to access non-existent developer drives.

#### Acceptance Criteria
1. THE system SHALL eliminate all hardcoded absolute filesystem paths (such as `d:\SteamLibrary\...`) from `Sprite.cpp` and related source files.
2. WHEN resolving `.spr` assets, THE loader SHALL query configured search directories from `Settings` and the loaded map directory.
3. IF a requested sprite asset is missing, THE system SHALL log a warning and use a placeholder or skip rendering without crashing.

### Requirement 4: Experimental Tooling & OBJ Export Stabilization

**User Story:** As a 3D modeler, I want the OBJ export tool to function reliably from both the CLI and GUI menu, preserving face materials and UV coordinates without memory errors.

#### Acceptance Criteria
1. THE OBJ exporter SHALL correctly write vertex positions, normals, and UV coordinates to `.obj` and accompanying `.mtl` files.
2. THE GUI menu entries for experimental tools SHALL clearly indicate WIP status where appropriate while ensuring safe, crash-free execution.
