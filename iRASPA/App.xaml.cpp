#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

// The application's own opt-in. The live view never traces on the software adapter, being far too
// slow for a frame, but the renderer asks the runtime what it can do in either process, and the two
// should not be answering from different runtimes.
#include "directxagilitysdk.h"

#if __has_include("App.g.cpp")
#include "App.g.cpp"
#endif

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;

namespace winrt::iRASPA_WinUI::implementation
{
    App::App()
    {
        // UI/resources are not loaded from App.xaml (see InitializeComponent no-op).
    }

    void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& e)
    {
        // Merge WinUI control templates so templated controls (TreeView, NumberBox,
        // …) work. Must happen in OnLaunched — creating XamlControlsResources in the
        // App constructor crashes because the XAML host is not ready yet. The
        // generated AppT base supplies the IXamlMetadataProvider this requires.
        try
        {
            Resources().MergedDictionaries().Append(
                winrt::Microsoft::UI::Xaml::Controls::XamlControlsResources());
        }
        catch (hresult_error const&)
        {
            // Without control resources, templated controls stay unavailable but
            // the basic shell still runs.
        }

        window = make<MainWindow>();
        window.Activate();
    }
}

int __stdcall wWinMain([[maybe_unused]] HINSTANCE, [[maybe_unused]] HINSTANCE, [[maybe_unused]] PWSTR, [[maybe_unused]] int)
{
    // XamlCheckProcessRequirements is required for WinUI 3 desktop apps.
    {
        void (WINAPI *pfnXamlCheckProcessRequirements)();
        if (auto module = ::LoadLibraryW(L"Microsoft.ui.xaml.dll"))
        {
            pfnXamlCheckProcessRequirements = reinterpret_cast<decltype(pfnXamlCheckProcessRequirements)>(
                GetProcAddress(module, "XamlCheckProcessRequirements"));
            if (pfnXamlCheckProcessRequirements)
            {
                (*pfnXamlCheckProcessRequirements)();
            }
            ::FreeLibrary(module);
        }
    }

    winrt::init_apartment(winrt::apartment_type::single_threaded);

    ::winrt::Microsoft::UI::Xaml::Application::Start(
        [](auto&&)
        {
            ::winrt::make<::winrt::iRASPA_WinUI::implementation::App>();
        });

    return 0;
}
