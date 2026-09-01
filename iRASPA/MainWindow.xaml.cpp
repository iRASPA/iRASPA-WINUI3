#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include "AppearanceDetailView.xaml.h"
#include "AtomsDetailView.xaml.h"
#include "BondsDetailView.xaml.h"
#include "CameraDetailView.xaml.h"
#include "CellDetailView.xaml.h"
#include "ElementsDetailView.xaml.h"
#include "FrameView.xaml.h"
#include "InfoDetailView.xaml.h"
#include "ProjectView.xaml.h"
#include "SceneView.xaml.h"

#include <microsoft.ui.xaml.window.h>
#include <Shobjidl.h>
#include <commdlg.h>
// commdlg maps FindText/ReplaceText to the W APIs; that breaks WinRT ITextRange.
#ifdef FindText
#undef FindText
#endif
#ifdef ReplaceText
#undef ReplaceText
#endif
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdi32.lib")
#include "resource.h"
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.System.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <filesystem>
#include "rkstring.h"
#include "rkcolor.h"
#include "atomstructureviewer.h"
#include "atomviewer.h"
#include "bondstructureviewer.h"
#include "crystal.h"
#include "scenelist.h"
#include "skatomtreecontroller.h"
#include "spacegroupviewer.h"
#include "structure.h"
#include "skcifwriter.h"
#include "skparser.h"
#include "skspacegroup.h"
#include "skxyzwriter.h"
#include "rkrenderuniforms.h"
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <iomanip>
#include <exception>
#include <utility>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Controls::Primitives;
using namespace winrt::Microsoft::UI::Xaml::Input;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace winrt::Microsoft::UI::Composition;
using namespace winrt::Microsoft::UI::Composition::SystemBackdrops;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Pickers;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;

namespace winrt::iRASPA_WinUI::implementation
{
    namespace
    {
        constexpr double kSplitterThickness = 6.0;
        constexpr double kLeftPanelMinWidth = 160.0;
        constexpr double kRightPanelMinWidth = 320.0;
        constexpr double kBottomPanelMinHeight = 80.0;

        constexpr wchar_t const* kInspectorTitles[] = {
            // Match Qt DetailTabViewController order (mainwindow.ui).
            L"Camera", L"Elements", L"Info", L"Appearance", L"Cell", L"Atoms", L"Bonds"
        };

        double PixelLength(GridLength const& length, double fallback)
        {
            return length.GridUnitType == GridUnitType::Pixel ? length.Value : fallback;
        }

        void SetPixelWidth(ColumnDefinition const& column, double pixels, double minimum)
        {
            if (pixels < minimum)
                pixels = minimum;
            column.Width(GridLengthHelper::FromPixels(pixels));
        }

        void SetPixelHeight(RowDefinition const& row, double pixels, double minimum)
        {
            if (pixels < minimum)
                pixels = minimum;
            row.Height(GridLengthHelper::FromPixels(pixels));
        }

        // Thin grip that resizes a fixed-pixel column. growOnPositiveDrag: left
        // pane (drag right → wider); false for the right pane (drag right →
        // narrower inspector).
        FrameworkElement MakeColumnSplitter(ColumnDefinition column, bool growOnPositiveDrag,
                                            double minimum)
        {
            Border grip;
            grip.Width(kSplitterThickness);
            grip.HorizontalAlignment(HorizontalAlignment::Stretch);
            grip.VerticalAlignment(VerticalAlignment::Stretch);
            grip.Background(SolidColorBrush(winrt::Windows::UI::Color{ 1, 0, 0, 0 }));

            Border line;
            line.Width(1);
            line.HorizontalAlignment(HorizontalAlignment::Center);
            line.VerticalAlignment(VerticalAlignment::Stretch);
            line.Background(SolidColorBrush(winrt::Windows::UI::Color{ 90, 128, 128, 128 }));
            line.IsHitTestVisible(false);

            Grid host;
            host.Width(kSplitterThickness);
            host.Children().Append(line);
            host.Children().Append(grip);

            auto lastX = std::make_shared<std::optional<double>>();
            grip.PointerPressed([lastX, grip](IInspectable const&, PointerRoutedEventArgs const& e)
            {
                grip.CapturePointer(e.Pointer());
                *lastX = e.GetCurrentPoint(nullptr).Position().X;
                e.Handled(true);
            });
            grip.PointerMoved([lastX, column, growOnPositiveDrag, minimum](
                                  IInspectable const&, PointerRoutedEventArgs const& e)
            {
                if (!lastX->has_value())
                    return;
                double const x = e.GetCurrentPoint(nullptr).Position().X;
                double const delta = x - **lastX;
                *lastX = x;
                double const current = PixelLength(column.Width(), minimum);
                double const next = growOnPositiveDrag ? current + delta : current - delta;
                SetPixelWidth(column, next, minimum);
                e.Handled(true);
            });
            auto endDrag = [lastX, grip](IInspectable const&, PointerRoutedEventArgs const& e)
            {
                if (!lastX->has_value())
                    return;
                lastX->reset();
                grip.ReleasePointerCapture(e.Pointer());
                e.Handled(true);
            };
            grip.PointerReleased(endDrag);
            grip.PointerCaptureLost([lastX](IInspectable const&, PointerRoutedEventArgs const&)
            {
                lastX->reset();
            });
            return host;
        }

        FrameworkElement MakeRowSplitter(RowDefinition row, double minimum)
        {
            Border grip;
            grip.Height(kSplitterThickness);
            grip.HorizontalAlignment(HorizontalAlignment::Stretch);
            grip.VerticalAlignment(VerticalAlignment::Stretch);
            grip.Background(SolidColorBrush(winrt::Windows::UI::Color{ 1, 0, 0, 0 }));

            Border line;
            line.Height(1);
            line.HorizontalAlignment(HorizontalAlignment::Stretch);
            line.VerticalAlignment(VerticalAlignment::Center);
            line.Background(SolidColorBrush(winrt::Windows::UI::Color{ 90, 128, 128, 128 }));
            line.IsHitTestVisible(false);

            Grid host;
            host.Height(kSplitterThickness);
            host.Children().Append(line);
            host.Children().Append(grip);

            auto lastY = std::make_shared<std::optional<double>>();
            grip.PointerPressed([lastY, grip](IInspectable const&, PointerRoutedEventArgs const& e)
            {
                grip.CapturePointer(e.Pointer());
                *lastY = e.GetCurrentPoint(nullptr).Position().Y;
                e.Handled(true);
            });
            grip.PointerMoved([lastY, row, minimum](IInspectable const&, PointerRoutedEventArgs const& e)
            {
                if (!lastY->has_value())
                    return;
                double const y = e.GetCurrentPoint(nullptr).Position().Y;
                double const delta = y - **lastY;
                *lastY = y;
                // Drag down shrinks the bottom panel under the splitter.
                double const current = PixelLength(row.Height(), minimum);
                SetPixelHeight(row, current - delta, minimum);
                e.Handled(true);
            });
            auto endDrag = [lastY, grip](IInspectable const&, PointerRoutedEventArgs const& e)
            {
                if (!lastY->has_value())
                    return;
                lastY->reset();
                grip.ReleasePointerCapture(e.Pointer());
                e.Handled(true);
            };
            grip.PointerReleased(endDrag);
            grip.PointerCaptureLost([lastY](IInspectable const&, PointerRoutedEventArgs const&)
            {
                lastY->reset();
            });
            return host;
        }

        // The pane and inspector selectors. SelectorBar draws the accent rule under
        // the selected item itself, so an item only has to carry the index it
        // stands for.
        SelectorBar MakeSelectorBar(wchar_t const* const* titles, int count)
        {
            auto bar = SelectorBar();
            for (int i = 0; i < count; ++i)
            {
                auto item = SelectorBarItem();
                item.Text(titles[i]);
                item.Tag(box_value(i));
                bar.Items().Append(item);
            }
            return bar;
        }

        void SelectTab(SelectorBar const& bar, int index)
        {
            if (!bar || index < 0 || static_cast<uint32_t>(index) >= bar.Items().Size())
                return;
            bar.SelectedItem(bar.Items().GetAt(static_cast<uint32_t>(index)));
        }

        int SelectedTab(SelectorBar const& bar)
        {
            if (!bar)
                return -1;
            auto item = bar.SelectedItem();
            if (!item || !item.Tag())
                return -1;
            return unbox_value<int>(item.Tag());
        }

        void AppendLabeledRow(Panel const& parent, wchar_t const* label, FrameworkElement const& control,
                              double labelWidth = 150.0)
        {
            auto row = Grid();
            row.Margin(ThicknessHelper::FromLengths(0, 2, 0, 2));
            ColumnDefinition c0;
            c0.Width(GridLengthHelper::FromPixels(labelWidth));
            ColumnDefinition c1;
            c1.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            row.ColumnDefinitions().Append(c0);
            row.ColumnDefinitions().Append(c1);

            auto tb = TextBlock();
            tb.Text(label);
            tb.VerticalAlignment(VerticalAlignment::Center);
            tb.TextWrapping(TextWrapping::NoWrap);
            tb.Margin(ThicknessHelper::FromLengths(0, 0, 8, 0));
            Grid::SetColumn(tb, 0);
            row.Children().Append(tb);

            control.VerticalAlignment(VerticalAlignment::Center);
            control.HorizontalAlignment(HorizontalAlignment::Stretch);
            Grid::SetColumn(control, 1);
            row.Children().Append(control);

            parent.Children().Append(row);
        }
    }

    MainWindow::MainWindow()
    {
        Title(L"iRASPA");
        // Before BuildUi: the views it creates attach themselves to the
        // controller, which has to be able to reach back here already.
        m_controller.SetHost(this);
        BuildUi();
        TrySetMicaBackdrop();
        Content().as<FrameworkElement>().Loaded({ this, &MainWindow::MainWindow_Loaded });
    }

    bool MainWindow::TrySetMicaBackdrop()
    {
        if (!MicaController::IsSupported())
            return false;

        try
        {
            m_backdropConfiguration = SystemBackdropConfiguration();
            m_backdropActivatedRevoker = Activated(auto_revoke, { this, &MainWindow::Window_Activated });
            m_backdropClosedRevoker = Closed(auto_revoke, { this, &MainWindow::Window_Closed });
            m_backdropConfiguration.IsInputActive(true);
            SetConfigurationSourceTheme();

            if (auto root = Content().try_as<FrameworkElement>())
            {
                m_backdropThemeRevoker = root.ActualThemeChanged(
                    auto_revoke,
                    [this](FrameworkElement const&, IInspectable const&)
                    {
                        SetConfigurationSourceTheme();
                    });
            }

            m_micaController = MicaController();
            m_micaController.SetSystemBackdropConfiguration(m_backdropConfiguration);
            return m_micaController.AddSystemBackdropTarget(
                try_as<ICompositionSupportsSystemBackdrop>());
        }
        catch (...)
        {
            m_micaController = nullptr;
            m_backdropConfiguration = nullptr;
            return false;
        }
    }

    void MainWindow::SetConfigurationSourceTheme()
    {
        if (!m_backdropConfiguration)
            return;

        auto root = Content().try_as<FrameworkElement>();
        const auto theme = root ? root.ActualTheme() : ElementTheme::Default;
        switch (theme)
        {
        case ElementTheme::Dark:
            m_backdropConfiguration.Theme(SystemBackdropTheme::Dark);
            break;
        case ElementTheme::Light:
            m_backdropConfiguration.Theme(SystemBackdropTheme::Light);
            break;
        default:
            m_backdropConfiguration.Theme(SystemBackdropTheme::Default);
            break;
        }
    }

    void MainWindow::Window_Activated([[maybe_unused]] IInspectable const&,
                                      WindowActivatedEventArgs const& args)
    {
        if (m_backdropConfiguration)
        {
            m_backdropConfiguration.IsInputActive(
                args.WindowActivationState() != WindowActivationState::Deactivated);
        }
    }

    void MainWindow::Window_Closed([[maybe_unused]] IInspectable const&,
                                   [[maybe_unused]] WindowEventArgs const&)
    {
        // Tear down the DX12 host now, while the XAML tree and dispatcher are
        // still alive. Waiting for the MainWindow destructor is too late: the
        // XAML core is gone by then and unhooking panel events / releasing the
        // swap chain from there crashes on exit.
        if (m_renderHost)
        {
            m_renderHost->Shutdown();
            m_renderHost.reset();
        }

        m_backdropActivatedRevoker.revoke();
        m_backdropThemeRevoker.revoke();
        if (m_micaController)
        {
            m_micaController.Close();
            m_micaController = nullptr;
        }

        ShutdownPython();
    }

    void MainWindow::BuildUi()
    {
        auto root = Grid();
        // Let Mica show through chrome; panels keep their own surfaces.
        root.Background(SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
        auto rootRows = root.RowDefinitions();
        RowDefinition toolbarRow;
        // Auto with a 40px floor, not a fixed 40px: the Windows text-size
        // setting scales the menu labels without scaling layout, and a fixed
        // row clips their descenders (the "p" of "Help").
        toolbarRow.Height(GridLengthHelper::Auto());
        toolbarRow.MinHeight(40);
        rootRows.Append(toolbarRow);
        RowDefinition bodyRow;
        bodyRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        rootRows.Append(bodyRow);

        // Menu bar (Qt/Cocoa File menu: New / Import / Open / Exit)
        auto menuBar = MenuBar();
        menuBar.Padding(ThicknessHelper::FromLengths(4, 0, 4, 0));

        auto fileMenu = MenuBarItem();
        fileMenu.Title(L"File");

        // Cocoa keeps About in the application menu, which Windows does not have;
        // it leads the File menu here.
        auto aboutItem = MenuFlyoutItem();
        aboutItem.Text(L"About iRASPA");
        aboutItem.Click({ this, &MainWindow::OnAboutClick });
        fileMenu.Items().Append(aboutItem);
        fileMenu.Items().Append(MenuFlyoutSeparator());

        auto newSub = MenuFlyoutSubItem();
        newSub.Text(L"New");
        auto newStructureItem = MenuFlyoutItem();
        newStructureItem.Text(L"Structure Project");
        newStructureItem.Click({ this, &MainWindow::OnFileNewStructureClick });
        newSub.Items().Append(newStructureItem);
        auto newGroupItem = MenuFlyoutItem();
        newGroupItem.Text(L"Project Group");
        newGroupItem.Click({ this, &MainWindow::OnFileNewGroupClick });
        newSub.Items().Append(newGroupItem);
        fileMenu.Items().Append(newSub);

        auto importItem = MenuFlyoutItem();
        importItem.Text(L"Import...");
        importItem.Click({ this, &MainWindow::OnImportClick });
        fileMenu.Items().Append(importItem);

        auto openItem = MenuFlyoutItem();
        openItem.Text(L"Open...");
        openItem.KeyboardAcceleratorTextOverride(L"Ctrl+O");
        {
            auto accel = KeyboardAccelerator();
            accel.Key(winrt::Windows::System::VirtualKey::O);
            accel.Modifiers(winrt::Windows::System::VirtualKeyModifiers::Control);
            openItem.KeyboardAccelerators().Append(accel);
        }
        openItem.Click({ this, &MainWindow::OnOpenClick });
        fileMenu.Items().Append(openItem);

        fileMenu.Items().Append(MenuFlyoutSeparator());

        // Cocoa File menu: Save (Cmd-S) and Save As (Cmd-Shift-S), both writing
        // the document as an .irspdoc archive.
        auto saveItem = MenuFlyoutItem();
        saveItem.Text(L"Save");
        saveItem.KeyboardAcceleratorTextOverride(L"Ctrl+S");
        {
            auto accel = KeyboardAccelerator();
            accel.Key(winrt::Windows::System::VirtualKey::S);
            accel.Modifiers(winrt::Windows::System::VirtualKeyModifiers::Control);
            saveItem.KeyboardAccelerators().Append(accel);
        }
        saveItem.Click({ this, &MainWindow::OnSaveClick });
        fileMenu.Items().Append(saveItem);

        auto saveAsItem = MenuFlyoutItem();
        saveAsItem.Text(L"Save As...");
        saveAsItem.KeyboardAcceleratorTextOverride(L"Ctrl+Shift+S");
        {
            auto accel = KeyboardAccelerator();
            accel.Key(winrt::Windows::System::VirtualKey::S);
            accel.Modifiers(winrt::Windows::System::VirtualKeyModifiers::Control |
                            winrt::Windows::System::VirtualKeyModifiers::Shift);
            saveAsItem.KeyboardAccelerators().Append(accel);
        }
        saveAsItem.Click({ this, &MainWindow::OnSaveAsClick });
        fileMenu.Items().Append(saveAsItem);

        fileMenu.Items().Append(MenuFlyoutSeparator());

        auto exitItem = MenuFlyoutItem();
        exitItem.Text(L"Exit");
        exitItem.Click({ this, &MainWindow::OnExitClick });
        fileMenu.Items().Append(exitItem);

        menuBar.Items().Append(fileMenu);

        // Cocoa Edit menu: the two items carry the pending action name and undo
        // against the project navigator's stack or the selected project's,
        // depending on where the focus is.
        auto editMenu = MenuBarItem();
        editMenu.Title(L"Edit");
        m_undoMenuItem = MenuFlyoutItem();
        m_undoMenuItem.Text(L"Undo");
        m_undoMenuItem.KeyboardAcceleratorTextOverride(L"Ctrl+Z");
        {
            auto accel = KeyboardAccelerator();
            accel.Key(winrt::Windows::System::VirtualKey::Z);
            accel.Modifiers(winrt::Windows::System::VirtualKeyModifiers::Control);
            m_undoMenuItem.KeyboardAccelerators().Append(accel);
        }
        m_undoMenuItem.Click([this](IInspectable const&, RoutedEventArgs const&) { PerformUndo(); });
        editMenu.Items().Append(m_undoMenuItem);

        m_redoMenuItem = MenuFlyoutItem();
        m_redoMenuItem.Text(L"Redo");
        m_redoMenuItem.KeyboardAcceleratorTextOverride(L"Ctrl+Y");
        {
            auto accel = KeyboardAccelerator();
            accel.Key(winrt::Windows::System::VirtualKey::Y);
            accel.Modifiers(winrt::Windows::System::VirtualKeyModifiers::Control);
            m_redoMenuItem.KeyboardAccelerators().Append(accel);
            auto shiftAccel = KeyboardAccelerator();
            shiftAccel.Key(winrt::Windows::System::VirtualKey::Z);
            shiftAccel.Modifiers(winrt::Windows::System::VirtualKeyModifiers::Control |
                                 winrt::Windows::System::VirtualKeyModifiers::Shift);
            m_redoMenuItem.KeyboardAccelerators().Append(shiftAccel);
        }
        m_redoMenuItem.Click([this](IInspectable const&, RoutedEventArgs const&) { PerformRedo(); });
        editMenu.Items().Append(m_redoMenuItem);
        menuBar.Items().Append(editMenu);

        // Cocoa Help menu: "iRASPA Help" opens the help pages (F1 is the Windows
        // equivalent of Cocoa's help key equivalent).
        auto helpMenu = MenuBarItem();
        helpMenu.Title(L"Help");
        auto helpItem = MenuFlyoutItem();
        helpItem.Text(L"iRASPA Help");
        helpItem.KeyboardAcceleratorTextOverride(L"F1");
        {
            auto accel = KeyboardAccelerator();
            accel.Key(winrt::Windows::System::VirtualKey::F1);
            helpItem.KeyboardAccelerators().Append(accel);
        }
        helpItem.Click({ this, &MainWindow::OnHelpClick });
        helpMenu.Items().Append(helpItem);
        menuBar.Items().Append(helpMenu);

        Grid::SetRow(menuBar, 0);
        root.Children().Append(menuBar);

        // Cocoa toolbar: three segments at the right toggling the left,
        // bottom and right panels. The icons are recreated from the Cocoa
        // vector assets (LeftCollapsablePanel/BottomCollapsablePanel/
        // RightCollapsablePanel): an 18x14 frame with a 2px stroke and a
        // filled bar hugging the corresponding inner edge.
        auto makePanelIcon = [](int which)
        {
            Media::Brush fg{ nullptr };
            if (auto v = Application::Current().Resources().TryLookup(box_value(L"TextFillColorPrimaryBrush")))
                fg = v.try_as<Media::Brush>();
            if (!fg)
                fg = SolidColorBrush(winrt::Windows::UI::Colors::Black());

            auto canvas = Canvas();
            canvas.Width(20);
            canvas.Height(16);

            auto frame = winrt::Microsoft::UI::Xaml::Shapes::Rectangle();
            frame.Width(20);
            frame.Height(16);
            frame.StrokeThickness(2);
            frame.Stroke(fg);
            canvas.Children().Append(frame);

            auto bar = winrt::Microsoft::UI::Xaml::Shapes::Rectangle();
            bar.Fill(fg);
            switch (which)
            {
            case 0: // left panel
                bar.Width(2); bar.Height(10);
                Canvas::SetLeft(bar, 3); Canvas::SetTop(bar, 3);
                break;
            case 1: // bottom panel
                bar.Width(14); bar.Height(2);
                Canvas::SetLeft(bar, 3); Canvas::SetTop(bar, 11);
                break;
            default: // right panel
                bar.Width(2); bar.Height(10);
                Canvas::SetLeft(bar, 15); Canvas::SetTop(bar, 3);
                break;
            }
            canvas.Children().Append(bar);
            return canvas;
        };

        auto panelToggles = StackPanel();
        panelToggles.Orientation(Orientation::Horizontal);
        panelToggles.Spacing(4);
        panelToggles.HorizontalAlignment(HorizontalAlignment::Right);
        panelToggles.VerticalAlignment(VerticalAlignment::Center);
        panelToggles.Margin(ThicknessHelper::FromLengths(0, 0, 8, 0));

        // Cocoa share toolbar button (NSShareTemplate / shareAction:): opens
        // the system share sheet with the selected structures as attachments.
        // Sits left of the three panel toggles with a small gap.
        auto shareBtn = Button();
        {
            auto shareIcon = FontIcon();
            shareIcon.Glyph(L"\uE72D"); // Segoe Fluent "Share"
            shareIcon.FontSize(15);
            shareBtn.Content(shareIcon);
        }
        shareBtn.Padding(ThicknessHelper::FromLengths(7, 5, 7, 5));
        shareBtn.Margin(ThicknessHelper::FromLengths(0, 0, 10, 0));
        ToolTipService::SetToolTip(shareBtn, box_value(L"Share the selected structures"));
        shareBtn.Click([this](IInspectable const&, RoutedEventArgs const&)
        {
            ShareSelectedStructures();
        });
        panelToggles.Children().Append(shareBtn);

        constexpr wchar_t const* kPanelTips[] = {
            L"Show or hide the project panel",
            L"Show or hide the Python console and log",
            L"Show or hide the inspector panel"
        };
        for (int i = 0; i < 3; ++i)
        {
            auto toggle = ToggleButton();
            toggle.IsChecked(true);
            toggle.Padding(ThicknessHelper::FromLengths(7, 5, 7, 5));
            toggle.Content(makePanelIcon(i));
            ToolTipService::SetToolTip(toggle, box_value(kPanelTips[i]));
            toggle.Click([this, i](IInspectable const& sender, RoutedEventArgs const&)
            {
                if (auto tb = sender.try_as<ToggleButton>())
                    SetPanelVisibility(i, tb.IsChecked() && tb.IsChecked().Value());
            });
            panelToggles.Children().Append(toggle);
        }

        Grid::SetRow(panelToggles, 0);
        root.Children().Append(panelToggles);

        // Cocoa NSInformationPanelView: a 350x32 rounded panel centered in
        // the toolbar, drawn iTunes-LCD style (greenish vertical gradient
        // with a hard step at 50%, gray border). Messages such as
        // "Loading (name)" or "name (N atoms)" appear inside and fade out
        // after 5 seconds.
        auto infoPanel = Border();
        infoPanel.Width(350);
        infoPanel.Height(32);
        infoPanel.CornerRadius(CornerRadiusHelper::FromUniformRadius(3.5));
        infoPanel.HorizontalAlignment(HorizontalAlignment::Center);
        infoPanel.VerticalAlignment(VerticalAlignment::Center);
        infoPanel.BorderThickness(ThicknessHelper::FromUniformLength(1));
        infoPanel.BorderBrush(SolidColorBrush(winrt::Windows::UI::Color{ 255, 145, 145, 145 }));
        {
            auto gradient = LinearGradientBrush();
            gradient.StartPoint(winrt::Windows::Foundation::Point{ 0.0f, 0.0f });
            gradient.EndPoint(winrt::Windows::Foundation::Point{ 0.0f, 1.0f });
            auto addStop = [&gradient](uint8_t r, uint8_t g, uint8_t b, double offset)
            {
                auto stop = GradientStop();
                stop.Color(winrt::Windows::UI::Color{ 255, r, g, b });
                stop.Offset(offset);
                gradient.GradientStops().Append(stop);
            };
            addStop(237, 241, 225, 0.0);
            addStop(230, 235, 213, 0.5);
            addStop(222, 228, 199, 0.5);
            addStop(242, 245, 224, 1.0);
            infoPanel.Background(gradient);
        }
        m_infoPanelContent = StackPanel();
        m_infoPanelContent.Orientation(Orientation::Horizontal);
        m_infoPanelContent.Spacing(2);
        m_infoPanelContent.Padding(ThicknessHelper::FromLengths(6, 0, 6, 0));
        m_infoPanelContent.VerticalAlignment(VerticalAlignment::Center);
        infoPanel.Child(m_infoPanelContent);
        Grid::SetRow(infoPanel, 0);
        root.Children().Append(infoPanel);

        // Body: left | splitter | center | splitter | right
        auto body = Grid();
        body.Padding(ThicknessHelper::FromUniformLength(8));
        auto cols = body.ColumnDefinitions();
        ColumnDefinition leftCol;
        leftCol.Width(GridLengthHelper::FromPixels(280));
        leftCol.MinWidth(kLeftPanelMinWidth);
        cols.Append(leftCol);
        ColumnDefinition leftSplitCol;
        leftSplitCol.Width(GridLengthHelper::FromPixels(kSplitterThickness));
        cols.Append(leftSplitCol);
        ColumnDefinition centerCol;
        centerCol.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        cols.Append(centerCol);
        ColumnDefinition rightSplitCol;
        rightSplitCol.Width(GridLengthHelper::FromPixels(kSplitterThickness));
        cols.Append(rightSplitCol);
        ColumnDefinition rightCol;
        // Wide enough for the full Camera..Bonds tab row and the Atoms tab's
        // monospace row (vis/name/atom id./el/ff id./occ./xyz/q) with TreeView
        // chevron + indent.
        rightCol.Width(GridLengthHelper::FromPixels(700));
        rightCol.MinWidth(kRightPanelMinWidth);
        cols.Append(rightCol);
        m_leftPanelColumn = leftCol;
        m_rightPanelColumn = rightCol;
        m_leftSplitterColumn = leftSplitCol;
        m_rightSplitterColumn = rightSplitCol;
        Grid::SetRow(body, 1);
        root.Children().Append(body);

        // Left pane
        auto left = Grid();
        auto leftRows = left.RowDefinitions();
        RowDefinition segRow;
        // The selector brings its own height, rule included.
        segRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Auto));
        leftRows.Append(segRow);
        // Each of the three panes is a UserControl that brings its own rows and its
        // own Cocoa-style bottom bar, so the pane row is all the left pane needs
        // below the Project / Scene / Frame selector.
        RowDefinition listRow;
        listRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        leftRows.Append(listRow);

        static constexpr wchar_t const* kLeftPaneTitles[] = { L"Project", L"Scene", L"Frame" };
        auto segment = MakeSelectorBar(kLeftPaneTitles, 3);
        segment.SelectionChanged({ this, &MainWindow::OnLeftPaneSelectionChanged });
        m_leftPaneTabs = segment;
        m_leftPaneIndex = 0;
        SyncTabSelection(segment, m_leftPaneIndex);
        Grid::SetRow(segment, 0);
        left.Children().Append(segment);

        // The three panes are XAML UserControls stacked in the same cell, each
        // bringing its own row template and its own [+] [−] bar; the selector above
        // shows one at a time. Each one is handed the controller and registers
        // itself with it as the presenter of its part of the document.
        auto lists = Grid();
        m_projectView = winrt::make<ProjectView>();
        ProjectViewImpl()->SetController(&m_controller);
        lists.Children().Append(m_projectView);

        m_sceneView = winrt::make<SceneView>();
        m_sceneView.Visibility(Visibility::Collapsed);
        SceneViewImpl()->SetController(&m_controller);
        lists.Children().Append(m_sceneView);

        m_frameView = winrt::make<FrameView>();
        m_frameView.Visibility(Visibility::Collapsed);
        FrameViewImpl()->SetController(&m_controller);
        lists.Children().Append(m_frameView);

        Grid::SetRow(lists, 1);
        left.Children().Append(lists);
        Grid::SetColumn(left, 0);
        body.Children().Append(left);
        m_leftPanel = left;

        m_leftSplitter = MakeColumnSplitter(leftCol, true, kLeftPanelMinWidth);
        Grid::SetColumn(m_leftSplitter, 1);
        body.Children().Append(m_leftSplitter);

        // Center: render | splitter | bottom
        auto center = Grid();
        auto centerRows = center.RowDefinitions();
        RowDefinition renderRow;
        renderRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        centerRows.Append(renderRow);
        RowDefinition bottomSplitRow;
        bottomSplitRow.Height(GridLengthHelper::FromPixels(kSplitterThickness));
        centerRows.Append(bottomSplitRow);
        RowDefinition logRow;
        logRow.Height(GridLengthHelper::FromPixels(180));
        logRow.MinHeight(kBottomPanelMinHeight);
        centerRows.Append(logRow);
        m_bottomPanelRow = logRow;
        m_bottomSplitterRow = bottomSplitRow;

        m_renderPanel = SwapChainPanel();
        Grid::SetRow(m_renderPanel, 0);
        center.Children().Append(m_renderPanel);

        // Transparent XAML layer above the swap chain for the rubber-band
        // selection rectangle (Cocoa draws this with a CAShapeLayer).
        m_renderOverlay = Canvas();
        m_renderOverlay.IsHitTestVisible(false);
        Grid::SetRow(m_renderOverlay, 0);
        center.Children().Append(m_renderOverlay);

        m_bottomSplitter = MakeRowSplitter(logRow, kBottomPanelMinHeight);
        Grid::SetRow(m_bottomSplitter, 1);
        center.Children().Append(m_bottomSplitter);

        // Bottom strip like iRASPA-COCOA: Python console on the left, the
        // log viewer next to it on the right.
        auto bottom = Grid();
        ColumnDefinition pyCol;
        pyCol.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        bottom.ColumnDefinitions().Append(pyCol);
        ColumnDefinition logCol;
        logCol.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        bottom.ColumnDefinitions().Append(logCol);

        auto pythonConsole = BuildPythonConsole();
        pythonConsole.Margin(ThicknessHelper::FromLengths(0, 0, 4, 0));
        Grid::SetColumn(pythonConsole, 0);
        bottom.Children().Append(pythonConsole);

        m_logBox = TextBox();
        m_logBox.IsReadOnly(true);
        m_logBox.AcceptsReturn(true);
        m_logBox.TextWrapping(TextWrapping::Wrap);
        m_logBox.FontFamily(winrt::Microsoft::UI::Xaml::Media::FontFamily(L"Consolas"));
        m_logBox.Margin(ThicknessHelper::FromLengths(4, 0, 0, 0));
        Grid::SetColumn(m_logBox, 1);
        bottom.Children().Append(m_logBox);

        Grid::SetRow(bottom, 2);
        center.Children().Append(bottom);
        m_bottomPanel = bottom;
        Grid::SetColumn(center, 2);
        body.Children().Append(center);

        m_rightSplitter = MakeColumnSplitter(rightCol, false, kRightPanelMinWidth);
        Grid::SetColumn(m_rightSplitter, 3);
        body.Children().Append(m_rightSplitter);

        // Right: inspector tab buttons + body
        auto right = Grid();
        auto rightRows = right.RowDefinitions();
        RowDefinition tabsRow;
        tabsRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Auto));
        rightRows.Append(tabsRow);
        RowDefinition inspRow;
        inspRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        rightRows.Append(inspRow);

        auto tabBar = MakeSelectorBar(kInspectorTitles, 7);
        tabBar.Margin(ThicknessHelper::FromLengths(0, 0, 0, 4));
        tabBar.SelectionChanged({ this, &MainWindow::OnInspectorSelectionChanged });
        m_inspectorTabs = tabBar;
        Grid::SetRow(tabBar, 0);
        right.Children().Append(tabBar);

        // Inspector body: a Grid host so tabs like Atoms can pin a bottom bar
        // while other tabs keep a scrolling StackPanel.
        m_inspectorHost = Grid();
        m_inspectorScroll = ScrollViewer();
        m_inspectorScroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        m_inspectorScroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        m_inspectorPanel = StackPanel();
        m_inspectorPanel.Margin(ThicknessHelper::FromUniformLength(12));
        m_inspectorPanel.Spacing(8);
        m_inspectorScroll.Content(m_inspectorPanel);
        m_inspectorHost.Children().Append(m_inspectorScroll);
        Grid::SetRow(m_inspectorHost, 1);
        right.Children().Append(m_inspectorHost);
        ShowInspector(0);

        Grid::SetColumn(right, 4);
        body.Children().Append(right);
        m_rightPanel = right;

        Content(root);
    }

    // Cocoa iRASPAWindowController.toggleProjectView: each toolbar segment
    // collapses/expands one split-view item. Here the panel is hidden and its
    // fixed grid track zeroed (saving the size for restore), so the render
    // view absorbs the freed space.
    void MainWindow::SetPanelVisibility(int which, bool visible)
    {
        const auto vis = visible ? Visibility::Visible : Visibility::Collapsed;
        switch (which)
        {
        case 0:
            if (!m_leftPanel || !m_leftPanelColumn)
                return;
            if (visible == (m_leftPanel.Visibility() == Visibility::Visible))
                return;
            if (!visible)
                m_savedLeftPanelWidth = m_leftPanelColumn.Width();
            m_leftPanelColumn.Width(visible ? m_savedLeftPanelWidth
                                            : GridLengthHelper::FromPixels(0));
            m_leftPanel.Visibility(vis);
            if (m_leftSplitterColumn)
                m_leftSplitterColumn.Width(visible
                    ? GridLengthHelper::FromPixels(kSplitterThickness)
                    : GridLengthHelper::FromPixels(0));
            if (m_leftSplitter)
                m_leftSplitter.Visibility(vis);
            break;
        case 1:
            if (!m_bottomPanel || !m_bottomPanelRow)
                return;
            if (visible == (m_bottomPanel.Visibility() == Visibility::Visible))
                return;
            if (!visible)
                m_savedBottomPanelHeight = m_bottomPanelRow.Height();
            m_bottomPanelRow.Height(visible ? m_savedBottomPanelHeight
                                            : GridLengthHelper::FromPixels(0));
            m_bottomPanel.Visibility(vis);
            if (m_bottomSplitterRow)
                m_bottomSplitterRow.Height(visible
                    ? GridLengthHelper::FromPixels(kSplitterThickness)
                    : GridLengthHelper::FromPixels(0));
            if (m_bottomSplitter)
                m_bottomSplitter.Visibility(vis);
            break;
        case 2:
            if (!m_rightPanel || !m_rightPanelColumn)
                return;
            if (visible == (m_rightPanel.Visibility() == Visibility::Visible))
                return;
            if (!visible)
                m_savedRightPanelWidth = m_rightPanelColumn.Width();
            m_rightPanelColumn.Width(visible ? m_savedRightPanelWidth
                                             : GridLengthHelper::FromPixels(0));
            m_rightPanel.Visibility(vis);
            if (m_rightSplitterColumn)
                m_rightSplitterColumn.Width(visible
                    ? GridLengthHelper::FromPixels(kSplitterThickness)
                    : GridLengthHelper::FromPixels(0));
            if (m_rightSplitter)
                m_rightSplitter.Visibility(vis);
            break;
        default:
            break;
        }
    }

    // Cocoa shareAction: the share sheet gets the selected structures as file
    // attachments plus an explanatory text line. Windows' share UI needs real
    // files, so each selected structure is exported to a temp CIF (or XYZ when
    // it has no cell) before the sheet opens.
    void MainWindow::ShareSelectedStructures()
    {
        namespace DataTransfer = winrt::Windows::ApplicationModel::DataTransfer;

        m_shareFilePaths.clear();
        try
        {
            if (!m_project || !m_project->sceneList())
            {
                AppendLog(L"Share: no project loaded");
                return;
            }

            auto sanitize = [](std::wstring name) -> std::wstring
            {
                for (auto& ch : name)
                {
                    if (ch == L'<' || ch == L'>' || ch == L':' || ch == L'"' || ch == L'/' ||
                        ch == L'\\' || ch == L'|' || ch == L'?' || ch == L'*')
                        ch = L'_';
                }
                return name;
            };

            const auto tempDir = std::filesystem::temp_directory_path();
            int counter = 0;
            for (auto const& movieFrames : m_project->sceneList()->selectediRASPAStructures())
            {
                for (auto const& frame : movieFrames)
                {
                    if (!frame)
                        continue;
                    auto structure = std::dynamic_pointer_cast<Structure>(frame->object());
                    if (!structure)
                        continue;

                    RKString displayName = structure->displayName();
                    SKSpaceGroup spaceGroup = SKSpaceGroup(1);
                    if (auto sgv = std::dynamic_pointer_cast<SpaceGroupViewer>(frame->object()))
                        spaceGroup = sgv->spaceGroup();

                    RKString out;
                    std::wstring extension;
                    if (auto cellData = structure->cellForFractionalPositions())
                    {
                        auto atoms = structure->asymmetricAtomsCopiedAndTransformedToFractionalPositions();
                        out = SKCIFWriter(displayName, spaceGroup, cellData->first,
                                          cellData->second, atoms).string();
                        extension = L".cif";
                    }
                    else
                    {
                        auto atoms = structure->asymmetricAtomsCopiedAndTransformedToCartesianPositions();
                        auto cartCell = structure->cellForCartesianPositions();
                        std::shared_ptr<SKCell> cell = cartCell ? cartCell->first : nullptr;
                        double3 origin = cartCell ? cartCell->second : double3(0.0, 0.0, 0.0);
                        out = SKXYZWriter(displayName, spaceGroup, cell, origin, atoms).string();
                        extension = L".xyz";
                    }

                    std::wstring baseName = sanitize(displayName.toStdWString());
                    if (baseName.empty())
                        baseName = L"structure-" + std::to_wstring(++counter);
                    const auto path = tempDir / (baseName + extension);

                    // UTF-8 on disk, like the file-export path.
                    const std::wstring wide = out.toStdWString();
                    std::string utf8;
                    if (!wide.empty())
                    {
                        const int len = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                                              static_cast<int>(wide.size()),
                                                              nullptr, 0, nullptr, nullptr);
                        utf8.resize(static_cast<size_t>(len));
                        ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                              static_cast<int>(wide.size()),
                                              utf8.data(), len, nullptr, nullptr);
                    }
                    std::ofstream file(path, std::ios::binary | std::ios::trunc);
                    if (!file)
                        continue;
                    file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
                    file.close();

                    m_shareFilePaths.push_back(path.wstring());
                }
            }
        }
        catch (...)
        {
            AppendLog(L"Share: exporting the structures failed");
            return;
        }

        if (m_shareFilePaths.empty())
        {
            AppendLog(L"Share: no structures selected");
            return;
        }

        try
        {
            HWND hwnd{};
            if (auto native = try_as<IWindowNative>())
                native->get_WindowHandle(&hwnd);
            if (!hwnd)
                return;

            // Desktop (HWND) apps must go through IDataTransferManagerInterop
            // instead of DataTransferManager::GetForCurrentView.
            auto interop = winrt::get_activation_factory<DataTransfer::DataTransferManager,
                                                         IDataTransferManagerInterop>();
            if (!m_dataTransferManager)
            {
                winrt::check_hresult(interop->GetForWindow(
                    hwnd, winrt::guid_of<DataTransfer::DataTransferManager>(),
                    winrt::put_abi(m_dataTransferManager)));
                m_dataTransferManager.DataRequested(
                    [this](DataTransfer::DataTransferManager const&,
                           DataTransfer::DataRequestedEventArgs const& args) -> winrt::fire_and_forget
                    {
                        auto request = args.Request();
                        auto deferral = request.GetDeferral();
                        auto data = request.Data();
                        data.Properties().Title(L"iRASPA structure(s)");
                        data.Properties().Description(
                            L"The structures can be found as attachments.");
                        data.SetText(L"The structures can be found as attachments. "
                                     L"Import these into the iRASPA project-view.");
                        try
                        {
                            auto paths = m_shareFilePaths;
                            auto items = single_threaded_vector<winrt::Windows::Storage::IStorageItem>();
                            for (auto const& p : paths)
                            {
                                auto file = co_await winrt::Windows::Storage::StorageFile::
                                    GetFileFromPathAsync(hstring(p));
                                items.Append(file);
                            }
                            if (items.Size() > 0)
                                data.SetStorageItems(items);
                        }
                        catch (...)
                        {
                        }
                        deferral.Complete();
                    });
            }
            winrt::check_hresult(interop->ShowShareUIForWindow(hwnd));
        }
        catch (...)
        {
            AppendLog(L"Share: the Windows share UI could not be opened");
        }
    }

    // Cocoa NSInformationPanelView.showInfoItem: replace the current item,
    // then remove it after 5 seconds with a 1 second fade.
    void MainWindow::ShowInfoPanelMessage(hstring const& glyph, hstring const& message)
    {
        if (!m_infoPanelContent)
            return;
        try
        {
            m_infoPanelContent.Children().Clear();
            m_infoPanelContent.Opacity(1.0);

            if (!glyph.empty())
            {
                auto icon = FontIcon();
                icon.Glyph(glyph);
                icon.FontSize(16);
                icon.VerticalAlignment(VerticalAlignment::Center);
                icon.Foreground(SolidColorBrush(winrt::Windows::UI::Color{ 255, 96, 96, 96 }));
                icon.Margin(ThicknessHelper::FromLengths(0, 0, 4, 0));
                m_infoPanelContent.Children().Append(icon);
            }

            // Cocoa: system font 18, NSColor.gray, truncating tail.
            auto text = TextBlock();
            text.Text(message);
            text.FontSize(18);
            text.Foreground(SolidColorBrush(winrt::Windows::UI::Color{ 255, 128, 128, 128 }));
            text.VerticalAlignment(VerticalAlignment::Center);
            text.TextTrimming(TextTrimming::CharacterEllipsis);
            text.MaxWidth(310);
            m_infoPanelContent.Children().Append(text);

            if (!m_infoPanelTimer)
            {
                m_infoPanelTimer = DispatcherTimer();
                m_infoPanelTimer.Interval(std::chrono::seconds(5));
                m_infoPanelTimer.Tick([this](IInspectable const&, IInspectable const&)
                {
                    m_infoPanelTimer.Stop();
                    if (!m_infoPanelContent)
                        return;
                    try
                    {
                        namespace Anim = winrt::Microsoft::UI::Xaml::Media::Animation;
                        auto fade = Anim::DoubleAnimation();
                        fade.From(1.0);
                        fade.To(0.0);
                        fade.Duration(DurationHelper::FromTimeSpan(std::chrono::seconds(1)));
                        auto storyboard = Anim::Storyboard();
                        storyboard.Children().Append(fade);
                        Anim::Storyboard::SetTarget(fade, m_infoPanelContent);
                        Anim::Storyboard::SetTargetProperty(fade, L"Opacity");
                        storyboard.Completed([this](IInspectable const&, IInspectable const&)
                        {
                            if (!m_infoPanelContent)
                                return;
                            m_infoPanelContent.Children().Clear();
                            m_infoPanelContent.Opacity(1.0);
                        });
                        storyboard.Begin();
                    }
                    catch (...)
                    {
                        m_infoPanelContent.Children().Clear();
                        m_infoPanelContent.Opacity(1.0);
                    }
                });
            }
            m_infoPanelTimer.Stop();
            m_infoPanelTimer.Start();
        }
        catch (...)
        {
        }
    }

    void MainWindow::AppendLog(std::wstring const& message)
    {
        m_log += message;
        if (!message.empty() && message.back() != L'\n')
            m_log += L'\n';
        if (m_logBox)
            m_logBox.Text(m_log);
    }

    void MainWindow::ShowInspector(int index)
    {
        if (index < 0 || index >= 7 || !m_inspectorPanel || !m_inspectorHost)
            return;
        m_inspectorIndex = index;
        SyncTabSelection(m_inspectorTabs, index);
        m_inspectorPanel.Children().Clear();

        // Restore the default scrolling stack for non-Atoms tabs.
        m_inspectorHost.Children().Clear();
        m_inspectorHost.RowDefinitions().Clear();
        m_inspectorScroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        m_inspectorScroll.Content(m_inspectorPanel);
        m_inspectorHost.Children().Append(m_inspectorScroll);

        switch (index)
        {
        case 0: ShowCameraDetailView(); return;
        case 1: ShowElementsDetailView(); return;
        case 2: ShowInfoDetailView(); return;
        case 3: ShowAppearanceDetailView(); return;
        case 4: ShowCellDetailView(); return;
        case 5: ShowAtomsDetailView(); return;
        case 6: ShowBondsDetailView(); return;
        default: break;
        }

        auto title = TextBlock();
        title.Text(std::wstring(kInspectorTitles[index]) + L" inspector");
        auto hint = TextBlock();
        hint.Text(L"Forms will be ported from Qt/Cocoa next.");
        hint.TextWrapping(TextWrapping::Wrap);
        hint.Opacity(0.7);
        m_inspectorPanel.Children().Append(title);
        m_inspectorPanel.Children().Append(hint);
    }

    // The Info tab is a UserControl rather than a form rebuilt per selection: it
    // is created the first time it is shown and reloaded after that, so its
    // fields, scroll position and open sections survive a selection change.
    void MainWindow::ShowInfoDetailView()
    {
        try
        {
            if (!m_infoDetailView)
            {
                m_infoDetailView = winrt::make<InfoDetailView>();
                winrt::get_self<InfoDetailView>(m_infoDetailView)->SetController(&m_controller);
            }
            m_inspectorPanel.Children().Append(m_infoDetailView);
            winrt::get_self<InfoDetailView>(m_infoDetailView)->Reload();
        }
        catch (hresult_error const& ex)
        {
            AppendLog(std::wstring(L"Info inspector error: ") + std::wstring(ex.message()));
        }
        catch (...)
        {
            AppendLog(L"Info inspector error");
        }
    }

    // The Cell tab, the same way. It registers itself as the pane the controller
    // reloads when a cell edit is undone.
    void MainWindow::ShowCellDetailView()
    {
        try
        {
            if (!m_cellDetailView)
            {
                m_cellDetailView = winrt::make<CellDetailView>();
                auto *view = winrt::get_self<CellDetailView>(m_cellDetailView);
                view->SetController(&m_controller);
                m_controller.SetCellPane(view);
            }
            m_inspectorPanel.Children().Append(m_cellDetailView);
            winrt::get_self<CellDetailView>(m_cellDetailView)->Reload();
        }
        catch (hresult_error const& ex)
        {
            AppendLog(std::wstring(L"Cell inspector error: ") + std::wstring(ex.message()));
        }
        catch (...)
        {
            AppendLog(L"Cell inspector error");
        }
    }

    // The Camera tab, the same way. It edits the renderer's camera through
    // CameraPaneHost rather than the document controller: camera changes are not
    // undoable, as in Cocoa.
    void MainWindow::ShowCameraDetailView()
    {
        try
        {
            if (!m_cameraDetailView)
            {
                m_cameraDetailView = winrt::make<CameraDetailView>();
                winrt::get_self<CameraDetailView>(m_cameraDetailView)->SetHost(this);
            }
            m_inspectorPanel.Children().Append(m_cameraDetailView);
            winrt::get_self<CameraDetailView>(m_cameraDetailView)->Reload();
        }
        catch (hresult_error const& ex)
        {
            AppendLog(std::wstring(L"Camera inspector error: ") + std::wstring(ex.message()));
        }
        catch (...)
        {
            AppendLog(L"Camera inspector error");
        }
    }

    void MainWindow::UpdateCameraReadouts()
    {
        if (m_cameraDetailView)
            winrt::get_self<CameraDetailView>(m_cameraDetailView)->ReloadReadouts();
    }

    // The Appearance tab. Its edits are not undoable either, but they are edits
    // of the selected structures rather than of the camera, so it goes through
    // the document controller's selection.
    void MainWindow::ShowAppearanceDetailView()
    {
        try
        {
            if (!m_appearanceDetailView)
            {
                m_appearanceDetailView = winrt::make<AppearanceDetailView>();
                winrt::get_self<AppearanceDetailView>(m_appearanceDetailView)->SetController(&m_controller);
            }
            m_inspectorPanel.Children().Append(m_appearanceDetailView);
            winrt::get_self<AppearanceDetailView>(m_appearanceDetailView)->Reload();
        }
        catch (hresult_error const& ex)
        {
            AppendLog(std::wstring(L"Appearance inspector error: ") + std::wstring(ex.message()));
        }
        catch (...)
        {
            AppendLog(L"Appearance inspector error");
        }
    }

    // The Elements tab. Its ItemsView has to own the scrolling for the cards to
    // virtualize (117 per set), so the view replaces the inspector's scrolling
    // stack rather than being placed inside it, as the Atoms tab does.
    void MainWindow::ShowElementsDetailView()
    {
        try
        {
            if (!m_elementsDetailView)
            {
                m_elementsDetailView = winrt::make<ElementsDetailView>();
                winrt::get_self<ElementsDetailView>(m_elementsDetailView)->SetController(&m_controller);
            }
            m_inspectorHost.Children().Clear();
            m_inspectorHost.RowDefinitions().Clear();
            m_inspectorHost.Children().Append(m_elementsDetailView);
            winrt::get_self<ElementsDetailView>(m_elementsDetailView)->Reload();
        }
        catch (hresult_error const& ex)
        {
            AppendLog(std::wstring(L"Elements inspector error: ") + std::wstring(ex.message()));
        }
        catch (...)
        {
            AppendLog(L"Elements inspector error");
        }
    }

    // The Atoms tab, the same way: a structure has thousands of atom rows, so its
    // ItemsView has to own the scrolling for them to virtualize. It registers
    // itself as the pane the controller refreshes after a field write or an undo.
    void MainWindow::ShowAtomsDetailView()
    {
        try
        {
            if (!m_atomsDetailView)
            {
                m_atomsDetailView = winrt::make<AtomsDetailView>();
                auto *view = winrt::get_self<AtomsDetailView>(m_atomsDetailView);
                view->SetController(&m_controller);
                m_controller.SetAtomsPane(view);
            }
            m_inspectorHost.Children().Clear();
            m_inspectorHost.RowDefinitions().Clear();
            m_inspectorHost.Children().Append(m_atomsDetailView);
            winrt::get_self<AtomsDetailView>(m_atomsDetailView)->Reload();
        }
        catch (hresult_error const& ex)
        {
            AppendLog(std::wstring(L"Atoms inspector error: ") + std::wstring(ex.message()));
        }
        catch (...)
        {
            AppendLog(L"Atoms inspector error");
        }
    }

    // The Bonds tab, the same way: as many rows as there are bonds, so its
    // ItemsView owns the scrolling. It registers itself as the pane the
    // controller re-points after a length change has regenerated the bond set.
    void MainWindow::ShowBondsDetailView()
    {
        try
        {
            if (!m_bondsDetailView)
            {
                m_bondsDetailView = winrt::make<BondsDetailView>();
                auto *view = winrt::get_self<BondsDetailView>(m_bondsDetailView);
                view->SetController(&m_controller);
                m_controller.SetBondsPane(view);
            }
            m_inspectorHost.Children().Clear();
            m_inspectorHost.RowDefinitions().Clear();
            m_inspectorHost.Children().Append(m_bondsDetailView);
            winrt::get_self<BondsDetailView>(m_bondsDetailView)->Reload();
        }
        catch (hresult_error const& ex)
        {
            AppendLog(std::wstring(L"Bonds inspector error: ") + std::wstring(ex.message()));
        }
        catch (...)
        {
            AppendLog(L"Bonds inspector error");
        }
    }

    std::shared_ptr<RKCamera> MainWindow::ActiveCamera() const
    {
        // With no project loaded there is nothing to point a camera at: the
        // renderer still holds the camera of the project that was there before,
        // and showing its values would outlive the structure they belong to.
        if (!m_project)
            return nullptr;
        if (auto camera = m_project->camera())
            return camera;
        if (auto *renderer = Renderer())
            return renderer->camera();
        return nullptr;
    }

    void MainWindow::ResetRendererCameraView()
    {
        if (auto *renderer = Renderer())
            renderer->resetCameraView();
    }

    void MainWindow::SetRendererCameraOrthographic(bool orthographic)
    {
        if (auto *renderer = Renderer())
            renderer->setCameraOrthographic(orthographic);
    }

    // Cocoa builds this in RenderTabViewController.viewDidLoad and hangs it off the render
    // view; the same items in the same order are built here. Checkable items are toggles
    // rather than plain items, standing in for NSMenuItem.state.
    void MainWindow::BuildRenderContextMenu()
    {
        m_renderContextMenu = MenuFlyout();

        auto resetDistance = MenuFlyoutItem();
        resetDistance.Text(L"Reset Camera Distance");
        resetDistance.Click([this](IInspectable const&, RoutedEventArgs const&)
        {
            auto camera = ActiveCamera();
            if (!camera)
                return;
            if (m_project)
                camera->setBoundingBox(m_project->renderBoundingBox());
            camera->resetCameraDistance();
            RedrawRenderer();
        });
        m_renderContextMenu.Items().Append(resetDistance);

        // Cocoa lists these Z, Y, X rather than in the axis order.
        auto resetTo = MenuFlyoutSubItem();
        resetTo.Text(L"Reset Camera To");
        const std::pair<const wchar_t*, ResetDirectionType> directions[] = {
            { L"Z-Direction", ResetDirectionType::plus_Z },
            { L"Y-Direction", ResetDirectionType::plus_Y },
            { L"X-Direction", ResetDirectionType::plus_X },
        };
        for (auto const& [text, direction] : directions)
        {
            auto item = MenuFlyoutItem();
            item.Text(text);
            const ResetDirectionType captured = direction;
            item.Click([this, captured](IInspectable const&, RoutedEventArgs const&)
            {
                ResetRenderCameraToDirection(captured);
            });
            resetTo.Items().Append(item);
        }
        m_renderContextMenu.Items().Append(resetTo);

        auto projection = MenuFlyoutSubItem();
        projection.Text(L"Camera Projection");
        m_orthographicMenuItem = ToggleMenuFlyoutItem();
        m_orthographicMenuItem.Text(L"Orthographic");
        m_orthographicMenuItem.Click([this](IInspectable const&, RoutedEventArgs const&)
        {
            SetRenderCameraProjection(true);
        });
        projection.Items().Append(m_orthographicMenuItem);
        m_perspectiveMenuItem = ToggleMenuFlyoutItem();
        m_perspectiveMenuItem.Text(L"Perspective");
        m_perspectiveMenuItem.Click([this](IInspectable const&, RoutedEventArgs const&)
        {
            SetRenderCameraProjection(false);
        });
        projection.Items().Append(m_perspectiveMenuItem);
        m_renderContextMenu.Items().Append(projection);

        m_boundingBoxMenuItem = ToggleMenuFlyoutItem();
        m_boundingBoxMenuItem.Text(L"Show Bounding Box");
        m_boundingBoxMenuItem.Click([this](IInspectable const&, RoutedEventArgs const&)
        {
            ToggleRenderBoundingBox();
        });
        m_renderContextMenu.Items().Append(m_boundingBoxMenuItem);

        auto ambientOcclusion = MenuFlyoutItem();
        ambientOcclusion.Text(L"Compute AO High-Quality");
        ambientOcclusion.Click([this](IInspectable const&, RoutedEventArgs const&)
        {
            ComputeHighQualityAmbientOcclusion();
        });
        m_renderContextMenu.Items().Append(ambientOcclusion);

        auto exportTo = MenuFlyoutSubItem();
        exportTo.Text(L"Export To");
        const std::pair<const wchar_t*, DocumentController::AtomExportFormat> formats[] = {
            { L"PDB", DocumentController::AtomExportFormat::PDB },
            { L"mmCIF", DocumentController::AtomExportFormat::mmCIF },
            { L"CIF", DocumentController::AtomExportFormat::CIF },
            { L"XYZ", DocumentController::AtomExportFormat::XYZ },
            { L"VASP POSCAR", DocumentController::AtomExportFormat::POSCAR },
        };
        for (auto const& [text, format] : formats)
        {
            auto item = MenuFlyoutItem();
            item.Text(text);
            const DocumentController::AtomExportFormat captured = format;
            item.Click([this, captured](IInspectable const&, RoutedEventArgs const&)
            {
                ExportRenderStructure(captured);
            });
            exportTo.Items().Append(item);
        }
        m_renderContextMenu.Items().Append(exportTo);
    }

    void MainWindow::ShowRenderContextMenu(Point const& position)
    {
        if (!m_renderContextMenu)
            BuildRenderContextMenu();
        if (!m_renderPanel)
            return;

        // Cocoa reflects the current state through validateMenuItem each time the menu is
        // about to be shown; there is no such callback here, so the state is written on the
        // way up instead.
        auto camera = ActiveCamera();
        const bool orthographic = camera && camera->isOrthographic();
        m_orthographicMenuItem.IsChecked(camera && orthographic);
        m_perspectiveMenuItem.IsChecked(camera && !orthographic);
        m_boundingBoxMenuItem.IsChecked(m_project && m_project->showBoundingBox());

        const bool hasProject = m_project != nullptr;
        for (auto const& item : m_renderContextMenu.Items())
        {
            if (auto const& control = item.try_as<MenuFlyoutItemBase>())
                control.IsEnabled(hasProject);
        }

        try
        {
            m_renderContextMenu.ShowAt(m_renderPanel, position);
        }
        catch (...)
        {
        }
    }

    void MainWindow::ResetRenderCameraToDirection(ResetDirectionType direction)
    {
        auto camera = ActiveCamera();
        if (!camera)
            return;
        if (m_project)
            camera->setBoundingBox(m_project->renderBoundingBox());
        camera->setResetDirectionType(direction);
        camera->resetCameraToDirection();
        camera->resetCameraDistance();
        ResetRendererCameraView();
        RedrawRenderer();
        RefreshInspector();
    }

    void MainWindow::SetRenderCameraProjection(bool orthographic)
    {
        auto camera = ActiveCamera();
        if (!camera)
            return;
        if (orthographic)
            camera->setCameraToOrthographic();
        else
            camera->setCameraToPerspective();
        SetRendererCameraOrthographic(orthographic);
        RedrawRenderer();
        RefreshInspector();
    }

    void MainWindow::ToggleRenderBoundingBox()
    {
        if (!m_project)
            return;
        m_project->setShowBoundingBox(!m_project->showBoundingBox());
        // The shader reads its geometry from the data source, which has to be rebuilt for
        // the box to appear at the size of the current structure.
        ReloadRenderer();
    }

    void MainWindow::ComputeHighQualityAmbientOcclusion()
    {
        auto *renderer = Renderer();
        if (!renderer)
            return;
        if (auto scene = m_controller.SelectedScene())
            InvalidateSceneAmbientOcclusion(scene);
        renderer->setAmbientOcclusionQuality(RKRenderQuality::picture);
        ReloadRenderer();
        Log(L"Ambient occlusion recomputed at high quality");
    }

    void MainWindow::ExportRenderStructure(DocumentController::AtomExportFormat format)
    {
        if (auto frame = m_controller.AtomsFrame())
            m_controller.ExportAtoms(frame, format);
        else
            Log(L"Export needs a frame holding a structure");
    }

    // The picker needs the window handle, so it stays here rather than in the
    // camera form.
    winrt::fire_and_forget MainWindow::PickBackgroundImageAsync()
    {
        try
        {
            FileOpenPicker picker;
            picker.ViewMode(PickerViewMode::Thumbnail);
            picker.SuggestedStartLocation(PickerLocationId::PicturesLibrary);
            picker.FileTypeFilter().Append(L".png");
            picker.FileTypeFilter().Append(L".jpg");
            picker.FileTypeFilter().Append(L".jpeg");
            picker.FileTypeFilter().Append(L".bmp");

            if (auto native = try_as<IWindowNative>())
            {
                HWND hwnd{};
                native->get_WindowHandle(&hwnd);
                if (auto init = picker.as<IInitializeWithWindow>())
                    init->Initialize(hwnd);
            }

            StorageFile file = co_await picker.PickSingleFileAsync();
            if (!file || !m_project)
                co_return;

            m_project->setBackgroundType(RKBackgroundType::image);
            m_project->loadBackgroundImage(RKString::fromStdWString(std::wstring(file.Path())));
            ReloadRenderer();
            AppendLog(L"Background image loaded: " + std::wstring(file.Name()));
            if (m_cameraDetailView)
                winrt::get_self<CameraDetailView>(m_cameraDetailView)->Reload();
        }
        catch (hresult_error const& ex)
        {
            AppendLog(std::wstring(L"Background image error: ") + std::wstring(ex.message()));
        }
        catch (...)
        {
            AppendLog(L"Background image error");
        }
    }

    // The other half of Export As: the document layer serialized the structure,
    // and the save picker it is written through needs the window handle.
    winrt::fire_and_forget MainWindow::SaveTextFileAsync(std::wstring text, std::wstring extension,
                                                        std::wstring typeName,
                                                        std::wstring suggestedName)
    {
        try
        {
            FileSavePicker picker;
            picker.SuggestedStartLocation(PickerLocationId::DocumentsLibrary);
            auto extensions = single_threaded_vector<hstring>();
            extensions.Append(hstring(extension));
            picker.FileTypeChoices().Insert(hstring(typeName), extensions);
            picker.SuggestedFileName(hstring(suggestedName));

            if (auto native = try_as<IWindowNative>())
            {
                HWND hwnd{};
                native->get_WindowHandle(&hwnd);
                if (auto init = picker.as<IInitializeWithWindow>())
                    init->Initialize(hwnd);
            }

            StorageFile file = co_await picker.PickSaveFileAsync();
            if (!file)
            {
                AppendLog(L"Export cancelled");
                co_return;
            }
            co_await FileIO::WriteTextAsync(file, hstring(text));
            AppendLog(L"Exported " + std::wstring(file.Name()));
        }
        catch (hresult_error const& ex)
        {
            AppendLog(std::wstring(L"Export failed: ") + std::wstring(ex.message()));
        }
        catch (...)
        {
            AppendLog(L"Export failed");
        }
    }

    void MainWindow::ZoomRendererCamera(double amount)
    {
        // Before the render view is up the camera is still there to zoom, so the
        // distance is changed directly and the redraw is a no-op.
        if (auto *renderer = Renderer())
        {
            renderer->zoomCamera(amount);
            return;
        }
        if (auto camera = ActiveCamera())
        {
            camera->increaseDistance(amount);
            RedrawRenderer();
        }
    }

    int64_t MainWindow::LiveAdapterLuid()
    {
        auto *renderer = Renderer();
        if (!renderer)
            return 0;
        const LUID luid = renderer->deviceContext().adapterLuid();
        return (static_cast<int64_t>(luid.HighPart) << 32) | static_cast<uint32_t>(luid.LowPart);
    }

    DirectXRenderer *MainWindow::Renderer() const
    {
        return m_renderHost ? m_renderHost->renderer() : nullptr;
    }

    bool MainWindow::LiveSupportsRaytracing()
    {
        auto *renderer = Renderer();
        return renderer && renderer->supportsRaytracing();
    }

    std::wstring MainWindow::LiveRaytracingStatus()
    {
        auto *renderer = Renderer();
        return renderer ? RKString(renderer->raytracingStatus()).toStdWString() : std::wstring();
    }

    void MainWindow::ReloadRenderer()
    {
        if (auto *r = Renderer())
            r->reloadData();
        if (m_renderHost)
            m_renderHost->RequestRedraw();
    }

    void MainWindow::RedrawRenderer()
    {
        if (m_renderHost)
            m_renderHost->RequestRedraw();
    }

    void MainWindow::ReloadRendererSelection()
    {
        if (auto *r = Renderer())
            r->reloadSelectionData();
        if (m_renderHost)
            m_renderHost->RequestRedraw();
    }

    // Cocoa hands over scene.allRenderFrames: every frame of every movie, whether it is the one on
    // screen or not, since the movie the scene is scrubbed to is not what was baked against.
    void MainWindow::InvalidateSceneAmbientOcclusion(std::shared_ptr<Scene> const& scene)
    {
        auto *renderer = Renderer();
        if (!renderer || !scene)
            return;

        std::vector<std::shared_ptr<RKRenderObject>> frames;
        for (auto const& movie : scene->movies())
        {
            if (!movie)
                continue;
            for (auto const& frame : movie->frames())
            {
                if (!frame)
                    continue;
                if (auto object = std::dynamic_pointer_cast<RKRenderObject>(frame->object()))
                    frames.push_back(object);
            }
        }
        renderer->invalidateCachedAmbientOcclusionTextures(frames);
    }

    void MainWindow::ApplyCellEditAndReload()
    {
        if (m_project)
        {
            if (auto cam = m_project->camera())
                cam->resetForNewBoundingBox(m_project->renderBoundingBox());
        }
        ReloadRenderer();
    }

    ProjectView* MainWindow::ProjectViewImpl() const
    {
        if (!m_projectView)
            return nullptr;
        return winrt::get_self<ProjectView>(m_projectView);
    }

    SceneView* MainWindow::SceneViewImpl() const
    {
        if (!m_sceneView)
            return nullptr;
        return winrt::get_self<SceneView>(m_sceneView);
    }

    FrameView* MainWindow::FrameViewImpl() const
    {
        if (!m_frameView)
            return nullptr;
        return winrt::get_self<FrameView>(m_frameView);
    }

    void MainWindow::SuppressProjectSelectionEvents(bool suppress)
    {
        if (auto *view = ProjectViewImpl())
            view->SuppressSelectionEvents(suppress);
    }

    void MainWindow::WireDocument()
    {
        try
    {
        m_document = std::make_shared<DocumentData>();
        // A new document invalidates every recorded operation.
        DocumentUndoStack().clear();
        UpdateEditMenuLabels();

            if (auto *view = ProjectViewImpl())
                view->Bind();

            m_controller.RefreshSceneAndFrameRows();

            AppendLog(L"Document initialized");

            // Cocoa/Qt: load gallery examples off the UI thread, then graft under Gallery.
            LoadGalleryDatabaseAsync();
            LoadStructureDatabasesAsync();
        }
        catch (...)
        {
            AppendLog(L"Document init error");
            m_document.reset();
        }
    }

    void MainWindow::UpdateLeftPaneVisibility()
    {
        try
        {
            SyncTabSelection(m_leftPaneTabs, m_leftPaneIndex);
            const bool project = m_leftPaneIndex == 0;
            const bool scene = m_leftPaneIndex == 1;
            const bool frame = m_leftPaneIndex == 2;
            if (m_projectView)
                m_projectView.Visibility(project ? Visibility::Visible : Visibility::Collapsed);
            if (m_sceneView)
                m_sceneView.Visibility(scene ? Visibility::Visible : Visibility::Collapsed);
            if (m_frameView)
                m_frameView.Visibility(frame ? Visibility::Visible : Visibility::Collapsed);

            // Cocoa's lists hand the inspector their own selection as they appear:
            // the frame list edits the frames picked in it, the other two the
            // selected movies whole.
            m_controller.SetInspectorSource(frame ? DocumentController::InspectorSource::Frames
                                                  : DocumentController::InspectorSource::Movies);
            // Appearance Primitive/Ribbon visibility follows the active left pane:
            // project → any structure in the project; scene → selected movies only;
            // frame → selected frames.
            if (project)
                m_controller.SetAppearanceSectionScope(DocumentController::AppearanceSectionScope::Project);
            else if (scene)
                m_controller.SetAppearanceSectionScope(DocumentController::AppearanceSectionScope::Movies);
            else
                m_controller.SetAppearanceSectionScope(DocumentController::AppearanceSectionScope::Frames);
        }
        catch (...)
        {
        }
    }

    void MainWindow::MainWindow_Loaded([[maybe_unused]] IInspectable const& sender,
                                       [[maybe_unused]] RoutedEventArgs const& e)
    {
        try
        {
            m_renderHost = std::make_unique<Dx12SwapChainPanelHost>();
            m_renderHost->SetPanel(m_renderPanel);
            m_renderHost->SetOverlayCanvas(m_renderOverlay);
            m_renderHost->SetContextMenuHandler([this](Point const& position)
            {
                ShowRenderContextMenu(position);
            });
            m_renderHost->Initialize();
            // 3D pick / rubber-band selection also highlights the atom rows.
            if (auto *renderer = m_renderHost->renderer())
            {
                renderer->setSelectionChangedCallback([this]() { m_controller.ReloadAtomRowSelection(); });
                // Mouse rotate/pan/zoom in the render view keeps the camera
                // inspector readouts live (Cocoa CameraDidChangeNotification).
                renderer->setCameraChangedCallback([this]() { UpdateCameraReadouts(); });
            }
            AppendLog(L"DirectX SwapChainPanel host ready");
        }
        catch (hresult_error const& ex)
        {
            AppendLog(std::wstring(L"DX host error: ") + std::wstring(ex.message()));
        }
        catch (std::exception const& ex)
        {
            AppendLog(std::wstring(L"DX host error: ") + winrt::to_hstring(ex.what()).c_str());
        }

        WireDocument();
        UpdateLeftPaneVisibility();
    }

    // DocumentHost: the parts of the window the document layer reaches into.
    void MainWindow::RefreshInspector()
    {
        ClearInspectorVisualState();
        ShowInspector(m_inspectorIndex);
    }

    // Cocoa reloadData after a selection change: the renderer follows the scene
    // list's selected structures. The inspector is rebuilt afterwards rather than
    // inline, so a click that changes the selection paints before the detail rows
    // are recreated; the epoch drops the rebuild if another project loads first.
    void MainWindow::ApplySelectedStructuresToRenderer(bool refreshInspector)
    {
        if (!m_project || !m_project->sceneList())
            return;
        auto *renderer = Renderer();
        if (!renderer)
            return;

        try
        {
            renderer->setRenderStructures(m_project->sceneList()->selectediRASPARenderStructures());
            renderer->reloadData();
        }
        catch (...)
        {
            AppendLog(L"Scene/Frame renderer update error");
            return;
        }

        if (!refreshInspector)
            return;

        const uint32_t epoch = ++m_projectLoadEpoch;
        DispatcherQueue().TryEnqueue(winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
            [this, epoch]()
            {
                try
                {
                    if (epoch == m_projectLoadEpoch)
                        RefreshInspector();
                }
                catch (...)
                {
                    AppendLog(L"Inspector refresh error");
                }
            });
    }

    void MainWindow::ShowMessage(std::wstring const& glyph, std::wstring const& message)
    {
        ShowInfoPanelMessage(hstring(glyph), hstring(message));
    }

    void MainWindow::LoadProjectIntoRenderer(std::shared_ptr<ProjectStructure> const& project,
                                             std::wstring const& displayName)
    {
        ApplyProjectToRenderer(project, displayName);
    }

    void MainWindow::Enqueue(std::function<void()> work)
    {
        DispatcherQueue().TryEnqueue([work = std::move(work)]() { work(); });
    }

    void MainWindow::ClearInspectorVisualState()
    {
        try
        {
            if (m_inspectorPanel)
                m_inspectorPanel.Children().Clear();
            if (m_inspectorHost)
            {
                m_inspectorHost.Children().Clear();
                m_inspectorHost.RowDefinitions().Clear();
                if (m_inspectorScroll)
                {
                    m_inspectorScroll.Content(m_inspectorPanel);
                    m_inspectorHost.Children().Append(m_inspectorScroll);
                }
            }
        }
        catch (...)
        {
        }
        // The element cards point into the document's force-field and color sets,
        // the atom rows hold nodes of the project's atom tree and the bond rows
        // hold its bonds, so all three go before the project they were built for
        // does.
        if (m_elementsDetailView)
            winrt::get_self<ElementsDetailView>(m_elementsDetailView)->Clear();
        if (m_atomsDetailView)
            winrt::get_self<AtomsDetailView>(m_atomsDetailView)->Clear();
        if (m_bondsDetailView)
            winrt::get_self<BondsDetailView>(m_bondsDetailView)->Clear();
    }

    void MainWindow::ApplyProjectToRenderer(std::shared_ptr<ProjectStructure> project,
                                            std::wstring const& displayName)
    {
        if (!project)
            return;

        // Drop inspector UI that may still reference the previous project before
        // swapping render data (avoids use-after-free when switching structures).
        ClearInspectorVisualState();

        m_project = std::move(project);
        try
        {
            m_project->setInitialSelectionIfNeeded();
        }
        catch (...)
        {
            AppendLog(L"Initial selection failed");
        }

        m_controller.RefreshSceneAndFrameRows();

        if (!m_renderHost || !m_renderHost->renderer())
        {
            AppendLog(L"Renderer not ready; structure loaded into document only");
            return;
        }

        try
        {
            auto *renderer = m_renderHost->renderer();
            auto sceneList = m_project->sceneList();
            if (!sceneList)
            {
                AppendLog(L"Project has no scene list");
                return;
            }
            auto structures = sceneList->selectediRASPARenderStructures();
            renderer->setRenderStructures(structures);
            // setRenderDataSource already reloads when the scene is ready.
            renderer->setRenderDataSource(m_project);

            size_t structureCount = 0;
            for (auto const& scene : structures)
                structureCount += scene.size();
            AppendLog(L"Loaded " + displayName + L" (" +
                      std::to_wstring(structureCount) + L" structure(s))");

            // Cocoa ProjectStructureNode.infoPanelString: after loading, the
            // toolbar message panel shows "name (N atoms)".
            size_t atomCount = 0;
            for (auto const& scene : sceneList->scenes())
            {
                if (!scene)
                    continue;
                for (auto const& movie : scene->movies())
                {
                    if (!movie)
                        continue;
                    for (auto const& f : movie->frames())
                        atomCount += CountFrameAtoms(f);
                }
            }
            ShowInfoPanelMessage(L"\uE7C3", hstring(displayName + L" (" +
                                                    std::to_wstring(atomCount) + L" atoms)"));
        }
        catch (hresult_error const& ex)
        {
            AppendLog(std::wstring(L"Renderer update error: ") + std::wstring(ex.message()));
            return;
        }
        catch (std::exception const& ex)
        {
            AppendLog(std::wstring(L"Renderer update error: ") + winrt::to_hstring(ex.what()).c_str());
            return;
        }
        catch (...)
        {
            AppendLog(L"Renderer update error");
            return;
        }

        const int inspector = m_inspectorIndex;
        const uint32_t epoch = ++m_projectLoadEpoch;
        // Low priority: let layout finish after ClearInspectorVisualState / renderer swap.
        DispatcherQueue().TryEnqueue(winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
            [this, inspector, epoch]()
            {
                try
                {
                    if (epoch != m_projectLoadEpoch)
                        return;
                    if (inspector == 0 || inspector == 1 || inspector == 2 || inspector == 3 ||
                        inspector == 4 || inspector == 5)
                        ShowInspector(inspector);
                }
                catch (...)
                {
                    AppendLog(L"Inspector refresh error");
                }
            });
    }

    // Cocoa: with no project selected the render view has no data source, so the
    // window shows an empty scene rather than the structure that was there before.
    void MainWindow::ClearProjectFromRenderer()
    {
        if (!m_project)
            return;

        ClearInspectorVisualState();
        m_project.reset();
        ++m_projectLoadEpoch;
        m_controller.RefreshSceneAndFrameRows();

        if (m_renderHost && m_renderHost->renderer())
        {
            try
            {
                auto *renderer = m_renderHost->renderer();
                renderer->setRenderStructures({});
                renderer->setRenderDataSource(nullptr);
            }
            catch (...)
            {
                AppendLog(L"Renderer clear error");
            }
        }

        try
        {
            ShowInspector(m_inspectorIndex);
        }
        catch (...)
        {
            AppendLog(L"Inspector refresh error");
        }
    }

    void MainWindow::ApplyImportedProject(std::shared_ptr<ProjectStructure> project,
                                          std::wstring const& displayName)
    {
        if (!project)
            return;
        ++m_projectLoadEpoch;
        SuppressProjectSelectionEvents(true);
        m_controller.InsertProjectIntoDocument(project, displayName);
        ApplyProjectToRenderer(project, displayName);
        SuppressProjectSelectionEvents(false);
    }

    void MainWindow::ImportPathsAsProjects(std::vector<std::wstring> const& paths,
                                           std::vector<std::wstring> const& names,
                                           SKParser::ImportType importType,
                                           bool proteinOnlyAsymmetricUnit,
                                           bool separatePolymerChains,
                                           bool asMolecule)
    {
        if (paths.empty())
            return;
        if (!m_document)
            m_document = std::make_shared<DocumentData>();

        try
        {
            if (importType == SKParser::ImportType::asSeparateProjects)
            {
                std::shared_ptr<ProjectStructure> last;
                std::wstring lastName;
                for (size_t i = 0; i < paths.size(); ++i)
                {
                    AppendLog(L"Importing " + paths[i] + L" ...");
                    std::filesystem::path url(paths[i]);
                    auto project = std::make_shared<ProjectStructure>(
                        std::vector<std::filesystem::path>{url},
                        m_document->colorSets(),
                        m_document->forceFieldSets(),
                        SKParser::ImportType::asSingleProject,
                        proteinOnlyAsymmetricUnit,
                        asMolecule,
                        separatePolymerChains);
                    const std::wstring& name = i < names.size() ? names[i] : paths[i];
                    ++m_projectLoadEpoch;
                    SuppressProjectSelectionEvents(true);
                    m_controller.InsertProjectIntoDocument(project, name);
                    SuppressProjectSelectionEvents(false);
                    last = project;
                    lastName = name;
                }
                if (last)
                    ApplyProjectToRenderer(last, lastName);
            }
            else
            {
                std::vector<std::filesystem::path> importPaths;
                for (auto const& path : paths)
                    importPaths.emplace_back(std::filesystem::path(path));

                AppendLog(L"Importing " + std::to_wstring(paths.size()) + L" file(s) ...");
                auto project = std::make_shared<ProjectStructure>(
                    importPaths,
                    m_document->colorSets(),
                    m_document->forceFieldSets(),
                    importType,
                    proteinOnlyAsymmetricUnit,
                    asMolecule,
                    separatePolymerChains);
                const std::wstring& name = !names.empty() ? names.front() : paths.front();
                ApplyImportedProject(project, name);
            }
        }
        catch (hresult_error const& ex)
        {
            SuppressProjectSelectionEvents(false);
            AppendLog(std::wstring(L"Import failed: ") + std::wstring(ex.message()));
        }
        catch (std::exception const& ex)
        {
            SuppressProjectSelectionEvents(false);
            AppendLog(std::wstring(L"Import failed: ") + winrt::to_hstring(ex.what()).c_str());
        }
        catch (...)
        {
            SuppressProjectSelectionEvents(false);
            AppendLog(L"Import failed");
        }
    }

    namespace
    {
        std::wstring StripExtension(std::wstring name)
        {
            auto pos = name.find_last_of(L'.');
            if (pos != std::wstring::npos && pos > 0)
                name.resize(pos);
            return name;
        }

        // Cocoa ImportAccessoryViewController: "Import Options" with radios in
        // the left column and protein/molecule checks in the right. That needs a
        // freeform layout, so Import uses the Explorer open dialog + a dialog
        // template (IFileDialogCustomize only stacks controls in one column).
        struct StructureImportPick
        {
            std::vector<std::wstring> paths;
            std::vector<std::wstring> names;
            SKParser::ImportType importType = SKParser::ImportType::asSingleProject;
            bool proteinOnlyAsymmetricUnit = true;
            bool separatePolymerChains = false;
            bool asMolecule = false;
            bool cancelled = true;
        };

        struct ImportAccessoryState
        {
            SKParser::ImportType importType = SKParser::ImportType::asSingleProject;
            bool proteinOnlyAsymmetricUnit = true;
            bool separatePolymerChains = false;
            bool asMolecule = false;
        };

        void ReadImportAccessory(HWND accessory, ImportAccessoryState& state)
        {
            if (IsDlgButtonChecked(accessory, IDC_IMPORT_SEPARATE) == BST_CHECKED)
                state.importType = SKParser::ImportType::asSeparateProjects;
            else if (IsDlgButtonChecked(accessory, IDC_IMPORT_FRAMES) == BST_CHECKED)
                state.importType = SKParser::ImportType::asMovieFrames;
            else
                state.importType = SKParser::ImportType::asSingleProject;

            state.proteinOnlyAsymmetricUnit =
                IsDlgButtonChecked(accessory, IDC_IMPORT_PROTEIN) == BST_CHECKED;
            state.separatePolymerChains =
                IsDlgButtonChecked(accessory, IDC_IMPORT_CHAINS) == BST_CHECKED;
            state.asMolecule =
                IsDlgButtonChecked(accessory, IDC_IMPORT_MOLECULE) == BST_CHECKED;
        }

        UINT_PTR CALLBACK ImportAccessoryHook(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            // Color only static text (group box caption). Handling WM_CTLCOLORBTN
            // with TRANSPARENT bk mode leaves checkboxes/radios looking
            // indeterminate until the next hover repaint.
            if (msg == WM_CTLCOLORSTATIC)
            {
                HDC const hdc = reinterpret_cast<HDC>(wParam);
                SetTextColor(hdc, RGB(0, 0, 0));
                SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
                SetBkMode(hdc, OPAQUE);
                return reinterpret_cast<UINT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
            }

            if (msg != WM_NOTIFY)
                return 0;

            auto const* notify = reinterpret_cast<OFNOTIFYW const*>(lParam);
            auto* state = reinterpret_cast<ImportAccessoryState*>(notify->lpOFN->lCustData);
            if (!state)
                return 0;

            switch (notify->hdr.code)
            {
            case CDN_INITDONE:
                CheckRadioButton(hwnd, IDC_IMPORT_SEPARATE, IDC_IMPORT_FRAMES, IDC_IMPORT_SINGLE);
                // On, as in Cocoa — avoid expanding a protein into every symmetry mate
                // while the ribbon only follows the chain that was read.
                // Use BST_CHECKED / BST_UNCHECKED only (never BST_INDETERMINATE).
                CheckDlgButton(hwnd, IDC_IMPORT_PROTEIN, BST_CHECKED);
                // Off by default: keep TER chains in one scene-view item.
                CheckDlgButton(hwnd, IDC_IMPORT_CHAINS, BST_UNCHECKED);
                CheckDlgButton(hwnd, IDC_IMPORT_MOLECULE, BST_UNCHECKED);
                return 0;
            case CDN_FILEOK:
                ReadImportAccessory(hwnd, *state);
                return 0;
            default:
                return 0;
            }
        }

        StructureImportPick ShowStructureImportDialog(HWND owner)
        {
            StructureImportPick result;
            ImportAccessoryState accessoryState;

            std::wstring fileBuffer(64 * 1024, L'\0');
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = owner;
            ofn.lpstrFilter =
                L"Structure files (*.cif;*.mmcif;*.pdb;*.xyz;*.poscar;*.vasp)\0"
                L"*.cif;*.mmcif;*.pdb;*.xyz;*.poscar;*.vasp\0"
                L"CIF (*.cif;*.mmcif)\0*.cif;*.mmcif\0"
                L"PDB (*.pdb)\0*.pdb\0"
                L"XYZ (*.xyz)\0*.xyz\0"
                L"POSCAR/VASP (*.poscar;*.vasp)\0*.poscar;*.vasp\0"
                L"All files (*.*)\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.lpstrFile = fileBuffer.data();
            ofn.nMaxFile = static_cast<DWORD>(fileBuffer.size());
            ofn.lpstrTitle = L"Import Structures";
            ofn.Flags = OFN_EXPLORER | OFN_ENABLEHOOK | OFN_ENABLETEMPLATE |
                        OFN_ENABLESIZING | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST |
                        OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
            ofn.lpfnHook = ImportAccessoryHook;
            ofn.hInstance = GetModuleHandleW(nullptr);
            ofn.lpTemplateName = MAKEINTRESOURCEW(IDD_IMPORT_ACCESSORY);
            ofn.lCustData = reinterpret_cast<LPARAM>(&accessoryState);

            if (!GetOpenFileNameW(&ofn))
                return result;

            result.importType = accessoryState.importType;
            result.proteinOnlyAsymmetricUnit = accessoryState.proteinOnlyAsymmetricUnit;
            result.separatePolymerChains = accessoryState.separatePolymerChains;
            result.asMolecule = accessoryState.asMolecule;

            // Multi-select: directory\0file1\0file2\0\0  (or a single full path).
            wchar_t const* p = fileBuffer.c_str();
            std::wstring const first = p;
            p += first.size() + 1;
            if (*p == L'\0')
            {
                result.paths.push_back(first);
            }
            else
            {
                std::filesystem::path const dir(first);
                while (*p)
                {
                    std::wstring const name = p;
                    p += name.size() + 1;
                    result.paths.push_back((dir / name).wstring());
                }
            }

            for (auto const& path : result.paths)
            {
                result.names.push_back(StripExtension(
                    std::filesystem::path(path).filename().wstring()));
            }

            result.cancelled = result.paths.empty();
            return result;
        }
    }

    winrt::fire_and_forget MainWindow::ImportStructuresAsync()
    {
        // fire_and_forget keeps the call site unchanged; the common dialog itself
        // is modal and synchronous (IFileOpenDialog::Show).
        try
        {
            HWND hwnd{};
            if (auto native = try_as<IWindowNative>())
                native->get_WindowHandle(&hwnd);

            auto const pick = ShowStructureImportDialog(hwnd);
            if (pick.cancelled)
            {
                AppendLog(L"Import cancelled");
                co_return;
            }

            ImportPathsAsProjects(pick.paths, pick.names, pick.importType,
                                  pick.proteinOnlyAsymmetricUnit,
                                  pick.separatePolymerChains, pick.asMolecule);
        }
        catch (hresult_error const& ex)
        {
            AppendLog(std::wstring(L"Import failed: ") + std::wstring(ex.message()));
        }
        catch (std::exception const& ex)
        {
            AppendLog(std::wstring(L"Import failed: ") + winrt::to_hstring(ex.what()).c_str());
        }
        co_return;
    }

    void MainWindow::OnOpenClick([[maybe_unused]] IInspectable const&,
                                 [[maybe_unused]] RoutedEventArgs const&)
    {
        OpenDocumentAsync();
    }

    void MainWindow::OnImportClick([[maybe_unused]] IInspectable const&,
                                   [[maybe_unused]] RoutedEventArgs const&)
    {
        ImportStructuresAsync();
    }

    void MainWindow::OnFileNewStructureClick([[maybe_unused]] IInspectable const&,
                                             [[maybe_unused]] RoutedEventArgs const&)
    {
        m_controller.AddProjectFromToolbar(/*group=*/false);
    }

    void MainWindow::OnFileNewGroupClick([[maybe_unused]] IInspectable const&,
                                         [[maybe_unused]] RoutedEventArgs const&)
    {
        m_controller.AddProjectFromToolbar(/*group=*/true);
    }

    void MainWindow::OnExitClick([[maybe_unused]] IInspectable const&,
                                 [[maybe_unused]] RoutedEventArgs const&)
    {
        Close();
    }

    // Setting SelectedItem raises SelectionChanged, so a selection written from
    // code holds the handler off; otherwise showing a pane would come straight
    // back through the handler that asked for it.
    void MainWindow::SyncTabSelection(SelectorBar const& bar, int index)
    {
        const bool wasSuppressed = m_suppressTabSelection;
        m_suppressTabSelection = true;
        SelectTab(bar, index);
        m_suppressTabSelection = wasSuppressed;
    }

    void MainWindow::OnLeftPaneSelectionChanged(
        SelectorBar const& sender, [[maybe_unused]] SelectorBarSelectionChangedEventArgs const&)
    {
        if (m_suppressTabSelection)
            return;
        const int index = SelectedTab(sender);
        if (index < 0)
            return;
        m_leftPaneIndex = index;
        UpdateLeftPaneVisibility();
    }

    void MainWindow::OnInspectorSelectionChanged(
        SelectorBar const& sender, [[maybe_unused]] SelectorBarSelectionChangedEventArgs const&)
    {
        if (m_suppressTabSelection)
            return;
        const int index = SelectedTab(sender);
        if (index >= 0)
            ShowInspector(index);
    }

}