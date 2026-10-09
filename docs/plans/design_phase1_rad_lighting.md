# Design Document: Phase 1 - External Lighting Engine & Tooling Pipeline

## Overview

This document specifies the technical design for integrating and hardening external HLRAD/VHLT lighting compilation in revamped newbspguy. It adapts and refines the implementation from branch `origin/fix/recompile-lighting-rad` into main, ensuring C++20 standard conformance, cross-platform safety, safe child process invocation with quoted argument vectors, and non-blocking lightmap lump reloading.

## System Architecture

### Component Map

| Component ID | Name | Type | Responsibility | Interfaces With |
|-------------|------|------|----------------|-----------------|
| COMP-RAD-1 | CompilerSettings | Config | Stores and validates compiler path and custom flags | `Settings.h`, `Renderer.cpp` |
| COMP-RAD-2 | ProcessExecutor | System Utility | Executes subprocess with argument escaping and output capture | OS Process API, `Bsp.cpp` |
| COMP-RAD-3 | RadPipelineManager | Service | Orchestrates temp BSP export, RAD execution, error catching, and lump reload | `Renderer.cpp`, `BspRenderer.cpp` |
| COMP-RAD-4 | CompileErrorModal | UI Modal | Renders compilation error log and actionable instructions in Blood Obsidian theme | ImGui, `Renderer.cpp` |

## Data Flow Specifications

### Compilation & Hot-Reload Flow

```
1. User clicks "Tools -> Recompile Lighting" (or hotkey)
2. CompilerSettings validates HLRAD executable path exists
3. Bsp serializes current BSP state to disk or temp working copy
4. ProcessExecutor invokes HLRAD subprocess with quoted paths & flags
5. ProcessExecutor waits or monitors process exit code and collects stdout
6. IF exit_code != 0:
     CompileErrorModal opens with compiler diagnostics
   ELSE:
     Bsp reloads LUMP_LIGHTING bytes
     BspRenderer rebuilds OpenGL lightmap textures & triggers redraw
```

## Integration Points & Contracts

### Interface: Process Execution
```cpp
struct CompileResult {
    int exitCode;
    std::string logOutput;
    bool success;
};

CompileResult executeCompiler(const std::string& compilerPath, const std::vector<std::string>& args, const std::string& workingDir);
```

### Path Quoting Rules
All filesystem paths passed to system subprocesses must be quoted using standard POSIX/Windows conventions:
```cpp
std::string quoteArgument(const std::string& arg);
```

## Error Handling

- **Compiler Binary Missing**: Check `fs::exists(compilerPath)` prior to spawn; if false, show instant modal dialog directing user to `Settings -> Compilers`.
- **Compiler Failure (Non-zero exit)**: Intercept stdout/stderr log, trim trailing whitespace, present in a scrollable, selectable text box in `CompileErrorModal`.
- **Lump Deserialization Failure**: Validate `LUMP_LIGHTING` header before updating GPU memory to avoid OpenGL crashes or corrupted textures.
