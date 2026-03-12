// SPDX-License-Identifier: BSD-3-Clause

#include "vtkActor.h"
#include "vtkConeSource.h"
#include "vtkFlyingEdges3D.h"
#include "vtkInteractorStyleTrackballCamera.h"
#include "vtkNIFTIImageReader.h"
#include "vtkNew.h"
#include "vtkPolyDataMapper.h"
#include "vtkProperty.h"
#include "vtkRenderer.h"
#include "vtkImageData.h"

#include "vtkWebAssemblyOpenGLRenderWindow.h"
#include "vtkWebAssemblyRenderWindowInteractor.h"

#include <emscripten/bind.h>

#include <iostream>
#include <string>

namespace {

constexpr const char* kCanvasSelector = "#canvas";

// Keep objects alive
vtkSmartPointer<vtkRenderer> g_renderer;
vtkSmartPointer<vtkWebAssemblyOpenGLRenderWindow> g_renderWindow;
vtkSmartPointer<vtkWebAssemblyRenderWindowInteractor> g_interactor;

vtkSmartPointer<vtkInteractorStyleTrackballCamera> g_style;

vtkSmartPointer<vtkConeSource> g_placeholder;
vtkSmartPointer<vtkPolyDataMapper> g_mapper;
vtkSmartPointer<vtkActor> g_actor;

vtkSmartPointer<vtkNIFTIImageReader> g_reader;
vtkSmartPointer<vtkFlyingEdges3D> g_isoSurface;

double g_isoValue = 300.0;
double g_scalarRange[2] = {0.0, 1.0};

void SafeRender()
{
  if (g_renderWindow)
  {
    g_renderWindow->Render();
  }
}

} // namespace

// -----------------------------------------------------------------------------
// Embind-exported API (callable from JS)
// -----------------------------------------------------------------------------

void render()
{
  SafeRender();
}

void resetCamera()
{
  if (!g_renderer)
    return;
  g_renderer->ResetCamera();
  SafeRender();
}

double getScalarMin()
{
  return g_scalarRange[0];
}

double getScalarMax()
{
  return g_scalarRange[1];
}

void setIsoValue(double value)
{
  g_isoValue = value;

  // If surface not yet built, nothing to update (still ok)
  if (g_isoSurface)
  {
    g_isoSurface->SetValue(0, g_isoValue);
    g_isoSurface->Modified();
    SafeRender();
  }
}

void loadNifti(const std::string& virtualPath)
{
  std::cout << "[VTK] loadNifti: " << virtualPath << std::endl;

  if (!g_reader)
  {
    g_reader = vtkSmartPointer<vtkNIFTIImageReader>::New();
  }
  g_reader->SetFileName(virtualPath.c_str());
  g_reader->Update();

  vtkImageData* image = g_reader->GetOutput();
  if (!image)
  {
    std::cerr << "[VTK] ERROR: reader output is null.\n";
    return;
  }

  image->GetScalarRange(g_scalarRange);
  std::cout << "[VTK] ScalarRange: [" << g_scalarRange[0] << ", " << g_scalarRange[1] << "]\n";

  if (!g_isoSurface)
  {
    g_isoSurface = vtkSmartPointer<vtkFlyingEdges3D>::New();
    g_isoSurface->ComputeNormalsOn();
    g_isoSurface->ComputeScalarsOff();
  }

  g_isoSurface->SetInputConnection(g_reader->GetOutputPort());
  g_isoSurface->SetValue(0, g_isoValue);
  g_isoSurface->Update();

  if (g_mapper)
  {
    g_mapper->SetInputConnection(g_isoSurface->GetOutputPort());
    g_mapper->Update();
  }

  if (g_renderer)
  {
    g_renderer->ResetCamera();
  }

  SafeRender();
}

EMSCRIPTEN_BINDINGS(med_mesh_module)
{
  emscripten::function("render", &render);
  emscripten::function("resetCamera", &resetCamera);
  emscripten::function("getScalarMin", &getScalarMin);
  emscripten::function("getScalarMax", &getScalarMax);
  emscripten::function("setIsoValue", &setIsoValue);
  emscripten::function("loadNifti", &loadNifti);
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(int, char**)
{
  // Renderer
  g_renderer = vtkSmartPointer<vtkRenderer>::New();
  g_renderer->SetBackground(0.12, 0.12, 0.12);

  // WebAssembly OpenGL RenderWindow
  g_renderWindow = vtkSmartPointer<vtkWebAssemblyOpenGLRenderWindow>::New();
  g_renderWindow->SetMultiSamples(0);
  g_renderWindow->SetCanvasSelector(kCanvasSelector);
  g_renderWindow->AddRenderer(g_renderer);

  // WebAssembly Interactor
  g_interactor = vtkSmartPointer<vtkWebAssemblyRenderWindowInteractor>::New();
  g_interactor->SetCanvasSelector(kCanvasSelector);
  g_interactor->SetRenderWindow(g_renderWindow);

  // Style
  g_style = vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New();
  g_interactor->SetInteractorStyle(g_style);
  g_style->SetDefaultRenderer(g_renderer);

  // Placeholder cone pipeline
  g_placeholder = vtkSmartPointer<vtkConeSource>::New();
  g_placeholder->SetResolution(24);
  g_placeholder->Update();

  g_mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
  g_mapper->SetInputConnection(g_placeholder->GetOutputPort());

  g_actor = vtkSmartPointer<vtkActor>::New();
  g_actor->SetMapper(g_mapper);
  // g_actor->GetProperty()->SetEdgeVisibility(1);
  // g_actor->GetProperty()->SetEdgeColor(1.0, 0.0, 1.0);

  g_renderer->AddActor(g_actor);
  g_renderer->ResetCamera();

  // Initial render
  g_renderWindow->Render();

  // Start event loop
  g_interactor->Start();

  return 0;
}
