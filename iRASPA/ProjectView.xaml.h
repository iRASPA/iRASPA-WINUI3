#pragma once

#include "ProjectView.g.h"
#include "DocumentController.h"
#include "InlineRename.h"
#include "projecttreenode.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace winrt::iRASPA_WinUI::implementation
{
    // Cocoa ProjectViewController: the LOCAL PROJECTS / GALLERY / DATABASES
    // source list. The rows and every gesture on them live here; the document
    // operations they trigger (insert, remove, rename, selection) go to the
    // DocumentController, because they record on the document's undo stack.
    struct ProjectView : ProjectViewT<ProjectView>, ProjectListPresenter
    {
        ProjectView();

        // Not projected: handed over in C++ right after construction.
        void SetController(DocumentController* controller);

        // ProjectListPresenter — what the document layer calls after it has
        // changed the project tree.
        void RefreshRows() override;
        void ReloadRowSelection() override;
        void RevealNode(std::shared_ptr<ProjectTreeNode> const& node) override;
        void ForgetNode(std::shared_ptr<ProjectTreeNode> const& node) override;
        void ExpandSectionRoots() override;
        void SuppressSelectionEvents(bool suppress) override { m_suppressSelectionEvents = suppress; }

        // Driven by the window when a document is opened or closed.
        void Bind();
        void Recreate();
        void ScheduleRefresh();
        void RevealSelected();
        void ClearExpandedNodes() { m_expandedNodes.clear(); }
        winrt::Microsoft::UI::Xaml::Controls::ItemsView ListControl() { return List(); }

        // Prefixed with List because UIElement already has protected virtuals
        // called OnDragOver / OnDrop / OnDoubleTapped and so on, which a
        // same-named handler would hide.
        void OnListSelectionChanged(winrt::Microsoft::UI::Xaml::Controls::ItemsView const& sender,
                                    winrt::Microsoft::UI::Xaml::Controls::ItemsViewSelectionChangedEventArgs const& e);
        void OnListDragOver(winrt::Windows::Foundation::IInspectable const& sender,
                            winrt::Microsoft::UI::Xaml::DragEventArgs const& e);
        void OnListDrop(winrt::Windows::Foundation::IInspectable const& sender,
                        winrt::Microsoft::UI::Xaml::DragEventArgs const& e);
        void OnDeleteAccelerator(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
                                 winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& e);
        void OnAddStructureClick(winrt::Windows::Foundation::IInspectable const& sender,
                                 winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnAddGroupClick(winrt::Windows::Foundation::IInspectable const& sender,
                             winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnRemoveClick(winrt::Windows::Foundation::IInspectable const& sender,
                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnSearchTextChanged(winrt::Windows::Foundation::IInspectable const& sender,
                                 winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& e);

    private:
        // These three need handledEventsToo, which markup cannot express: the
        // ItemContainer eats the pointer and double-tap events before they bubble.
        void OnListPointerPressed(winrt::Windows::Foundation::IInspectable const& sender,
                                  winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnListRightTapped(winrt::Windows::Foundation::IInspectable const& sender,
                               winrt::Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs const& e);
        void OnListDoubleTapped(winrt::Windows::Foundation::IInspectable const& sender,
                                winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& e);
        void SyncSelectionFromView();
        std::shared_ptr<ProjectTreeController> Tree() const;

        DocumentController* m_controller{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_flatItems{ nullptr };
        // Nodes whose children are shown; weak so deleted nodes fall out.
        std::set<std::weak_ptr<ProjectTreeNode>, std::owner_less<std::weak_ptr<ProjectTreeNode>>> m_expandedNodes;
        // Non-empty: the list shows a flat filtered set of matching nodes instead
        // of the tree (Cocoa filter field behaviour).
        std::wstring m_filter;
        std::vector<std::shared_ptr<ProjectTreeNode>> m_dragNodes;
        bool m_suppressSelectionEvents = false;
        bool m_refreshPending = false;
        InlineRenameController m_rename;
    };
}

namespace winrt::iRASPA_WinUI::factory_implementation
{
    struct ProjectView : ProjectViewT<ProjectView, implementation::ProjectView>
    {
    };
}
