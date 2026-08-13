#pragma once

#include "ElementsDetailView.g.h"

#include "DocumentController.h"

namespace winrt::iRASPA_WinUI::implementation
{
    // The Elements tab (Cocoa StructureElementDetailViewController): the atom
    // types of one force-field set, one card per type, with the set and the color
    // set they are read from picked in the bottom toolbar.
    //
    // The cards write straight through to the ForceFieldType and the SKColorSet,
    // as the Cocoa cell's target/actions do, so there is nothing to commit here;
    // an edit only has to be pushed at the structures afterwards, which the
    // controller does. None of it is undoable, as in Cocoa.
    struct ElementsDetailView : ElementsDetailViewT<ElementsDetailView>
    {
        ElementsDetailView();

        void SetController(DocumentController* controller) { m_controller = controller; }
        // Refill the two combos and the cards from the document.
        void Reload();
        // Drop the cards. They hold pointers into the document's force-field and
        // color sets, which do not outlive the document being replaced.
        void Clear();

        void OnForceFieldSetChanged(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        void OnColorSetChanged(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        // A name typed into a combo, which selects that set or forks the current
        // one under it (Cocoa addForceFieldSet: / addColorSet:).
        void OnForceFieldSetSubmitted(
            winrt::Microsoft::UI::Xaml::Controls::ComboBox const& sender,
            winrt::Microsoft::UI::Xaml::Controls::ComboBoxTextSubmittedEventArgs const& e);
        void OnColorSetSubmitted(
            winrt::Microsoft::UI::Xaml::Controls::ComboBox const& sender,
            winrt::Microsoft::UI::Xaml::Controls::ComboBoxTextSubmittedEventArgs const& e);

        // The type operations of Cocoa's [+] / [-] and its row context menu.
        void OnSelectionChanged(
            winrt::Microsoft::UI::Xaml::Controls::ItemsView const& sender,
            winrt::Microsoft::UI::Xaml::Controls::ItemsViewSelectionChangedEventArgs const& e);
        void OnAddTypeClick(winrt::Windows::Foundation::IInspectable const& sender,
                            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnRemoveTypeClick(winrt::Windows::Foundation::IInspectable const& sender,
                               winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnListRightTapped(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs const& e);
        void OnInsertTypeMenu(winrt::Windows::Foundation::IInspectable const& sender,
                              winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnDeleteTypeMenu(winrt::Windows::Foundation::IInspectable const& sender,
                              winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        // TwoWay on IsChecked lets ItemsView recycle write null (= '-'). The
        // binding is OneWay; Click on the box writes the toggled value.
        void OnVisibilityClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnVisibilityBoxDataContextChanged(
            winrt::Microsoft::UI::Xaml::FrameworkElement const& sender,
            winrt::Microsoft::UI::Xaml::DataContextChangedEventArgs const& e);

        // A card renames its type through here: the name is the key of the color
        // sets as well, so the document has to do it.
        bool RenameType(int row, std::wstring const& name);

    private:
        // Needs handledEventsToo, which markup cannot express: the color well
        // sits inside the ItemContainer, which eats PointerPressed before it
        // bubbles.
        void OnListPointerPressed(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        winrt::fire_and_forget PickColorAsync(winrt::Windows::Foundation::IInspectable item);

        void FillSetCombos();
        void ReloadCards();
        void CoerceVisibilityCheckBoxes();

        int SelectedRow();
        bool CanRemoveRow(int row);
        void UpdateTypeButtons();
        void InsertTypeAt(int row);
        void RemoveTypeAt(int row);
        void SelectRow(int row);

        DocumentController* m_controller{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<
            winrt::Windows::Foundation::IInspectable> m_cards{ nullptr };
        // The combos are refilled from the document, which raises
        // SelectionChanged for a change nobody made.
        bool m_suppress = false;
        // Which set the cards come from, kept across tab switches as the Cocoa
        // controller keeps its popup selection.
        int m_forceFieldSetIndex = 0;
        int m_colorSetIndex = 0;
        // Cocoa's clickedRow: the row the context menu was opened on, which is
        // not necessarily the selected one.
        int m_menuRow = -1;
        winrt::Microsoft::UI::Xaml::Controls::MenuFlyout m_rowMenu{ nullptr };
    };
}

namespace winrt::iRASPA_WinUI::factory_implementation
{
    struct ElementsDetailView : ElementsDetailViewT<ElementsDetailView, implementation::ElementsDetailView>
    {
    };
}
