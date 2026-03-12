// SPDX-License-Identifier: BSD-3-Clause

#include "MedMeshApp.h"

#include "vtkActor.h"
#include "vtkCleanPolyData.h"
#include "vtkDICOMImageReader.h"
#include "vtkFlyingEdges3D.h"
#include "vtkImageData.h"
#include "vtkLight.h"
#include "vtkInteractorStyleTrackballCamera.h"
#include "vtkNIFTIImageReader.h"
#include "vtkNew.h"
#include "vtkPolyData.h"
#include "vtkPolyDataConnectivityFilter.h"
#include "vtkPolyDataMapper.h"
#include "vtkProperty.h"
#include "vtkRenderer.h"
#include "vtkSTLWriter.h"
#include "vtkTriangleFilter.h"
#include "vtkWebAssemblyOpenGLRenderWindow.h"
#include "vtkWebAssemblyRenderWindowInteractor.h"
#include "vtkWindowedSincPolyDataFilter.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

constexpr const char* kCanvasSelector = "#canvas";
constexpr int kSmoothingIterations = 15;
constexpr double kSmoothingPassBand = 0.12;
constexpr double kSmoothingFeatureAngle = 120.0;
constexpr double kSmoothingEdgeAngle = 15.0;

void ConfigureMeshMaterial(vtkActor* actor)
{
  if (!actor)
  {
    return;
  }

  vtkProperty* property = actor->GetProperty();
  property->SetColor(0.86, 0.72, 0.66);
  property->SetInterpolationToPhong();
  property->SetAmbient(0.18);
  property->SetDiffuse(0.78);
  property->SetSpecular(0.08);
  property->SetSpecularPower(20.0);
}

void AddCameraLight(vtkRenderer* renderer, double x, double y, double z, double intensity,
  double r, double g, double b)
{
  vtkNew<vtkLight> light;
  light->SetLightTypeToCameraLight();
  light->SetPosition(x, y, z);
  light->SetFocalPoint(0.0, 0.0, 0.0);
  light->SetDiffuseColor(r, g, b);
  light->SetSpecularColor(r, g, b);
  light->SetIntensity(intensity);
  renderer->AddLight(light);
}

void ConfigureLighting(vtkRenderer* renderer)
{
  if (!renderer)
  {
    return;
  }

  renderer->AutomaticLightCreationOff();
  renderer->RemoveAllLights();
  renderer->LightFollowCameraOn();

  AddCameraLight(renderer, 0.9, 1.0, 1.2, 1.00, 1.00, 0.97, 0.92);
  AddCameraLight(renderer, -1.2, 0.3, 0.8, 0.45, 0.84, 0.90, 1.00);
  AddCameraLight(renderer, -0.6, -1.0, 0.4, 0.20, 1.00, 1.00, 1.00);
}

void ConfigureCleanFilter(vtkCleanPolyData* cleaner)
{
  if (!cleaner)
  {
    return;
  }

  cleaner->PointMergingOn();
  cleaner->SetTolerance(0.0);
}

void ConfigureConnectivityFilter(
  vtkPolyDataConnectivityFilter* connectivity, bool keepLargestComponent)
{
  if (!connectivity)
  {
    return;
  }

  connectivity->ColorRegionsOff();
  if (keepLargestComponent)
  {
    connectivity->SetExtractionModeToLargestRegion();
  }
  else
  {
    connectivity->SetExtractionModeToAllRegions();
  }
}

void ConfigureSmoother(vtkWindowedSincPolyDataFilter* smoother)
{
  if (!smoother)
  {
    return;
  }

  smoother->SetNumberOfIterations(kSmoothingIterations);
  smoother->SetPassBand(kSmoothingPassBand);
  smoother->SetFeatureAngle(kSmoothingFeatureAngle);
  smoother->SetEdgeAngle(kSmoothingEdgeAngle);
  smoother->BoundarySmoothingOff();
  smoother->FeatureEdgeSmoothingOff();
  smoother->NormalizeCoordinatesOn();
}

std::vector<std::filesystem::path> ListSortedChildren(const std::filesystem::path& directory)
{
  std::vector<std::filesystem::path> children;
  std::error_code errorCode;
  for (const auto& entry : std::filesystem::directory_iterator(directory, errorCode))
  {
    if (errorCode)
    {
      break;
    }
    children.push_back(entry.path());
  }

  std::sort(children.begin(), children.end());
  return children;
}

} // namespace

MedMeshApp& MedMeshApp::Instance()
{
  static MedMeshApp instance;
  return instance;
}

void MedMeshApp::Initialize()
{
  if (initialized_)
  {
    return;
  }

  renderer_ = vtkSmartPointer<vtkRenderer>::New();
  renderer_->SetBackground(0.10, 0.10, 0.12);
  ConfigureLighting(renderer_);

  renderWindow_ = vtkSmartPointer<vtkWebAssemblyOpenGLRenderWindow>::New();
  renderWindow_->SetMultiSamples(0);
  renderWindow_->SetCanvasSelector(kCanvasSelector);
  renderWindow_->AddRenderer(renderer_);

  interactor_ = vtkSmartPointer<vtkWebAssemblyRenderWindowInteractor>::New();
  interactor_->SetCanvasSelector(kCanvasSelector);
  interactor_->SetRenderWindow(renderWindow_);

  style_ = vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New();
  style_->SetDefaultRenderer(renderer_);
  interactor_->SetInteractorStyle(style_);

  isoSurface_ = vtkSmartPointer<vtkFlyingEdges3D>::New();
  isoSurface_->ComputeNormalsOn();
  isoSurface_->ComputeScalarsOff();

  meshPreClean_ = vtkSmartPointer<vtkCleanPolyData>::New();
  ConfigureCleanFilter(meshPreClean_);
  meshPreClean_->SetInputConnection(isoSurface_->GetOutputPort());

  meshConnectivity_ = vtkSmartPointer<vtkPolyDataConnectivityFilter>::New();
  meshConnectivity_->SetInputConnection(meshPreClean_->GetOutputPort());
  UpdateConnectivityMode();

  meshTriangulator_ = vtkSmartPointer<vtkTriangleFilter>::New();
  meshTriangulator_->SetInputConnection(meshConnectivity_->GetOutputPort());
  meshTriangulator_->PassVertsOff();
  meshTriangulator_->PassLinesOff();

  meshSmoother_ = vtkSmartPointer<vtkWindowedSincPolyDataFilter>::New();
  meshSmoother_->SetInputConnection(meshTriangulator_->GetOutputPort());
  ConfigureSmoother(meshSmoother_);

  meshFinalClean_ = vtkSmartPointer<vtkCleanPolyData>::New();
  ConfigureCleanFilter(meshFinalClean_);
  meshFinalClean_->SetInputConnection(meshSmoother_->GetOutputPort());

  mapper_ = vtkSmartPointer<vtkPolyDataMapper>::New();
  mapper_->SetInputConnection(meshFinalClean_->GetOutputPort());
  mapper_->ScalarVisibilityOff();

  actor_ = vtkSmartPointer<vtkActor>::New();
  actor_->SetMapper(mapper_);
  ConfigureMeshMaterial(actor_);
  renderer_->AddActor(actor_);
  renderer_->ResetCamera();

  niftiReader_ = vtkSmartPointer<vtkNIFTIImageReader>::New();
  dicomReader_ = vtkSmartPointer<vtkDICOMImageReader>::New();

  initialized_ = true;
}

void MedMeshApp::Start()
{
  if (!initialized_)
  {
    Initialize();
  }

  Render();
  interactor_->Start();
}

void MedMeshApp::Render()
{
  if (renderWindow_)
  {
    renderWindow_->Render();
  }
}

void MedMeshApp::ResetCamera()
{
  if (!renderer_)
  {
    return;
  }

  renderer_->ResetCamera();
  Render();
}

void MedMeshApp::SetIsoValue(double value)
{
  isoValue_ = value;
  UpdateSurface();
  Render();
}

void MedMeshApp::SetKeepLargestComponent(bool keepLargestComponent)
{
  if (keepLargestComponent_ == keepLargestComponent)
  {
    return;
  }

  keepLargestComponent_ = keepLargestComponent;
  UpdateSurface();
  Render();
}

double MedMeshApp::GetIsoValue() const
{
  return isoValue_;
}

double MedMeshApp::GetScalarMin() const
{
  return scalarRange_[0];
}

double MedMeshApp::GetScalarMax() const
{
  return scalarRange_[1];
}

const std::string& MedMeshApp::GetLastError() const
{
  return lastError_;
}

bool MedMeshApp::HasMesh() const
{
  return hasMesh_;
}

bool MedMeshApp::GetKeepLargestComponent() const
{
  return keepLargestComponent_;
}

bool MedMeshApp::LoadNifti(const std::string& virtualPath)
{
  ClearError();
  std::cout << "[VTK] loadNifti: " << virtualPath << std::endl;

  niftiReader_->SetFileName(virtualPath.c_str());
  niftiReader_->Update();
  isoSurface_->SetInputConnection(niftiReader_->GetOutputPort());

  return LoadVolume(niftiReader_->GetOutput());
}

bool MedMeshApp::LoadDicom(const std::string& virtualDirectory)
{
  ClearError();
  std::cout << "[VTK] loadDicom: " << virtualDirectory << std::endl;

  const std::string seriesDirectory = FindFirstDicomDirectory(virtualDirectory);
  if (seriesDirectory.empty())
  {
    if (lastError_.empty())
    {
      SetError("No readable DICOM files were found in the dropped directory.");
    }
    return false;
  }

  std::cout << "[VTK] loadDicom series: " << seriesDirectory << std::endl;
  dicomReader_->SetDirectoryName(seriesDirectory.c_str());
  dicomReader_->Update();
  isoSurface_->SetInputConnection(dicomReader_->GetOutputPort());

  return LoadVolume(dicomReader_->GetOutput());
}

bool MedMeshApp::ExportStl(const std::string& virtualPath)
{
  ClearError();

  if (!hasMesh_ || !meshFinalClean_ || isoSurface_->GetNumberOfInputConnections(0) == 0)
  {
    SetError("No mesh is available to export.");
    return false;
  }

  meshFinalClean_->Update();
  vtkPolyData* surface = GetMeshOutput();
  if (!surface || surface->GetNumberOfPoints() == 0 || surface->GetNumberOfCells() == 0)
  {
    hasMesh_ = false;
    SetError("The current iso-surface is empty.");
    return false;
  }

  vtkNew<vtkSTLWriter> writer;
  writer->SetFileTypeToBinary();
  writer->SetFileName(virtualPath.c_str());
  writer->SetInputConnection(meshFinalClean_->GetOutputPort());

  if (writer->Write() == 0)
  {
    SetError("Failed to write STL into the virtual filesystem.");
    return false;
  }

  return true;
}

void MedMeshApp::UpdateSurface()
{
  if (!isoSurface_ || !meshFinalClean_ || isoSurface_->GetNumberOfInputConnections(0) == 0)
  {
    hasMesh_ = false;
    return;
  }

  isoSurface_->SetValue(0, isoValue_);
  isoSurface_->Modified();
  UpdateConnectivityMode();
  meshFinalClean_->Update();
  mapper_->Update();

  vtkPolyData* surface = GetMeshOutput();
  hasMesh_ = surface && surface->GetNumberOfPoints() > 0 && surface->GetNumberOfCells() > 0;
}

void MedMeshApp::UpdateScalarRange(vtkImageData* image)
{
  if (!image)
  {
    scalarRange_ = {0.0, 1.0};
    return;
  }

  image->GetScalarRange(scalarRange_.data());
  std::cout << "[VTK] ScalarRange: [" << scalarRange_[0] << ", " << scalarRange_[1] << "]" << std::endl;
}

void MedMeshApp::ClampIsoValueToRange()
{
  if (scalarRange_[1] > scalarRange_[0] &&
      (isoValue_ < scalarRange_[0] || isoValue_ > scalarRange_[1]))
  {
    isoValue_ = (scalarRange_[0] + scalarRange_[1]) * 0.5;
  }
}

void MedMeshApp::UpdateConnectivityMode()
{
  ConfigureConnectivityFilter(meshConnectivity_, keepLargestComponent_);
  if (meshConnectivity_)
  {
    meshConnectivity_->Modified();
  }
}

vtkPolyData* MedMeshApp::GetMeshOutput()
{
  return meshFinalClean_ ? meshFinalClean_->GetOutput() : nullptr;
}

bool MedMeshApp::LoadVolume(vtkImageData* image)
{
  if (!image)
  {
    hasMesh_ = false;
    SetError("The reader returned no image data.");
    return false;
  }

  const int* dims = image->GetDimensions();
  if (!dims || dims[0] <= 0 || dims[1] <= 0 || dims[2] <= 0)
  {
    hasMesh_ = false;
    SetError("The loaded volume has invalid dimensions.");
    return false;
  }

  UpdateScalarRange(image);
  ClampIsoValueToRange();
  UpdateSurface();

  if (renderer_)
  {
    renderer_->ResetCamera();
  }
  Render();

  return true;
}

bool MedMeshApp::DirectoryHasDicomFiles(const std::string& directory)
{
  for (const auto& child : ListSortedChildren(directory))
  {
    std::error_code errorCode;
    if (!std::filesystem::is_regular_file(child, errorCode) || errorCode)
    {
      continue;
    }

    if (dicomReader_->CanReadFile(child.string().c_str()))
    {
      return true;
    }
  }

  return false;
}

std::string MedMeshApp::FindFirstDicomDirectory(const std::string& rootDirectory)
{
  std::error_code errorCode;
  const std::filesystem::path rootPath(rootDirectory);
  if (!std::filesystem::exists(rootPath, errorCode) ||
      !std::filesystem::is_directory(rootPath, errorCode) || errorCode)
  {
    SetError("Dropped directory was not available in the virtual filesystem.");
    return {};
  }

  std::vector<std::filesystem::path> pending = {rootPath};
  for (std::size_t index = 0; index < pending.size(); ++index)
  {
    const std::filesystem::path& current = pending[index];
    if (DirectoryHasDicomFiles(current.string()))
    {
      return current.string();
    }

    for (const auto& child : ListSortedChildren(current))
    {
      std::error_code childError;
      if (std::filesystem::is_directory(child, childError) && !childError)
      {
        pending.push_back(child);
      }
    }
  }

  return {};
}

void MedMeshApp::SetError(std::string message)
{
  lastError_ = std::move(message);
  std::cerr << "[VTK] ERROR: " << lastError_ << std::endl;
}

void MedMeshApp::ClearError()
{
  lastError_.clear();
}
