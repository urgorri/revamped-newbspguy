# Implementation Plan: Phase 2 - Universal Collision Hull Generation & Model Collision Overhaul

- [ ] 1. Core Clipnode Generation Algorithms
  - [ ] 1.1 Review and adapt `convert_nodes_to_clipnodes_recursive` and `generate_clipnodes_from_model_faces` from `origin/feat/bsp-model-hull-generation-overhaul`
    - Verify plane matching and insertion logic into `LUMP_PLANES`
    - Ensure recursion termination conditions and stack safety
    - _Requirements: REQ-1_
  - [ ] 1.2 Implement clipnode limit bounds checking (`MAX_MAP_CLIPNODES`)
    - Protect lump buffer from overflow
    - _Requirements: REQ-1, REQ-2_

- [ ] 2. Hull Simplification & Optimization
  - [ ] 2.1 Port and harden hull simplification logic
    - Implement bounding box / convex approximation for complex models
    - Allow configuring hull target (Hull 1, 2, 3, or all)
    - _Requirements: REQ-2_
  - [ ] 2.2 Wire model `headnodes[hull]` updates and verify physics collision
    - Ensure proper leaf contents assignment (`CONTENTS_EMPTY` / `CONTENTS_SOLID`)
    - _Requirements: REQ-1, REQ-2_

- [ ] 3. GUI Unblocking & Blood Obsidian Integration
  - [ ] 3.1 Unblock "Create Hull" and "Simplify Hull" menu actions and button triggers
    - Allow invoking on any selected brush model or entity
    - Provide dialog modal to pick target hulls (1, 2, 3, or All)
    - _Requirements: REQ-3_
  - [ ] 3.2 Ensure undo/redo history registers hull modifications
    - Support undoing clipnode generation
    - _Requirements: REQ-3_

- [ ] 4. Verification & Testing
  - [ ] 4.1 Test generating Hull 1, 2, 3 on simple brush entities (e.g. `func_wall`)
  - [ ] 4.2 Test generating hulls on complex angled/sloped brush geometry
  - [ ] 4.3 Verify collision visualization in 3D viewport (F1/clipnode display mode)
