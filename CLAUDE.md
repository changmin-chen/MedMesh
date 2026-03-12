# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

A WebAssembly-based medical imaging viewer that loads uncompressed NIFTI (.nii) volumetric data and renders interactive 3D iso-surfaces in the browser. The C++ core uses VTK's Flying Edges 3D algorithm, compiled to WASM via Emscripten and bridged to a vanilla JS frontend via Embind.

## Build Commands

All build steps run inside the `kitware/vtk-wasm-sdk` Docker container:

**Configure:**
```powershell
docker run --rm -it -v "${PWD}:/work" -w /work kitware/vtk-wasm-sdk:latest `
  emcmake cmake -GNinja -S /work -B /work/build -DVTK_DIR=/VTK-install/Release/wasm32/lib/cmake/vtk
```

**Build:**
```powershell
docker run --rm -it -v "${PWD}:/work" -w /work kitware/vtk-wasm-sdk:latest `
  cmake --build /work/build
```

**Run (after build):**
```powershell
cd build && python -m http.server 8000
```

There is no automated test suite. Manual testing: upload an uncompressed `.nii` file, adjust the iso slider, use Reset Camera.

## Architecture

### JS ↔ C++ Bridge

The frontend (`web/app.js`) calls into the C++ backend (`main.cpp`) via Emscripten Embind exports available on the global `Module` object:

| JS call | C++ function | Effect |
|---|---|---|
| `Module.loadNifti(path)` | `loadNifti(path)` | Read NIFTI from VFS, build iso-surface |
| `Module.setIsoValue(v)` | `setIsoValue(v)` | Update iso level, re-render |
| `Module.resetCamera()` | `resetCamera()` | Fit camera to geometry |
| `Module.getScalarMin/Max()` | `getScalarMin/Max()` | Query data range for slider bounds |

### File Upload Flow

`app.js:writeFileToVfs()` converts the browser `File` → `Uint8Array` → writes to Emscripten's virtual filesystem at `/data/{filename}`. That VFS path is then passed to `Module.loadNifti()`.

### VTK Rendering Pipeline (main.cpp)

Long-lived globals (`g_` prefix) hold the VTK pipeline objects: `g_renderer`, `g_renderWindow`, `g_interactor`, `g_style`, `g_reader` (vtkNIFTIImageReader), `g_isoSurface` (vtkFlyingEdges3D), `g_mapper`, `g_actor`. On startup a cone placeholder is shown; `loadNifti()` replaces it with the actual iso-surface.

`main()` is blocking — it initializes the VTK pipeline and starts the Emscripten event loop.

### CMake / Emscripten Flags

Key flags set in `CMakeLists.txt`: memory growth enabled, filesystem exported (`FS.*` available in JS), single-file WASM output. Post-build copies `web/` assets into `build/`.

## Coding Style

- C++: `g_` prefix for long-lived globals, C++20 standard
- JS: `camelCase`, 2-space indent, lowercase DOM IDs, IIFE module pattern
- Commit messages: imperative mood, <72 chars, one logical change per commit
