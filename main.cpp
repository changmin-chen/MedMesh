// SPDX-License-Identifier: BSD-3-Clause

#include "MedMeshApp.h"

#include <emscripten/bind.h>

namespace {

MedMeshApp& app()
{
  return MedMeshApp::Instance();
}

void render()
{
  app().Render();
}

void resetCamera()
{
  app().ResetCamera();
}

bool applyMeshSettings(double value, bool keepLargestComponent)
{
  return app().ApplyMeshSettings(value, keepLargestComponent);
}

double getIsoValue()
{
  return app().GetIsoValue();
}

double getScalarMin()
{
  return app().GetScalarMin();
}

double getScalarMax()
{
  return app().GetScalarMax();
}

std::string getLastError()
{
  return app().GetLastError();
}

bool hasMesh()
{
  return app().HasMesh();
}

bool getKeepLargestComponent()
{
  return app().GetKeepLargestComponent();
}

void setIsoValue(double value)
{
  app().SetIsoValue(value);
}

void setKeepLargestComponent(bool keepLargestComponent)
{
  app().SetKeepLargestComponent(keepLargestComponent);
}

bool loadNifti(const std::string& virtualPath)
{
  return app().LoadNifti(virtualPath);
}

bool loadDicom(const std::string& virtualDirectory)
{
  return app().LoadDicom(virtualDirectory);
}

bool exportStl(const std::string& virtualPath)
{
  return app().ExportStl(virtualPath);
}

} // namespace

EMSCRIPTEN_BINDINGS(med_mesh_module)
{
  emscripten::function("render", &render);
  emscripten::function("resetCamera", &resetCamera);
  emscripten::function("applyMeshSettings", &applyMeshSettings);
  emscripten::function("getIsoValue", &getIsoValue);
  emscripten::function("getScalarMin", &getScalarMin);
  emscripten::function("getScalarMax", &getScalarMax);
  emscripten::function("getLastError", &getLastError);
  emscripten::function("hasMesh", &hasMesh);
  emscripten::function("getKeepLargestComponent", &getKeepLargestComponent);
  emscripten::function("setIsoValue", &setIsoValue);
  emscripten::function("setKeepLargestComponent", &setKeepLargestComponent);
  emscripten::function("loadNifti", &loadNifti);
  emscripten::function("loadDicom", &loadDicom);
  emscripten::function("exportStl", &exportStl);
}

int main(int, char**)
{
  app().Initialize();
  app().Start();
  return 0;
}
