// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <array>
#include <string>

#include <vtkSmartPointer.h>

class vtkActor;
class vtkCleanPolyData;
class vtkConeSource;
class vtkDICOMImageReader;
class vtkFlyingEdges3D;
class vtkImageGaussianSmooth;
class vtkImageResample;
class vtkInteractorStyleTrackballCamera;
class vtkNIFTIImageReader;
class vtkPolyData;
class vtkPolyDataConnectivityFilter;
class vtkPolyDataMapper;
class vtkPolyDataNormals;
class vtkRenderer;
class vtkTrivialProducer;
class vtkTriangleFilter;
class vtkWebAssemblyOpenGLRenderWindow;
class vtkWebAssemblyRenderWindowInteractor;
class vtkWindowedSincPolyDataFilter;
class vtkImageData;

class MedMeshApp
{
public:
  static MedMeshApp& Instance();

  void Initialize();
  void Start();

  void Render();
  void ResetCamera();
  bool ApplyMeshSettings(double value, bool keepLargestComponent);
  void SetIsoValue(double value);
  void SetKeepLargestComponent(bool keepLargestComponent);

  double GetIsoValue() const;
  double GetScalarMin() const;
  double GetScalarMax() const;
  const std::string& GetLastError() const;
  bool HasMesh() const;
  bool GetKeepLargestComponent() const;

  bool LoadNifti(const std::string& virtualPath);
  bool LoadDicom(const std::string& virtualDirectory);
  bool ExportStl(const std::string& virtualPath);

private:
  MedMeshApp() = default;

  void UpdateSurface();
  void UpdateScalarRange(vtkImageData* image);
  void ClampIsoValueToRange();
  void UpdateConnectivityMode();
  bool HasVolumeInput() const;
  vtkPolyData* GetMeshOutput();
  bool LoadVolume(vtkImageData* image);
  bool DirectoryHasDicomFiles(const std::string& directory);
  std::string FindFirstDicomDirectory(const std::string& rootDirectory);
  void SetError(std::string message);
  void ClearError();

  bool initialized_ = false;
  bool hasMesh_ = false;
  bool keepLargestComponent_ = true;
  double isoValue_ = 300.0;
  std::array<double, 2> scalarRange_ = {0.0, 1.0};
  std::string lastError_;

  vtkSmartPointer<vtkRenderer> renderer_;
  vtkSmartPointer<vtkWebAssemblyOpenGLRenderWindow> renderWindow_;
  vtkSmartPointer<vtkWebAssemblyRenderWindowInteractor> interactor_;
  vtkSmartPointer<vtkInteractorStyleTrackballCamera> style_;
  vtkSmartPointer<vtkPolyDataMapper> mapper_;
  vtkSmartPointer<vtkActor> actor_;
  vtkSmartPointer<vtkConeSource> placeholderCone_;
  vtkSmartPointer<vtkTrivialProducer> pipelineInput_;
  vtkSmartPointer<vtkNIFTIImageReader> niftiReader_;
  vtkSmartPointer<vtkDICOMImageReader> dicomReader_;
  vtkSmartPointer<vtkImageResample> volumeResample_;
  vtkSmartPointer<vtkImageGaussianSmooth> volumeDenoise_;
  vtkSmartPointer<vtkFlyingEdges3D> isoSurface_;
  vtkSmartPointer<vtkPolyDataConnectivityFilter> meshConnectivity_;
  vtkSmartPointer<vtkTriangleFilter> meshTriangulator_;
  vtkSmartPointer<vtkWindowedSincPolyDataFilter> meshSmoother_;
  vtkSmartPointer<vtkCleanPolyData> meshFinalClean_;
  vtkSmartPointer<vtkPolyDataNormals> meshNormals_;
};
