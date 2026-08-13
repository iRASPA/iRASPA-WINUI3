#include "pch.h"
#include "InlineRename.h"

#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.System.h>

#include <utility>

using namespace winrt;
using namespace winrt::Microsoft::UI::Dispatching;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Input;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace winrt::Windows::Foundation;

namespace
{
    // Cocoa has one field editor for the whole window, so opening an editor
    // anywhere commits whatever was being edited before it.
    InlineRenameController* g_active = nullptr;

    // The row element the double-click hit-test lands on is somewhere inside
    // the row, not necessarily the container the list gives the focus to, so
    // the container is looked up from the editor upwards.
    ItemContainer FindRowContainer(DependencyObject const& editor)
    {
        for (auto parent = VisualTreeHelper::GetParent(editor); parent;
             parent = VisualTreeHelper::GetParent(parent))
        {
            if (auto container = parent.try_as<ItemContainer>())
                return container;
        }
        return nullptr;
    }

    // The name editor sits collapsed inside every row template, so it is
    // already in the visual tree by the time a row is double-clicked.
    TextBox FindRenameEditor(DependencyObject const& root)
    {
        if (!root)
            return nullptr;
        if (auto box = root.try_as<TextBox>();
            box && unbox_value_or<hstring>(box.Tag(), L"") == L"editor")
            return box;
        const int count = VisualTreeHelper::GetChildrenCount(root);
        for (int i = 0; i < count; ++i)
        {
            if (auto found = FindRenameEditor(VisualTreeHelper::GetChild(root, i)))
                return found;
        }
        return nullptr;
    }
}

// Cocoa TableListNameTextField.beginRenaming: turn the row's name field into an
// editor, give it the focus and select its text.
bool InlineRenameController::Begin(FrameworkElement const& row,
                                   std::wstring const& initialText,
                                   DispatcherQueue const& queue,
                                   std::function<void(std::optional<std::wstring>)> finish)
{
    if (!row || !finish || !queue)
        return false;

    // Whatever was being renamed elsewhere commits first. This happens before
    // the editor is looked up, because committing can rebuild the list and
    // recycle its rows.
    if (g_active)
        g_active->End(true);

    TextBox editor{ nullptr };
    try
    {
        editor = FindRenameEditor(row);
    }
    catch (...)
    {
    }
    if (!editor)
        return false;

    m_editor = editor;
    m_finish = std::move(finish);
    m_queue = queue;
    g_active = this;
    editor.Text(hstring(initialText));

    m_keyDownRevoker = editor.KeyDown(winrt::auto_revoke,
        [this](IInspectable const&, KeyRoutedEventArgs const& e)
        {
            using winrt::Windows::System::VirtualKey;
            if (e.Key() == VirtualKey::Enter)
            {
                e.Handled(true);
                End(true);
            }
            else if (e.Key() == VirtualKey::Escape)
            {
                e.Handled(true);
                End(false);
            }
            else if (e.Key() == VirtualKey::Space)
            {
                // Space escapes a TextBox that sits inside an ItemContainer,
                // and the list takes it as a row toggle and pulls the focus off
                // the editor, ending the rename halfway through a name like
                // "New Project". Stopping it here keeps the focus in the
                // editor; the space itself still reaches the text.
                e.Handled(true);
            }
        });
    // Loading the row's project rebuilds the inspector, which moves the focus
    // programmatically. That is not the user leaving the field, and this one can
    // simply be refused.
    m_losingFocusRevoker = editor.LosingFocus(winrt::auto_revoke,
        [](IInspectable const&, LosingFocusEventArgs const& e)
        {
            if (e.InputDevice() == FocusInputDeviceKind::None)
                e.TryCancel();
        });
    // Clicking another row (or anything else) ends the edit the way the Cocoa
    // field editor does: the typed name is kept. The commit waits a turn,
    // because the list takes the focus for the row container before handing it
    // on to the editor below, and that is not the user leaving the field.
    m_lostFocusRevoker = editor.LostFocus(winrt::auto_revoke,
        [this](IInspectable const&, RoutedEventArgs const&)
        {
            auto pending = m_editor;
            m_queue.TryEnqueue([this, pending]()
            {
                if (!pending || m_editor != pending)
                    return;
                if (pending.FocusState() != FocusState::Unfocused)
                    return;
                End(true);
            });
        });

    // The list claims the focus for the row's ItemContainer as the last step of
    // the click that opened the editor, and that claim can be neither refused
    // nor out-run: it lands after the release has bubbled all the way up. So the
    // editor waits for the release and then takes the focus a turn later, once
    // the list has had its say. handledEventsToo, because the container marks
    // the release handled.
    if (auto container = FindRowContainer(editor))
    {
        m_releaseTarget = container;
        m_releaseHandler = box_value(PointerEventHandler(
            [this](IInspectable const&, PointerRoutedEventArgs const&)
            {
                auto weakBox = make_weak(m_editor);
                m_queue.TryEnqueue([weakBox]()
                {
                    if (auto box = weakBox.get())
                    {
                        box.Focus(FocusState::Programmatic);
                        box.SelectAll();
                    }
                });
            }));
        container.AddHandler(UIElement::PointerReleasedEvent(), m_releaseHandler, true);
    }

    // The editor only just became visible, so let the layout pass run and then
    // take the focus.
    auto weak = make_weak(editor);
    m_queue.TryEnqueue([weak]()
    {
        try
        {
            if (auto box = weak.get())
            {
                box.Focus(FocusState::Programmatic);
                box.SelectAll();
            }
        }
        catch (...)
        {
        }
    });
    return true;
}

void InlineRenameController::End(bool commit)
{
    if (!m_editor)
        return;

    auto editor = std::exchange(m_editor, nullptr);
    auto finish = std::exchange(m_finish, nullptr);
    auto row = std::exchange(m_releaseTarget, nullptr);
    m_keyDownRevoker.revoke();
    m_losingFocusRevoker.revoke();
    m_lostFocusRevoker.revoke();
    if (row && m_releaseHandler)
        row.RemoveHandler(UIElement::PointerReleasedEvent(), m_releaseHandler);
    m_releaseHandler = nullptr;
    if (g_active == this)
        g_active = nullptr;

    std::optional<std::wstring> text;
    if (commit)
        text = std::wstring(editor.Text());
    if (finish)
        finish(text);
}
