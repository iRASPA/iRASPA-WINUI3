#pragma once

#include "AboutView.g.h"

#include <functional>
#include <string>

namespace winrt::iRASPA_WinUI::implementation
{
    // The content of the About panel (Cocoa LocalAboutViewController). The panel
    // itself is in AboutView.xaml, including the credits and their links; what
    // lives here is what markup cannot state: the version out of the executable's
    // version resource, the application icon deployed next to it, and the two
    // things the buttons open.
    struct AboutView : AboutViewT<AboutView>
    {
        AboutView();

        // Not projected: handed over in C++ right after construction, so the
        // panel can report a missing AcknowledgedLicenses.pdf in the log.
        void SetLog(std::function<void(std::wstring const&)> log) { m_log = std::move(log); }

        void OnIconClick(winrt::Windows::Foundation::IInspectable const& sender,
                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnAcknowledgementsClick(winrt::Windows::Foundation::IInspectable const& sender,
                                     winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

    private:
        std::function<void(std::wstring const&)> m_log;
    };
}

namespace winrt::iRASPA_WinUI::factory_implementation
{
    struct AboutView : AboutViewT<AboutView, implementation::AboutView>
    {
    };
}
