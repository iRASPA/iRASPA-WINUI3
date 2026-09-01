#pragma once

#include "CameraDetailView.g.h"
#include "CameraPaneHost.h"
#include "ExportJobWriter.h"
#include "MovieWriter.h"

#include "projectstructure.h"
#include "rklight.h"
#include "rkrendersettings.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace winrt::iRASPA_WinUI::implementation
{
    // Cocoa StructureCameraDetailViewController: the Camera tab of the inspector.
    // The form is CameraDetailView.xaml; this fills it and applies its edits.
    struct CameraDetailView : CameraDetailViewT<CameraDetailView>
    {
        CameraDetailView();

        void SetHost(CameraPaneHost* host) { m_host = host; }
        // Also the Cocoa CameraDidChangeNotification path: dragging in the render
        // view moves the camera, and the readouts follow it.
        void Reload();
        void ReloadReadouts();

        void OnResetFractionChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                                    winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);
        void OnResetDirectionChecked(winrt::Windows::Foundation::IInspectable const& sender,
                                     winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnResetCamera(winrt::Windows::Foundation::IInspectable const& sender,
                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnProjectionChecked(winrt::Windows::Foundation::IInspectable const& sender,
                                 winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnAngleOfViewChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                                  winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);
        void OnRotationAngleChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                                    winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);
        void OnRotateClick(winrt::Windows::Foundation::IInspectable const& sender,
                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnZoomIn(winrt::Windows::Foundation::IInspectable const& sender,
                      winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnZoomOut(winrt::Windows::Foundation::IInspectable const& sender,
                       winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

        void OnAxesPositionChanged(winrt::Windows::Foundation::IInspectable const& sender,
                                   winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        void OnAxesStyleChanged(winrt::Windows::Foundation::IInspectable const& sender,
                                winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        void OnAxesSizeChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                               winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);
        void OnAxesOffsetChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                                 winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);
        void OnAxesBackgroundStyleChanged(winrt::Windows::Foundation::IInspectable const& sender,
                                          winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        void OnAxesBackgroundSizeChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                                         winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);
        void OnTextScaleChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                                winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);
        void OnTextDisplacementChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                                       winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);

        void OnLightStyleChanged(winrt::Windows::Foundation::IInspectable const& sender,
                                 winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        // The export settings that travel with the document, unlike the interactive ones below.
        void OnPictureRayTracingToggled(winrt::Windows::Foundation::IInspectable const& sender,
                                        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnPictureShadowsToggled(winrt::Windows::Foundation::IInspectable const& sender,
                                     winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnPictureSampleCountChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                                         winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);
        void OnPictureMaximumBouncesChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                                            winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);

        // The machine's own render settings, which are not part of any document.
        void OnInteractiveRayTracingToggled(winrt::Windows::Foundation::IInspectable const& sender,
                                            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnInteractiveShadowsToggled(winrt::Windows::Foundation::IInspectable const& sender,
                                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnInteractiveSampleCountChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                                             winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);
        void OnInteractiveRotatingSampleCountChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                                                     winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);
        void OnInteractiveMaximumBouncesChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                                                winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);

        void OnImageDpiChanged(winrt::Windows::Foundation::IInspectable const& sender,
                               winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        void OnImageQualityChanged(winrt::Windows::Foundation::IInspectable const& sender,
                                   winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        void OnPhysicalWidthChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                                    winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);
        void OnPixelWidthChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                                 winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);
        void OnImageDimensionsChecked(winrt::Windows::Foundation::IInspectable const& sender,
                                      winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnImageUnitsChecked(winrt::Windows::Foundation::IInspectable const& sender,
                                 winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnFramesPerSecondChanged(winrt::Microsoft::UI::Xaml::Controls::NumberBox const& sender,
                                      winrt::Microsoft::UI::Xaml::Controls::NumberBoxValueChangedEventArgs const& e);
        void OnMovieTypeChanged(winrt::Windows::Foundation::IInspectable const& sender,
                                winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        void OnMakePicture(winrt::Windows::Foundation::IInspectable const& sender,
                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnMakeMovie(winrt::Windows::Foundation::IInspectable const& sender,
                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnCancelExport(winrt::Windows::Foundation::IInspectable const& sender,
                            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

        void OnBackgroundTypeChecked(winrt::Windows::Foundation::IInspectable const& sender,
                                     winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnSelectBackgroundImage(winrt::Windows::Foundation::IInspectable const& sender,
                                     winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

    private:
        void BuildComboItems();
        void ConfigureRanges();
        void BuildViewMatrix();
        void BuildLightSlots();
        void WireSliderRows();
        void WireColorWells();

        void ReloadCameraSection();
        void ReloadAxes();
        void ReloadLights();
        void ReloadRaytracing();
        void ReloadPictures();
        void ReloadBackground();
        void ShowBackgroundPanel(int type);

        // Everything the export needs off the project, read on the UI thread: nothing
        // locks the model against the form editing it.
        struct ExportSettings
        {
            int width{ 0 };
            int height{ 0 };
            double dotsPerInch{ 72.0 };
            int framesPerSecond{ 1 };
            ProjectStructure::MovieType movieType{ ProjectStructure::MovieType::frames };
        };

        void BuildMovieFormats();
        bool CollectExportSettings(ExportSettings& settings) const;
        bool BuildExportJob(ExportSettings const& settings, std::wstring const& outputPath,
                            bool movie, MovieWriter::Format format,
                            ExportJobRequest& request) const;
        // Each choice is a type name and the one extension it stands for.
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring>
        PickExportPath(std::vector<std::pair<winrt::hstring, winrt::hstring>> choices,
                       winrt::hstring suggestedName);
        winrt::fire_and_forget RunPictureExport(ExportSettings settings);
        winrt::fire_and_forget RunMovieExport(ExportSettings settings, MovieWriter::Format format);
        // Writes the job file, runs iRASPA.Export.exe on it and follows its progress
        // through to EndExport. \a label names the job in the log ("Picture", "Movie").
        winrt::Windows::Foundation::IAsyncAction RunExportJob(ExportJobRequest request,
                                                              std::wstring label);
        void ReportExportProgress(int completed, int total);
        void BeginExport(std::wstring const& status, bool cancellable);
        void EndExport(std::wstring const& message);

        std::shared_ptr<ProjectStructure> LightProject() const;
        std::shared_ptr<RKLight> LightAt(size_t slot) const;
        // Every light edit goes through here: it writes the change, renames the
        // style to whatever the rig now amounts to, refreshes the slot whose
        // other controls follow its checkbox, and reloads the renderer.
        void ApplyToLight(size_t slot, std::function<void(RKLight&)> change);
        void ApplySceneLighting(std::function<void(ProjectStructure&)> change, bool rebakesOcclusion);
        void ReloadLightStyle();
        void ReloadLightSlot(size_t slot);

        // The axes text rows all write the same three properties of a different
        // axis, so the row a box belongs to is looked up rather than repeated.
        int TextRowOf(winrt::Windows::Foundation::IInspectable const& sender, int& outAxis);

        CameraPaneHost* m_host{ nullptr };
        // Set while the form is being filled in, so writing a value back into a
        // control does not look like the user editing it.
        bool m_suppressEvents{ false };
        // The export itself lives in iRASPA.Export.exe; all this side keeps is the
        // running process, so cancelling can kill it, and the two flags the form is
        // driven by. None of them leaves the UI thread: the worker only reads the
        // helper's pipe and hops back here to touch anything else.
        bool m_cancelExport{ false };
        bool m_exportRunning{ false };
        HANDLE m_exportProcess{ nullptr };
        // Index in MovieFormat() to the encoder it stands for; empty when the
        // machine has none.
        std::vector<MovieWriter::Format> m_movieFormats;

        // The controls of one role's box, built by BuildLightSlots. The eight
        // boxes are alike, so they are generated rather than written out.
        struct LightSlot
        {
            winrt::Microsoft::UI::Xaml::Controls::CheckBox enabled{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::ComboBox type{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Slider diffuseSlider{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::NumberBox diffuseBox{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::DropDownButton diffuseWell{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Border diffuseSwatch{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Slider specularSlider{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::NumberBox specularBox{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::DropDownButton specularWell{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Border specularSwatch{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::Slider shininessSlider{ nullptr };
            winrt::Microsoft::UI::Xaml::Controls::NumberBox shininessBox{ nullptr };
        };
        LightSlot m_lightSlots[RKLight::numberOfRoles];
        winrt::Microsoft::UI::Xaml::Controls::TextBox m_matrix[16]{
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
    };
}

namespace winrt::iRASPA_WinUI::factory_implementation
{
    struct CameraDetailView : CameraDetailViewT<CameraDetailView, implementation::CameraDetailView>
    {
    };
}
