#include "pch.h"
#include "MainWindow.xaml.h"

#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <string>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Windows::Foundation;

// Undo/redo, ported from the NSUndoManager use in iRASPA-COCOA: every model
// mutation registers the closure that reverses it, and the reversing closure
// registers its own inverse, which becomes the redo entry.

namespace winrt::iRASPA_WinUI::implementation
{
    // Cocoa windowWillReturnUndoManager: the project navigator undoes against
    // the document, every other pane against the selected project.
    RKUndoStack* MainWindow::ActiveUndoStack()
    {
        try
        {
            // The project pane, not the grid it shares with the scene and frame
            // lists: those two edit the project, so they undo against it. The
            // pane is one control, so anything inside its subtree counts.
            if (auto pane = m_projectView.try_as<DependencyObject>())
            {
                auto focused = Input::FocusManager::GetFocusedElement(Content().XamlRoot())
                                   .try_as<DependencyObject>();
                for (auto element = focused; element; )
                {
                    if (element == pane)
                        return &DocumentUndoStack();
                    element = Media::VisualTreeHelper::GetParent(element);
                }
            }
        }
        catch (...)
        {
        }
        return ObjectUndoStack();
    }

    // The focus is only a preference: a keystroke that the focused pane cannot
    // serve falls through to the other stack instead of doing nothing.
    RKUndoStack* MainWindow::UndoStackForCommand(bool redo)
    {
        auto document = &DocumentUndoStack();
        auto preferred = ActiveUndoStack();
        auto other = (preferred == document) ? ProjectUndoStack() : document;
        if (!preferred)
            return other;
        if (redo ? preferred->canRedo() : preferred->canUndo())
            return preferred;
        if (other && (redo ? other->canRedo() : other->canUndo()))
            return other;
        return preferred;
    }

    void MainWindow::PerformUndo()
    {
        auto stack = UndoStackForCommand(false);
        if (!stack || !stack->canUndo())
        {
            AppendLog(L"Nothing to undo");
            return;
        }
        const std::wstring name = stack->undoActionName();
        stack->undo();
        AppendLog(name.empty() ? std::wstring(L"Undo") : (L"Undo " + name));
        UpdateEditMenuLabels();
    }

    void MainWindow::PerformRedo()
    {
        auto stack = UndoStackForCommand(true);
        if (!stack || !stack->canRedo())
        {
            AppendLog(L"Nothing to redo");
            return;
        }
        const std::wstring name = stack->redoActionName();
        stack->redo();
        AppendLog(name.empty() ? std::wstring(L"Redo") : (L"Redo " + name));
        UpdateEditMenuLabels();
    }

    // Cocoa's Edit menu shows the pending action ("Undo Add Projects").
    void MainWindow::UpdateEditMenuLabels()
    {
        if (m_undoMenuItem)
        {
            auto stack = UndoStackForCommand(false);
            const bool can = stack && stack->canUndo();
            const std::wstring name = can ? stack->undoActionName() : std::wstring();
            m_undoMenuItem.Text(name.empty() ? hstring(L"Undo") : hstring(L"Undo " + name));
            m_undoMenuItem.IsEnabled(can);
        }
        if (m_redoMenuItem)
        {
            auto stack = UndoStackForCommand(true);
            const bool can = stack && stack->canRedo();
            const std::wstring name = can ? stack->redoActionName() : std::wstring();
            m_redoMenuItem.Text(name.empty() ? hstring(L"Redo") : hstring(L"Redo " + name));
            m_redoMenuItem.IsEnabled(can);
        }
    }
}
