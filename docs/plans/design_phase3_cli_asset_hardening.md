# Design Document: Phase 3 - CLI Alignment, Asset Resolution & WIP Feature Hardening

## Overview

This document describes the architectural design for aligning command-line arguments, updating the CLI documentation, eliminating hardcoded development paths in `Sprite.cpp`, and stabilizing the OBJ export workflow.

## System Architecture

### Component Map

| Component ID | Name | Type | Responsibility | Interfaces With |
|-------------|------|------|----------------|-----------------|
| COMP-CLI-1 | CommandLineParser | CLI Module | Parses CLI flags, validates arguments, maps aliases | `CommandLine.cpp`, `main.cpp` |
| COMP-ASSET-1 | AssetResolver | Asset Manager | Searches configured game paths and map dirs for sprites/WADs | `Settings.h`, `Sprite.cpp` |
| COMP-EXP-1 | ObjExporter | Exporter | Serializes BSP faces into Wavefront OBJ/MTL formats | `ExportObj.cpp`, `Bsp.h` |

## CLI Syntax & Alias Specification

### Command Arguments
The CLI parser in `CommandLine.cpp` will be updated to accept both legacy and modern syntaxes:

| Modern Parameter | Legacy Parameter | Value | Behavior |
|------------------|------------------|-------|----------|
| `--verticalMerge` / `verticalMerge` | (implicit with `-overlapgap`) | None | Enables vertical merge mode |
| `--verticalGap <val>` / `verticalGap <val>` | `-overlapgap <val>` | Float / Integer | Sets vertical distance between merged maps |

Example invocation:
```bash
newbspguy merge map1.bsp map2.bsp -out merged.bsp --verticalMerge --verticalGap 256
newbspguy merge map1.bsp map2.bsp -out merged.bsp -overlapgap 256
```

## Asset Resolution Strategy

### Sprite Resolution Pipeline
Replace hardcoded path lookups in `Sprite.cpp`:
1. Search directory containing the opened `.bsp`.
2. Search configured base game directory (e.g., `valve/` or custom mod folder in `Settings`).
3. Search user-specified asset directories from `Settings::g_settings.assetDirs`.
4. Fall back to internal placeholder or skip rendering if not found, logging a clean warning message via `print_log`.

## Error Handling

- **Invalid CLI Arguments**: Print descriptive usage text and return non-zero exit code.
- **Unresolved Sprite Assets**: Soft fallback; do not abort map loading or throw unhandled exceptions.
- **OBJ File Write Permissions**: Catch filesystem exceptions and surface clear error message.
