#include "pch.h"
#include "AboutView.xaml.h"
#if __has_include("AboutView.g.cpp")
#include "AboutView.g.cpp"
#endif

#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Foundation.h>

#include <filesystem>
#include <shellapi.h>

#pragma comment(lib, "version.lib")
#pragma comment(lib, "shell32.lib")

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Media::Imaging;
using namespace winrt::Windows::Foundation;

namespace winrt::iRASPA_WinUI::implementation
{
    namespace
    {
        std::filesystem::path AppDirectory()
        {
            wchar_t buffer[MAX_PATH]{};
            const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
            if (length == 0 || length == MAX_PATH)
                return {};
            return std::filesystem::path(buffer).parent_path();
        }

        // Cocoa reads CFBundleShortVersionString/CFBundleVersion; the executable's
        // version resource is the equivalent here.
        std::wstring VersionText()
        {
            std::wstring version = L"1.0.0";
            std::wstring build = L"1";

            wchar_t module[MAX_PATH]{};
            if (GetModuleFileNameW(nullptr, module, MAX_PATH) != 0)
            {
                DWORD handle = 0;
                const DWORD size = GetFileVersionInfoSizeW(module, &handle);
                if (size > 0)
                {
                    std::vector<std::byte> data(size);
                    if (GetFileVersionInfoW(module, handle, size, data.data()))
                    {
                        VS_FIXEDFILEINFO* info = nullptr;
                        UINT infoSize = 0;
                        if (VerQueryValueW(data.data(), L"\\", reinterpret_cast<LPVOID*>(&info), &infoSize) &&
                            info && infoSize >= sizeof(VS_FIXEDFILEINFO))
                        {
                            version = std::to_wstring(HIWORD(info->dwFileVersionMS)) + L"." +
                                      std::to_wstring(LOWORD(info->dwFileVersionMS)) + L"." +
                                      std::to_wstring(HIWORD(info->dwFileVersionLS));
                            build = std::to_wstring(LOWORD(info->dwFileVersionLS));
                        }
                    }
                }
            }
            return L"Version " + version + L" (build " + build + L")";
        }

        void OpenUrl(std::wstring const& url)
        {
            ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }

    AboutView::AboutView()
    {
        InitializeComponent();

        VersionLabel().Text(hstring(VersionText()));

        // The icon is copied next to the executable by the build; without it the
        // button shows the wordmark instead.
        const auto iconPath = AppDirectory() / L"Assets" / L"AppIcon.png";
        bool haveIcon = false;
        if (!iconPath.empty())
        {
            std::error_code ec;
            haveIcon = std::filesystem::exists(iconPath, ec) && !ec;
        }
        if (haveIcon)
        {
            AppIcon().Source(BitmapImage(Uri(hstring(L"file:///" + iconPath.wstring()))));
        }
        else
        {
            AppIcon().Visibility(Visibility::Collapsed);
            Wordmark().Visibility(Visibility::Visible);
        }
    }

    void AboutView::OnIconClick([[maybe_unused]] IInspectable const&,
                                [[maybe_unused]] RoutedEventArgs const&)
    {
        OpenUrl(L"https://www.uva.nl/en/profile/d/u/d.dubbeldam/d.dubbeldam.html");
    }

    // Cocoa opens Bundle.main's AcknowledgedLicenses.pdf; the build copies
    // iraspa/datafiles/acknowledgedlicenses.pdf next to the executable as
    // AcknowledgedLicenses.pdf (DeployAcknowledgedLicenses).
    void AboutView::OnAcknowledgementsClick([[maybe_unused]] IInspectable const&,
                                            [[maybe_unused]] RoutedEventArgs const&)
    {
        const auto path = AppDirectory() / L"AcknowledgedLicenses.pdf";
        std::error_code ec;
        if (path.empty() || !std::filesystem::exists(path, ec) || ec)
        {
            if (m_log)
                m_log(L"AcknowledgedLicenses.pdf not found next to the application");
            return;
        }
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}
