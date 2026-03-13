// SPDX-License-Identifier: BSD-3-Clause

#include "MedMeshApp.h"

#include "vtkActor.h"
#include "vtkCleanPolyData.h"
#include "vtkConeSource.h"
#include "vtkDICOMImageReader.h"
#include "vtkFlyingEdges3D.h"
#include "vtkImageGaussianSmooth.h"
#include "vtkImageData.h"
#include "vtkImageResample.h"
#include "vtkLight.h"
#include "vtkInteractorStyleTrackballCamera.h"
#include "vtkNIFTIImageReader.h"
#include "vtkNew.h"
#include "vtkPolyData.h"
#include "vtkPolyDataConnectivityFilter.h"
#include "vtkPolyDataMapper.h"
#include "vtkPolyDataNormals.h"
#include "vtkProperty.h"
#include "vtkRenderer.h"
#include "vtkSTLWriter.h"
#include "vtkTrivialProducer.h"
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
constexpr double kIsoSpacing = 0.8;
constexpr double kDenoiseStdDev = 0.8;
constexpr double kDenoiseRadiusFactor = 3.0;
constexpr int kSmoothingIterations = 15;
constexpr double kSmoothingPassBand = 0.10;
constexpr double kCleanAbsoluteTolerance = 0.005;

void ConfigureMeshMaterial(vtkActor* actor) {
    if (!actor) {
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
                    double r, double g, double b) {
    vtkNew<vtkLight> light;
    light->SetLightTypeToCameraLight();
    light->SetPosition(x, y, z);
    light->SetFocalPoint(0.0, 0.0, 0.0);
    light->SetDiffuseColor(r, g, b);
    light->SetSpecularColor(r, g, b);
    light->SetIntensity(intensity);
    renderer->AddLight(light);
}

void ConfigureLighting(vtkRenderer* renderer) {
    if (!renderer) {
        return;
    }

    renderer->AutomaticLightCreationOff();
    renderer->RemoveAllLights();
    renderer->LightFollowCameraOn();

    AddCameraLight(renderer, 0.9, 1.0, 1.2, 1.00, 1.00, 0.97, 0.92);
    AddCameraLight(renderer, -1.2, 0.3, 0.8, 0.45, 0.84, 0.90, 1.00);
    AddCameraLight(renderer, -0.6, -1.0, 0.4, 0.20, 1.00, 1.00, 1.00);
}

void ConfigureCleanFilter(vtkCleanPolyData* cleaner) {
    if (!cleaner) {
        return;
    }

    cleaner->PointMergingOn();
    cleaner->ToleranceIsAbsoluteOn();
    cleaner->SetAbsoluteTolerance(kCleanAbsoluteTolerance);
}

void ConfigureConnectivityFilter(
    vtkPolyDataConnectivityFilter* connectivity, bool keepLargestComponent) {
    if (!connectivity) {
        return;
    }

    connectivity->ColorRegionsOff();
    if (keepLargestComponent) {
        connectivity->SetExtractionModeToLargestRegion();
    } else {
        connectivity->SetExtractionModeToAllRegions();
    }
}

void ConfigureSmoother(vtkWindowedSincPolyDataFilter* smoother) {
    if (!smoother) {
        return;
    }

    smoother->SetNumberOfIterations(kSmoothingIterations);
    smoother->SetPassBand(kSmoothingPassBand);
    smoother->BoundarySmoothingOff();
    smoother->FeatureEdgeSmoothingOff();
    smoother->NonManifoldSmoothingOff();
    smoother->NormalizeCoordinatesOn();
}

void ConfigureVolumeResample(vtkImageResample* resample) {
    if (!resample) {
        return;
    }

    resample->SetAxisOutputSpacing(0, kIsoSpacing);
    resample->SetAxisOutputSpacing(1, kIsoSpacing);
    resample->SetAxisOutputSpacing(2, kIsoSpacing);
    resample->SetInterpolationModeToLinear();
}

void ConfigureVolumeDenoise(vtkImageGaussianSmooth* denoise) {
    if (!denoise) {
        return;
    }

    denoise->SetStandardDeviation(kDenoiseStdDev, kDenoiseStdDev, kDenoiseStdDev);
    denoise->SetRadiusFactor(kDenoiseRadiusFactor);
}

void ConfigureNormals(vtkPolyDataNormals* normals) {
    if (!normals) {
        return;
    }

    normals->ComputePointNormalsOn();
    normals->ComputeCellNormalsOff();
    normals->ConsistencyOn();
    normals->SplittingOff();
    normals->AutoOrientNormalsOn();
}

std::vector<std::filesystem::path> ListSortedChildren(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> children;
    std::error_code errorCode;
    for (const auto& entry : std::filesystem::directory_iterator(directory, errorCode)) {
        if (errorCode) {
            break;
        }
        children.push_back(entry.path());
    }

    std::ranges::sort(children);
    return children;
}
} // namespace

MedMeshApp& MedMeshApp::Instance() {
    static MedMeshApp instance;
    return instance;
}

void MedMeshApp::Initialize() {
    if (initialized_) {
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

    volumeResample_ = vtkSmartPointer<vtkImageResample>::New();
    ConfigureVolumeResample(volumeResample_);

    volumeDenoise_ = vtkSmartPointer<vtkImageGaussianSmooth>::New();
    ConfigureVolumeDenoise(volumeDenoise_);
    volumeDenoise_->SetInputConnection(volumeResample_->GetOutputPort());

    isoSurface_ = vtkSmartPointer<vtkFlyingEdges3D>::New();
    isoSurface_->SetInputConnection(volumeDenoise_->GetOutputPort());
    isoSurface_->ComputeNormalsOff();
    isoSurface_->ComputeScalarsOff();

    meshConnectivity_ = vtkSmartPointer<vtkPolyDataConnectivityFilter>::New();
    meshConnectivity_->SetInputConnection(isoSurface_->GetOutputPort());
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

    meshNormals_ = vtkSmartPointer<vtkPolyDataNormals>::New();
    ConfigureNormals(meshNormals_);
    meshNormals_->SetInputConnection(meshFinalClean_->GetOutputPort());

    placeholderCone_ = vtkSmartPointer<vtkConeSource>::New();
    placeholderCone_->SetResolution(64);
    placeholderCone_->SetHeight(1.5);
    placeholderCone_->SetRadius(0.6);
    placeholderCone_->SetDirection(0.0, 1.0, 0.0);
    placeholderCone_->CappingOn();
    placeholderCone_->Update();

    pipelineInput_ = vtkSmartPointer<vtkTrivialProducer>::New();
    pipelineInput_->SetOutput(placeholderCone_->GetOutput());

    mapper_ = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper_->SetInputConnection(pipelineInput_->GetOutputPort());
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

void MedMeshApp::Start() {
    if (!initialized_) {
        Initialize();
    }

    Render();
    interactor_->Start();
}

void MedMeshApp::Render() {
    if (renderWindow_) {
        renderWindow_->Render();
    }
}

void MedMeshApp::ResetCamera() {
    if (!renderer_) {
        return;
    }

    renderer_->ResetCamera();
    Render();
}

bool MedMeshApp::ApplyMeshSettings(double value, bool keepLargestComponent) {
    ClearError();
    isoValue_ = value;
    keepLargestComponent_ = keepLargestComponent;

    if (!isoSurface_ || !meshNormals_ || !HasVolumeInput()) {
        hasMesh_ = false;
        SetError("No volume is loaded.");
        return false;
    }

    UpdateSurface();
    Render();
    return true;
}

void MedMeshApp::SetIsoValue(double value) {
    ApplyMeshSettings(value, keepLargestComponent_);
}

void MedMeshApp::SetKeepLargestComponent(bool keepLargestComponent) {
    if (keepLargestComponent_ == keepLargestComponent) {
        return;
    }

    ApplyMeshSettings(isoValue_, keepLargestComponent);
}

double MedMeshApp::GetIsoValue() const {
    return isoValue_;
}

double MedMeshApp::GetScalarMin() const {
    return scalarRange_[0];
}

double MedMeshApp::GetScalarMax() const {
    return scalarRange_[1];
}

const std::string& MedMeshApp::GetLastError() const {
    return lastError_;
}

bool MedMeshApp::HasMesh() const {
    return hasMesh_;
}

bool MedMeshApp::GetKeepLargestComponent() const {
    return keepLargestComponent_;
}

bool MedMeshApp::LoadNifti(const std::string& virtualPath) {
    ClearError();
    std::cout << "[VTK] loadNifti: " << virtualPath << std::endl;

    niftiReader_->SetFileName(virtualPath.c_str());
    niftiReader_->Update();
    volumeResample_->SetInputConnection(niftiReader_->GetOutputPort());

    return LoadVolume(niftiReader_->GetOutput());
}

bool MedMeshApp::LoadDicom(const std::string& virtualDirectory) {
    ClearError();
    std::cout << "[VTK] loadDicom: " << virtualDirectory << std::endl;

    const std::string seriesDirectory = FindFirstDicomDirectory(virtualDirectory);
    if (seriesDirectory.empty()) {
        if (lastError_.empty()) {
            SetError("No readable DICOM files were found in the dropped directory.");
        }
        return false;
    }

    std::cout << "[VTK] loadDicom series: " << seriesDirectory << std::endl;
    dicomReader_->SetDirectoryName(seriesDirectory.c_str());
    dicomReader_->Update();
    volumeResample_->SetInputConnection(dicomReader_->GetOutputPort());

    return LoadVolume(dicomReader_->GetOutput());
}

bool MedMeshApp::ExportStl(const std::string& virtualPath) {
    ClearError();

    if (!hasMesh_ || !meshFinalClean_ || !meshNormals_ || !HasVolumeInput()) {
        SetError("No mesh is available to export.");
        return false;
    }

    meshNormals_->Update();
    vtkPolyData* surface = GetMeshOutput();
    if (!surface || surface->GetNumberOfPoints() == 0 || surface->GetNumberOfCells() == 0) {
        hasMesh_ = false;
        SetError("The current iso-surface is empty.");
        return false;
    }

    vtkNew<vtkSTLWriter> writer;
    writer->SetFileTypeToBinary();
    writer->SetFileName(virtualPath.c_str());
    writer->SetInputConnection(meshFinalClean_->GetOutputPort());

    if (writer->Write() == 0) {
        SetError("Failed to write STL into the virtual filesystem.");
        return false;
    }

    return true;
}

void MedMeshApp::UpdateSurface() {
    if (!isoSurface_ || !meshNormals_ || !HasVolumeInput()) {
        hasMesh_ = false;
        return;
    }

    isoSurface_->SetValue(0, isoValue_);
    isoSurface_->Modified();
    UpdateConnectivityMode();
    meshNormals_->Update();
    mapper_->Update();

    vtkPolyData* surface = GetMeshOutput();
    hasMesh_ = surface && surface->GetNumberOfPoints() > 0 && surface->GetNumberOfCells() > 0;
}

void MedMeshApp::UpdateScalarRange(vtkImageData* image) {
    if (!image) {
        scalarRange_ = {0.0, 1.0};
        return;
    }

    image->GetScalarRange(scalarRange_.data());
    std::cout << "[VTK] ScalarRange: [" << scalarRange_[0] << ", " << scalarRange_[1] << "]" <<
        std::endl;
}

void MedMeshApp::ClampIsoValueToRange() {
    if (scalarRange_[1] > scalarRange_[0] &&
        (isoValue_ < scalarRange_[0] || isoValue_ > scalarRange_[1])) {
        isoValue_ = (scalarRange_[0] + scalarRange_[1]) * 0.5;
    }
}

void MedMeshApp::UpdateConnectivityMode() {
    ConfigureConnectivityFilter(meshConnectivity_, keepLargestComponent_);
    if (meshConnectivity_) {
        meshConnectivity_->Modified();
    }
}

bool MedMeshApp::HasVolumeInput() const {
    return volumeResample_ && volumeResample_->GetNumberOfInputConnections(0) > 0;
}

vtkPolyData* MedMeshApp::GetMeshOutput() {
    return meshNormals_ ? meshNormals_->GetOutput() : nullptr;
}

bool MedMeshApp::LoadVolume(vtkImageData* image) {
    if (!image) {
        hasMesh_ = false;
        SetError("The reader returned no image data.");
        return false;
    }

    const int* dims = image->GetDimensions();
    if (!dims || dims[0] <= 0 || dims[1] <= 0 || dims[2] <= 0) {
        hasMesh_ = false;
        SetError("The loaded volume has invalid dimensions.");
        return false;
    }

    UpdateScalarRange(image);
    ClampIsoValueToRange();
    UpdateSurface();
    pipelineInput_->SetOutput(meshNormals_->GetOutput());

    if (renderer_) {
        renderer_->ResetCamera();
    }
    Render();

    return true;
}

bool MedMeshApp::DirectoryHasDicomFiles(const std::string& directory) {
    for (const auto& child : ListSortedChildren(directory)) {
        std::error_code errorCode;
        if (!std::filesystem::is_regular_file(child, errorCode) || errorCode) {
            continue;
        }

        if (dicomReader_->CanReadFile(child.string().c_str())) {
            return true;
        }
    }

    return false;
}

std::string MedMeshApp::FindFirstDicomDirectory(const std::string& rootDirectory) {
    std::error_code errorCode;
    const std::filesystem::path rootPath(rootDirectory);
    if (!std::filesystem::exists(rootPath, errorCode) ||
        !std::filesystem::is_directory(rootPath, errorCode) || errorCode) {
        SetError("Dropped directory was not available in the virtual filesystem.");
        return {};
    }

    std::vector<std::filesystem::path> pending = {rootPath};
    for (std::size_t index = 0; index < pending.size(); ++index) {
        const std::filesystem::path& current = pending[index];
        if (DirectoryHasDicomFiles(current.string())) {
            return current.string();
        }

        for (const auto& child : ListSortedChildren(current)) {
            std::error_code childError;
            if (std::filesystem::is_directory(child, childError) && !childError) {
                pending.push_back(child);
            }
        }
    }

    return {};
}

void MedMeshApp::SetError(std::string message) {
    lastError_ = std::move(message);
    std::cerr << "[VTK] ERROR: " << lastError_ << std::endl;
}

void MedMeshApp::ClearError() {
    lastError_.clear();
}
