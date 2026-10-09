# Requirements Document: Phase 2 - Universal Collision Hull Generation & Model Collision Overhaul

## Introduction

In GoldSrc BSP files, solid collision for players and monsters is handled via specialized bounding clipnodes stored in collision hulls (Hull 0 = point/rendering, Hull 1 = human standing player, Hull 2 = large monster, Hull 3 = crouching player). Currently, submodels (brush entities like `func_wall`, `func_door`, `func_pushable`) often lack clipnodes or suffer from broken hull generation when manipulated or simplified in the editor.

Furthermore, UI operations for "Create Hull", "Simplify Hull", and related clipnode generation tools were previously restricted, causing user frustration or crashes. This phase delivers universal collision hull generation and unlocks complete clipnode editing across all BSP submodels.

## Glossary

- **Clipnode**: Node structure in BSP (`BSPCLIPNODE`) that defines a dividing plane and child leaves (contents `CONTENTS_EMPTY` or `CONTENTS_SOLID`) used strictly for collision raycasts and player physics.
- **Hull**: Collision hierarchy. Hull 0 is standard rendering BSP nodes; Hulls 1, 2, and 3 are offset collision hulls for different bounding box dimensions.
- **Model Faces Ladder / Polygon Clipping**: Algorithm for deriving convex or approximated convex collision boundaries from the visual polygon faces of a brush entity model.
- **Submodel**: Any non-worldspawn model index (`1 <= modelIdx < nummodels`).

## Requirements

### Requirement 1: Universal Submodel Clipnode Generation

**User Story:** As a level designer, I want to generate collision hulls 1, 2, and 3 for any brush entity model from its visual polygon faces, so that players and entities collide accurately with custom brush models.

#### Acceptance Criteria
1. WHEN the user triggers "Create Hull" or "Generate Clipnodes" on a selected submodel, THE system SHALL convert the visual faces of the model into a valid BSP clipnode tree.
2. THE system SHALL support generating clipnodes for Hull 1, Hull 2, Hull 3, or all hulls simultaneously.
3. THE generated clipnode tree SHALL correctly close empty and solid space, ensuring no player leaks or fall-throughs occur.

### Requirement 2: Hull Simplification and Leaf Pruning

**User Story:** As an optimizer, I want to simplify complex model collision hulls into coarse bounding hulls, so that I can prevent clipnode lump limits (`MAX_MAP_CLIPNODES`) from being exceeded.

#### Acceptance Criteria
1. WHEN the user selects "Simplify Hull", THE system SHALL calculate a simplified convex hull or bounding convex volume for the target submodel.
2. THE system SHALL replace the detailed clipnode tree of the selected hull with the simplified clipnode tree while preserving original visual faces.
3. THE system SHALL update the model's `headnodes` array indices accordingly.

### Requirement 3: Unrestricted UI Controls & Model Selection

**User Story:** As an editor, I want the collision hull tools in the GUI to be active and functional for any valid model without arbitrary UI disabling or crashes on complex geometry.

#### Acceptance Criteria
1. WHERE any valid submodel or brush entity is selected, THE "Create Hull" and "Simplify Hull" menu items and panel buttons SHALL be enabled and responsive.
2. IF a model has zero faces or invalid geometry, THEN the system SHALL gracefully alert the user rather than throwing an unhandled exception or crashing.
3. THE system SHALL support undo/redo for collision hull generation operations.
