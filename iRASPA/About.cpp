/********************************************************************************************************************
    The window the About panel is shown in, ported from the iRASPA-COCOA AboutWindow
    (AboutWindow.storyboard): a single, non-resizable 720x450 panel, centered over the
    main window and reused on every invocation. Its content is AboutView.xaml.
    Height is a little above Cocoa's 400 so the extra creator and acknowledgement
    lines still fit without scrolling the credits.
 ********************************************************************************************************************/

#include "pch.h"
#include "MainWindow.xaml.h"

#include "AboutView.xaml.h"

#include <winrt/Microsoft.UI.Windowing.h>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Windows::Foundation;

namespace winrt::iRASPA_WinUI::implementation
{
    void MainWindow::OnAboutClick([[maybe_unused]] IInspectable const&,
                                  [[maybe_unused]] RoutedEventArgs const&)
    {
        ShowAboutWindow();
    }

    // Single non-resizable about window (Cocoa AboutWindow.storyboard), reused
    // on every invocation.
    void MainWindow::ShowAboutWindow()
    {
        try
        {
            if (m_aboutWindow)
            {
                m_aboutWindow.Activate();
                return;
            }

            auto view = winrt::make<AboutView>();
            winrt::get_self<AboutView>(view)->SetLog(
                [this](std::wstring const& message) { AppendLog(message); });

            Window window;
            window.Title(L"About iRASPA");
            window.Content(view);
            m_aboutWindow = window;
            window.Closed([this](IInspectable const&, WindowEventArgs const&)
            {
                m_aboutWindow = nullptr;
            });

            if (auto appWindow = window.AppWindow())
            {
                // Cocoa's panel cannot be resized, minimized or zoomed.
                if (auto presenter = appWindow.Presenter()
                        .try_as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>())
                {
                    presenter.IsResizable(false);
                    presenter.IsMaximizable(false);
                    presenter.IsMinimizable(false);
                }

                double scale = 1.0;
                if (auto root = Content(); root && root.XamlRoot())
                    scale = root.XamlRoot().RasterizationScale();
                // Cocoa started at 650x400; extra credit lines need more height, and a
                // bit more width keeps "CoRE MOF database" on one line.
                const int32_t width = static_cast<int32_t>(720 * scale);
                const int32_t height = static_cast<int32_t>(450 * scale);
                appWindow.ResizeClient({ width, height });

                // Centered over the main window, like Cocoa's centered panel.
                if (auto owner = AppWindow())
                {
                    auto const position = owner.Position();
                    auto const size = owner.Size();
                    appWindow.Move({ position.X + (size.Width - width) / 2,
                                     position.Y + (size.Height - height) / 2 });
                }
            }

            window.Activate();
        }
        catch (hresult_error const& ex)
        {
            AppendLog(std::wstring(L"About panel error: ") + std::wstring(ex.message()));
        }
        catch (...)
        {
            AppendLog(L"About panel error");
        }
    }
}
