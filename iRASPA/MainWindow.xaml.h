#pragma once

#include "MainWindow.g.h"
#include "CameraPaneHost.h"
#include "DocumentController.h"
#include "Hosting/Dx12SwapChainPanelHost.h"
#include "documentdata.h"
#include "iraspaobject.h"
#include "movie.h"
#include "projectstructure.h"
#include "projecttreenode.h"
#include "scene.h"
#include "rkcamera.h"
#include "skatomtreenode.h"
#include "skparser.h"
#include "forcefieldtype.h"
#include <rkundostack.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

class Structure;

namespace winrt::iRASPA_WinUI::implementation
{
    struct ProjectView;
    struct SceneView;
    struct FrameView;
    struct InfoDetailView;
    struct CameraDetailView;
    struct AppearanceDetailView;
    struct ElementsDetailView;
    struct AtomsDetailView;
    struct BondsDetailView;

    // The list and detail views are XAML UserControls with their own
    // code-behind; they own their rows and gestures and reach the document
    // through DocumentController, never through this window. What they still
    // need from the window itself (log pane, renderer, message panel, Edit menu,
    // inspector) is what DocumentHost exposes.
    struct MainWindow : MainWindowT<MainWindow>, DocumentHost, CameraPaneHost
    {
        MainWindow();

        // CameraPaneHost: the camera form edits the renderer's camera, the
        // project's axes and lights, and the picture settings.
        std::shared_ptr<RKCamera> PaneCamera() override { return ActiveCamera(); }
        std::shared_ptr<ProjectStructure> PaneProject() override { return m_project; }
        void ResetRendererCameraView() override;
        void SetRendererCameraOrthographic(bool orthographic) override;
        void ZoomRendererCamera(double amount) override;
        void PickBackgroundImage() override { PickBackgroundImageAsync(); }
        int64_t LiveAdapterLuid() override;

        // DocumentHost
        void Log(std::wstring const& message) override { AppendLog(message); }
        void ReloadRendererData() override { ReloadRenderer(); }
        void ReloadRendererSelection() override;
        void InvalidateSceneAmbientOcclusion(std::shared_ptr<Scene> const& scene) override;
        void RefreshInspector() override;
        void RefreshEditMenuLabels() override { UpdateEditMenuLabels(); }
        void ShowMessage(std::wstring const& glyph, std::wstring const& message) override;
        void LoadProjectIntoRenderer(std::shared_ptr<ProjectStructure> const& project,
                                     std::wstring const& displayName) override;
        void ClearRenderer() override { ClearProjectFromRenderer(); }
        void ReloadAfterCellEdit() override { ApplyCellEditAndReload(); }
        void ApplySelectionToRenderer(bool refreshInspector) override
        {
            ApplySelectedStructuresToRenderer(refreshInspector);
        }
        void Enqueue(std::function<void()> work) override;
        void SaveTextFile(std::wstring const& text, std::wstring const& extension,
                          std::wstring const& typeName,
                          std::wstring const& suggestedName) override
        {
            SaveTextFileAsync(text, extension, typeName, suggestedName);
        }

        DocumentController& Controller() { return m_controller; }

        // CppWinRT make<> always invokes InitializeComponent after construction.
        // No-op avoids unpackaged LoadComponent / ms-appx metadata failures.
        void InitializeComponent() {}

        void MainWindow_Loaded(winrt::Windows::Foundation::IInspectable const& sender,
                               winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

    private:
        void BuildUi();
        bool TrySetMicaBackdrop();
        void SetConfigurationSourceTheme();
        void Window_Activated(winrt::Windows::Foundation::IInspectable const& sender,
                              winrt::Microsoft::UI::Xaml::WindowActivatedEventArgs const& args);
        void Window_Closed(winrt::Windows::Foundation::IInspectable const& sender,
                           winrt::Microsoft::UI::Xaml::WindowEventArgs const& args);
        void WireDocument();
        // The project list is a UserControl (ProjectView.xaml); this resolves the
        // implementation behind the projected type so the document operations can
        // ask it to refresh.
        ProjectView* ProjectViewImpl() const;
        // Reads the gallery database off-thread, then hands it to the controller.
        winrt::fire_and_forget LoadGalleryDatabaseAsync();
        // Same for the shipped CoRE MOF and IZA databases, which land under DATABASES PUBLIC.
        winrt::fire_and_forget LoadStructureDatabasesAsync();
        // Same for the scene and frame panes (SceneView.xaml / FrameView.xaml).
        SceneView* SceneViewImpl() const;
        FrameView* FrameViewImpl() const;
        void ApplySelectedStructuresToRenderer(bool refreshInspector);
        void ClearInspectorVisualState();
        void AppendLog(std::wstring const& message);

        // Python console (Cocoa InterpreterViewController port): embedded
        // CPython next to the log view at the bottom. See PythonConsole.cpp.
        winrt::Microsoft::UI::Xaml::Controls::Grid BuildPythonConsole();
        void OnPythonInputKeyDown(winrt::Windows::Foundation::IInspectable const& sender,
                                  winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e);
        void RunPythonConsoleCommand(std::wstring const& command);
        void AppendPythonOutput(std::wstring const& text);
        void ShutdownPython();
        void UpdateLeftPaneVisibility();
        void ShowInspector(int index);
        // The Info tab is InfoDetailView.xaml, kept alive across selection
        // changes and reloaded rather than rebuilt.
        void ShowInfoDetailView();
        // The Cell tab is CellDetailView.xaml, kept alive the same way.
        void ShowCellDetailView();
        // The Camera tab is CameraDetailView.xaml, likewise.
        void ShowCameraDetailView();
        // The Appearance tab is AppearanceDetailView.xaml, likewise.
        void ShowAppearanceDetailView();
        // The Elements tab is ElementsDetailView.xaml. Its cards have to
        // virtualize, so it takes over the inspector host instead of going into
        // the shared scrolling stack.
        void ShowElementsDetailView();
        // The Atoms tab is AtomsDetailView.xaml. Its rows have to virtualize, so
        // it takes over the inspector host the same way.
        void ShowAtomsDetailView();
        // The Bonds tab is BondsDetailView.xaml, hosted the same way.
        void ShowBondsDetailView();
        void ApplyProjectToRenderer(std::shared_ptr<ProjectStructure> project, std::wstring const& displayName);
        void ClearProjectFromRenderer();
        // Open reads .irspdoc documents. Import uses the Explorer open dialog
        // with a Cocoa-style two-column Import Options accessory template.
        winrt::fire_and_forget OpenDocumentAsync();
        winrt::fire_and_forget ImportStructuresAsync();
        // Cocoa iRASPADocument.write(to:ofType:) — zip up the local projects.
        winrt::fire_and_forget SaveDocumentAsync(bool saveAs);
        bool WriteDocumentToFile(std::wstring const& path, std::wstring& error);
        std::shared_ptr<DocumentData> ReadDocumentFromFile(std::wstring const& path, std::wstring& error);
        void ApplyOpenedDocument(std::shared_ptr<DocumentData> opened, std::wstring const& path);
        void ApplyImportedProject(std::shared_ptr<ProjectStructure> project,
                                  std::wstring const& displayName);
        void ImportPathsAsProjects(std::vector<std::wstring> const& paths,
                                   std::vector<std::wstring> const& names,
                                   SKParser::ImportType importType,
                                   bool proteinOnlyAsymmetricUnit,
                                   bool separatePolymerChains,
                                   bool asMolecule);
        DirectXRenderer *Renderer() const;
        // Also CameraPaneHost, whose edits all end in one of these two.
        void ReloadRenderer() override;
        /// Redraw only (no scene-geometry rebuild); use for camera-only
        /// changes like rotation/zoom, which are far cheaper than reloadData.
        void RedrawRenderer() override;
        // The camera the Camera tab shows: the loaded project's, falling back to
        // the renderer's own. Null with no project, because the renderer keeps the
        // camera of whatever was loaded before.
        std::shared_ptr<RKCamera> ActiveCamera() const;
        void ApplyCellEditAndReload();
        // The detail panes edit the whole selection; the query itself belongs to
        // the document, so this only forwards while they still live in here.
        std::vector<std::shared_ptr<iRASPAObject>> TargetStructures()
        {
            return m_controller.TargetStructures();
        }

        /// Refresh the camera readouts (Euler angles, center, view matrix,
        /// position, distance) from the active camera; called after the render
        /// view rotated/panned/zoomed the camera (Cocoa's updateCameraViews on
        /// CameraDidChangeNotification).
        void UpdateCameraReadouts();
        winrt::fire_and_forget PickBackgroundImageAsync();

        void OnOpenClick(winrt::Windows::Foundation::IInspectable const& sender,
                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnImportClick(winrt::Windows::Foundation::IInspectable const& sender,
                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnSaveClick(winrt::Windows::Foundation::IInspectable const& sender,
                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnSaveAsClick(winrt::Windows::Foundation::IInspectable const& sender,
                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnFileNewStructureClick(winrt::Windows::Foundation::IInspectable const& sender,
                                     winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnFileNewGroupClick(winrt::Windows::Foundation::IInspectable const& sender,
                                 winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnExitClick(winrt::Windows::Foundation::IInspectable const& sender,
                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void SyncTabSelection(winrt::Microsoft::UI::Xaml::Controls::SelectorBar const& bar, int index);
        void OnLeftPaneSelectionChanged(
            winrt::Microsoft::UI::Xaml::Controls::SelectorBar const& sender,
            winrt::Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const& e);
        void OnInspectorSelectionChanged(
            winrt::Microsoft::UI::Xaml::Controls::SelectorBar const& sender,
            winrt::Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const& e);

        // Writes an exported structure out; the save picker needs the window
        // handle, so the window owns this half of Export As.
        winrt::fire_and_forget SaveTextFileAsync(std::wstring text, std::wstring extension,
                                                 std::wstring typeName, std::wstring suggestedName);

        // Undo/redo (Cocoa: one NSUndoManager per project plus the document's
        // own manager for the project tree; windowWillReturnUndoManager picks
        // between them by keyboard focus).
        // The stacks live on the controller; the panes inside a project still
        // reach them through the window they are hosted in.
        RKUndoStack& DocumentUndoStack() { return m_controller.DocumentUndoStack(); }
        RKUndoStack* ProjectUndoStack() { return m_controller.ProjectUndoStack(); }
        // Where operations inside a project record; the focus only decides which
        // stack Edit > Undo acts on.
        RKUndoStack* ObjectUndoStack() { return m_controller.ObjectUndoStack(); }
        // Which stack Edit > Undo acts on depends on the keyboard focus, so this
        // one stays with the window.
        RKUndoStack* ActiveUndoStack();
        RKUndoStack* UndoStackForCommand(bool redo);
        void RegisterUndo(RKUndoStack* stack, std::wstring name, std::function<void()> action)
        {
            m_controller.RegisterUndo(stack, std::move(name), std::move(action));
        }
        void PerformUndo();
        void PerformRedo();
        void UpdateEditMenuLabels();

        // About panel (Cocoa AboutWindow / LocalAboutViewController). The panel
        // itself is AboutView.xaml; the window it is shown in is About.cpp.
        void OnAboutClick(winrt::Windows::Foundation::IInspectable const& sender,
                          winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void ShowAboutWindow();
        winrt::Microsoft::UI::Xaml::Window m_aboutWindow{ nullptr };

        // Help menu (Cocoa "iRASPA Help": the help pages, here from help.iraspa.org).
        void OnHelpClick(winrt::Windows::Foundation::IInspectable const& sender,
                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void ShowHelpWindow();
        winrt::fire_and_forget EnsureHelpBrowserAsync(
            winrt::Microsoft::UI::Xaml::Controls::WebView2 browser);
        winrt::Microsoft::UI::Xaml::Window m_helpWindow{ nullptr };

        winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem m_undoMenuItem{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem m_redoMenuItem{ nullptr };

        // Right-click menu over the 3D view, the counterpart of the NSMenu that
        // Cocoa's RenderTabViewController hangs off its view.
        void BuildRenderContextMenu();
        void ShowRenderContextMenu(winrt::Windows::Foundation::Point const& position);
        void ResetRenderCameraToDirection(ResetDirectionType direction);
        void SetRenderCameraProjection(bool orthographic);
        void ToggleRenderBoundingBox();
        void ComputeHighQualityAmbientOcclusion();
        void ExportRenderStructure(DocumentController::AtomExportFormat format);
        winrt::Microsoft::UI::Xaml::Controls::MenuFlyout m_renderContextMenu{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ToggleMenuFlyoutItem m_orthographicMenuItem{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ToggleMenuFlyoutItem m_perspectiveMenuItem{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ToggleMenuFlyoutItem m_boundingBoxMenuItem{ nullptr };

        int m_leftPaneIndex = 0;
        // Project list (ProjectView.xaml): markup, rows and gestures live in the
        // control; this window only drives it after document changes.
        winrt::iRASPA_WinUI::ProjectView m_projectView{ nullptr };
        void SuppressProjectSelectionEvents(bool suppress);
        // Scene/movie list (SceneView.xaml) and frame list (FrameView.xaml), the
        // other two tabs of the left pane, on the same footing as the project one.
        winrt::iRASPA_WinUI::SceneView m_sceneView{ nullptr };
        winrt::iRASPA_WinUI::FrameView m_frameView{ nullptr };
        // Inspector tabs that are already UserControls. Built once and reloaded,
        // so they keep their controls (and their expander state) across the
        // selection changes that used to rebuild the whole form.
        winrt::iRASPA_WinUI::InfoDetailView m_infoDetailView{ nullptr };
        winrt::iRASPA_WinUI::CellDetailView m_cellDetailView{ nullptr };
        winrt::iRASPA_WinUI::CameraDetailView m_cameraDetailView{ nullptr };
        winrt::iRASPA_WinUI::AppearanceDetailView m_appearanceDetailView{ nullptr };
        winrt::iRASPA_WinUI::ElementsDetailView m_elementsDetailView{ nullptr };
        winrt::iRASPA_WinUI::AtomsDetailView m_atomsDetailView{ nullptr };
        winrt::iRASPA_WinUI::BondsDetailView m_bondsDetailView{ nullptr };
        uint32_t m_projectLoadEpoch = 0;

        winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel m_renderPanel{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Canvas m_renderOverlay{ nullptr };
        // Collapsible panels (Cocoa toolbar segments at the right: left panel,
        // bottom panel, right panel), plus the saved sizes while hidden.
        winrt::Microsoft::UI::Xaml::Controls::Grid m_leftPanel{ nullptr };
        // The Project / Scene / Frame and Camera..Bonds selectors, kept so a
        // selection made elsewhere can be written back to them.
        winrt::Microsoft::UI::Xaml::Controls::SelectorBar m_leftPaneTabs{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::SelectorBar m_inspectorTabs{ nullptr };
        bool m_suppressTabSelection = false;
        winrt::Microsoft::UI::Xaml::Controls::Grid m_bottomPanel{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Grid m_rightPanel{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition m_leftPanelColumn{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition m_rightPanelColumn{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::RowDefinition m_bottomPanelRow{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition m_leftSplitterColumn{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition m_rightSplitterColumn{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::RowDefinition m_bottomSplitterRow{ nullptr };
        winrt::Microsoft::UI::Xaml::FrameworkElement m_leftSplitter{ nullptr };
        winrt::Microsoft::UI::Xaml::FrameworkElement m_rightSplitter{ nullptr };
        winrt::Microsoft::UI::Xaml::FrameworkElement m_bottomSplitter{ nullptr };
        winrt::Microsoft::UI::Xaml::GridLength m_savedLeftPanelWidth{};
        winrt::Microsoft::UI::Xaml::GridLength m_savedRightPanelWidth{};
        winrt::Microsoft::UI::Xaml::GridLength m_savedBottomPanelHeight{};
        void SetPanelVisibility(int which, bool visible); // 0=left, 1=bottom, 2=right
        // Cocoa share toolbar button: export the selected structures to temp
        // files and hand them to the Windows Share UI.
        void ShareSelectedStructures();
        winrt::Windows::ApplicationModel::DataTransfer::DataTransferManager m_dataTransferManager{ nullptr };
        std::vector<std::wstring> m_shareFilePaths;
        // Cocoa NSInformationPanelView: the iTunes-LCD style message panel in
        // the toolbar center; messages (icon + text) fade out after 5 seconds.
        winrt::Microsoft::UI::Xaml::Controls::StackPanel m_infoPanelContent{ nullptr };
        winrt::Microsoft::UI::Xaml::DispatcherTimer m_infoPanelTimer{ nullptr };
        void ShowInfoPanelMessage(winrt::hstring const& glyph, winrt::hstring const& message);
        winrt::Microsoft::UI::Xaml::Controls::TextBox m_logBox{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBox m_pythonOutputBox{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::TextBox m_pythonInputBox{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ScrollViewer m_pythonScroll{ nullptr };
        std::wstring m_pythonOutput;
        std::vector<std::wstring> m_pythonHistory;
        int m_pythonHistoryPos = 0;
        winrt::Microsoft::UI::Xaml::Controls::Grid m_inspectorHost{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ScrollViewer m_inspectorScroll{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::StackPanel m_inspectorPanel{ nullptr };
        int m_inspectorIndex = 0;

        std::unique_ptr<Dx12SwapChainPanelHost> m_renderHost;
        // The document, the loaded project and the document undo stack live
        // here; the panes that have not been extracted into their own
        // UserControl yet still reach them through the two aliases below.
        DocumentController m_controller;
        std::shared_ptr<DocumentData>& m_document{ m_controller.Document() };
        std::shared_ptr<ProjectStructure>& m_project{ m_controller.Project() };
        // File the document was last saved to; empty means untitled, so Save
        // asks for a location just like Cocoa does.
        std::wstring m_documentPath;
        bool m_saveInProgress = false;
        int m_savedProjectCount = 0;
        std::wstring m_log;

        winrt::Microsoft::UI::Composition::SystemBackdrops::SystemBackdropConfiguration m_backdropConfiguration{ nullptr };
        winrt::Microsoft::UI::Composition::SystemBackdrops::MicaController m_micaController{ nullptr };
        winrt::Microsoft::UI::Xaml::Window::Activated_revoker m_backdropActivatedRevoker;
        winrt::Microsoft::UI::Xaml::Window::Closed_revoker m_backdropClosedRevoker;
        winrt::Microsoft::UI::Xaml::FrameworkElement::ActualThemeChanged_revoker m_backdropThemeRevoker;
    };
}

namespace winrt::iRASPA_WinUI::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
