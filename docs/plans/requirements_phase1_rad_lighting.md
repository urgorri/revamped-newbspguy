# Requirements Document: Phase 1 - External Lighting Engine & Tooling Pipeline

## Introduction

Revamped newbspguy requires a reliable, integrated compilation pipeline for external RAD (HLRAD/VHLT) compilers to recalculate lightmaps directly from the editor GUI. Previous implementations failed when compiler paths or working directories contained spaces, lacked robust exit code verification, failed to report compilation errors to users, or did not properly reload lightmap textures without restarting the editor. This phase restores and hardens the external lighting recompilation pipeline.

## Glossary

- **HLRAD / VHLT**: Half-Life Radiosity lighting compiler executables that calculate bounce lighting and generate BSP lightmap lumps.
- **RAD Lump**: `LUMP_LIGHTING` within the Half-Life BSP format storing ambient and direct light data samples.
- **Settings**: Configuration system in `src/util/Settings.h` storing compiler paths, custom flags, and asset directories in `settings.ini`.
- **Lightmap Reload**: Hot-reloading modified OpenGL lightmap textures and lumps in the running 3D viewport without re-opening the map.

## Requirements

### Requirement 1: Configurable Compiler Path with Whitespace Support

**User Story:** As a level designer, I want to specify any HLRAD compiler binary path (including paths with spaces such as `C:\Program Files (x86)\...`), so that compilation launches reliably regardless of installation location.

#### Acceptance Criteria
1. WHEN the user enters an HLRAD path in Settings containing spaces, THE compilation command generator SHALL wrap the executable and parameter paths in valid quote escaping.
2. WHERE the specified HLRAD executable does not exist or cannot be accessed, THE system SHALL prevent execution and notify the user with an actionable error modal.
3. THE system SHALL persist the configured compiler path across editor sessions in `settings.ini`.

### Requirement 2: Asynchronous / Protected Execution and Error Diagnostics

**User Story:** As a map editor, I want compilation to run safely without freezing or crashing the GUI, and to receive immediate error diagnostics if compilation fails, so that I understand why the lighting failed to build.

#### Acceptance Criteria
1. WHEN external compilation starts, THE system SHALL execute the compiler process in the map directory or designated temporary directory.
2. IF the compiler process exits with a non-zero exit code or terminates unexpectedly, THEN the system SHALL capture stdout/stderr and display an error dialog modal containing the compiler output summary.
3. THE system SHALL prevent concurrent compilation runs while a compilation is already active.

### Requirement 3: Automated Lightmap Hot-Reload

**User Story:** As a map editor, I want the editor to automatically refresh lightmaps in the 3D viewport once HLRAD completes, so that I can inspect lighting changes immediately without reloading the BSP.

#### Acceptance Criteria
1. WHEN compilation succeeds with exit code 0, THE system SHALL reload `LUMP_LIGHTING` from the updated BSP file on disk.
2. THE renderer SHALL regenerate OpenGL lightmap textures and bind them to the existing geometry without corrupting vertex buffers or model transforms.
3. THE status bar SHALL display a confirmation message indicating successful recompilation and elapsed duration.
