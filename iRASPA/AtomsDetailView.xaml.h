#pragma once

#include "AtomsDetailView.g.h"
#include "DocumentController.h"

#include <memory>
#include <string>
#include <vector>

class SKAtomTreeNode;

namespace winrt::iRASPA_WinUI::implementation
{
    // The Atoms tab of the inspector (Cocoa StructureAtomDetailViewController).
    // The form is AtomsDetailView.xaml; this owns the flattened rows and the
    // gestures over them (expand/collapse, Extended selection, drag & drop, the
    // row context menu) and routes every mutation through the document
    // controller, which owns the undo.
    struct AtomsDetailView : AtomsDetailViewT<AtomsDetailView>, AtomsPanePresenter
    {
        AtomsDetailView();

        void SetController(DocumentController* controller) { m_controller = controller; }
        // Rebuild the rows from the atom tree of the selected frame.
        void Reload();
        // Drop the rows. They hold nodes of the project's atom tree, which does
        // not outlive the project being replaced.
        void Clear();

        // AtomsPanePresenter
        void RefreshField(std::shared_ptr<SKAtomTreeNode> const& node, AtomField field) override;
        void ReloadRowSelection() override;
        void RefreshNetCharge() override;

        // A row reports what the user typed here rather than writing it itself,
        // so the mutation, its undo registration and the refresh it needs all
        // happen in one place (Cocoa's setAtomName / setAtomPositionX / ...).
        void CommitField(std::shared_ptr<SKAtomTreeNode> const& node, AtomField field,
                         AtomFieldValue const& value);
        // A row's visibility control writes straight to the atoms, as the Cocoa cell's action does;
        // only the renderer has to be told.
        void AtomVisibilityChanged();
        // Switching a group switches everything under it, so the rows of its contents are told to
        // re-read their atom.
        void RefreshVisibilityUnder(std::shared_ptr<SKAtomTreeNode> const& node);
        // And any switch can leave the groups above it no longer all-on or all-off, which each of
        // them draws on its own row.
        void RefreshGroupsAbove(std::shared_ptr<SKAtomTreeNode> const& node);
        uint32_t RowOf(std::shared_ptr<SKAtomTreeNode> const& node);

        void OnAddClick(winrt::Windows::Foundation::IInspectable const& sender,
                        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnAddAtomMenuClick(winrt::Windows::Foundation::IInspectable const& sender,
                                winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnAddAtomGroupMenuClick(winrt::Windows::Foundation::IInspectable const& sender,
                                     winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnRemoveClick(winrt::Windows::Foundation::IInspectable const& sender,
                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        // A non-empty filter switches to a flat list of the leaf atoms whose name
        // or element matches (Cocoa's filterContent / updateFilteredNodes).
        void OnSearchTextChanged(winrt::Windows::Foundation::IInspectable const& sender,
                                 winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& e);
        // Mirror the row selection into the atom tree, which owns the selection
        // (Cocoa-style), and refresh the selection glow in the 3D view.
        void OnSelectionChanged(
            winrt::Microsoft::UI::Xaml::Controls::ItemsView const& sender,
            winrt::Microsoft::UI::Xaml::Controls::ItemsViewSelectionChangedEventArgs const& e);
        // The framework selects the whole text when a TextBox takes focus, so the
        // first keystroke would wipe the value; collapse that to a caret instead.
        void OnFieldGotFocus(winrt::Windows::Foundation::IInspectable const& sender,
                             winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        // The visibility box reports what the user did rather than writing through
        // a two-way binding, which also fires while rows are being recycled, when
        // the box belongs to another atom.
        void OnVisibilityClick(winrt::Windows::Foundation::IInspectable const& sender,
                               winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        // A recycled row is handed a new atom; the box is told what that atom is,
        // since the cleared binding leaves the nullable IsChecked on null, which
        // draws as the indeterminate '-' glyph.
        void OnVisibilityBoxDataContextChanged(
            winrt::Microsoft::UI::Xaml::FrameworkElement const& sender,
            winrt::Microsoft::UI::Xaml::DataContextChangedEventArgs const& e);
        // The two halves of the segmented control on a chain, a segment or a residue row, which
        // report what the user did and are re-read on recycling for the same reasons as the box.
        void OnShowsAtomsClick(winrt::Windows::Foundation::IInspectable const& sender,
                               winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnShowsRibbonClick(winrt::Windows::Foundation::IInspectable const& sender,
                                winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnSegmentDataContextChanged(
            winrt::Microsoft::UI::Xaml::FrameworkElement const& sender,
            winrt::Microsoft::UI::Xaml::DataContextChangedEventArgs const& e);
        // Fixed X/Y/Z toggles: same recycle null → indeterminate glyph as the box.
        void OnBinaryCheckDataContextChanged(
            winrt::Microsoft::UI::Xaml::FrameworkElement const& sender,
            winrt::Microsoft::UI::Xaml::DataContextChangedEventArgs const& e);
        void OnListDragOver(winrt::Windows::Foundation::IInspectable const& sender,
                            winrt::Microsoft::UI::Xaml::DragEventArgs const& e);
        void OnListDrop(winrt::Windows::Foundation::IInspectable const& sender,
                        winrt::Microsoft::UI::Xaml::DragEventArgs const& e);

    private:
        // Both need handledEventsToo, which markup cannot express: the TextBoxes
        // and the ItemContainer eat the pointer before it bubbles to the list.
        void OnListPointerPressed(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnListRightTapped(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs const& e);
        // The markup carries the labels and the command tags; the commands are
        // dispatched and gated here.
        void RunRowCommand(std::wstring const& command);
        void ScrollTo(int mode); // 0=top, 1=bottom, 2=first selected, 3=last selected

        // Rows the current filter yields, and the row edits go through.
        void Populate();
        void AddNode(bool group);
        // The rows the user is acting on: everything selected, or the last row
        // clicked when the selection is empty (clicking into a field leaves the
        // list selection untouched).
        std::vector<std::shared_ptr<SKAtomTreeNode>> ActedOnNodes();
        std::shared_ptr<SKAtomTreeNode> NodeAt(uint32_t index);
        // The frame the rows were built from, so an edit still lands on it after
        // the selection has moved on.
        std::weak_ptr<iRASPAObject> m_frame;

        DocumentController* m_controller{ nullptr };
        // The row context menu from the markup; its Click handlers are wired in
        // code, so the markup only has to carry the labels and the command tags.
        winrt::Microsoft::UI::Xaml::Controls::MenuFlyout m_rowMenu{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<
            winrt::Windows::Foundation::IInspectable> m_items{ nullptr };
        // The last row the pointer went down on, which the [+] and [-] buttons
        // fall back to.
        winrt::Windows::Foundation::IInspectable m_activeItem{ nullptr };
        // Inserting or removing rows shifts the index-based ItemsView selection
        // and raises SelectionChanged, which would write the shifted selection
        // back into the model.
        bool m_suppressSelectionEvents = false;
        // The rows a drag started on: the whole selection when the pressed row is
        // part of it, otherwise just that row (Cocoa outline view).
        std::vector<std::shared_ptr<SKAtomTreeNode>> m_dragNodes;
        // Where a Shift+click range starts.
        int32_t m_selectionAnchor = -1;
    };
}

namespace winrt::iRASPA_WinUI::factory_implementation
{
    struct AtomsDetailView : AtomsDetailViewT<AtomsDetailView, implementation::AtomsDetailView>
    {
    };
}
