## Configure
docker run --rm -it -v "${PWD}:/work" -w /work kitware/vtk-wasm-sdk:latest `
  emcmake cmake -GNinja -S /work -B /work/build -DVTK_DIR=/VTK-install/Release/wasm32/lib/cmake/vtk


## Build 
docker run --rm -it -v "${PWD}:/work" -w /work kitware/vtk-wasm-sdk:latest `
  cmake --build /work/build
