# Implementation Plan: Phase 3 - CLI Alignment, Asset Resolution & WIP Feature Hardening

- [ ] 1. CLI Parameter Alignment & Documentation Update
  - [ ] 1.1 Support `--verticalMerge` and `--verticalGap <dist>` in `CommandLine.cpp`
    - Add parsing support for `--verticalMerge` flag and `--verticalGap` value
    - Map them to the internal vertical overlap gap variable used by `-overlapgap`
    - Ensure backwards compatibility with `-overlapgap`
    - _Requirements: REQ-1_
  - [ ] 1.2 Update `README.md` CLI documentation
    - Document both `--verticalMerge`/`--verticalGap` and `-overlapgap`
    - Document `modent` (entity import/export) and `screenshot` CLI features
    - Ensure all CLI table entries reflect current codebase flags
    - _Requirements: REQ-1, REQ-2_

- [ ] 2. Asset Resolution Sanitization in Sprite Loading
  - [ ] 2.1 Audit and clean `Sprite.cpp`
    - Locate and remove all hardcoded paths (e.g. `d:\SteamLibrary\...`)
    - Connect sprite asset lookups to configured paths in `Settings` and map folder
    - Add graceful fallback if a `.spr` file is missing
    - _Requirements: REQ-3_

- [ ] 3. Experimental Tooling & OBJ Export Stabilization
  - [ ] 3.1 Audit `ExportObj.cpp` and CLI `exportobj` integration
    - Validate OBJ vertex/UV/face export logic
    - Ensure UI menu and CLI entry points pass valid parameters and handle errors gracefully
    - Preserve appropriate `(WIP)` markers on experimental menu items
    - _Requirements: REQ-4_

- [ ] 4. Verification & Testing
  - [ ] 4.1 Test CLI map merge using both `--verticalMerge --verticalGap` and `-overlapgap`
  - [ ] 4.2 Test sprite loading on systems without developer drive letters
  - [ ] 4.3 Verify compilation and build green on CI/local MSVC
