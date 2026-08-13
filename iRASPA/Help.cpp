/********************************************************************************************************************
    Help menu, ported from iRASPA-COCOA (Main.storyboard "iRASPA Help" / AppDelegate
    openOnlineHelp). Cocoa opens its help book in the macOS Help Viewer; Windows has no
    equivalent viewer, so the same pages are shown from https://help.iraspa.org in a
    help window of their own.
 ********************************************************************************************************************/

#include "pch.h"
#include "MainWindow.xaml.h"

#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.Web.WebView2.Core.h>
#include <winrt/Windows.Foundation.h>

#include <shellapi.h>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace winrt::Windows::Foundation;

namespace winrt::iRASPA_WinUI::implementation
{
    namespace
    {
        constexpr wchar_t const* kHelpUrl = L"https://help.iraspa.org/index.html";
    }

    void MainWindow::OnHelpClick([[maybe_unused]] IInspectable const&,
                                 [[maybe_unused]] RoutedEventArgs const&)
    {
        ShowHelpWindow();
    }

    void MainWindow::ShowHelpWindow()
    {
        try
        {
            // Cocoa's Help Viewer is a single window that comes forward again.
            if (m_helpWindow)
            {
                m_helpWindow.Activate();
                return;
            }

            auto layout = Grid();
            if (auto resources = Application::Current().Resources();
                resources.HasKey(box_value(L"ApplicationPageBackgroundThemeBrush")))
            {
                layout.Background(resources.Lookup(box_value(L"ApplicationPageBackgroundThemeBrush"))
                                      .try_as<Brush>());
            }

            auto browser = WebView2();
            browser.Source(Uri(kHelpUrl));
            layout.Children().Append(browser);

            Window window;
            window.Title(L"iRASPA Help");
            window.Content(layout);
            m_helpWindow = window;
            window.Closed([this](IInspectable const&, WindowEventArgs const&)
            {
                m_helpWindow = nullptr;
            });

            if (auto appWindow = window.AppWindow())
            {
                double scale = 1.0;
                if (auto root = Content(); root && root.XamlRoot())
                    scale = root.XamlRoot().RasterizationScale();
                const int32_t width = static_cast<int32_t>(1000 * scale);
                const int32_t height = static_cast<int32_t>(760 * scale);
                appWindow.ResizeClient({ width, height });

                if (auto owner = AppWindow())
                {
                    auto const position = owner.Position();
                    auto const size = owner.Size();
                    appWindow.Move({ position.X + (size.Width - width) / 2,
                                     position.Y + (size.Height - height) / 2 });
                }
            }

            window.Activate();

            // Without the WebView2 runtime there is nothing to show the pages in, so
            // hand the same address to the default browser instead.
            browser.NavigationCompleted([this](WebView2 const&,
                                               Microsoft::Web::WebView2::Core::CoreWebView2NavigationCompletedEventArgs const& args)
            {
                if (!args.IsSuccess())
                    AppendLog(L"Help page could not be loaded (no network connection?)");
            });
            EnsureHelpBrowserAsync(browser);
        }
        catch (hresult_error const& ex)
        {
            AppendLog(std::wstring(L"Help window error: ") + std::wstring(ex.message()));
            ShellExecuteW(nullptr, L"open", kHelpUrl, nullptr, nullptr, SW_SHOWNORMAL);
        }
    }

    winrt::fire_and_forget MainWindow::EnsureHelpBrowserAsync(
        winrt::Microsoft::UI::Xaml::Controls::WebView2 browser)
    {
        auto lifetime = get_strong();
        try
        {
            co_await browser.EnsureCoreWebView2Async();
        }
        catch (hresult_error const& ex)
        {
            AppendLog(std::wstring(L"Help viewer unavailable, opening in the browser: ") +
                      std::wstring(ex.message()));
            if (m_helpWindow)
            {
                m_helpWindow.Close();
                m_helpWindow = nullptr;
            }
            ShellExecuteW(nullptr, L"open", kHelpUrl, nullptr, nullptr, SW_SHOWNORMAL);
        }
    }
}
