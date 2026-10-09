# Implementation Plan: Phase 1 - External Lighting Engine & Tooling Pipeline

- [ ] 1. Core Subprocess & Path Escaping Setup
  - [ ] 1.1 Review and adapt subprocess execution from `origin/fix/recompile-lighting-rad`
    - Verify whitespace handling in compiler executable paths and map paths
    - Ensure clean compilation under C++20 standard
    - _Requirements: REQ-1_
  - [ ] 1.2 Implement argument vector quoting utility
    - Create robust `quoteArgument` / parameter builder
    - _Requirements: REQ-1_

- [ ] 2. Execution Orchestration & Error Diagnostics
  - [ ] 2.1 Integrate `RadPipelineManager` and compiler runner
    - Support cancelable or safe synchronous execution with status feedback
    - Capture stdout/stderr buffer for diagnostics
    - _Requirements: REQ-2_
  - [ ] 2.2 Implement `CompileErrorModal` dialog in Blood Obsidian theme
    - Display compiler output when exit code is non-zero
    - Include "Copy to Clipboard" and "Open Settings" shortcuts
    - _Requirements: REQ-2_

- [ ] 3. Lightmap Lump Hot-Reloading & Viewport Refresh
  - [ ] 3.1 Implement `reload_lighting_lump` in `Bsp.cpp`
    - Read only `LUMP_LIGHTING` from disk into memory
    - Validate lump sizes against face light offsets
    - _Requirements: REQ-3_
  - [ ] 3.2 Update `BspRenderer.cpp` lightmap texture recreation
    - Invalidate and re-upload lightmap texture atlas to OpenGL
    - Trigger immediate viewport redraw
    - _Requirements: REQ-3_

- [ ] 4. Verification & Testing
  - [ ] 4.1 Test compilation on paths containing spaces (e.g. `C:\Program Files\...`)
  - [ ] 4.2 Test error handling when compiler binary does not exist
  - [ ] 4.3 Test in-place lightmap reload on a sample GoldSrc map
