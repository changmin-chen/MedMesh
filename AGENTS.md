# Repository Guidelines

## Project Structure & Module Organization
This repository is a small VTK WebAssembly demo for loading `.nii` volumes in the browser. Core rendering logic lives in `main.cpp`, and the build definition is in `CMakeLists.txt`. Browser UI assets live in `web/`:

- `web/index.html` boots the page and Emscripten `Module`
- `web/app.js` wires file upload, iso slider, and camera reset
- `web/style.css` handles the toolbar and canvas layout

Treat `build/`, `build-*`, and `cmake-build-*` as generated output. Do not hand-edit generated files such as `build/nifti_demo.js`.

## Build, Test, and Development Commands
Builds are expected to run inside Kitware’s VTK WebAssembly SDK container.

```powershell
docker run --rm -it -v "${PWD}:/work" -w /work kitware/vtk-wasm-sdk:latest `
  emcmake cmake -GNinja -S /work -B /work/build -DVTK_DIR=/VTK-install/Release/wasm32/lib/cmake/vtk
docker run --rm -it -v "${PWD}:/work" -w /work kitware/vtk-wasm-sdk:latest `
  cmake --build /work/build
```

Use a static server to preview the generated app from `build/`, for example:

```powershell
cd build
python -m http.server 8000
```

## Coding Style & Naming Conventions
Follow the existing style: 2-space indentation, short functions, and minimal comments. Keep C++ in C++20-compatible form. Match current naming patterns: `camelCase` for JS functions (`writeFileToVfs`), `g_` prefixes for long-lived C++ VTK state (`g_renderWindow`), and lowercase IDs for DOM elements (`file`, `iso`, `reset`). No formatter or linter is configured, so keep edits consistent with neighboring code.

## Testing Guidelines
There is no automated test suite or coverage gate yet. Validate changes manually by rebuilding, opening the app, uploading an uncompressed `.nii` file, adjusting the iso slider, and using Reset Camera. If you add non-trivial logic, prefer adding a future `tests/` target through CTest rather than expanding manual-only verification.

## Commit & Pull Request Guidelines
No local `.git` history is available in this workspace, so use a simple conventional standard: imperative commit subjects under 72 characters, focused per change. Pull requests should describe the user-visible effect, list validation steps, and include screenshots for UI changes. Exclude generated build artifacts and IDE folders unless the change explicitly targets build output.
