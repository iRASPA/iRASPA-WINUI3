#pragma once

#include "DirectoryCollectionView.g.h"
#include "projecttreenode.h"

#include <functional>
#include <memory>
#include <vector>

namespace winrt::iRASPA_WinUI::implementation
{
    // Cocoa DirectoryCollectionViewController: virtualized grid of ProjectSlideCarrierView
    // items for the descendant leaf nodes of the selected group ProjectTreeNode, filtered
    // by matchesFilter() and the active predicate from PredicateEditorView.
    struct DirectoryCollectionView : DirectoryCollectionViewT<DirectoryCollectionView>
    {
        DirectoryCollectionView();

        void SetPredicateEvaluator(std::function<bool(std::shared_ptr<iRASPAProject> const&)> evaluator);
        void SetNodeActivatedCallback(std::function<void(std::shared_ptr<ProjectTreeNode> const&)> onActivate);
        void SetNodeDragStartedCallback(std::function<void(std::shared_ptr<ProjectTreeNode> const&)> onDragStart);

        void SetProxyProject(std::shared_ptr<ProjectTreeNode> const& proxyProject);
        std::shared_ptr<ProjectTreeNode> ProxyProject() const { return m_proxyProject; }
        void ReloadData();

        void OnGridSelectionChanged(winrt::Microsoft::UI::Xaml::Controls::ItemsView const& sender,
                                    winrt::Microsoft::UI::Xaml::Controls::ItemsViewSelectionChangedEventArgs const& e);
        void OnTileDragStarting(winrt::Microsoft::UI::Xaml::UIElement const& sender,
                                winrt::Microsoft::UI::Xaml::DragStartingEventArgs const& e);
        void OnTilePointerEntered(winrt::Windows::Foundation::IInspectable const& sender,
                                  winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnTilePointerExited(winrt::Windows::Foundation::IInspectable const& sender,
                                 winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);

    private:
        void OnGridPointerPressed(winrt::Windows::Foundation::IInspectable const& sender,
                                  winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnGridDoubleTapped(winrt::Windows::Foundation::IInspectable const& sender,
                                winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& e);

        std::shared_ptr<ProjectTreeNode> m_proxyProject;
        std::vector<std::shared_ptr<ProjectTreeNode>> m_filteredNodes;
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_items{ nullptr };
        winrt::Windows::Foundation::IInspectable m_selectedItem{ nullptr };
        std::function<bool(std::shared_ptr<iRASPAProject> const&)> m_predicateEvaluator;
        std::function<void(std::shared_ptr<ProjectTreeNode> const&)> m_onActivate;
        std::function<void(std::shared_ptr<ProjectTreeNode> const&)> m_onDragStart;
    };
}

namespace winrt::iRASPA_WinUI::factory_implementation
{
    struct DirectoryCollectionView : DirectoryCollectionViewT<DirectoryCollectionView, implementation::DirectoryCollectionView>
    {
    };
}
