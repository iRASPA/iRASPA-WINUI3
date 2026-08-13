#pragma once

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Windows.Foundation.h>

#include <functional>
#include <optional>
#include <string>

// Cocoa TableListNameTextField: every row of the project, scene and frame lists
// carries a name field that only becomes an editor once the row is
// double-clicked. Enter or clicking away commits, Escape cancels; `finish`
// receives the new text, or nothing when the edit was cancelled.
//
// Cocoa has a single field editor, so at most one row of one list is editing at
// any time; each list owns one of these and calls End() on the others' behalf by
// virtue of the row template only ever revealing one editor.
struct InlineRenameController
{
    // The row is searched for the TextBox tagged "editor" that every row
    // template carries collapsed. Returns false when the row has none.
    bool Begin(winrt::Microsoft::UI::Xaml::FrameworkElement const& row,
               std::wstring const& initialText,
               winrt::Microsoft::UI::Dispatching::DispatcherQueue const& queue,
               std::function<void(std::optional<std::wstring>)> finish);
    void End(bool commit);
    bool Active() const noexcept { return m_editor != nullptr; }

private:
    winrt::Microsoft::UI::Xaml::Controls::TextBox m_editor{ nullptr };
    std::function<void(std::optional<std::wstring>)> m_finish;
    winrt::Microsoft::UI::Dispatching::DispatcherQueue m_queue{ nullptr };
    winrt::Microsoft::UI::Xaml::UIElement::KeyDown_revoker m_keyDownRevoker;
    winrt::Microsoft::UI::Xaml::UIElement::LosingFocus_revoker m_losingFocusRevoker;
    winrt::Microsoft::UI::Xaml::UIElement::LostFocus_revoker m_lostFocusRevoker;
    // AddHandler/RemoveHandler pair on the row container: the release the
    // editor waits for is one the container has already marked handled.
    winrt::Microsoft::UI::Xaml::UIElement m_releaseTarget{ nullptr };
    winrt::Windows::Foundation::IInspectable m_releaseHandler{ nullptr };
};
