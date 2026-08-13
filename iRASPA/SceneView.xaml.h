#pragma once

#include "SceneView.g.h"
#include "DocumentController.h"
#include "InlineRename.h"

#include <memory>

namespace winrt::iRASPA_WinUI::implementation
{
    // Cocoa MovieListViewController: the Scene tab of the left pane, a flat
    // source list of scene group rows each followed by its movie rows. The rows
    // and the gestures on them live here; selecting, renaming, adding and
    // removing go to the DocumentController.
    struct SceneView : SceneViewT<SceneView>, SceneListPresenter
    {
        SceneView();

        // Not projected: handed over in C++ right after construction.
        void SetController(DocumentController* controller);

        // SceneListPresenter
        void RefreshRows() override;
        void ReloadRowSelection() override;

        void OnListSelectionChanged(winrt::Microsoft::UI::Xaml::Controls::ItemsView const& sender,
                                    winrt::Microsoft::UI::Xaml::Controls::ItemsViewSelectionChangedEventArgs const& e);
        void OnAddCrystalClick(winrt::Windows::Foundation::IInspectable const& sender,
                               winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnRemoveClick(winrt::Windows::Foundation::IInspectable const& sender,
                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        // Cocoa toggleMovieVisibility, from the checkbox on a movie row.
        void OnMovieVisibilityClick(winrt::Windows::Foundation::IInspectable const& sender,
                                    winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnMovieVisibilityBoxDataContextChanged(
            winrt::Microsoft::UI::Xaml::FrameworkElement const& sender,
            winrt::Microsoft::UI::Xaml::DataContextChangedEventArgs const& e);

    private:
        // Both need handledEventsToo, which markup cannot express: the
        // ItemContainer eats the press and the double-tap before they bubble.
        void OnListPointerPressed(winrt::Windows::Foundation::IInspectable const& sender,
                                  winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnListDoubleTapped(winrt::Windows::Foundation::IInspectable const& sender,
                                winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& e);
        // The list's own selection, pushed to the model.
        void CommitSelectionFromRows();
        // Cocoa's list refuses an empty selection, but the refusal has to wait
        // for the click to finish; see the call.
        void RestoreSelectionIfEmpty();

        DocumentController* m_controller{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_rows{ nullptr };
        bool m_suppressSelectionEvents = false;
        InlineRenameController m_rename;
    };
}

namespace winrt::iRASPA_WinUI::factory_implementation
{
    struct SceneView : SceneViewT<SceneView, implementation::SceneView>
    {
    };
}
