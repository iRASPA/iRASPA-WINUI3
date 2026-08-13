#pragma once

#include "BondsDetailView.g.h"
#include "DocumentController.h"

#include <memory>

class SKAsymmetricBond;

namespace winrt::iRASPA_WinUI::implementation
{
    // The Bonds tab of the inspector (Cocoa StructureBondDetailViewController).
    // The form is BondsDetailView.xaml; this owns the rows and routes their
    // edits through the document controller. As in Cocoa a bond edit is not
    // undoable: the visibility, the fixed toggles and the bond type write
    // straight to the model, and only the length change has to regenerate the
    // structure around it.
    struct BondsDetailView : BondsDetailViewT<BondsDetailView>, BondsPanePresenter
    {
        BondsDetailView();

        void SetController(DocumentController* controller) { m_controller = controller; }
        // Rebuild the rows from the bond set of the selected frame.
        void Reload();
        // Drop the rows. They hold bonds of the project's bond set, which does
        // not outlive the project being replaced.
        void Clear();

        // BondsPanePresenter
        void RebindBonds() override;

        // What a row needs from the pane: the length to show, the length write,
        // and the renderer refresh a visibility or type change asks for.
        double LengthOf(std::shared_ptr<SKAsymmetricBond> const& bond) const;
        void CommitLength(std::shared_ptr<SKAsymmetricBond> const& bond, double length);
        void BondChanged();

        void OnRecomputeClick(winrt::Windows::Foundation::IInspectable const& sender,
                              winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        // Mirror the row selection into the bond set, which owns it, and refresh
        // the selection in the 3D view.
        void OnSelectionChanged(
            winrt::Microsoft::UI::Xaml::Controls::ItemsView const& sender,
            winrt::Microsoft::UI::Xaml::Controls::ItemsViewSelectionChangedEventArgs const& e);
        // The framework selects the whole text when a TextBox takes focus, so the
        // first keystroke would wipe the value; collapse that to a caret instead.
        void OnFieldGotFocus(winrt::Windows::Foundation::IInspectable const& sender,
                             winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        // The visibility box reports what the user did rather than writing through
        // a two-way binding, which also fires while rows are being recycled, when
        // the box belongs to another bond.
        void OnVisibilityClick(winrt::Windows::Foundation::IInspectable const& sender,
                               winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        // A recycled row is handed a new bond; the box is told what that bond is,
        // since the cleared binding leaves the nullable IsChecked on null, which
        // draws as the indeterminate '-' glyph.
        void OnVisibilityBoxDataContextChanged(
            winrt::Microsoft::UI::Xaml::FrameworkElement const& sender,
            winrt::Microsoft::UI::Xaml::DataContextChangedEventArgs const& e);

    private:
        void Populate();

        // The frame the rows were built from, so an edit still lands on it after
        // the selection has moved on.
        std::weak_ptr<iRASPAObject> m_frame;
        DocumentController* m_controller{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<
            winrt::Windows::Foundation::IInspectable> m_items{ nullptr };
        // Replacing the rows raises SelectionChanged for the rows that went
        // away, which would write that back into the model.
        bool m_suppressSelectionEvents = false;
    };
}

namespace winrt::iRASPA_WinUI::factory_implementation
{
    struct BondsDetailView : BondsDetailViewT<BondsDetailView, implementation::BondsDetailView>
    {
    };
}
