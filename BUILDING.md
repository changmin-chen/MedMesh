# Building MedMesh

All build steps run inside the [`kitware/vtk-wasm-sdk`](https://hub.docker.com/r/kitware/vtk-wasm-sdk) Docker container.

## Configure

```powershell
docker run --rm -it -v "${PWD}:/work" -w /work kitware/vtk-wasm-sdk:latest `
  emcmake cmake -GNinja -S /work -B /work/build -DVTK_DIR=/VTK-install/Release/wasm32/lib/cmake/vtk
```

## Build

```powershell
docker run --rm -it -v "${PWD}:/work" -w /work kitware/vtk-wasm-sdk:latest `
  cmake --build /work/build
```

## Run

```powershell
cd build && python -m http.server 8000
```

Then open `http://localhost:8000` in your browser.
