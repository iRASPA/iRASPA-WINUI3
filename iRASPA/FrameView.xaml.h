#pragma once

#include "FrameView.g.h"
#include "DocumentController.h"
#include "InlineRename.h"

#include <memory>
#include <vector>

namespace winrt::iRASPA_WinUI::implementation
{
    // Cocoa FrameListViewController: the Frame tab of the left pane, the frames of
    // the selected movie. Multi-selectable and drag-reorderable; the rows and the
    // gestures on them live here, the model changes go to the DocumentController.
    struct FrameView : FrameViewT<FrameView>, FrameListPresenter
    {
        FrameView();

        // Not projected: handed over in C++ right after construction.
        void SetController(DocumentController* controller);

        // FrameListPresenter
        void RefreshRows() override;
        void ReloadRowSelection() override;
        std::shared_ptr<iRASPAObject> FrameAtRow(size_t row) const override;

        void OnListSelectionChanged(winrt::Microsoft::UI::Xaml::Controls::ItemsView const& sender,
                                    winrt::Microsoft::UI::Xaml::Controls::ItemsViewSelectionChangedEventArgs const& e);
        void OnListKeyDown(winrt::Windows::Foundation::IInspectable const& sender,
                           winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e);
        void OnListDragOver(winrt::Windows::Foundation::IInspectable const& sender,
                            winrt::Microsoft::UI::Xaml::DragEventArgs const& e);
        void OnListDrop(winrt::Windows::Foundation::IInspectable const& sender,
                        winrt::Microsoft::UI::Xaml::DragEventArgs const& e);
        void OnAddClick(winrt::Windows::Foundation::IInspectable const& sender,
                        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnRemoveClick(winrt::Windows::Foundation::IInspectable const& sender,
                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

    private:
        // These need handledEventsToo, which markup cannot express: the CanDrag row
        // content captures the pointer and the ItemContainer marks the press and the
        // double-tap handled before either bubbles to the list.
        void OnListPointerPressed(winrt::Windows::Foundation::IInspectable const& sender,
                                  winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnListPointerReleased(winrt::Windows::Foundation::IInspectable const& sender,
                                   winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnListDoubleTapped(winrt::Windows::Foundation::IInspectable const& sender,
                                winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& e);
        // Cocoa tableViewSelectionDidChange: the rows changed, the model follows.
        void CommitSelectionFromRows();

        DocumentController* m_controller{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_rows{ nullptr };
        // The frames the rows currently stand for, in row order.
        std::vector<std::shared_ptr<iRASPAObject>> m_frames;
        bool m_suppressSelectionEvents = false;
        InlineRenameController m_rename;

        // Cocoa's drag session state: which frames are being dragged, the Shift
        // anchor, and the row a click on a multi-selection collapses onto if it
        // never becomes a drag.
        std::vector<std::shared_ptr<iRASPAObject>> m_dragFrames;
        bool m_dragActive = false;
        int32_t m_selectionAnchor = -1;
        int32_t m_pendingCollapseRow = -1;
    };
}

namespace winrt::iRASPA_WinUI::factory_implementation
{
    struct FrameView : FrameViewT<FrameView, implementation::FrameView>
    {
    };
}
