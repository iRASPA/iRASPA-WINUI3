#pragma once

#include "App.g.h"

namespace winrt::iRASPA_WinUI::implementation
{
    struct App : AppT<App>
    {
        App();

        // CppWinRT make<> always invokes InitializeComponent after construction.
        // Provide a no-op so LoadComponent(App.xaml) is never called (unpackaged bring-up).
        void InitializeComponent() {}

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

    private:
        winrt::Microsoft::UI::Xaml::Window window{ nullptr };
    };
}

namespace winrt::iRASPA_WinUI::factory_implementation
{
    struct App : AppT<App, implementation::App>
    {
    };
}
